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

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "libmdmcli/mdm_cli.h"

#define CRM_MODULE_TAG "CLIW"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/time.h"
#include "utils/socket.h"
#include "plugins/mdmcli_wire.h"

#define MAX_SOCKET_TIMEOUT 1000
#define MAX_SOCKET_NAME 16

/* The maximum message size is for a 'restart' message that contains:
 * - HEADER
 *   - Event/Request ID : 'sizeof(uint32_t) bytes'
 *   - data len         : 'sizeof(uint32_t) bytes'
 * - RESTART CAUSE
 *   - restart cause    : 'sizeof(uint32_t) bytes'
 * - DBG_INFO_DATA (mdm_cli_dbg_info_data_t)
 *   - DATA_FIX
 *     - type           : 'sizeof(uint32_t) bytes'
 *     - ap_logs_size   : 'sizeof(uint32_t) bytes'
 *     - bp_logs_size   : 'sizeof(uint32_t) bytes'
 *     - bp_logs_time   : 'sizeof(uint32_t) bytes'
 *     - nb_data_dyn    : 'sizeof(uint32_t) bytes'
 *   - DATA_DYN (Array of nb_data_dyn size)
 *     - data (Array of strings)
 *                      : string length: 'sizeof(uint32_t) bytes'
 *                      : string content: 'length bytes'
 */

#define MSG_HEADER_SIZE (2 * sizeof(uint32_t))
#define MSG_RESTART_CAUSE (1 * sizeof(uint32_t))
#define MSG_DBG_DATA_FIXED_SIZE (5 * sizeof(uint32_t))
#define MSG_DBG_DATA_DYNAMIC_SIZE_MAX (MDM_CLI_MAX_NB_DATA * \
                                       ((sizeof(uint32_t) + MDM_CLI_MAX_LEN_DATA * \
                                         sizeof(char))))

#define MSG_SIZE_MAX (MSG_HEADER_SIZE + MSG_RESTART_CAUSE + MSG_DBG_DATA_FIXED_SIZE + \
                      MSG_DBG_DATA_DYNAMIC_SIZE_MAX)

typedef struct crm_mdmcli_wire_ctx_internal {
    crm_mdmcli_wire_ctx_t ctx; // Need to be first

    /* Internal variables */
    crm_mdmcli_wire_direction_t direction;
    char socket_name[MAX_SOCKET_NAME];

    /* Pre-allocated data to return to caller in case of message receive.
     * Note: could be slightly optimized as some information is not used depending of the direction
     *       of the marshalling.
     */
    crm_mdmcli_wire_msg_t msg;
    mdm_cli_dbg_info_t dbg_info;
    char client_name[MDM_CLI_NAME_LEN];
    char dbg_strings[MDM_CLI_MAX_NB_DATA][MDM_CLI_MAX_LEN_DATA];
    const char *dbg_strings_ptr[MDM_CLI_MAX_NB_DATA];

    /* Pre-allocated memory for the wire protocol (could be slightly optimized here too). */
    unsigned char rcv_buf[MSG_SIZE_MAX];
    unsigned char snd_buf[MSG_SIZE_MAX];
} crm_mdmcli_wire_ctx_internal_t;

/**
 * @see mdmcli_wire.h
 */
static void dispose(crm_mdmcli_wire_ctx_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_mdmcli_wire_ctx_internal_t *i_ctx = (crm_mdmcli_wire_ctx_internal_t *)ctx;

    free(i_ctx);
}

/**
 * @see mdmcli_wire.h
 */
static const char *get_socket_name(crm_mdmcli_wire_ctx_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_mdmcli_wire_ctx_internal_t *i_ctx = (crm_mdmcli_wire_ctx_internal_t *)ctx;

    return i_ctx->socket_name;
}

static void serialize_uint32(uint32_t data, unsigned char **buffer, size_t *remaining_data)
{
    ASSERT(buffer != NULL);
    ASSERT(*buffer != NULL);
    ASSERT(*remaining_data >= sizeof(data));

    data = ntohl(data);
    memcpy(*buffer, &data, sizeof(data));
    *buffer += sizeof(data);
    *remaining_data -= sizeof(data);
}

