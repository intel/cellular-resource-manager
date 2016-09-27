/*
 * Copyright (C) Intel 2016
 *
 * CRM has been designed by:
 *  - Cesar De Oliveira <cesar.de.oliveira@intel.com>
 *  - Erwan Bracq <erwan.bracq@intel.com>
 *  - Lionel Ulmer <lionel.ulmer@intel.com>
 *  - Marc Bellanger <marc.bellanger@intel.com>
 *
 * Original CRM contributors are:
 *  - Cesar De Oliveira <cesar.de.oliveira@intel.com>
 *  - Lionel Ulmer <lionel.ulmer@intel.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define CRM_MODULE_TAG "HAL"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/file.h"
#include "utils/property.h"
#include "utils/keys.h"

#include "common.h"
#include "daemons.h"
#include "fsm_hal.h"
#include "modem.h"
#include "request.h"
#include "nvm_manager.h"

/* Permissions for the NVM directory / files when restored by CRM */
#define NVM_FOLDER_PERMISSION (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP)

static void folder_create(const char *path)
{
    ASSERT(path != NULL);

    mode_t old_umask = umask(~NVM_FOLDER_PERMISSION & 0777);
    if (mkdir(path, NVM_FOLDER_PERMISSION)) {
        DASSERT(errno == EEXIST, "Failed to create %s: %s", path, strerror(errno));
    } else {
        if (chmod(path, NVM_FOLDER_PERMISSION))
            LOGE("Failed to change access rights for folder %s", path);

        struct passwd *pwd = getpwnam("system");
        struct group *gp = getgrnam("radio");
        if (pwd && gp)
            if (chown(path, pwd->pw_uid, gp->gr_gid))
                LOGE("Failed to change user / group for folder %s", path);
    }
    umask(old_umask & 0777);
}

static char *gen_path(const char *nvm_folder, char *file_name)
{
    ASSERT(file_name);
    size_t path_len = strlen(nvm_folder) + 1 + strlen(file_name) + 1;
    char *ret = malloc(path_len);
    ASSERT(ret);
    snprintf(ret, path_len, "%s/%s", nvm_folder, file_name);
    free(file_name);
    return ret;
}

/**
 * @see hal.h
 */
static void dispose(crm_hal_ctx_t *ctx)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    /* notify thread termination */
    crm_ipc_msg_t msg = { .scalar = -1 };
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);

    i_ctx->thread_fsm->dispose(i_ctx->thread_fsm, NULL);
    // @TODO: thread cancellation?
    if (i_ctx->thread_cfg)
        i_ctx->thread_cfg->dispose(i_ctx->thread_cfg, NULL);

    i_ctx->ipc->dispose(i_ctx->ipc, NULL);

    shutdown(i_ctx->net_fd, SHUT_RDWR);
    close(i_ctx->net_fd);
    close(i_ctx->mcd_fd);
    // @TODO: RPCD socket release

    crm_hal_daemon_dispose(&i_ctx->daemon_ctx);

    free(i_ctx->nvm_calib_file);
    free(i_ctx->nvm_calib_bkup_file);
    free(i_ctx->flash_node);
    free(i_ctx->modem_node);
    free(i_ctx->ping_mux_node);
    free(i_ctx->shutdown_node);
    free(i_ctx->pcie_pwr_ctrl);
    free(i_ctx->pcie_rm);
    free(i_ctx->pcie_rescan);
    free(i_ctx->pcie_rtpm);
    free(i_ctx);
}

/**
 * @see hal.h
 */
