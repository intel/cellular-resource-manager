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

#ifndef __CRM_WAKELOCK_HEADER__
#define __CRM_WAKELOCK_HEADER__

#ifdef __cplusplus
extern "C" {
#endif

enum {
    WAKELOCK_WATCHDOG_PING,
    WAKELOCK_WATCHDOG_REQ,
    WAKELOCK_CLA,
    WAKELOCK_MDMCLI
};

#include <stdbool.h>

typedef struct crm_wakelock crm_wakelock_t;

/* Used by crm_plugin_load API */
#define CRM_WAKELOCK_INIT "crm_wakelock_init"
typedef crm_wakelock_t * (*crm_wakelock_init_t)(const char *);

/**
 * Initializes the module
 *
 * @param [in] name Name of the wakelock.
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_wakelock_t *crm_wakelock_init(const char *name);

struct crm_wakelock {
    /**
     * Disposes the module
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_wakelock_t *ctx);

    /**
     * Acquires the wakelock
     *
     * @param [in] ctx Module context
     */
    void (*acquire)(crm_wakelock_t *ctx, int module_id);

    /**
     * Releases the wakelock
     *
     * @param [in] ctx Module context
     */
    void (*release)(crm_wakelock_t *ctx, int module_id);

    /**
     * Checks if wakelock is hold by the module
     *
     * @param [in] ctx Module context
     *
     * return true if wakelock is hold
     */
    bool (*is_held_by_module)(crm_wakelock_t *ctx, int module_id);

    /**
     * Checks if at least one module holds a wakelock
     *
     * @return true if wakelock is hold
     */
    bool (*is_held)(crm_wakelock_t *ctx);
};

#ifdef __cplusplus
}
#endif

#endif /* __CRM_WAKELOCK_HEADER__ */
