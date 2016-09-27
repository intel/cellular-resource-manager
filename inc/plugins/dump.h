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

#ifndef __CRM_CORE_DUMP_HEADER__
#define __CRM_CORE_DUMP_HEADER__

#include <stdbool.h>

#include "plugins/dependent_types.h"
#include "utils/process_factory.h"
#include "libtcs2/tcs.h"

typedef struct crm_dump_ctx crm_dump_ctx_t;

/* Used by crm_plugin_load API */
#define CRM_DUMP_INIT "crm_dump_init"
typedef crm_dump_ctx_t * (*crm_dump_init_t)(tcs_ctx_t *, crm_ctrl_ctx_t *,
                                            crm_process_factory_ctx_t *, bool);

typedef enum crm_dump_evt {
    DUMP_SUCCESS = 1,
    /* core dump retrieval took too much time. The operation has been aborted */
    DUMP_TIMEOUT,
    /* A protocol error happened during the core dump retrieval */
    DUMP_PROTOCOL_ERR,
    /* not able to open the fd (device not available) */
    DUMP_LINK_ERR,
    /* Modem has self reset during the core dump transmission. Information provided by HAL */
    DUMP_SELF_RESET,
    /* generic failure */
    DUMP_OTHER_ERR,
} crm_dump_evt_t;

/**
 * Initializes the module
 *
 * @param [in] tcs        TCS context
 * @param [in] ctrl       Control context
 * @param [in] factory    Process factory context
 * @param [in] host_debug Enable HOST debug mode
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_dump_ctx_t *crm_dump_init(tcs_ctx_t *tcs, crm_ctrl_ctx_t *ctrl,
                              crm_process_factory_ctx_t *factory, bool host_debug);

struct crm_dump_ctx {
    /**
     * Disposes the module
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_dump_ctx_t *ctx);

    /**
     * Reads the modem core dump and stores it to file system.
     * NB: Asynchronous API. The result of the operation is provided by the callback
     *
     * @param [in] ctx   Module context
     * @param [in] nodes Nodes to be used to read the modem dump. Nodes are separated by a ';'
     * @param [in] fw    Firmware used by some type of modems to retrieve the core dump
     */
    void (*read)(crm_dump_ctx_t *ctx, const char *nodes, const char *fw);

    /**
     * Stops the core dump retrieval.
     * NB: Shall be called only if an external error is detected
     * NB2: Synchronous API
     *
     * @param [in] ctx  Module context
     */
    void (*stop)(crm_dump_ctx_t *ctx);
};

#endif /* __CRM_CORE_DUMP_HEADER__ */
