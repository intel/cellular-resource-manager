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

#ifndef __CRM_UTILS_THREAD_HEADER__
#define __CRM_UTILS_THREAD_HEADER__

#include <stdbool.h>

#include "utils/ipc.h"

typedef struct crm_thread_ctx crm_thread_ctx_t;

/**
 * Create the thread that will start executing the given function (with the
 * given argument).
 *
 * @param [in] start_routine main function of the created thread
 * @param [in] arg argument passed to start_routine function
 * @param [in] create_ipc set to true to associate a bi-directional IPC pipe between both threads
 * @param [in] detached set to true to make the thread detached. If true, dispose function must be
 *                      called by the thread itself.
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
/* *INDENT-OFF* */
crm_thread_ctx_t *crm_thread_init(void *(*start_routine)(crm_thread_ctx_t *, void *),
                                  void *arg, bool create_ipc, bool detached);
/* *INDENT-ON* */

struct crm_thread_ctx {
    /**
     * Disposes the module. Blocks until the created thread finishes.
     *
     * Note: if IPC is activated, communication pipes are closed before waiting on the thread to end
     *       so thread routines can use the SIGHUP on the file descriptor returned by 'get_poll_fd'
     *       as a signal the thread needs to stop.
     *
     * @param [in] ctx Module context
     * @param [in] free_routine Optional function that will be called on all pending messages
     *             (can be NULL for example if the code only uses the 'scalar' message type). Only
     *             applicable if create_ipc is set to true.
     */
    void (*dispose)(crm_thread_ctx_t *ctx, void (*free_routine)(const crm_ipc_msg_t *));

    /**
     * Gets the file descriptor that needs to be polled (READ + ERR) to receive a message
     * notification. This file descriptor can only be used to receive events. It cannot be
     * used to read, write. Must not be closed.
     *
     * Only applicable if create_ipc is set to true.
     *
     * @param [in] ctx Module context
     *
     * @return file descriptor
     * @return -1 in case of error
     */
    int (*get_poll_fd)(crm_thread_ctx_t *ctx);

    /**
     * Gets a message.
     *
     * This function must be called when an event is notified by the polled FD until it returns
     * false indicating that all messages have been read.
     *
     * Only applicable if create_ipc is set to true.
     *
     * @param [in] ctx Module context
     * @param [out] msg Message retrieved from FIFO
     *
     * @return true if a message was retrieved, false otherwise.
     */
    bool (*get_msg)(crm_thread_ctx_t *ctx, crm_ipc_msg_t *msg);

    /**
     * Sends a message.
     *
     * Only applicable if create_ipc is set to true.
     *
     * @param [in] ctx Module context
     * @param [in] msg Message to be sent
     *
     * @return true if the message was sent, false otherwise. The 'false' use case can be normal
     *         when the thread is being shut down.
     */
    bool (*send_msg)(crm_thread_ctx_t *ctx, const crm_ipc_msg_t *msg);
};

#endif /* __CRM_UTILS_THREAD_HEADER__ */
