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

#include "libmdmcli/mdm_cli.h"

#define CRM_MODULE_TAG "UTILS"
#include "utils/common.h"
#include "utils/string_helpers.h"
#include "plugins/mdmcli_wire.h"
#include "plugins/client_abstraction.h"
#include "plugins/escalation.h"

/**
 * @see string_helpers.h
 */
const char *crm_mdmcli_wire_req_to_string(int req_id)
{
    switch (req_id) {
    case MDM_DOWN: return "MDM_DOWN";
    case MDM_ON: return "MDM_ON";
    case MDM_UP: return "MDM_UP";
    case MDM_OOS: return "MDM_OOS";
    case MDM_COLD_RESET: return "MDM_COLD_RESET";
    case MDM_SHUTDOWN: return "MDM_SHUTDOWN";
    case MDM_DBG_INFO: return "MDM_DBG_INFO";
    case CRM_REQ_REGISTER: return "REGISTER";
    case CRM_REQ_REGISTER_DBG: return "REGISTER_DBG";
    case CRM_REQ_ACQUIRE: return "ACQUIRE";
    case CRM_REQ_RELEASE: return "RELEASE";
    case CRM_REQ_RESTART: return "RESTART";
    case CRM_REQ_SHUTDOWN: return "SHUTDOWN";
    case CRM_REQ_NVM_BACKUP: return "NVM_BACKUP";
    case CRM_REQ_ACK_COLD_RESET: return "ACK_COLD_RESET";
    case CRM_REQ_ACK_SHUTDOWN: return "ACK_SHUTDOWN";
    case CRM_REQ_NOTIFY_DBG: return "NOTIFY_DBG";
    default: ASSERT(0);
    }
}

/**
 * @see string_helpers.h
 */
const char *crm_mdmcli_dbg_type_to_string(mdm_cli_dbg_type_t id)
{
    switch (id) {
    case DBG_TYPE_STATS: return "STATS";
    case DBG_TYPE_INFO: return "INFO";
    case DBG_TYPE_ERROR: return "ERROR";
    case DBG_TYPE_PLATFORM_REBOOT: return "PLATFORM_REBOOT";
    case DBG_TYPE_DUMP_START: return "DUMP_START";
    case DBG_TYPE_DUMP_END: return "DUMP_END";
    case DBG_TYPE_APIMR: return "APIMR";
    case DBG_TYPE_SELF_RESET: return "SELF_RESET";
    case DBG_TYPE_FW_SUCCES: return "FW_SUCCESS";
    case DBG_TYPE_FW_FAILURE: return "FW_FAILURE";
    case DBG_TYPE_TLV_NONE: return "TLV_NONE";
    case DBG_TYPE_TLV_SUCCESS: return "TLV_SUCCESS";
    case DBG_TYPE_TLV_FAILURE: return "TLV_FAILURE";
    case DBG_TYPE_NVM_BACKUP_SUCCESS: return "NVM_BACKUP_SUCCESS";
    case DBG_TYPE_NVM_BACKUP_FAILURE: return "NVM_BACKUP_FAILURE";
    default: ASSERT(0);
    }
}

/**
 * @see string_helpers.h
 */
const char *crm_mdmcli_restart_cause_to_string(mdm_cli_restart_cause_t id)
{
    switch (id) {
    case RESTART_MDM_OOS: return "MDM_OOS";
    case RESTART_MDM_ERR: return "MDM_ERR";
    case RESTART_APPLY_UPDATE: return "APPLY_UPDATE";
    default: ASSERT(0);
    }
}

/**
 * @see string_helpers.h
 */
const char *crm_cli_abs_mdm_state_to_string(int mdm_state)
{
    switch (mdm_state) {
    case MDM_STATE_OFF: return "MDM_STATE_OFF";
    case MDM_STATE_UNRESP: return "MDM_STATE_UNRESP";
    case MDM_STATE_BUSY: return "MDM_STATE_BUSY";
    case MDM_STATE_READY: return "MDM_STATE_READY";
    case MDM_STATE_PLATFORM_REBOOT: return "MDM_STATE_PLATFORM_REBOOT";
    case MDM_STATE_UNKNOWN: return "MDM_STATE_UNKNOWN";
    default: ASSERT(0);
    }
}

/**
 * @see string_helpers.h
 */
const char *crm_escalation_level_to_string(int level)
{
    switch (level) {
    case STEP_MDM_WARM_RESET: return "warm reset";
    case STEP_MDM_COLD_RESET: return "cold reset";
    case STEP_PLATFORM_REBOOT: return "platform reboot";
    case STEP_OOS: return "modem oos";
    default: ASSERT(0);
    }
}
