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

#ifndef __CRM_CLIENT_ABSTRACTION_HEADER__
#define __CRM_CLIENT_ABSTRACTION_HEADER__

#include "utils/wakelock.h"
#include "plugins/dependent_types.h"
#include "libmdmcli/mdm_cli.h"
#include "libtcs2/tcs.h"

typedef struct crm_cli_abs_ctx crm_cli_abs_ctx_t;

/* Used by crm_plugin_load API */
#define CRM_CLI_ABS_INIT "crm_cli_abs_init"
typedef crm_cli_abs_ctx_t * (*crm_cli_abs_init_t)(int, bool, tcs_ctx_t *, crm_ctrl_ctx_t *,
                                                  crm_wakelock_t *);

/**
 * Modem state reported by control to client abstraction module
 */
typedef enum crm_cli_abs_mdm_state {
    /* Modem is unavailable, client needs to acquire it */
    MDM_STATE_OFF,
    /* Modem is unresponsive and is unrecoverable */
    MDM_STATE_UNRESP,
    /* Modem is unavailable and is being recovered */
    MDM_STATE_BUSY,
    /* Modem is available for clients to use */
    MDM_STATE_READY,
    /* Modem is unresponsive and platform reboot initiated */
    MDM_STATE_PLATFORM_REBOOT,
    /* Modem state is unknown (it happens at CRM start) */
    MDM_STATE_UNKNOWN,
} crm_cli_abs_mdm_state_t;

/**
 * Initializes the client abstraction module
 *
 * @param [in] inst_id     CRM instance ID
 * @param [in] sanity_mode Starts CLA in sanity mode
 * @param [in] tcs         TCS context
 * @param [in] control     Control context
 * @param [in] wakelock    Wakelock context
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_cli_abs_ctx_t *crm_cli_abs_init(int inst_id, bool sanity_mode, tcs_ctx_t *tcs,
                                    crm_ctrl_ctx_t *control, crm_wakelock_t *wakelock);

struct crm_cli_abs_ctx {
    /**
     * Disposes the module
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_cli_abs_ctx_t *ctx);

    /**
     * Notifies an event to clients
     * NB: Synchronous function
     *
     * @param [in] ctx Module context
     * @param [in] evt_id
     * @param [in] data_size
     * @param [in] data
     */
    void (*notify_client)(crm_cli_abs_ctx_t *ctx, mdm_cli_event_t evt_id, size_t data_size,
                          const void *data);

    /**
     * Notifies the modem state
     * NB: Synchronous function
     *
     * @param [in] ctx Module context
     * @param [in] mdm_state Modem state
     */
    void (*notify_modem_state)(crm_cli_abs_ctx_t *ctx, crm_cli_abs_mdm_state_t mdm_state);

    /**
     * Notifies operation result
     * Callback used by modem control to notify the operation result
     * NB: Synchronous function
     *
     * @param [in] ctx Module context
     * @param [in] result Operation result
     */
    void (*notify_operation_result)(crm_cli_abs_ctx_t *ctx, int result);
};

#endif /* __CRM_CLIENT_ABSTRACTION_HEADER__ */
