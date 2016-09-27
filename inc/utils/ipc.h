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

#ifndef __CRM_UTILS_IPC_HEADER__
#define __CRM_UTILS_IPC_HEADER__

#include <stdbool.h>
#include <stddef.h>

/**
 * Type of the communication pipe.
 */
typedef enum crm_ipc_type {
    CRM_IPC_PROCESS,
    CRM_IPC_THREAD,
} crm_ipc_type_t;

/**
 * Format of the message exchanged through the 'xxx_msg' functions.
 *
 * @var scalar Used to exchange 'scalar' information.
 * @var data_size Size of the data pointed to by 'msg'. Used when IPC is of type CRM_IPC_PROCESS
 *                to do the data copy between both processes.
 * @var data Used to exchange complex structures. When IPC is of type CRM_IPC_THREAD, pointer is
 *           passed unmodified to other thread (i.e. no data is copied) so the data pointed to
 *           needs to have a proper lifetime.
 */
typedef struct crm_ipc_msg {
    long long scalar;
    size_t data_size;
    void *data;
} crm_ipc_msg_t;

typedef struct crm_ipc_ctx crm_ipc_ctx_t;

/**
 * Create the monodirectional IPC pipe.
 *
 * @param [in] type Type of the pipe (inter-thread vs inter-process)
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_ipc_ctx_t *crm_ipc_init(crm_ipc_type_t type);

struct crm_ipc_ctx {
    /**
     * Disposes the module. If free_routine is non-NULL, it will iterate on all messages still
     * pending in the message queue (only valid for thread type).
     *
     * @param [in] ctx Module context
     * @param [in] free_routine Optional function that will be called on all pending messages
     */
    void (*dispose)(crm_ipc_ctx_t *ctx, void (*free_routine)(const crm_ipc_msg_t *));

    /**
     * Gets the file descriptor that needs to be polled (READ) to receive a message notification.
     * This file descriptor can only be used to receive events. It cannot be used to read, write.
     * Must not be closed.
     *
     * @param [in] ctx Module context
     *
     * @return file descriptor
     * @return -1 in case of error
     */
    int (*get_poll_fd)(crm_ipc_ctx_t *ctx);

    /**
     * Gets a message.
     *
     * This function must be called when an event is notified by the polled FD until it returns
     * false indicating that all pending messages have been read.
     *
     * In case of pipe of CRM_IPC_PROCESS type, if msg.data_size is non-zero, msg.data needs to
     * be freed.
     *
     * @param [in] ctx Module context
     * @param [out] msg Message retrieved from FIFO
     *
     * @return true if a message was retrieved, false otherwise.
     */
    bool (*get_msg)(crm_ipc_ctx_t *ctx, crm_ipc_msg_t *msg);

    /**
     * Sends a message.
     *
     * @param [in] ctx Module context
     * @param [in] msg Message to be sent
     *
     * @return true if the message was sent, false otherwise.
     */
    bool (*send_msg)(crm_ipc_ctx_t *ctx, const crm_ipc_msg_t *msg);
};

#endif /* __CRM_UTILS_IPC_HEADER__ */
