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

#ifndef __CRM_HEADER_HAL_DAEMONS__
#define __CRM_HEADER_HAL_DAEMONS__

#include <stdbool.h>
#include <sys/types.h>
#include <poll.h>

#define CRM_HAL_MAX_DAEMONS 2

/**
 * Daemon event types for callback
 */
typedef enum crm_hal_daemon_evt {
    HAL_DAEMON_CONNECTED,    // Daemon has started and is connected to CRM
    HAL_DAEMON_DISCONNECTED, // Daemon has disconnected from CRM. When called, daemon has been
                             // force-stopped in case it was still running
    HAL_DAEMON_DATA_IN       // Daemon has sent data to CRM
} crm_hal_daemon_evt_t;

/**
 * Return types for 'handle_poll'
 */
typedef enum crm_hal_daemon_poll {
    HAL_DAEMON_POLL_NOT_HANDLED,
    HAL_DAEMON_NO_CALLBACK_RETVALUE_SET,
    HAL_DAEMON_CALLBACK_RETVALUE_SET
} crm_hal_daemon_poll_t;

/**
 * Callback to handle daemon events
 *
 * @param [in] id       identifier of the daemon
 * @param [in] ctx      pointer to context given at daemon registration
 * @param [in] evt      event that triggered this callback
 * @param [in] msg_id   id of the message sent by the daemon (in case of DATA_IN event)
 * @param [in] msg_len  length of the additional data (in case of DATA_IN event)
 *                      To be read with a call to crm_hal_daemon_msg_read function in the callback.
 *
 * @return value that will be passed through 'handle_poll' caller.
 */
typedef int (*crm_hal_daemon_callback_t)(int id, void *ctx, crm_hal_daemon_evt_t evt, int msg_id,
                                         size_t msg_len);

typedef struct crm_hal_daemons {
    char *name;
    crm_hal_daemon_callback_t callback;
    void *ctx;
    int socket;
    size_t msg_to_read;
} crm_hal_daemons_t;

typedef struct crm_hal_daemon_ctx {
    int server_socket;
    int num_sockets;
    int pending[CRM_HAL_MAX_DAEMONS];
    crm_hal_daemons_t daemons[CRM_HAL_MAX_DAEMONS];
} crm_hal_daemon_ctx_t;

/**
 * Initialize daemon service
 *
 * @param [in] ctx           daemon context to initialize
 * @param [in] socket_name   name of the control socket to open
 */
void crm_hal_daemon_init(crm_hal_daemon_ctx_t *ctx, const char *socket_name);

/**
 * Disposes daemon service
 *
 * @param [in] ctx           daemon context to dispose
 */
void crm_hal_daemon_dispose(crm_hal_daemon_ctx_t *ctx);

/**
 * Get list of socket to listen to in poll system call
 *
 * @param [in] ctx      daemon context for which to get the sockets
 * @param [in/out] pfd  pointer to first 'poll' entry to fill. Maximum 1 + CRM_HAL_MAX_DAEMONS will
 *                      be filled in
 *
 * @return number of entry actually filled
 */
int crm_hal_daemon_get_sockets(crm_hal_daemon_ctx_t *ctx, struct pollfd *pfd);

/**
 * Add daemon to be handled at HAL side. Note that this will by default stop the daemon
 * if still running.
 *
 * @param [in] ctx           daemon context in which the daemon is added
 * @param [in] service_name  name of the service to start
 * @param [in] callback      callback that will handle the various daemon events
 * @param [in] callback_ctx  pointer to context given as callback parameter
 *
 * @return daemon id to be used in further calls to service, -1 in case of failure
 */
int crm_hal_daemon_add(crm_hal_daemon_ctx_t *ctx, const char *service_name,
                       crm_hal_daemon_callback_t callback, void *callback_ctx);

/**
 * Remove daemon. This will also stop the daemon.
 *
 * @param [in] ctx daemon context in which to remove the daemon
 * @param [in] id  identifier of the daemon to remove
 */
void crm_hal_daemon_remove(crm_hal_daemon_ctx_t *ctx, int id);

/**
 * Start daemon
 *
 * @param [in] ctx daemon context in which to start the daemon
 * @param [in] id  identifier of the daemon to start
 */
void crm_hal_daemon_start(crm_hal_daemon_ctx_t *ctx, int id);

/**
 * Stop daemon
 *
 * @param [in] ctx daemon context in which to stop the daemon
 * @param [in] id  identifier of the daemon to stop
 */
void crm_hal_daemon_stop(crm_hal_daemon_ctx_t *ctx, int id);

/**
 * Read data coming from daemon
 *
 * @param [in] ctx       daemon context in which the message is to be read
 * @param [in] id        identifier of the daemon for which data is to be read
 * @param [in] data      pointer to store the message
 * @param [in] data_len  length of data pointer (needs to be bigger than msg_len callback parameter)
 *
 * @return length of data read, -1 in case of errors
 */
ssize_t crm_hal_daemon_msg_read(crm_hal_daemon_ctx_t *ctx, int id, void *data, size_t data_len);

/**
 * Send data to a daemon
 *
 * @param [in] ctx       daemon context in which the message is to be sent
 * @param [in] id        identifier of the daemon for which data is to be sent
 * @param [in] msg_id    message id
 * @param [in] data      pointer to additional data in the message
 * @param [in] data_len  length of data to send
 *
 * @return length of data sent, -1 in case of errors (it will always be data_len or -1)
 */
ssize_t crm_hal_daemon_msg_send(crm_hal_daemon_ctx_t *ctx, int id, int msg_id, void *data,
                                size_t data_len);

/**
 * Pass user-received 'poll' event to daemon service for the latter to handle appropriately.
 *
 * Note that all 'poll' events can be passed to this function as it will check if the file
 * descriptor is handled by the daemon service or not.
 *
 * @param [in] ctx     daemon context for which this event is to be checked.
 * @param [in] pfd     pointer to 'struct pollfd' on which an event has been detected
 * @param [out] cb_ret return value of the callback (if a callback has been called)
 *
 * @return POLL_NOT_HANDLED if the file descriptor is not handled by the daemon management;
 *         NO_CALLBACK_RETVALUE_SET if the poll is handled but no callback called
 *         CALLBACK_REVATLUE_SET if a callback was called
 */
crm_hal_daemon_poll_t crm_hal_daemon_handle_poll(crm_hal_daemon_ctx_t *ctx,
                                                 const struct pollfd *pfd, int *cb_ret);

#endif /* __CRM_HEADER_HAL_DAEMONS__ */