static void serialize_string(const char *data, unsigned char **buffer, size_t *remaining_data)
{
    ASSERT(buffer != NULL);
    ASSERT(*buffer != NULL);
    ASSERT(data != NULL);

    size_t len = strlen(data);
    serialize_uint32(len, buffer, remaining_data);

    ASSERT(*remaining_data >= len);
    memcpy(*buffer, data, len);
    *buffer += len;
    *remaining_data -= len;
}

/* Note: no ASSERTs in the deserialization functions on data coming from the wire interface to be
 *       able to handle protocol errors (from a rogue application for example to prevent denial of
 *       service).
 */
static uint32_t deserialize_uint32(const unsigned char **buffer, size_t *remaining_data,
                                   bool *error)
{
    ASSERT(buffer != NULL);
    ASSERT(*buffer != NULL);
    ASSERT(error != NULL);

    uint32_t data;

    if (*remaining_data < sizeof(data)) {
        *error = true;
        return 0;
    }
    *error = false;

    memcpy(&data, *buffer, sizeof(data));
    *buffer += sizeof(data);
    *remaining_data -= sizeof(data);

    return htonl(data);
}

static char *deserialize_string(const unsigned char **buffer, size_t *remaining_data, char *dest,
                                size_t dest_size)
{
    ASSERT(buffer != NULL);
    ASSERT(*buffer != NULL);
    ASSERT(dest != NULL);

    bool error;
    size_t len = deserialize_uint32(buffer, remaining_data, &error);

    if (error || (*remaining_data < len) || (dest_size <= len))
        return NULL;

    memcpy(dest, *buffer, len);
    dest[len] = '\0';

    *buffer += len;
    *remaining_data -= len;

    return dest;
}

/**
 * @see mdmcli_wire.h
 */
static void *serialize_msg(crm_mdmcli_wire_ctx_t *ctx, const crm_mdmcli_wire_msg_t *msg,
                           bool allocate)
{
    ASSERT(ctx != NULL);
    ASSERT(msg != NULL);
    crm_mdmcli_wire_ctx_internal_t *i_ctx = (crm_mdmcli_wire_ctx_internal_t *)ctx;

    ASSERT(((msg->id < MDM_NUM_EVENTS) && (i_ctx->direction == CRM_SERVER_TO_CLIENT)) ||
           ((msg->id >= CRM_REQ_REGISTER) && (i_ctx->direction == CRM_CLIENT_TO_SERVER)));

    unsigned char *buf;
    size_t remaining;
    if (allocate) {
        buf = malloc(MSG_SIZE_MAX);
        remaining = MSG_SIZE_MAX;
    } else {
        buf = i_ctx->snd_buf;
        remaining = sizeof(i_ctx->snd_buf);
    }
    unsigned char *ret_buf = buf;

    serialize_uint32(msg->id, &buf, &remaining);
    unsigned char *size_buf = buf;
    size_t size_remaining = remaining;
    /* Serialize a place holder for the message size (it will be overwritten once size is known). */
    serialize_uint32(0, &buf, &remaining);

    if (msg->id == CRM_REQ_REGISTER || msg->id == CRM_REQ_REGISTER_DBG) {
        /* In case of register, serialize events bitmap and client name */
        serialize_uint32(msg->msg.register_client.events_bitmap, &buf, &remaining);
        serialize_string(msg->msg.register_client.name, &buf, &remaining);
    } else if ((msg->id == CRM_REQ_RESTART) || (msg->id == CRM_REQ_NOTIFY_DBG) ||
               (msg->id == MDM_DBG_INFO)) {
        const mdm_cli_dbg_info_t *dbg_ptr;
        if (msg->id == CRM_REQ_RESTART) {
            /* In case of restart, serialize first the restart cause */
            dbg_ptr = msg->msg.restart.debug;
            serialize_uint32(msg->msg.restart.cause, &buf, &remaining);
        } else {
            dbg_ptr = msg->msg.debug;
        }
        if (dbg_ptr) {
            /* Then (common to restart request and debug information reply), serialize the debug
             * info structure (if present)
             */
            serialize_uint32(dbg_ptr->type, &buf, &remaining);
            serialize_uint32(dbg_ptr->ap_logs_size, &buf, &remaining);
            serialize_uint32(dbg_ptr->bp_logs_size, &buf, &remaining);
            serialize_uint32(dbg_ptr->bp_logs_time, &buf, &remaining);
            serialize_uint32(dbg_ptr->nb_data, &buf, &remaining);
            for (size_t i = 0; i < dbg_ptr->nb_data; i++)
                serialize_string(dbg_ptr->data[i], &buf, &remaining);
        }
    }

    size_t msg_size = buf - ret_buf;
    /* Now that size is known, override the 0 size with the proper value */
    serialize_uint32(msg_size, &size_buf, &size_remaining);

    return ret_buf;
}

