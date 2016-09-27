/*
 * Copyright (C) Intel 2015
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
#include <unistd.h>
#include <string.h>

#define CRM_MODULE_TAG "HAL"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/property.h"
#include "utils/keys.h"

#include "common.h"
#include "modem.h"
#include "request.h"
#include "fsm_hal.h"
#include "rpcd.h"

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
    if (i_ctx->thread_ping)
        i_ctx->thread_ping->dispose(i_ctx->thread_ping, NULL);

    i_ctx->ipc->dispose(i_ctx->ipc, NULL);

    shutdown(i_ctx->s_fd, SHUT_RDWR);
    close(i_ctx->s_fd);

    free(i_ctx->vmodem_sysfs_mdm_state);
    free(i_ctx->vmodem_sysfs_mdm_ctrl);
    free(i_ctx->uevent_vmodem);
    free(i_ctx->ping_node);
    free(i_ctx->dump_node);
    free(i_ctx->flash_node);

    crm_hal_rpcd_dispose(i_ctx);

    free(i_ctx);
}

/**
 * @see hal.h
 */
crm_hal_ctx_t *crm_hal_init(int inst_id, bool host_debug, bool dump_enabled, tcs_ctx_t *tcs,
                            crm_ctrl_ctx_t *control)
{
    crm_hal_ctx_internal_t *i_ctx = calloc(1, sizeof(crm_hal_ctx_internal_t));

    ASSERT(i_ctx != NULL);
    ASSERT(tcs != NULL);
    ASSERT(control != NULL);
    (void)inst_id;  // UNUSED

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.power_on = hal_power_on;
    i_ctx->ctx.boot = hal_boot;
    i_ctx->ctx.shutdown = hal_shutdown;
    i_ctx->ctx.reset = hal_reset;

    ASSERT(tcs->select_group(tcs, ".hal") == 0);
    i_ctx->vmodem_sysfs_mdm_state = tcs->get_string(tcs, "vmodem_sysfs_mdm_state");
    i_ctx->vmodem_sysfs_mdm_ctrl = tcs->get_string(tcs, "vmodem_sysfs_mdm_ctrl");
    i_ctx->uevent_vmodem = tcs->get_string(tcs, "uevent_vmodem_filter");
    i_ctx->ping_node = tcs->get_string(tcs, "ping_node");
    i_ctx->dump_node = tcs->get_string(tcs, "dump_node");
    ASSERT(i_ctx->vmodem_sysfs_mdm_state != NULL);
    ASSERT(i_ctx->vmodem_sysfs_mdm_ctrl != NULL);
    ASSERT(i_ctx->uevent_vmodem != NULL);
    ASSERT(i_ctx->ping_node != NULL);
    ASSERT(i_ctx->dump_node != NULL);

    /* sanity check: is the size of node buffer in crm_hal_evt_t structure big enough to
     * store the path of link? */
    crm_hal_evt_t tmp;
    DASSERT(strlen(i_ctx->dump_node) < sizeof(tmp.nodes), "buffer too small");

    ASSERT(tcs->get_bool(tcs, "sevcm_flash", &i_ctx->secvm_flash) == 0);
    i_ctx->flash_node = tcs->get_string(tcs, "flash_node");
    if (i_ctx->secvm_flash) {
        ASSERT(i_ctx->flash_node == NULL);
        i_ctx->flash_node = strdup("");
    } else {
        ASSERT(i_ctx->flash_node != NULL);
        DASSERT(strlen(i_ctx->flash_node) < sizeof(tmp.nodes), "buffer too small");
    }

    i_ctx->control = control;

    /* Optional parameter */
    char *host_dbg_socket = NULL;
    if (host_debug) {
        host_dbg_socket = tcs->get_string(tcs, "uevent_host_debug_socket");
        ASSERT(host_dbg_socket);
    }

    i_ctx->s_fd = crm_hal_get_poll_mdm_fd(host_dbg_socket);
    free(host_dbg_socket);

    (void)dump_enabled;  // @TODO: use it once this hack is removed
    char value[CRM_PROPERTY_VALUE_MAX];
    crm_property_get(CRM_KEY_DBG_ENABLE_SILENT_RESET, value, "false");
    if (!strcmp(value, "true")) {
        i_ctx->dump_enabled = true;
        i_ctx->silent_reset_enabled = true;
    } else {
        i_ctx->dump_enabled = false;
        i_ctx->silent_reset_enabled = false;
    }

    ASSERT(tcs->get_int(tcs, "ping_timeout", &i_ctx->ping_timeout) == 0);
    ASSERT(tcs->get_bool(tcs, "support_modem_up_at_start", &i_ctx->support_mdm_up_on_start) == 0);

    i_ctx->ipc = crm_ipc_init(CRM_IPC_THREAD);
    i_ctx->thread_fsm = crm_thread_init(crm_hal_sofia_fsm, i_ctx, false, false);

    LOGV("context %p", i_ctx);
    return &i_ctx->ctx;
}
