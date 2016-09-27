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

#ifndef __CRM_ESCALATION_HEADER__
#define __CRM_ESCALATION_HEADER__

#include "libtcs2/tcs.h"

typedef struct crm_escalation_ctx crm_escalation_ctx_t;

/* Used by crm_plugin_load API */
#define CRM_ESCALATION_INIT "crm_escalation_init"
typedef crm_escalation_ctx_t * (*crm_escalation_init_t)(bool, tcs_ctx_t *);

typedef enum crm_escalation_next_step {
    STEP_MDM_WARM_RESET = 1,
    STEP_MDM_COLD_RESET,
    STEP_PLATFORM_REBOOT,
    STEP_OOS,
    STEP_NUM,
} crm_escalation_next_step_t;

/**
 * Initializes the module
 *
 * @param [in] sanity_mode Starts Escalation module in sanity mode
 * @param [in] tcs         TCS context
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_escalation_ctx_t *crm_escalation_init(bool sanity_mode, tcs_ctx_t *tcs);

struct crm_escalation_ctx {
    /**
     * Disposes the module
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_escalation_ctx_t *ctx);

    /**
     * Gets next step to do in the escalation recovery
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     *
     * @return Type of next step
     */
    crm_escalation_next_step_t (*get_next_step)(crm_escalation_ctx_t *ctx);

    /**
     * Gets last step to do in the escalation recovery
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     *
     * @return Type of last step
     */
    crm_escalation_next_step_t (*get_last_step)(crm_escalation_ctx_t *ctx);
};

#endif /* __CRM_ESCALATION_HEADER__ */
