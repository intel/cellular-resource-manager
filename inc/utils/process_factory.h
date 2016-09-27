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

#ifndef __CRM_UTILS_PROCESS_FACTORY_HEADER__
#define __CRM_UTILS_PROCESS_FACTORY_HEADER__

#include <stdbool.h>

#include "utils/ipc.h"

/**
 * Process factory is an helper used by CRM to create processes.
 *
 * Process factory loads the library specified in create() function and then calls the
 * start_process() function which must follow this prototype:
 *
 * void start_process(crm_ipc_ctx_t *in, crm_ipc_ctx_t *out, void *data, size_t data_size);
 */

typedef struct crm_process_factory_ctx crm_process_factory_ctx_t;

/**
 * Creates the process factory. This is an helper that allows to create processes and establish
 * communication with it
 *
 * @param[in] nb Maximum number of process
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_process_factory_ctx_t *crm_process_factory_init(int nb);

struct crm_process_factory_ctx {
    /**
     * Disposes the module. Blocks until the created processes finishes.
     *
     * Note: communication pipes are closed before waiting on the process to end so process
     *       routines can use the SIGHUP on the file descriptor returned by 'get_poll_fd' as a
     *       signal the process needs to stop.
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_process_factory_ctx_t *ctx);

    /**
     * Creates a new process
     *
     * @param [in] ctx         Module context
     * @param [in] plugin_name Name of the plugin to be loaded
     * @param [in] data        Data passed to start_process() function
     * @param [in] data_len    Size of data, passed to start_process() function
     *
     * @return process ID (>= 0)
     * @return -1 in case of error
     */
    int (*create)(crm_process_factory_ctx_t *ctx, const char *plugin_name, void *data,
                  size_t data_len);

    /**
     * Parent of child process shall call this function once last message has been read.
     *
     * @param [in] ctx        Module context
     * @param [in] process_id Process ID
     */
    void (*clean)(crm_process_factory_ctx_t *ctx, int process_id);

    /**
     * Kills the specified process
     *
     * @param [in] ctx        Module context
     * @param [in] process_id Process ID
     */
    void (*kill)(crm_process_factory_ctx_t *ctx, int process_id);

    /**
     * Gets the file descriptor that needs to be polled (READ + ERR) to receive a message
     * notification. This file descriptor can only be used to receive events. It cannot be
     * used to read, write. Must not be closed.
     *
     * @param [in] ctx        Module context
     * @param [in] process_id Process ID
     *
     * @return file descriptor
     * @return -1 in case of error
     */
    int (*get_poll_fd)(crm_process_factory_ctx_t *ctx, int process_id);

    /**
     * Gets a message.
     *
     * @param [in]  ctx     Module context
     * @param [in]  process Process context
     * @param [out] msg     Message retrieved from FIFO
     *
     * @return true if a message was retrieved, false otherwise.
     */
    bool (*get_msg)(crm_process_factory_ctx_t *ctx, int process_id, crm_ipc_msg_t *msg);

    /**
     * Sends a message.
     *
     * @param [in] ctx     Module context
     * @param [in] process Process context
     * @param [in] msg     Message to be sent
     *
     * @return true if the message was sent, false otherwise. The 'false' use case can be normal
     *         when the process is being shut down.
     */
    bool (*send_msg)(crm_process_factory_ctx_t *ctx, int process_id, const crm_ipc_msg_t *msg);
};

#endif /* __CRM_UTILS_PROCESS_FACTORY_HEADER__ */
