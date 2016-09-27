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


#ifndef __CRM_CONTROL_COMMON_HEADER__
#define __CRM_CONTROL_COMMON_HEADER__

#include <time.h>

#include "libmdmcli/mdm_cli.h"

#include "utils/plugins.h"
#include "utils/thread.h"
#include "utils/wakelock.h"
#include "plugins/control.h"
#include "plugins/client_abstraction.h"
#include "plugins/hal.h"
#include "plugins/fw_upload.h"
#include "plugins/fw_elector.h"
#include "plugins/mdm_customization.h"
#include "plugins/dump.h"
#include "plugins/escalation.h"

enum ctrl_plugins {
    PLUGIN_CLIENTS,
    PLUGIN_HAL,
    PLUGIN_FW_UPLOAD,
    PLUGIN_FW_ELECTOR,
    PLUGIN_CUSTOMIZATION,
    PLUGIN_ESCALATION,
    PLUGIN_WAKELOCK,
    PLUGIN_DUMP,
    PLUGIN_NB
};

typedef enum ctrl_events {
    EV_CLI_START = 0,
    EV_CLI_STOP,
    EV_CLI_RESET,
    EV_CLI_UPDATE,
    EV_CLI_NVM_BACKUP,

    EV_HAL_MDM_OFF,
    EV_HAL_MDM_RUN,
    EV_HAL_MDM_BUSY,
    EV_HAL_MDM_NEED_RESET,
    EV_HAL_MDM_FLASH,
    EV_HAL_MDM_DUMP,
    EV_HAL_MDM_UNRESPONSIVE,

    EV_NVM_SUCCESS,
    EV_FW_SUCCESS,
    EV_DUMP_SUCCESS,
    EV_FAILURE,
    EV_TIMEOUT,

    EV_NUM
} ctrl_events_t;

typedef enum cli_request {
    REQ_NONE = 0,
    REQ_RESET,
    REQ_STOP,
    REQ_START,
} cli_request_t;

typedef struct crm_ctrl_state {
    /* on-going request */
    cli_request_t client_request;

    /* pre-flashing information */
    bool fw_ready;
    crm_hal_evt_t *hal_evt;

    /* flashing flags */
    bool waiting_hal_busy_reason;
    bool flash_done;
    bool run_ipc;
} crm_ctrl_state_t;

typedef struct crm_ctrl_dbg_info {
    bool do_not_report;
    bool reset_initiated_by_cla;
    mdm_cli_dbg_info_t *evt;
} crm_ctrl_dbg_info_t;

typedef struct crm_control_ctx_internal {
    crm_ctrl_ctx_t ctx; //Needs to be first

    /* plugins */
    crm_plugin_t plugins[PLUGIN_NB];
    crm_cli_abs_ctx_t *clients;
    crm_hal_ctx_t *hal;
    crm_fw_upload_ctx_t *upload;
    crm_fw_elector_ctx_t *elector;
    crm_customization_ctx_t *customization;
    crm_wakelock_t *wakelock;
    crm_dump_ctx_t *dump;
    crm_escalation_ctx_t *escalation;

    /* internal variables */
    crm_ipc_ctx_t *ipc;
    crm_thread_ctx_t *watchdog;
    crm_ctrl_state_t state;

    bool is_mdm_oos;

    int inst_id;
    int watch_id;
    int timeout;

    crm_ctrl_dbg_info_t dbg_info;

    bool timer_armed;
    struct timespec timer_end;
} crm_control_ctx_internal_t;

#endif /* __CRM_CONTROL_COMMON_HEADER__ */
