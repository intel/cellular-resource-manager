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

#define CRM_MODULE_TAG "HAL"
#include "utils/common.h"
#include "utils/logs.h"

#include "common.h"
#include "request.h"

/**
 * @see request.h
 */
void hal_power_on(crm_hal_ctx_t *ctx)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s()", __FUNCTION__);
    crm_ipc_msg_t msg = { .scalar = EV_POWER };
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);
}

/**
 * @see request.h
 */
void hal_boot(crm_hal_ctx_t *ctx)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s()", __FUNCTION__);
    crm_ipc_msg_t msg = { .scalar = EV_BOOT };
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);
}

/**
 * @see request.h
 */
void hal_shutdown(crm_hal_ctx_t *ctx)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s()", __FUNCTION__);
    crm_ipc_msg_t msg = { .scalar = EV_STOP };
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);
}

/**
 * @see request.h
 */
void hal_reset(crm_hal_ctx_t *ctx, crm_hal_reset_type_t type)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    crm_ipc_msg_t msg = { .scalar = EV_RESET };

    char *type_txt;
    switch (type) {
    case RESET_WARM:   type_txt = "warm"; break;
    case RESET_COLD:   type_txt = "cold"; break;
    case RESET_BACKUP:
        type_txt = "backup";
        msg.scalar = EV_BACKUP;
        break;
    default: ASSERT(0);
    }
    LOGD("->%s(%s)", __FUNCTION__, type_txt);
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);
}