crm_hal_ctx_t *crm_hal_init(int inst_id, bool debug, bool dump_enabled, tcs_ctx_t *tcs,
                            crm_ctrl_ctx_t *control)
{
    crm_hal_ctx_internal_t *i_ctx = calloc(1, sizeof(crm_hal_ctx_internal_t));
    char group[16];

    ASSERT(i_ctx);
    ASSERT(tcs);
    ASSERT(control);
    (void)dump_enabled;  // UNUSED

    ASSERT(tcs->select_group(tcs, ".hal") == 0);
    ASSERT(!tcs->get_int(tcs, "ping_timeout", &i_ctx->ping_timeout));
    i_ctx->flash_node = tcs->get_string(tcs, "flash_node");
    i_ctx->modem_node = tcs->get_string(tcs, "modem_node");
    i_ctx->ping_mux_node = tcs->get_string(tcs, "ping_mux_node");
    i_ctx->shutdown_node = tcs->get_string(tcs, "shutdown_node");
    ASSERT(i_ctx->flash_node);
    ASSERT(i_ctx->modem_node);
    ASSERT(i_ctx->ping_mux_node);
    ASSERT(i_ctx->shutdown_node);
    /* Optional parameter */
    char *dbg_socket = NULL;
    if (debug) {
        dbg_socket = tcs->get_string(tcs, "debug_socket");
        ASSERT(dbg_socket);
    }

    ASSERT(tcs->select_group(tcs, ".hal.pcie") == 0);
    i_ctx->pcie_pwr_ctrl = tcs->get_string(tcs, "power_control");
    ASSERT(i_ctx->pcie_pwr_ctrl);
    /* Optional fields: PCIe remove and rescan are only used for a WA on BXT */
    i_ctx->pcie_rm = tcs->get_string(tcs, "remove");
    i_ctx->pcie_rescan = tcs->get_string(tcs, "rescan");
    /* WA on CHT for LPM support:
     * interface to enable/disable PCIe runtime power management */
    i_ctx->pcie_rtpm = tcs->get_string(tcs, "rtpm_enable");

    snprintf(group, sizeof(group), "nvm%d", inst_id);
    tcs->add_group(tcs, group, false); // group already printed by FW upload
    ASSERT(!tcs->select_group(tcs, group));
    char *nvm_folder = tcs->get_string(tcs, "data_folder");
    ASSERT(nvm_folder);

    snprintf(group, sizeof(group), "nvm%d.calib", inst_id);
    ASSERT(!tcs->select_group(tcs, group));
    i_ctx->nvm_calib_file = gen_path(nvm_folder, tcs->get_string(tcs, "work_file"));
    ASSERT(!tcs->get_bool(tcs, "backup_is_raw", &i_ctx->nvm_calib_bkup_is_raw));
    char *backup_file = tcs->get_string(tcs, "backup_file");
    ASSERT(backup_file);
    if (i_ctx->nvm_calib_bkup_is_raw)
        i_ctx->nvm_calib_bkup_file = backup_file;
    else
        i_ctx->nvm_calib_bkup_file = gen_path(nvm_folder, backup_file);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.power_on = hal_power_on;
    i_ctx->ctx.boot = hal_boot;
    i_ctx->ctx.shutdown = hal_shutdown;
    i_ctx->ctx.reset = hal_reset;

    i_ctx->inst_id = inst_id;
    i_ctx->control = control;
    i_ctx->mux_fd = -1;
    i_ctx->first_expiration = true;
    i_ctx->ipc = crm_ipc_init(CRM_IPC_THREAD);
    i_ctx->net_fd = crm_hal_get_poll_mdm_fd(dbg_socket);

    char value[CRM_PROPERTY_VALUE_MAX];
    crm_property_get(CRM_KEY_DBG_DISABLE_DUMP, value, "false");
    if (!strcmp(value, "true"))
        i_ctx->dump_disabled = true;

    /* Check for presence of existing NVM file */
    /** @TODO check for presence of static / dynamic to force a TLV re-application ? */
    if (!crm_file_exists(i_ctx->nvm_calib_file)) {
        /* In case directory does not exist, create it (in case the folder already exists, this
         * call won't do anything). */
        folder_create(nvm_folder);

        if (crm_file_copy(i_ctx->nvm_calib_bkup_file, i_ctx->nvm_calib_file,
                          i_ctx->nvm_calib_bkup_is_raw, false, NVM_FILE_PERMISSION)) {
            /* Note: this is not a fatal error as it can happen at factory before initial
             *       calibration. */
            LOGE("Failed to restore calibration, device must be re-calibrated");
        } else {
            LOGD("Restored calibration from %s (%s)",
                 i_ctx->nvm_calib_bkup_is_raw ? "GPP partition" : "backup file",
                 i_ctx->nvm_calib_bkup_file);
        }
    }

    /* Start daemon service and register NVM manager */
    char ctrlsock[16];
    snprintf(ctrlsock, sizeof(ctrlsock), "crmctrl%d", inst_id);
    crm_hal_daemon_init(&i_ctx->daemon_ctx, ctrlsock);
    i_ctx->nvm_daemon_id = crm_hal_daemon_add(&i_ctx->daemon_ctx, "nvm_manager",
                                              crm_hal_nvm_daemon_cb, i_ctx);
    ASSERT(i_ctx->nvm_daemon_id >= 0);
    crm_hal_daemon_start(&i_ctx->daemon_ctx, i_ctx->nvm_daemon_id);
    i_ctx->nvm_daemon_connected = false;
    i_ctx->thread_fsm = crm_thread_init(crm_hal_pcie_fsm, i_ctx, false, false);

    free(nvm_folder);
    free(dbg_socket);

    LOGV("context %p", i_ctx);
    return &i_ctx->ctx;
}
