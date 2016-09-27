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

#include <errno.h>
#include <string.h>
#include <unistd.h>

#define CRM_MODULE_TAG "HAL"
#include "utils/common.h"
#include "utils/keys.h"
#include "utils/logs.h"
#include "utils/property.h"
#include "utils/socket.h"
#include "utils/thread.h"

#include "daemons.h"

#define MAX_SOCKET_TIMEOUT 1000

/**
 * @see daemons.h
 */
void crm_hal_daemon_init(crm_hal_daemon_ctx_t *ctx, const char *socket_name)
{
    ASSERT(ctx != NULL);
    memset(ctx, 0, sizeof(*ctx));
    for (int i = 0; i < CRM_HAL_MAX_DAEMONS; i++) {
        ctx->pending[i] = -1;
        ctx->daemons[i].socket = -1;
    }

    ctx->server_socket = crm_socket_create(socket_name, CRM_HAL_MAX_DAEMONS);
    DASSERT(ctx->server_socket >= 0, "Could not open control socket (%d / %s)", errno,
            strerror(errno));
}

/**
 * @see daemons.h
 */
void crm_hal_daemon_dispose(crm_hal_daemon_ctx_t *ctx)
{
    ASSERT(ctx != NULL);
    for (int i = 0; i < CRM_HAL_MAX_DAEMONS; i++) {
        if (ctx->daemons[i].socket >= 0) {
            close(ctx->daemons[i].socket);
            ctx->daemons[i].socket = -1;
        }
    }
    close(ctx->server_socket);
    ctx->server_socket = -1;
}

/**
 * @see daemons.h
 */
int crm_hal_daemon_get_sockets(crm_hal_daemon_ctx_t *ctx, struct pollfd *pfd)
{
    ASSERT(ctx != NULL);
    ASSERT(pfd);
    ASSERT(ctx->server_socket >= 0);

    pfd[0].fd = ctx->server_socket;
    pfd[0].events = POLLIN;

    int n = 1;
    for (int i = 0; i < CRM_HAL_MAX_DAEMONS; i++) {
        if ((ctx->daemons[i].name != NULL) && (ctx->daemons[i].socket >= 0)) {
            pfd[n].fd = ctx->daemons[i].socket;
            pfd[n].events = POLLIN;
            n += 1;
            ASSERT(n <= (1 + CRM_HAL_MAX_DAEMONS));
        }
    }
    for (int i = 0; i < CRM_HAL_MAX_DAEMONS; i++) {
        if (ctx->pending[i] >= 0) {
            pfd[n].fd = ctx->pending[i];
            pfd[n].events = POLLIN;
            n += 1;
            ASSERT(n <= (1 + CRM_HAL_MAX_DAEMONS));
        }
    }
    return n;
}

/**
 * @see daemons.h
 */
int crm_hal_daemon_add(crm_hal_daemon_ctx_t *ctx,
                       const char *service_name,
                       crm_hal_daemon_callback_t callback,
                       void *callback_ctx)
{
    ASSERT(ctx != NULL);
    ASSERT(service_name);
    ASSERT(callback);

    int i;
    for (i = 0; i < CRM_HAL_MAX_DAEMONS; i++)
        if (ctx->daemons[i].name == NULL)
            break;
    if (i == CRM_HAL_MAX_DAEMONS) {
        DASSERT(0, "Trying to register too many daemons");
        return -1;
    }
    ctx->daemons[i].name = strdup(service_name);
    ASSERT(ctx->daemons[i].name);
    ctx->daemons[i].callback = callback;
    ctx->daemons[i].ctx = callback_ctx;
    ctx->daemons[i].socket = -1;

    crm_property_set(CRM_KEY_SERVICE_STOP, service_name);

    return i;
}

/**
 * @see daemons.h
 */
void crm_hal_daemon_remove(crm_hal_daemon_ctx_t *ctx, int id)
{
    /* All ASSERTs are done in 'stop' */
    crm_hal_daemon_stop(ctx, id);

    free(ctx->daemons[id].name);
    ctx->daemons[id].name = NULL;
}

/**
 * @see daemons.h
 */