/**
 * @see mdmcli_wire.h
 */
static int send_serialized_msg(crm_mdmcli_wire_ctx_t *ctx, const void *msg, int socket)
{
    (void)ctx;

    const unsigned char *data_tmp = msg;
    size_t msg_size;
    size_t remaining_data = MSG_SIZE_MAX;
    bool error;

    msg_size = deserialize_uint32(&data_tmp, &remaining_data, &error);
    ASSERT(!error);
    msg_size = deserialize_uint32(&data_tmp, &remaining_data, &error);
    ASSERT(!error);
    ASSERT(msg_size >= MSG_HEADER_SIZE);

    int ret = crm_socket_write(socket, MAX_SOCKET_TIMEOUT, msg, msg_size);
    if (ret < 0)
        LOGE("error or time-out writing to socket (%d / %s)", errno, strerror(errno));
    return ret;
}

/**
 * @see mdmcli_wire.h
 */
static int send_msg(crm_mdmcli_wire_ctx_t *ctx, const crm_mdmcli_wire_msg_t *msg, int socket)
{
    const void *data = serialize_msg(ctx, msg, false);

    return send_serialized_msg(ctx, data, socket);
}

static int read_from_fd(int socket, size_t size_to_read, unsigned char *dest_buffer)
{
    ASSERT(dest_buffer != NULL);

    if (size_to_read == 0)
        return 0;

    int ret = crm_socket_read(socket, MAX_SOCKET_TIMEOUT, dest_buffer, size_to_read);
    if (ret < 0)
        LOGE("error or time-out reading from socket (%d / %s)", errno, strerror(errno));
    return ret;
}

/**
 * @see mdmcli_wire.h
 */
