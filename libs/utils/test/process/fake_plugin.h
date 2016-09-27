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

#ifndef __CRM_FAKE_PLUGIN_HEADER__
#define __CRM_FAKE_PLUGIN_HEADER__

#include <stdbool.h>

#include "utils/process_factory.h"

typedef struct crm_fake_plugin_ctx crm_fake_plugin_ctx_t;

/**
 * Initializes the module
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_fake_plugin_ctx_t *crm_fake_plugin_init(crm_process_factory_ctx_t *factory,
                                            void (*notify)(void));

struct crm_fake_plugin_ctx {
    /**
     * Disposes the module
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_fake_plugin_ctx_t *ctx);

    /**
     *
     * @param [in] ctx      Module context
     * @param [in] deadlock Makes the function hang if true
     */
    int (*start)(crm_fake_plugin_ctx_t *ctx, bool deadlock);

    void (*kill)(crm_fake_plugin_ctx_t *ctx);
};

#endif /* __CRM_FAKE_PLUGIN_HEADER__ */
