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

#ifndef __CRM_CLIENT_FACTORY_HEADER__
#define __CRM_CLIENT_FACTORY_HEADER__

#include "libmdmcli/mdm_cli.h"

typedef struct crm_client_factory crm_client_factory_t;
typedef struct crm_client_stub crm_client_stub_t;

/**
 * Callback function protype.
 *
 * @param [in] client Pointer to the client that received this event
 * @param [in] callback_data See mdm_cli.h. Note that the context variable of this structure is
 *                           the one passed in the client factory init function
 *
 * @return 0 or -1 (-1 is used in cases where a client wants to manually ack the cold reset or
 *         shutdown events)
 */
typedef int (*crm_client_stub_callback_t) (crm_client_stub_t *client,
                                           const mdm_cli_callback_data_t *callback_data);

/**
 * Initializes the client factory.
 *
 * @param [in] instance_id Instance of the CRM to connect to
 * @param [in] callback Pointer to a function that will be called on each client event.
 *             Can be NULL (in which case cold reset / shutdown events will be auto-acked)
 * @param [in] context Pointer that will be passed (unmodified) in the callback_data
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_client_factory_t *crm_client_factory_init(int instance_id, crm_client_stub_callback_t callback,
                                              void *context);

struct crm_client_factory {
    /**
     * Disposes the module
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_client_factory_t *ctx);

    /**
     * Adds a new client
     *
     * @param [in] ctx Module context
     * @param [in] name Name of the client (can be NULL for 'auto naming')
     * @param [in] event_bitmap Bitmap of the events that the client will handle
     *
     * @return a valid handle. Must be freed by calling the kill or disconnect functions
     */
    crm_client_stub_t *(*add_client)(crm_client_factory_t *ctx, const char *name, int event_bitmap);
};

struct crm_client_stub {
    /**
     * Kills the client (and releases its context)
     *
     * @param [in] ctx Module context
     */
    void (*kill)(crm_client_stub_t *ctx);

    /**
     * Attempts to connect to the CRM.
     * @param [in] ctx Module context
     *
     * @return 0 in case of success, -1 otherwise
     */
    int (*connect)(crm_client_stub_t *ctx);

    /**
     * See mdm_cli.h for description of following APIs.
     *
     * Note that disconnect will also release the client context.
     */
    int (*disconnect)(crm_client_stub_t *ctx);
    int (*acquire)(crm_client_stub_t *ctx);
    int (*release)(crm_client_stub_t *ctx);
    int (*restart)(crm_client_stub_t *ctx, mdm_cli_restart_cause_t cause,
                   const mdm_cli_dbg_info_t *data);
    int (*shutdown)(crm_client_stub_t *ctx);
    int (*nvm_bckup)(crm_client_stub_t *ctx);
    int (*ack_cold_reset)(crm_client_stub_t *ctx);
    int (*ack_shutdown)(crm_client_stub_t *ctx);
    int (*notify_dbg)(crm_client_stub_t *ctx, const mdm_cli_dbg_info_t *data);
};

#endif /* __CRM_CLIENT_FACTORY_HEADER__ */
