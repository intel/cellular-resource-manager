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

#ifndef __CRM_HEADER_HAL_SOFIA_COMMON__
#define __CRM_HEADER_HAL_SOFIA_COMMON__

#include <stdbool.h>
#include <time.h>

#include "utils/thread.h"
#include "plugins/hal.h"
#include "plugins/control.h"

typedef enum hal_events {
    /* control requests */
    EV_POWER,
    EV_BOOT,
    EV_STOP,
    EV_RESET,
    EV_BACKUP,

    /* modem events */
    EV_MDM_OFF,
    EV_MDM_ON,
    EV_MDM_TRAP,
    EV_MDM_FW_FAIL,
    EV_MDM_RUN,
    EV_TIMEOUT,

    /* RPC Daemon events */
    EV_RPC_DEAD,
    EV_RPC_RUN,

    EV_NUM
} hal_events_t;

typedef struct crm_hal_ctx_internal {
    crm_hal_ctx_t ctx; //Needs to be first

    /* modules */
    crm_ctrl_ctx_t *control;
    crm_ipc_ctx_t *ipc;
    crm_thread_ctx_t *thread_fsm;
    crm_thread_ctx_t *thread_ping;

    /* configuration */
    char *uevent_vmodem;
    char *vmodem_sysfs_mdm_state;
    char *vmodem_sysfs_mdm_ctrl;
    char *ping_node;
    char *dump_node;
    char *flash_node;
    bool dump_enabled;
    bool silent_reset_enabled;
    int ping_timeout;
    bool secvm_flash;
    bool support_mdm_up_on_start;

    /* variables */
    int s_fd; // socket (uevent) file descriptor
    bool stopping;
    bool timer_armed;
    struct timespec timer_end;
    int mdm_state;

    /* RPC Daemon management */
    int rpcd_inotify_socket;
    int rpcd_folder_watch;
    bool rpcd_running;
} crm_hal_ctx_internal_t;

#endif /* __CRM_HEADER_HAL_SOFIA_COMMON__ */
