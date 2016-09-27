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

#ifndef __CRM_NVM_HEADER__
#define __CRM_NVM_HEADER__

#include "plugins/dependent_types.h"
#include "libtcs2/tcs.h"

typedef struct crm_nvm_ctx crm_nvm_ctx_t;

/**
 * Initializes the module
 *
 * @param [in] inst_id Instance ID of CRM
 * @param [in] tcs     TCS context
 * @param [in] ctx     Context of the modem control
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_nvm_ctx_t *nvm_init(int inst_id, tcs_ctx_t *tcs, crm_ctrl_ctx_t *ctx);

struct crm_nvm_ctx {
    /**
     * Disposes the module
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_nvm_ctx_t *ctx);

    /**
     * Starts NVM synchronization
     * NB: Asynchronous API. The result of the operation is provided by the callback
     *
     * @param [in] ctx Module context
     */
    void (*start)(crm_nvm_ctx_t *ctx);

    /**
     * Stops NVM synchronization
     * NB: Asynchronous API. The result of the operation is provided by the callback
     *
     * @param [in] ctx Module context
     */
    void (*stop)(crm_nvm_ctx_t *ctx);

    /**
     * Prepares NVM server for flush before modem shutdown
     * DEPRECATED: should be replaced by flush
     * @TODO: challenge if this function can be removed on NVM server
     *
     * @param [in] ctx Module context
     */
    void (*prepare_for_shutdown)(crm_nvm_ctx_t *ctx);

    /**
     * Prepares NVM server for flush before modem reset
     * DEPRECATED: should be replaced by flush
     * NB: Asynchronous API. The result of the operation is provided by the callback
     *
     * @param [in] ctx Module context
     */
    void (*prepare_for_reset)(crm_nvm_ctx_t *ctx);

    /**
     * Flush NVM data
     * @TODO: to be implemented with NVM server 2
     * NB: Asynchronous API. The result of the operation is provided by the callback
     *
     * @param [in] ctx Module context
     */
    void (*flush)(crm_nvm_ctx_t *ctx);
};

#endif /* __CRM_NVM_HEADER__ */
