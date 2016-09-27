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

#include <string.h>

#define CRM_MODULE_TAG "CTRL"
#include "utils/common.h"
#include "utils/string_helpers.h"
#include "utils/logs.h"

#include "common.h"
#include "utils.h"

static const char *get_restart_type_string(crm_ctrl_restart_type_t type)
{
    switch (type) {
    case CTRL_MODEM_RESTART:  return "modem restart";
    case CTRL_MODEM_UPDATE: return "modem update";
    case CTRL_BACKUP_NVM: return "modem NVM backup";
    default: ASSERT(0);
    }
}

/**
 * @see control.h
 */
void crm_ctrl_start(crm_ctrl_ctx_t *ctx)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s()", __FUNCTION__);
    crm_ipc_msg_t msg = { .scalar = EV_CLI_START };
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);
}

/**
 * @see control.h
 */
void crm_ctrl_stop(crm_ctrl_ctx_t *ctx)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s()", __FUNCTION__);
    crm_ipc_msg_t msg = { .scalar = EV_CLI_STOP };
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);
}

/**
 * @see control.h
 */
void crm_ctrl_restart(crm_ctrl_ctx_t *ctx, crm_ctrl_restart_type_t type,
                      const mdm_cli_dbg_info_t *dbg_info)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    crm_ipc_msg_t msg = { .scalar = EV_CLI_RESET, .data = NULL };
    switch (type) {
    case CTRL_MODEM_RESTART:
        // default message is EV_CLI_RESET
        if (dbg_info) {
            msg.data = copy_dbg_info(dbg_info);
            msg.data_size = sizeof(dbg_info);
        }
        break;
    case CTRL_MODEM_UPDATE:
        ASSERT(!dbg_info);
        msg.scalar = EV_CLI_UPDATE;
        break;
    case CTRL_BACKUP_NVM:
        ASSERT(!dbg_info);
        msg.scalar = EV_CLI_NVM_BACKUP;
        break;
    default: ASSERT(0);
    }

    LOGD("->%s(type: %s)", __FUNCTION__, get_restart_type_string(type));
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);
}
