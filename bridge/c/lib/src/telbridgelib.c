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
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <cutils/sockets.h>

/* NOTE: CRM dependency is only on logging and makefiles, could easily be removed in the future */
#define CRM_MODULE_TAG "JVBD"
#include "utils/debug.h"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/time.h"

#include "bridge_internal.h"
#include "tel_java_bridge.h"

#define MAX_SOCKET_TIMEOUT 500

typedef struct tel_java_bridge_internal_ctx {
    tel_java_bridge_ctx_t ctx; // Needs to be first

    int fd;
} tel_java_bridge_internal_ctx_t;

/**
 * Helper function to serialize an uint32_t value in msg.
 */
static inline void serialize_uint32(char **msg, uint32_t val, char *org, size_t len)
{
    ASSERT(*msg - org + sizeof(uint32_t) <= len);
    val = htonl(val);
    memcpy(*msg, &val, sizeof(val));
    *msg += sizeof(val);
}

/**
 * Helper function to serialize a string value in msg.
 */
static inline void serialize_string(char **msg, const char *param, size_t len_copy, char *org,
                                    size_t len)
{
    size_t param_len = len_copy > 0 ? len_copy : strlen(param);

    ASSERT(*msg - org + 2 * sizeof(uint32_t) + param_len <= len);
    serialize_uint32(msg, param_len, org, len);
    serialize_uint32(msg, TEL_BRIDGE_DATA_TYPE_STRING, org, len);
    memcpy(*msg, param, param_len);
    *msg += param_len;
}

/**
 * Helper function to serialize and send a message to the bridge.
 *
 * To be able to use this function for all APIs, this will always:
 *  = serialize param1 as a string (if not NULL)
 *  = serialize param2 as a string (if not NULL)
 *  = then serialize the ellipsis "params3" according to format
 */
static int send_msg(tel_java_bridge_internal_ctx_t *i_ctx, tel_bridge_commands_t cmd,
                    const char *param1, const char *param2,
                    const char *format, va_list params3)
{
    ASSERT(i_ctx);

    if (i_ctx->fd < 0)
        return -1;

    /* Compute message size */
    int msg_size = 2 * sizeof(uint32_t);
    if (param1)
        msg_size += 2 * sizeof(uint32_t) + strlen(param1);
    if (param2)
        msg_size += 2 * sizeof(uint32_t) + strlen(param2);
    if (format && *format != '\0') {
        va_list c_params;
        va_copy(c_params, params3);

        const char *c_format = format;
        while (*c_format != '\0') {
            const char *sep = strchr(c_format, '%');
            ASSERT(sep != c_format && sep != NULL);
            char c = *(sep + 1);
            ASSERT(c == 'd' || c == 's');
            msg_size += 2 * sizeof(uint32_t) + sep - c_format + 2 * sizeof(uint32_t);
            if (c == 'd') {
                va_arg(c_params, int);
                msg_size += sizeof(uint32_t);
            } else {
                char *dummy = va_arg(c_params, char *);
                msg_size += strlen(dummy);
            }
            c_format = sep + 2;
        }
        va_end(c_params);
    }

    /* Serialize message */
    char *msg = malloc(msg_size);
    ASSERT(msg);
    char *c_msg = msg;

    serialize_uint32(&c_msg, msg_size - 2 * sizeof(uint32_t), msg, msg_size);
    serialize_uint32(&c_msg, cmd, msg, msg_size);
    if (param1)
        serialize_string(&c_msg, param1, 0, msg, msg_size);
    if (param2)
        serialize_string(&c_msg, param2, 0, msg, msg_size);
    if (format && *format != '\0') {
        const char *c_format = format;
        while (*c_format != '\0') {
            const char *sep = strchr(c_format, '%');
            ASSERT(sep != c_format && sep != NULL);
            char c = *(sep + 1);
            ASSERT(c == 'd' || c == 's');
            serialize_string(&c_msg, c_format, sep - c_format, msg, msg_size);
            if (c == 'd') {
                int val = va_arg(params3, int);
                serialize_uint32(&c_msg, sizeof(uint32_t), msg, msg_size);
                serialize_uint32(&c_msg, TEL_BRIDGE_DATA_TYPE_INT, msg, msg_size);
                serialize_uint32(&c_msg, val, msg, msg_size);
            } else {
                char *val = va_arg(params3, char *);
                serialize_string(&c_msg, val, 0, msg, msg_size);
            }
            c_format = sep + 2;
        }
    }

    /* Send message */
    struct timespec timer_end;
    crm_time_add_ms(&timer_end, MAX_SOCKET_TIMEOUT);
    int data_sent = 0;
    int time_remaining;
    int ret = 0;
    while ((data_sent < msg_size) &&
           ((time_remaining = crm_time_get_remain_ms(&timer_end)) > 0) &&
           (ret == 0)) {
        errno = 0;
        struct pollfd pfd = { .fd = i_ctx->fd, .events = POLLOUT };
        int poll_ret = poll(&pfd, 1, time_remaining);
        if ((poll_ret < 0) && (errno == EINTR))
            continue;
        if ((poll_ret <= 0) || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            ret = -1;
            continue;
        }

        errno = 0;
        ssize_t ret = write(i_ctx->fd, &msg[data_sent], msg_size - data_sent);
        if ((ret < 0) && (errno == EINTR))
            continue;
        if (ret <= 0) {
            ret = -1;
            continue;
        }
        data_sent += ret;
    }
    if (data_sent < msg_size)
        ret = -1;

    free(msg);
    return ret;
}

