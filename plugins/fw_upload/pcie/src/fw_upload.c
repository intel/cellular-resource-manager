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

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#define CRM_MODULE_TAG "FWUP"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/file.h"
#include "utils/thread.h"
#include "utils/property.h"
#include "utils/keys.h"
#include "utils/time.h"
#include "plugins/fw_upload.h"
#include "plugins/control.h"
#include "ifwd/crm_ifwd.h"

#define PROCESS_LIB "libcrm_fw_upload_pcie_process.so"

typedef struct crm_fw_upload_internal_ctx {
    crm_fw_upload_ctx_t ctx; // Must be first

    crm_ctrl_ctx_t *control;
    crm_process_factory_ctx_t *factory;

    /* Configuration */
    int timeout;
    char *nvm_folder;
    char *run_folder;

    /* Variables */
    int process_id;
    bool fw_injected_ready;
    char *injected_fw;
    char *trace_file_path;
} crm_fw_upload_internal_ctx_t;

static inline void set_instance_id(char *src, int inst_id)
{
    char *find = strstr(src, "@");

    ASSERT(find != NULL);
    src[find - src] = '0' + inst_id;
}

static char *compute_path(const char *folder, const char *filename)
{
    ASSERT(folder);
    ASSERT(filename);

    size_t len = strlen(folder) + strlen(filename) + 2;
    char *path = malloc(sizeof(char) * len);
    ASSERT(path);
    snprintf(path, len, "%s/%s", folder, filename);

    return path;
}

static void *control_flash_process(crm_thread_ctx_t *thread_ctx, void *arg)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)arg;
    int status = -1;

    ASSERT(i_ctx);
    ASSERT(i_ctx->process_id >= 0);
    ASSERT(thread_ctx);

    struct pollfd pfd = { .fd = i_ctx->factory->get_poll_fd(i_ctx->factory, i_ctx->process_id),
                          .events = POLLIN };

    struct timespec timer_end;
    crm_time_add_ms(&timer_end, i_ctx->timeout);
    while (true) {
        int err = poll(&pfd, 1, crm_time_get_remain_ms(&timer_end));
        if (pfd.revents & POLLIN) {
            crm_ipc_msg_t msg;
            ASSERT(i_ctx->factory->get_msg(i_ctx->factory, i_ctx->process_id, &msg));
            ASSERT(msg.scalar == 0 || msg.scalar == -1);
            status = msg.scalar;
            break;
        } else if (err == 0) {
            LOGE("timeout");
            i_ctx->factory->kill(i_ctx->factory, i_ctx->process_id);
            break;
        } else {
            ASSERT(0);
        }
    }

    thread_ctx->dispose(thread_ctx, NULL);
    i_ctx->factory->clean(i_ctx->factory, i_ctx->process_id);
    i_ctx->process_id = -1;
    i_ctx->fw_injected_ready = false;

    i_ctx->control->notify_fw_upload_status(i_ctx->control, status);
    return NULL;
}

/**
 * @see fw_upload.h
 */
static void package(crm_fw_upload_ctx_t *ctx, const char *fw_path)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    ASSERT(fw_path != NULL);

    LOGD("->%s(%s)", __FUNCTION__, fw_path);

    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->process_id == -1);

    if (i_ctx->fw_injected_ready)
        return;

    ASSERT(crm_file_exists(fw_path));

    /* Set umask for any files created by DL lib code */
    mode_t old_umask = umask(S_IWOTH | S_IXOTH | S_IROTH);
    crm_ifwd_package(fw_path, i_ctx->injected_fw, i_ctx->nvm_folder);
    umask(old_umask & 0777);

    i_ctx->fw_injected_ready = true;
    i_ctx->control->notify_fw_upload_status(i_ctx->control, 0);
}

/**
 * @see fw_upload.h
 */
static void flash(crm_fw_upload_ctx_t *ctx, const char *nodes)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    LOGD("->%s(%s)", __FUNCTION__, nodes);

    ASSERT(i_ctx != NULL);
    ASSERT(nodes);

    ASSERT(i_ctx->fw_injected_ready == true);
    ASSERT(i_ctx->process_id == -1);

    int args_size = strlen(nodes) + strlen(i_ctx->injected_fw) + strlen(i_ctx->trace_file_path) + 4;
    char *args = malloc(sizeof(char) * args_size);
    ASSERT(args);
    snprintf(args, args_size, "%s;%s;%s;", nodes, i_ctx->injected_fw, i_ctx->trace_file_path);
    i_ctx->process_id = i_ctx->factory->create(i_ctx->factory, PROCESS_LIB, args, args_size);
    free(args);

    if (i_ctx->process_id >= 0) {
        crm_thread_init(control_flash_process, i_ctx, false, true);
    } else {
        LOGE("failed to create process");
        i_ctx->control->notify_fw_upload_status(i_ctx->control, -1);
    }
}

/**
 * @see fw_upload.h
 */
static void dispose(crm_fw_upload_ctx_t *ctx)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->process_id == -1);

    free(i_ctx->injected_fw);
    free(i_ctx->run_folder);
    free(i_ctx->nvm_folder);
    free(i_ctx->trace_file_path);

    free(i_ctx);
}

/**
 * @see fw_upload.h
 */
crm_fw_upload_ctx_t *crm_fw_upload_init(int inst_id, bool flashless, tcs_ctx_t *tcs,
                                        crm_ctrl_ctx_t *control, crm_process_factory_ctx_t *factory)
{
    crm_fw_upload_internal_ctx_t *i_ctx = calloc(1, sizeof(crm_fw_upload_internal_ctx_t));

    (void)flashless; //unused

    ASSERT(i_ctx != NULL);
    ASSERT(tcs != NULL);
    ASSERT(control != NULL);
    ASSERT(factory);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.package = package;
    i_ctx->ctx.flash = flash;
    i_ctx->control = control;
    i_ctx->factory = factory;

    i_ctx->process_id = -1;

    ASSERT(tcs->select_group(tcs, ".firmware_upload") == 0);
    i_ctx->run_folder = tcs->get_string(tcs, "run_folder");
    ASSERT(i_ctx->run_folder);
    set_instance_id(i_ctx->run_folder, inst_id);
    ASSERT(!tcs->get_int(tcs, "timeout", &i_ctx->timeout));
    ASSERT(i_ctx->timeout > 0);

    char group[5];
    snprintf(group, sizeof(group), "nvm%d", inst_id);
    tcs->add_group(tcs, group, true);
    ASSERT(!tcs->select_group(tcs, group));
    i_ctx->nvm_folder = tcs->get_string(tcs, "data_folder");
    ASSERT(i_ctx->nvm_folder);

    i_ctx->injected_fw = compute_path(i_ctx->run_folder, "injected_firmware.fls");

    char value[CRM_PROPERTY_VALUE_MAX];
    crm_property_get(CRM_KEY_DBG_ENABLE_FLASHING_LOG, value, "off");
    if (!strcmp(value, "on"))
        i_ctx->trace_file_path = compute_path(i_ctx->run_folder, "download_lib_logs.log");
    else
        i_ctx->trace_file_path = strdup("");

    /* sub-folders will be created by the download lib if they doesn't exist */

    LOGV("context %p", i_ctx);
    return &i_ctx->ctx;
}