static crm_mdmcli_wire_msg_t *recv_msg(crm_mdmcli_wire_ctx_t *ctx, int socket)
{
    ASSERT(ctx != NULL);
    crm_mdmcli_wire_ctx_internal_t *i_ctx = (crm_mdmcli_wire_ctx_internal_t *)ctx;

    if (socket < 0) {
        LOGD("invalid socket");
        return NULL;
    }

    crm_mdmcli_wire_msg_t *msg = &i_ctx->msg;

    /* First read the message header to know the message size */
    if (read_from_fd(socket, MSG_HEADER_SIZE, i_ctx->rcv_buf))
        return NULL;
    const unsigned char *buf = i_ctx->rcv_buf;
    size_t remaining_data = MSG_HEADER_SIZE;
    bool error;

    msg->id = deserialize_uint32(&buf, &remaining_data, &error);
    size_t msg_size = deserialize_uint32(&buf, &remaining_data, &error);
    if (error) {
        LOGE("failed to read message header");
        return NULL;
    }
    ASSERT(remaining_data == 0);
    if ((msg_size < MSG_HEADER_SIZE) || (msg_size > MSG_SIZE_MAX)) {
        LOGE("bad message size (%zd)", msg_size);
        return NULL;
    }

    /* Then read remaining of data */
    if (read_from_fd(socket, msg_size - MSG_HEADER_SIZE, &i_ctx->rcv_buf[MSG_HEADER_SIZE]))
        return NULL;
    remaining_data = msg_size - MSG_HEADER_SIZE;

    /* And finally deserialize the actual message content */
    if (msg->id == CRM_REQ_REGISTER || msg->id == CRM_REQ_REGISTER_DBG) {
        /* In case of register, deserialize events bitmap and client name */
        msg->msg.register_client.events_bitmap = deserialize_uint32(&buf, &remaining_data, &error);
        msg->msg.register_client.name = deserialize_string(&buf, &remaining_data,
                                                           i_ctx->client_name,
                                                           sizeof(i_ctx->client_name));
        if (error || (msg->msg.register_client.name == NULL)) {
            LOGE("failed to read REGISTER message");
            return NULL;
        }
    } else if ((msg->id == CRM_REQ_RESTART) || (msg->id == CRM_REQ_NOTIFY_DBG) ||
               (msg->id == MDM_DBG_INFO)) {
        const mdm_cli_dbg_info_t **dbg_info_storage;
        if (msg->id == CRM_REQ_RESTART) {
            /* In case of restart, deserialize first the restart cause */
            dbg_info_storage = &msg->msg.restart.debug;
            msg->msg.restart.debug = &i_ctx->dbg_info;
            msg->msg.restart.cause = deserialize_uint32(&buf, &remaining_data, &error);
        } else {
            dbg_info_storage = &msg->msg.debug;
            msg->msg.debug = &i_ctx->dbg_info;
        }
        /* Then (common to restart request and debug information reply), deserialize the debug info
         * structure (if present)
         */
        if (remaining_data) {
            i_ctx->dbg_info.type = deserialize_uint32(&buf, &remaining_data, &error);
            i_ctx->dbg_info.ap_logs_size = deserialize_uint32(&buf, &remaining_data, &error);
            i_ctx->dbg_info.bp_logs_size = deserialize_uint32(&buf, &remaining_data, &error);
            i_ctx->dbg_info.bp_logs_time = deserialize_uint32(&buf, &remaining_data, &error);
            i_ctx->dbg_info.nb_data = deserialize_uint32(&buf, &remaining_data, &error);
            if (error || i_ctx->dbg_info.nb_data > MDM_CLI_MAX_NB_DATA) {
                LOGE("failed to read DBG_INFO / RESTART message");
                return NULL;
            }
            i_ctx->dbg_info.data = i_ctx->dbg_strings_ptr;
            for (size_t i = 0; i < i_ctx->dbg_info.nb_data; i++) {
                i_ctx->dbg_info.data[i] = deserialize_string(&buf, &remaining_data,
                                                             i_ctx->dbg_strings[i],
                                                             MDM_CLI_MAX_LEN_DATA);
                if (i_ctx->dbg_info.data[i] == NULL) {
                    LOGE("failed to read debug info strings");
                    return NULL;
                }
            }
        } else {
            *dbg_info_storage = NULL;
        }
    }

    if (remaining_data != 0) {
        LOGE("extra data at end of message");
        return NULL;
    }

    return msg;
}

/**
 * @see mdmcli_wire.h
 */
crm_mdmcli_wire_ctx_t *crm_mdmcli_wire_init(crm_mdmcli_wire_direction_t direction, int instance_id)
{
    crm_mdmcli_wire_ctx_internal_t *i_ctx = calloc(1, sizeof(*i_ctx));

    ASSERT(i_ctx);

    snprintf(i_ctx->socket_name, sizeof(i_ctx->socket_name), "crm%d", instance_id);

    i_ctx->direction = direction;

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.get_socket_name = get_socket_name;
    i_ctx->ctx.send_msg = send_msg;
    i_ctx->ctx.recv_msg = recv_msg;
    i_ctx->ctx.serialize_msg = serialize_msg;
    i_ctx->ctx.send_serialized_msg = send_serialized_msg;

    return &i_ctx->ctx;
}
