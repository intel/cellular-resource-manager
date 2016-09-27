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

#define CRM_MODULE_TAG "HAL"
#include "utils/common.h"
#include "utils/logs.h"

#include "common.h"
#include "daemons.h"
#include "nvm_manager.h"

#define CRM_MMB_MSG_START   0
#define CRM_MMB_MSG_STOP    1

#define MMB_CRM_MSG_SUCCESS 0
#define MMB_CRM_MSG_FAILURE 1
#define MMB_CRM_MSG_FATAL_FAILURE 2

/**
 * @see daemons.h
 */
int crm_hal_nvm_daemon_cb(int id, void *ctx, crm_hal_daemon_evt_t evt, int msg_id,
                          size_t msg_len)
{
    ASSERT(ctx);
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)ctx;
    ASSERT(i_ctx->nvm_daemon_id == id);

    int ret = -1;

    switch (evt) {
    case HAL_DAEMON_CONNECTED:
        LOGD("NVM server connected");
        i_ctx->nvm_daemon_connected = true;
        break;

    case HAL_DAEMON_DISCONNECTED:
        LOGD("NVM server disconnected");
        /* In case of NVM server disconnection, restart the daemon */
        crm_hal_daemon_start(&i_ctx->daemon_ctx, i_ctx->nvm_daemon_id);

        if (i_ctx->nvm_daemon_connected) {
            /* Request a modem restart.
             *
             * Note that if the disconnection is due to a request from CRM,
             * nvm_daemon_connected will already have been set to false and so this restart is
             * not done.
             */
            /** @TODO */
        }

        i_ctx->nvm_daemon_connected = false;
        i_ctx->nvm_daemon_syncing = false;
        ret = EV_NVM_STOP;
        break;

    case HAL_DAEMON_DATA_IN:
        switch (msg_id) {
        case MMB_CRM_MSG_SUCCESS:
            ASSERT(msg_len == 0);
            LOGD("NVM server connected to modem");
            i_ctx->nvm_daemon_syncing = true;
            ret = EV_NVM_RUN;
            break;

        default:
            DASSERT(0, "Unexpected message id received: %d", msg_id);
            break;
        }
        break;

    default:
        ASSERT(0);
        break;
    }

    return ret;
}

/**
 * @see nvm_manager.h
 */
void crm_hal_nvm_on_modem_up(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx);

    ssize_t sent = crm_hal_daemon_msg_send(&i_ctx->daemon_ctx, i_ctx->nvm_daemon_id,
                                           CRM_MMB_MSG_START, NULL, 0);
    if (sent < 0) {
        LOGE("Failed to send 'START' message to NVM manager");
        /* Restart NVM server */
        i_ctx->nvm_daemon_connected = false;
        crm_hal_daemon_stop(&i_ctx->daemon_ctx, i_ctx->nvm_daemon_id);
        crm_hal_daemon_start(&i_ctx->daemon_ctx, i_ctx->nvm_daemon_id);

        /** @TODO send error to FSM */
    }
}

/**
 * @see nvm_manager.h
 */
void crm_hal_nvm_on_modem_down(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx);

    ssize_t sent = crm_hal_daemon_msg_send(&i_ctx->daemon_ctx, i_ctx->nvm_daemon_id,
                                           CRM_MMB_MSG_STOP, NULL, 0);
    if (sent < 0) {
        LOGE("Failed to send 'STOP' message to NVM manager");
        crm_hal_daemon_stop(&i_ctx->daemon_ctx, i_ctx->nvm_daemon_id);
    }
}

/**
 * @see nvm_manager.h
 */
void crm_hal_nvm_on_manager_crash(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx);
    crm_hal_daemon_stop(&i_ctx->daemon_ctx, i_ctx->nvm_daemon_id);
    crm_hal_daemon_start(&i_ctx->daemon_ctx, i_ctx->nvm_daemon_id);
    i_ctx->nvm_daemon_syncing = false;
}
