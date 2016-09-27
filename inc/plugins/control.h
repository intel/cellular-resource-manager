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

#ifndef __CRM_CONTROL_HEADER__
#define __CRM_CONTROL_HEADER__

#include "plugins/dependent_types.h"
#include "utils/process_factory.h"
#include "libmdmcli/mdm_cli.h"
#include "libtcs2/tcs.h"

typedef enum crm_ctrl_restart_type {
    CTRL_MODEM_RESTART = 1,
    CTRL_MODEM_UPDATE,
    CTRL_BACKUP_NVM
} crm_ctrl_restart_type_t;

/* Used by crm_plugin_load API */
#define CRM_CTRL_INIT "crm_ctrl_init"
typedef crm_ctrl_ctx_t * (*crm_ctrl_init_t)(int, tcs_ctx_t *, crm_process_factory_ctx_t *);

/**
 * Initializes the modem control module
 *
 * @param [in] inst_id  CRM instance ID
 * @param [in] tcs      TCS context
 * @param [in] factory  Process factory context
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_ctrl_ctx_t *crm_ctrl_init(int inst_id, tcs_ctx_t *tcs, crm_process_factory_ctx_t *factory);

struct crm_ctrl_ctx {
    /**
     * Disposes the module
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_ctrl_ctx_t *ctx);

    /**
     * Launches the main event loop. This function returns only in case of error
     *
     * @param [in] ctx Module context
     */
    void (*event_loop)(crm_ctrl_ctx_t *ctx);

    /**
     * Functions used by client abstraction module:
     */

    /**
     * Starts the modem
     * NB: Asynchronous API. The callback is used to notify the operation result
     *
     * @param [in] ctx Module context
     */
    void (*start)(crm_ctrl_ctx_t *ctx);

    /**
     * Stops the modem
     * NB: Asynchronous API. The callback is used to notify the operation result
     *
     * @param [in] ctx Module context
     */
    void (*stop)(crm_ctrl_ctx_t *ctx);

    /**
     * Restarts the modem
     * NB: Asynchronous API. The callback is used to notify the operation result
     *
     * @param [in] ctx    Module context
     * @param [in] cause  Reset type
     * @param [in] dbg    Debug data
     */
    void (*restart)(crm_ctrl_ctx_t *ctx, crm_ctrl_restart_type_t type,
                    const mdm_cli_dbg_info_t *dbg);

    /**
     * Callback functions used by module to notify events:
     */

    /**
     * Notifies a HAL event
     * NB: Synchronous API
     *
     * @param [in] ctx    Module context
     * @param [in] event  HAL event
     */
    void (*notify_hal_event)(crm_ctrl_ctx_t *ctx, const crm_hal_evt_t *event);

    /**
     * Notifies a NVM status
     * NB: Synchronous API
     *
     * @param [in] ctx    Module context
     * @param [in] status Status 0 if successful
     */
    void (*notify_nvm_status)(crm_ctrl_ctx_t *ctx, int status);

    /**
     * Notifies a Firmware Upload status
     * NB: Synchronous API
     *
     * @param [in] ctx    Module context
     * @param [in] status Status 0 if successful
     */
    void (*notify_fw_upload_status)(crm_ctrl_ctx_t *ctx, int status);

    /**
     * Notifies a Firmware customization status
     * NB: Synchronous API
     *
     * @param [in] ctx    Module context
     * @param [in] status Status 0 if successful
     */
    void (*notify_customization_status)(crm_ctrl_ctx_t *ctx, int status);

    /**
     * Notifies a Dump status
     * NB: Synchronous API
     *
     * @param [in] ctx    Module context
     * @param [in] status Status 0 if successful
     */
    void (*notify_dump_status)(crm_ctrl_ctx_t *ctx, int status);

    /**
     * Others:
     */

    /**
     * Notifies an event to all clients
     * Pass-through function used to notify an event to all clients
     * NB: Synchronous API
     *
     * @param [in] ctx        Module context
     * @param [in] evt_id     Event ID
     * @param [in] data_size  Size of data
     * @param [in] data       Data to transfer to clients
     */
    void (*notify_client)(crm_ctrl_ctx_t *ctx, mdm_cli_event_t evt_id, size_t data_size,
                          const void *data);
};

#endif /* __CRM_CONTROL_HEADER__ */
