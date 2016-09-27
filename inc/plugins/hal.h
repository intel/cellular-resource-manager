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

#ifndef __CRM_HAL_HEADER__
#define __CRM_HAL_HEADER__

#include "plugins/dependent_types.h"
#include "libtcs2/tcs.h"

typedef struct crm_hal_ctx crm_hal_ctx_t;

/* Used by crm_plugin_load API */
#define CRM_HAL_INIT "crm_hal_init"
typedef crm_hal_ctx_t * (*crm_hal_init_t)(int, bool, bool, tcs_ctx_t *, crm_ctrl_ctx_t *);

typedef enum crm_hal_reset_type {
    RESET_WARM,
    RESET_COLD,
    RESET_BACKUP,
} crm_hal_reset_type_t;

/**
 * Initializes the module
 *
 * @param [in] inst_id      CRM instance ID
 * @param [in] host_debug   Starts HAL module in HOST debug mode
 * @param [in] dump_enabled Enables core dump retrieval
 * @param [in] tcs          TCS context
 * @param [in] control      Control context
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_hal_ctx_t *crm_hal_init(int inst_id, bool host_debug, bool dump_enabled, tcs_ctx_t *tcs,
                            crm_ctrl_ctx_t *control);

struct crm_hal_ctx {
    /**
     * Disposes the module
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_hal_ctx_t *ctx);

    /**
     * Power on the modem
     * NB: Asynchronous API. The result of the operation is provided indirectly by modem events
     *
     * @param [in] ctx Module context
     */
    void (*power_on)(crm_hal_ctx_t *ctx);

    /**
     * Boots the modem.
     * For flashless modems, this function must be called after the flashing operation
     * NB: Asynchronous API. The result of the operation is provided indirectly by modem events
     *
     * @param [in] ctx Module context
     */
    void (*boot)(crm_hal_ctx_t *ctx);

    /**
     * Shuts down the modem. Sends AT command and power off the modem
     * NB: Asynchronous API. The result of the operation is provided indirectly by modem events
     *
     * @param [in] ctx Module context
     */
    void (*shutdown)(crm_hal_ctx_t *ctx);

    /**
     * Resets the modem
     * NB: Asynchronous API. The result of the operation is provided indirectly by modem events
     *
     * @param [in] ctx Module context
     * @param [in] type Type of reset
     */
    void (*reset)(crm_hal_ctx_t *ctx, crm_hal_reset_type_t type);
};

#endif /* __CRM_HAL_HEADER__ */
