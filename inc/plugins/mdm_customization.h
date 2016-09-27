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

#ifndef __CRM_MDM_CUSTOMIZATION_HEADER__
#define __CRM_MDM_CUSTOMIZATION_HEADER__

#include "plugins/dependent_types.h"
#include "libtcs2/tcs.h"

typedef struct crm_customization_ctx crm_customization_ctx_t;

/* Used by crm_plugin_load API */
#define CRM_CUSTOMIZATION_INIT "crm_customization_init"
typedef crm_customization_ctx_t * (*crm_customization_init_t)(tcs_ctx_t *, crm_ctrl_ctx_t *);

/**
 * Initializes the module
 *
 * @param [in] tcs    TCS context
 * @param [in] ctx    Control context
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_customization_ctx_t *crm_customization_init(tcs_ctx_t *tcs, crm_ctrl_ctx_t *ctx);

struct crm_customization_ctx {
    /**
     * Disposes the module
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_customization_ctx_t *ctx);

    /**
     * Sends customization to modem
     * NB: Asynchronous API. The result of the operation is provided by the callback
     *
     * @param [in] ctx       Module context
     * @param [in] tlvs      List of customizations (TLV) files
     * @param [in] nb        Number of TLV in the list
     *
     */
    void (*send)(crm_customization_ctx_t *ctx, const char *const *tlvs, int nb);
};

#endif /* __CRM_MDM_CUSTOMIZATION_HEADER__ */