void crm_hal_daemon_start(crm_hal_daemon_ctx_t *ctx, int id)
{
    ASSERT(ctx != NULL);
    ASSERT(id >= 0 && id < CRM_HAL_MAX_DAEMONS);
    ASSERT(ctx->daemons[id].name != NULL);
    ASSERT(ctx->daemons[id].socket == -1);

    crm_property_set(CRM_KEY_SERVICE_START, ctx->daemons[id].name);
}

/**
 * @see daemons.h
 */
void crm_hal_daemon_stop(crm_hal_daemon_ctx_t *ctx, int id)
{
    ASSERT(ctx != NULL);
    ASSERT(id >= 0 && id < CRM_HAL_MAX_DAEMONS);
    ASSERT(ctx->daemons[id].name != NULL);

    crm_property_set(CRM_KEY_SERVICE_STOP, ctx->daemons[id].name);
    if (ctx->daemons[id].socket >= 0) {
        close(ctx->daemons[id].socket);
        ctx->num_sockets -= 1;
        ASSERT(ctx->num_sockets >= 0);
        ctx->daemons[id].socket = -1;
    }
}

/**
 * @see daemons.h
 */
crm_hal_daemon_poll_t crm_hal_daemon_handle_poll(crm_hal_daemon_ctx_t *ctx,
                                                 const struct pollfd *pfd,
                                                 int *cb_ret)
{
    ASSERT(ctx != NULL);
    ASSERT(pfd != NULL);

    crm_hal_daemon_poll_t ret = HAL_DAEMON_NO_CALLBACK_RETVALUE_SET;

    if (pfd->fd == ctx->server_socket) {
        /* Event received on the server socket. Not supposed to have anything else than POLLIN */
        DASSERT(pfd->revents == POLLIN, "Event received: 0x%x", pfd->revents);

        /* Client connected to server socket, put its socket in pending waiting for its
         * registration */
        int socket = crm_socket_accept(ctx->server_socket);
        if (socket >= 0) {
            int i;
            for (i = 0; i < CRM_HAL_MAX_DAEMONS; i++)
                if (ctx->pending[i] == -1)
                    break;
            if (i == CRM_HAL_MAX_DAEMONS || ctx->num_sockets >= CRM_HAL_MAX_DAEMONS) {
                close(socket);
                DASSERT(0, "not able to handle incoming daemon connection (%d / %d)", i,
                        ctx->num_sockets);
            } else {
                ctx->num_sockets += 1;
                ctx->pending[i] = socket;
            }
        }
        return ret;
    }
    for (int i = 0; i < CRM_HAL_MAX_DAEMONS; i++) {
        if (pfd->fd == ctx->daemons[i].socket) {
            /* Event received on an already connected daemon */
            bool close_socket = true;
            if (pfd->revents == POLLIN) {
                /* Read message from daemon */
                int msg_hdr[2];
                if (crm_socket_read(pfd->fd, MAX_SOCKET_TIMEOUT, msg_hdr, sizeof(msg_hdr)) == 0) {
                    ASSERT(ctx->daemons[i].callback);
                    ctx->daemons[i].msg_to_read = msg_hdr[1];
                    int i_cb_ret = ctx->daemons[i].callback(i, ctx->daemons[i].ctx,
                                                            HAL_DAEMON_DATA_IN,
                                                            msg_hdr[0], msg_hdr[1]);
                    if (cb_ret) {
                        ret = HAL_DAEMON_CALLBACK_RETVALUE_SET;
                        *cb_ret = i_cb_ret;
                    }
                    close_socket = false;
                }
            }

            if (close_socket) {
                /* Handle daemon disconnection or error during reading */
                ASSERT(ctx->daemons[i].callback);
                crm_hal_daemon_stop(ctx, i);
                int i_cb_ret = ctx->daemons[i].callback(i, ctx->daemons[i].ctx,
                                                        HAL_DAEMON_DISCONNECTED, 0, 0);
                if (cb_ret) {
                    ret = HAL_DAEMON_CALLBACK_RETVALUE_SET;
                    *cb_ret = i_cb_ret;
                }
            }
            return ret;
        }
    }
    for (int i = 0; i < CRM_HAL_MAX_DAEMONS; i++) {
        if (pfd->fd == ctx->pending[i]) {
            bool close_socket = true;
            /* Event received on an connecting daemon */
            if (pfd->revents == POLLIN) {
                /* Read registration message from daemon */
                int name_size;
                if (crm_socket_read(pfd->fd, MAX_SOCKET_TIMEOUT, &name_size,
                                    sizeof(name_size)) == 0 &&
                    name_size < CRM_PROPERTY_VALUE_MAX) {
                    char client_name[CRM_PROPERTY_VALUE_MAX];
                    if (crm_socket_read(pfd->fd, MAX_SOCKET_TIMEOUT, client_name, name_size) == 0) {
                        /* Now search if it corresponds to a registered daemon */
                        client_name[name_size] = '\0';
                        for (int id = 0; id < CRM_HAL_MAX_DAEMONS; id++) {
                            if (ctx->daemons[id].name != NULL &&
                                !strcmp(ctx->daemons[id].name, client_name)) {
                                if (ctx->daemons[id].socket >= 0) {
                                    /* Client registers again !!! */
                                    crm_hal_daemon_remove(ctx, id);
                                    ASSERT(ctx->daemons[id].callback);
                                    int i_cb_ret =
                                        ctx->daemons[id].callback(id, ctx->daemons[id].ctx,
                                                                  HAL_DAEMON_DISCONNECTED,
                                                                  0, 0);
                                    if (cb_ret) {
                                        ret = HAL_DAEMON_CALLBACK_RETVALUE_SET;
                                        *cb_ret = i_cb_ret;
                                    }
                                } else {
                                    ctx->daemons[id].socket = ctx->pending[i];
                                    ctx->pending[i] = -1;
                                    ASSERT(ctx->daemons[id].callback);
                                    int i_cb_ret =
                                        ctx->daemons[id].callback(id, ctx->daemons[id].ctx,
                                                                  HAL_DAEMON_CONNECTED,
                                                                  0, 0);
                                    if (cb_ret) {
                                        ret = HAL_DAEMON_CALLBACK_RETVALUE_SET;
                                        *cb_ret = i_cb_ret;
                                    }
                                    close_socket = false;
                                }
                            }
                        }
                    }
                }
            }

            if (close_socket) {
                /* Daemon disconnects before sending registration or error during reading of
                 * registration message. */
                close(ctx->pending[i]);
                ctx->pending[i] = -1;
                ctx->num_sockets -= 1;
                ASSERT(ctx->num_sockets >= 0);
            }
            return ret;
        }
    }
    return HAL_DAEMON_POLL_NOT_HANDLED;
}

