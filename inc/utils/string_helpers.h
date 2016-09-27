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

#ifndef __CRM_UTILS_STRING_HELPERS_HEADER__
#define __CRM_UTILS_STRING_HELPERS_HEADER__

#include "libmdmcli/mdm_cli.h"

/**
 * Converts request id to string value. Request id can be of type
 * crm_mdmcli_wire_req_ids_t or mdm_cli_event_t.
 *
 * @param [in] req_id Request id
 *
 * @return request id as string
 */
const char *crm_mdmcli_wire_req_to_string(int req_id);

/**
 * Converts debug type id to string value
 *
 * @param [in] id Debug type id
 *
 * @return debug type as string
 */
const char *crm_mdmcli_dbg_type_to_string(mdm_cli_dbg_type_t id);

/**
 * Converts modem restart cause id to string value
 *
 * @param [in] id Modem restart cause id
 *
 * @return restart cause as string
 */
const char *crm_mdmcli_restart_cause_to_string(mdm_cli_restart_cause_t id);

/**
 * Converts client abstraction modem state to string value
 *
 * @param [in] mdm_state Modem state value
 *
 * @return modem state as string
 */
const char *crm_cli_abs_mdm_state_to_string(int mdm_state);

/**
 * Converts escalation level to string value
 *
 * @param [in] level Escalation level value
 *
 * @return escalation level as string
 */
const char *crm_escalation_level_to_string(int level);

#endif /* __CRM_UTILS_STRING_HELPERS_HEADER__ */
