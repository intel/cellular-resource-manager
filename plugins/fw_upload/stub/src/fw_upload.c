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

#define CRM_MODULE_TAG "FWUP"
#include "utils/logs.h"
#include "utils/common.h"
#include "plugins/fw_upload.h"
#include "plugins/control.h"

typedef struct crm_fw_upload_internal_ctx {
    crm_fw_upload_ctx_t ctx; // Must be first

    crm_ctrl_ctx_t *control;
} crm_fw_upload_internal_ctx_t;

static void dispose(crm_fw_upload_ctx_t *ctx)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    ASSERT(i_ctx != NULL);

    free(i_ctx);
}

static void package(crm_fw_upload_ctx_t *ctx, const char *fw_path)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    ASSERT(i_ctx != NULL);
    (void)fw_path; //UNUSED

    LOGD("->%s()", __FUNCTION__);

    i_ctx->control->notify_fw_upload_status(i_ctx->control, 0);
}

static void flash(crm_fw_upload_ctx_t *ctx, const char *nodes)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s()", __FUNCTION__);

    (void)nodes; //UNUSED

    i_ctx->control->notify_fw_upload_status(i_ctx->control, 0);
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
    (void)inst_id;   // UNUSED
    (void)flashless; // UNUSED
    (void)tcs;       // UNUSED
    (void)factory;   // UNUSED

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.package = package;
    i_ctx->ctx.flash = flash;

    i_ctx->control = control;

    LOGV("context %p", i_ctx);
    return &i_ctx->ctx;
}