/**
 * @see daemons.h
 */
ssize_t crm_hal_daemon_msg_read(crm_hal_daemon_ctx_t *ctx, int id, void *data, size_t data_len)
{
    ASSERT(ctx != NULL);
    ASSERT(id >= 0 && id < CRM_HAL_MAX_DAEMONS);
    ASSERT(data != NULL);
    ASSERT(ctx->daemons[id].msg_to_read > 0 &&
           data_len >= ctx->daemons[id].msg_to_read);
    ASSERT(ctx->daemons[id].socket >= 0);

    if (crm_socket_read(ctx->daemons[id].socket, MAX_SOCKET_TIMEOUT, data,
                        ctx->daemons[id].msg_to_read) == 0)
        return ctx->daemons[id].msg_to_read;
    else
        return -1;
}

/**
 * @see daemons.h
 */
ssize_t crm_hal_daemon_msg_send(crm_hal_daemon_ctx_t *ctx, int id, int msg_id, void *data,
                                size_t data_len)
{
    ASSERT(ctx != NULL);
    ASSERT(id >= 0 && id < CRM_HAL_MAX_DAEMONS);
    ASSERT(data_len == 0 || data);
    ASSERT(ctx->daemons[id].socket >= 0);

    int msg_hdr[2] = { msg_id, data_len };
    if (crm_socket_write(ctx->daemons[id].socket, MAX_SOCKET_TIMEOUT, msg_hdr,
                         sizeof(msg_hdr)))
        return -1;
    if ((data_len > 0) &&
        (crm_socket_write(ctx->daemons[id].socket, MAX_SOCKET_TIMEOUT, data, data_len)))
        return -1;

    return data_len;
}
