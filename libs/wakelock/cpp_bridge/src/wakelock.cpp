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

#define CRM_MODULE_TAG "WAKE"
#include "utils/common.h"
#include "utils/wakelock.h"

#include "CrmWakelockMux.hpp"

typedef struct crm_wakelock_internal_ctx
{
    crm_wakelock_t ctx; // Must be first

    CrmWakelockMux *lock;
} crm_fw_upload_internal_ctx_t;

/**
 * @see wakelock.h
 */
static void dispose(crm_wakelock_t *ctx)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;
    ASSERT(i_ctx != NULL);

    delete i_ctx->lock;
    free(i_ctx);
}

/**
 * @see wakelock.h
 */
static void acquire(crm_wakelock_t *ctx, int module_id)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->lock != NULL);

    i_ctx->lock->acquire(module_id);
}

/**
 * @see wakelock.h
 */
static void release(crm_wakelock_t *ctx, int module_id)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->lock != NULL);

    i_ctx->lock->release(module_id);
}

/**
 * @see wakelock.h
 */
static bool is_held_by_module(crm_wakelock_t *ctx, int module_id)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->lock != NULL);

    return i_ctx->lock->isHeld(module_id);
}

/**
 * @see wakelock.h
 */
static bool is_held(crm_wakelock_t *ctx)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->lock != NULL);

    return i_ctx->lock->isHeld();
}

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @see wakelock.h
 */
crm_wakelock_t *crm_wakelock_init(const char *name)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)
                                          calloc(1, sizeof(crm_fw_upload_internal_ctx_t));

    ASSERT(i_ctx != NULL);

    i_ctx->lock = new CrmWakelockMux(name);
    ASSERT(i_ctx->lock != NULL);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.acquire = acquire;
    i_ctx->ctx.release = release;
    i_ctx->ctx.is_held = is_held;
    i_ctx->ctx.is_held_by_module = is_held_by_module;

    return &i_ctx->ctx;
}

#ifdef __cplusplus
}
#endif
