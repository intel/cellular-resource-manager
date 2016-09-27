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

#ifndef __CRM_TYPES_HEADER__
#define __CRM_TYPES_HEADER__

#include "libmdmcli/mdm_cli_dbg.h"
#include "libtcs2/tcs.h"

typedef struct crm_ctrl_ctx crm_ctrl_ctx_t;

/**
 * HAL structures
 */
typedef enum crm_hal_evt_type {
    HAL_MDM_OFF = 1,      // modem is turned off
    HAL_MDM_RUN,          // modem is ready
    HAL_MDM_BUSY,         // modem is not available. Reason will be provided in a second message
                          // Next enums represent the reasons:
    HAL_MDM_NEED_RESET,   // reset is needed
    HAL_MDM_FLASH,        // modem ready for flashing
    HAL_MDM_DUMP,         // modem ready for core dump reading
    HAL_MDM_UNRESPONSIVE, // modem is unresponsive. Cannot be recovered
} crm_hal_evt_type_t;

/**
 * Structure used by HAL to report an event to control
 *
 * @var type     Type of event
 * @var node     Available nodes. Nodes are separated by a ';'
 * @var dbg_info Debug information provided by HAL. This field is set when event type is NEED_RESET
 *               or HAL_MDM_UNRESPONSIVE
 */
typedef struct crm_hal_evt {
    crm_hal_evt_type_t type;
    char nodes[128];
    mdm_cli_dbg_info_t *dbg_info;
} crm_hal_evt_t;

#endif /* __CRM_TYPES_HEADER__ */