/**
 * @see tel_java_bridge.h
 */
static void dispose(tel_java_bridge_ctx_t *ctx)
{
    ASSERT(ctx);
    tel_java_bridge_internal_ctx_t *i_ctx = (tel_java_bridge_internal_ctx_t *)ctx;

    if (i_ctx->fd >= 0)
        close(i_ctx->fd);
    free(ctx);
}

/**
 * @see tel_java_bridge.h
 */
static int bridge_connect(tel_java_bridge_ctx_t *ctx)
{
    ASSERT(ctx);
    tel_java_bridge_internal_ctx_t *i_ctx = (tel_java_bridge_internal_ctx_t *)ctx;

    i_ctx->fd =
        socket_local_client(BRIDGE_SOCKET_C, ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM);
    return i_ctx->fd >= 0 ? 0 : -1;
}

/**
 * @see tel_java_bridge.h
 */
static void bridge_disconnect(tel_java_bridge_ctx_t *ctx)
{
    ASSERT(ctx);
    tel_java_bridge_internal_ctx_t *i_ctx = (tel_java_bridge_internal_ctx_t *)ctx;

    if (i_ctx->fd >= 0) {
        int fd = i_ctx->fd;
        i_ctx->fd = -1;
        close(fd);
    }
}

/**
 * @see tel_java_bridge.h
 */
static int get_poll_fd(tel_java_bridge_ctx_t *ctx)
{
    ASSERT(ctx);
    return ((tel_java_bridge_internal_ctx_t *)ctx)->fd;
}

/**
 * @see tel_java_bridge.h
 */
static int handle_poll_event(tel_java_bridge_ctx_t *ctx, int revent)
{
    ASSERT(ctx);
    tel_java_bridge_internal_ctx_t *i_ctx = (tel_java_bridge_internal_ctx_t *)ctx;

    if (revent & (POLLERR | POLLHUP | POLLNVAL)) {
        int fd = i_ctx->fd;
        i_ctx->fd = -1;
        close(fd);
        return -1;
    } else {
        return 0;
    }
}

/**
 * @see tel_java_bridge.h
 */
static int wakelock(tel_java_bridge_ctx_t *ctx, bool acquire)
{
    ASSERT(ctx);
    return send_msg((tel_java_bridge_internal_ctx_t *)ctx,
                    acquire ? TEL_BRIDGE_COMMAND_WAKELOCK_ACQUIRE : TEL_BRIDGE_COMMAND_WAKELOCK_RELEASE,
                    NULL, NULL, NULL, NULL);
}

/**
 * @see tel_java_bridge.h
 */
static int start_service(tel_java_bridge_ctx_t *ctx, const char *s_package, const char *s_class)
{
    ASSERT(ctx);
    return send_msg((tel_java_bridge_internal_ctx_t *)ctx,
                    TEL_BRIDGE_COMMAND_START_SERVICE, s_package, s_class, NULL, NULL);
}

/**
 * @see tel_java_bridge.h
 */
static int broadcast_intent(tel_java_bridge_ctx_t *ctx, const char *name, const char *format, ...)
{
    ASSERT(ctx);
    va_list params;
    va_start(params, format);
    int ret = send_msg((tel_java_bridge_internal_ctx_t *)ctx,
                       TEL_BRIDGE_COMMAND_BROADCAST_INTENT, name, NULL, format, params);
    va_end(params);
    return ret;
}

/**
 * @see tel_java_bridge.h
 */
tel_java_bridge_ctx_t *tel_java_bridge_init(void)
{
    tel_java_bridge_internal_ctx_t *ctx = calloc(1, sizeof(tel_java_bridge_internal_ctx_t));

    ASSERT(ctx);

    ctx->fd = -1;

    ctx->ctx.dispose = dispose;
    ctx->ctx.connect = bridge_connect;
    ctx->ctx.disconnect = bridge_disconnect;
    ctx->ctx.get_poll_fd = get_poll_fd;
    ctx->ctx.handle_poll_event = handle_poll_event;
    ctx->ctx.wakelock = wakelock;
    ctx->ctx.start_service = start_service;
    ctx->ctx.broadcast_intent = broadcast_intent;

    return &ctx->ctx;
}

/**
 * @see tel_java_bridge.h
 */
int tel_java_brige_broadcast_intent(const char *name, const char *format, ...)
{
    int ret = -1;
    tel_java_bridge_ctx_t *ctx = tel_java_bridge_init();

    ASSERT(ctx);

    if (!ctx->connect(ctx)) {
        va_list params;
        va_start(params, format);
        ret = send_msg((tel_java_bridge_internal_ctx_t *)ctx, TEL_BRIDGE_COMMAND_BROADCAST_INTENT,
                       name, NULL, format, params);
        va_end(params);
    }

    ctx->dispose(ctx);
    return ret;
}

/**
 * @see tel_java_bridge.h
 */
int tel_java_brige_start_service(const char *s_package, const char *s_class)
{
    int ret = -1;
    tel_java_bridge_ctx_t *ctx = tel_java_bridge_init();

    ASSERT(ctx);

    if (!ctx->connect(ctx))
        ret = send_msg((tel_java_bridge_internal_ctx_t *)ctx, TEL_BRIDGE_COMMAND_START_SERVICE,
                       s_package, s_class, NULL, NULL);

    ctx->dispose(ctx);
    return ret;
}
