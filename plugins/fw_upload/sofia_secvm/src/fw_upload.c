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
#include <fcntl.h>
#include <sys/sendfile.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define CRM_MODULE_TAG "FWUP"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/thread.h"
#include "plugins/fw_upload.h"
#include "plugins/control.h"

#include "tee_sec_boot.h"

typedef struct crm_fw_upload_internal_ctx {
    crm_fw_upload_ctx_t ctx; // Must be first

    crm_ctrl_ctx_t *control;

    bool op_ongoing;
    char *fw_path;
} crm_fw_upload_internal_ctx_t;

static void *write_firmware(crm_thread_ctx_t *thread_ctx, void *arg)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)arg;

    ASSERT(i_ctx != NULL);
    ASSERT(thread_ctx != NULL);

    ASSERT(i_ctx->op_ongoing == false);
    i_ctx->op_ongoing = true;

    int ret = tee_sec_boot_firmware_update(MEX, i_ctx->fw_path, true);
    if (ret != 0)
        LOGE("failed to flash firmware, error %d", ret);
    else
        LOGD("firmware flashed successfully");

    /* detached thread: Memory must be cleaned before thread termination */
    thread_ctx->dispose(thread_ctx, NULL);

    free(i_ctx->fw_path);
    i_ctx->fw_path = NULL;
    i_ctx->op_ongoing = false;
    i_ctx->control->notify_fw_upload_status(i_ctx->control, ret == 0 ? 0 : -1);

    return NULL;
}

/**
 * @see fw_upload.h
 */
static void dispose(crm_fw_upload_ctx_t *ctx)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->op_ongoing == false);

    free(i_ctx->fw_path);

    free(i_ctx);
}

/**
 * @see fw_upload.h
 */
static void package(crm_fw_upload_ctx_t *ctx, const char *fw_path)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    LOGD("->%s()", __FUNCTION__);
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->op_ongoing == false);

    if (i_ctx->fw_path != NULL)
        return;

    i_ctx->fw_path = strdup(fw_path);
    ASSERT(i_ctx->fw_path != NULL);

    i_ctx->control->notify_fw_upload_status(i_ctx->control, 0);
}

/**
 * @see fw_upload.h
 */
static void flash(crm_fw_upload_ctx_t *ctx, const char *node)
{
    (void)node;

    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    LOGD("->%s()", __FUNCTION__);

    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->op_ongoing == false);
    ASSERT(i_ctx->fw_path != NULL);

    crm_thread_init(write_firmware, i_ctx, false, true);
}

/**
 * @see fw_upload.h
 */
crm_fw_upload_ctx_t *crm_fw_upload_init(int inst_id, bool flashless, tcs_ctx_t *tcs,
                                        crm_ctrl_ctx_t *control, crm_process_factory_ctx_t *factory)
{
    crm_fw_upload_internal_ctx_t *i_ctx = calloc(1, sizeof(crm_fw_upload_internal_ctx_t));

    ASSERT(i_ctx != NULL);
    ASSERT(control != NULL);
    (void)tcs;       // UNUSED
    (void)inst_id;   // UNUSED
    (void)flashless; // UNUSED
    (void)factory;   // UNUSED

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.package = package;
    i_ctx->ctx.flash = flash;

    i_ctx->control = control;

    LOGV("context %p", i_ctx);
    return &i_ctx->ctx;
}
