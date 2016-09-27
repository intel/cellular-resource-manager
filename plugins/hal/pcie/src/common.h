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

#ifndef __CRM_HEADER_HAL_PCIE_COMMON__
#define __CRM_HEADER_HAL_PCIE_COMMON__

#include <sys/stat.h>
#include <stdbool.h>
#include <time.h>

#include "utils/thread.h"
#include "plugins/hal.h"
#include "plugins/control.h"

#include "daemons.h"

#define NVM_FILE_PERMISSION (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)

typedef enum hal_events {
    /* control requests */
    EV_POWER,
    EV_BOOT,
    EV_STOP,
    EV_RESET,
    EV_BACKUP,

    /* PCIE events */
    EV_MDM_OFF,        // NL_EVENT_MDM_NOT_READY
    EV_MDM_FLASH,      // NL_EVENT_ROM_READY
    EV_MDM_RUN,        // NL_EVENT_MDM_READY
    EV_MDM_CRASH,      // NL_EVENT_CRASH
    EV_MDM_DUMP_READY, // NL_EVENT_CD_READY
    EV_MDM_LINK_DOWN,  // NL_EVENT_CD_READY_LINK_DOWN

    /* other modem related events */
    EV_MDM_CONFIGURED,
    EV_MUX_ERR,
    EV_MUX_DEAD,
    EV_TIMEOUT,

    /* RPC Daemon events */
    EV_RPC_DEAD,
    EV_RPC_RUN,

    /* NVM server events */
    EV_NVM_RUN,
    EV_NVM_STOP,

    EV_NUM
} hal_events_t;

typedef enum ctrl_request {
    REQ_NONE = 0,
    REQ_POWER,
    REQ_BOOT,
    REQ_RESET,
    REQ_STOP,
} ctrl_request_t;

typedef struct crm_hal_ctx_internal {
    crm_hal_ctx_t ctx; //Needs to be first

    /* modules */
    crm_ctrl_ctx_t *control;
    crm_ipc_ctx_t *ipc;
    crm_thread_ctx_t *thread_fsm;
    crm_thread_ctx_t *thread_cfg;

    /* configuration */
    int inst_id;
    int ping_timeout;
    char *flash_node;
    char *modem_node;
    char *ping_mux_node;
    char *shutdown_node;
    char *pcie_pwr_ctrl;
    char *pcie_rtpm; /* interface to enable/disable PCIe runtime PM*/
    char *pcie_rm;
    char *pcie_rescan;
    char *nvm_calib_file;
    char *nvm_calib_bkup_file;
    bool nvm_calib_bkup_is_raw;

    /* variables */
    int mux_fd;
    int mcd_fd;
    int net_fd;
    int mdm_state;
    bool timer_armed;
    bool first_expiration;
    struct timespec timer_end;

    ctrl_request_t request;
    bool backup;
    bool dump_disabled;

    /* NVM manager management */
    crm_hal_daemon_ctx_t daemon_ctx;
    int nvm_daemon_id;
    bool nvm_daemon_connected;
    bool nvm_daemon_syncing;

    /* RPC Daemon management */
    int rpcd_inotify_socket;
    int rpcd_folder_watch;
    bool rpcd_running;
} crm_hal_ctx_internal_t;

#endif /* __CRM_HEADER_HAL_PCIE_COMMON__ */
