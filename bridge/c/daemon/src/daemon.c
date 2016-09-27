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

#ifdef HOST_BUILD
#include "test/test_utils.h"
#endif

#include "bridge_internal.h"

/* @TODO: remove ... or make it configurable / dynamic ?
 *        Today 3 due to:
 *          = CRM
 *          = RIL (Rapid / AT)
 *          = AT Proxy
 */
#define MAX_CLIENTS 3

/* To prevent entering an infinite loop in case a client sends a message that generates
 * an exception in the Java application (or makes it crash), limit the number of attempts
 * to send the same message.
 */
#define MAX_RETRIES 3

#define MAX_CLIENT_MSG_DURATION 500
#define MAX_JAVA_MSG_DURATION 1000
#define MAX_JAVA_ACK_DURATION 5000 // Very long time-out due to SoFIA's speed :)
#define MAX_MSG_SIZE 2048

typedef enum msg_state {
    MSG_STATE_NONE,
    MSG_STATE_IN_HDR,
    MSG_STATE_IN_MSG
} msg_state_t;

typedef struct client_context {
    int fd;

    msg_state_t msg_state;

    uint32_t msg_hdr[2];
    char *msg_buffer;
    int data_read;
    int data_to_read;

    int wakelock_cnt;

    struct timespec msg_read_end;
} client_context_t;

typedef struct msg_queue {
    char *msg;
    int msg_size;
    int retries;
    struct msg_queue *next;
} msg_queue_t;

typedef struct java_context {
    int fd;
    uint32_t msg_count;

    struct timespec msg_end;
    bool msg_in_progress;

    msg_queue_t *msg_queue;
    msg_queue_t *msg_queue_tail;

    uint32_t wakelock_msg[3];

    char *msg;
    int data_to_send;
    int data_sent;

    bool wakelock_held;

    bool wait_ack;
    uint32_t wait_ack_count;

    uint32_t ack_reply;
    int ack_reply_pos;
} java_context_t;

typedef struct daemon_context {
    int c_sock_l;
    int j_sock_l;
    bool wakelock_held;

    client_context_t client_ctx[MAX_CLIENTS];
    java_context_t java_ctx;
} daemon_context_t;

static const char *msg_to_string(uint32_t msg)
{
    switch (msg) {
    case TEL_BRIDGE_COMMAND_WAKELOCK_ACQUIRE: return "WAKE_ACQUIRE";
    case TEL_BRIDGE_COMMAND_WAKELOCK_RELEASE: return "WAKE_RELEASE";
    case TEL_BRIDGE_COMMAND_START_SERVICE:    return "START_SERVICE";
    case TEL_BRIDGE_COMMAND_BROADCAST_INTENT: return "BROADCAST_INTENT";
    default: DASSERT(0, "Invalid message type %u", msg);
    }
}

#ifdef HOST_BUILD
/*
 * To test on HOST, we need to use an INET socket as it's difficult to use Unix sockets
 * in a Java program.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

static inline int inet_socket_create(const char *socket_name, int max_conn)
{
    (void)socket_name;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(1703);

    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    ASSERT(bind(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0);
    ASSERT(listen(fd, max_conn) == 0);
    return fd;
}
#endif

static inline int socket_create(const char *socket_name, int max_conn)
{
    ASSERT(socket_name);
    int fd = android_get_control_socket(socket_name);

    if (fd >= 0) {
        int ret = listen(fd, max_conn);
        if (ret != 0) {
            close(fd);
            fd = -1;
        }
    }
    DASSERT(fd >= 0, "Could not open socket '%s' (%d [%s])", socket_name, errno, strerror(errno));
    return fd;
}

static int fill_pollfd(daemon_context_t *ctx, struct pollfd *pfd)
{
    ASSERT(ctx);
    ASSERT(pfd);

    int timeout = -1;
    size_t i;

    // Client sockets
    for (i = 0; i < ARRAY_SIZE(ctx->client_ctx); i++) {
        pfd[i].fd = ctx->client_ctx[i].fd;
        pfd[i].events = POLLIN;
        if (ctx->client_ctx[i].fd >= 0 && ctx->client_ctx[i].msg_state != MSG_STATE_NONE) {
            int to = crm_time_get_remain_ms(&ctx->client_ctx[i].msg_read_end);
            if (timeout == -1 || to < timeout)
                timeout = to;
        }
    }
    // Java bridge socket
    pfd[i].fd = ctx->java_ctx.fd;
    if (ctx->java_ctx.fd >= 0) {
        if (ctx->java_ctx.wait_ack) {
            pfd[i].events = POLLIN;
        } else if (ctx->java_ctx.msg_queue ||
                   ctx->java_ctx.wakelock_held != ctx->wakelock_held ||
                   ctx->java_ctx.msg_in_progress) {
            pfd[i].events = POLLOUT;
            if (!ctx->java_ctx.msg_in_progress) {
                ctx->java_ctx.msg_in_progress = true;
                ctx->java_ctx.data_sent = 0;
                crm_time_add_ms(&ctx->java_ctx.msg_end, MAX_JAVA_MSG_DURATION);
                if (ctx->java_ctx.wakelock_held != ctx->wakelock_held) {
                    ctx->java_ctx.data_to_send = sizeof(ctx->java_ctx.wakelock_msg);
                    ctx->java_ctx.msg = (char *)ctx->java_ctx.wakelock_msg;
                    ctx->java_ctx.wakelock_msg[0] = htonl(ctx->java_ctx.msg_count++);
                    ctx->java_ctx.wakelock_msg[1] = htonl(0);
                    tel_bridge_commands_t cmd_id = ctx->wakelock_held ?
                                                   TEL_BRIDGE_COMMAND_WAKELOCK_ACQUIRE :
                                                   TEL_BRIDGE_COMMAND_WAKELOCK_RELEASE;
                    ctx->java_ctx.wakelock_msg[2] = htonl(cmd_id);
                } else {
                    ctx->java_ctx.data_to_send = ctx->java_ctx.msg_queue->msg_size;
                    ctx->java_ctx.msg = ctx->java_ctx.msg_queue->msg;
                }
                LOGD("[%2d] Sending message %s", ctx->java_ctx.fd,
                     msg_to_string(ntohl(((uint32_t *)ctx->java_ctx.msg)[2])));
            }
        }
        if (ctx->java_ctx.msg_in_progress || ctx->java_ctx.wait_ack) {
            int to = crm_time_get_remain_ms(&ctx->java_ctx.msg_end);
            if (timeout == -1 || to < timeout)
                timeout = to;
        }
    }

    return timeout;
}

static void handle_wakelock_change(daemon_context_t *ctx)
{
    ASSERT(ctx);

    bool new_is_held = false;

    for (size_t i = 0; i < ARRAY_SIZE(ctx->client_ctx) && !new_is_held; i++)
        new_is_held = ctx->client_ctx[i].fd != -1 && ctx->client_ctx[i].wakelock_cnt > 0;
    ctx->wakelock_held = new_is_held;
}

static void handle_java_bridge_remove(daemon_context_t *ctx)
{
    ASSERT(ctx);

    close(ctx->java_ctx.fd);
    ctx->java_ctx.fd = -1;

    if (ctx->java_ctx.msg != (char *)ctx->java_ctx.wakelock_msg) {
        msg_queue_t *to_free = ctx->java_ctx.msg_queue;
        ASSERT(to_free);
        to_free->retries += 1;
        if (to_free->retries >= MAX_RETRIES) {
            LOGE("[  ] Message %s dropped due to max retries",
                 msg_to_string(ntohl(((uint32_t *)ctx->java_ctx.msg)[2])));
            ctx->java_ctx.msg_queue = to_free->next;
            if (ctx->java_ctx.msg_queue == NULL)
                ctx->java_ctx.msg_queue_tail = NULL;
            free(to_free->msg);
            free(to_free);
        }
    }
}

static void handle_c_client_remove(daemon_context_t *ctx, int client_idx)
{
    ASSERT(ctx);

    client_context_t *client_ctx = &ctx->client_ctx[client_idx];

    close(client_ctx->fd);
    client_ctx->fd = -1;
    free(client_ctx->msg_buffer);
    if (client_ctx->wakelock_cnt > 0)
        handle_wakelock_change(ctx);
}

static void handle_timeout(daemon_context_t *ctx)
{
    ASSERT(ctx);

    for (size_t i = 0; i < ARRAY_SIZE(ctx->client_ctx); i++) {
        if (ctx->client_ctx[i].fd >= 0 && ctx->client_ctx[i].msg_state != MSG_STATE_NONE) {
            int to = crm_time_get_remain_ms(&ctx->client_ctx[i].msg_read_end);
            if (to == 0) {
                LOGE("[%2d] error reading on socket, client disconnected", ctx->client_ctx[i].fd);
                handle_c_client_remove(ctx, i);
            }
        }
    }
    if (ctx->java_ctx.fd >= 0 && (ctx->java_ctx.msg_in_progress || ctx->java_ctx.wait_ack)) {
        int to = crm_time_get_remain_ms(&ctx->java_ctx.msg_end);
        if (to == 0) {
            LOGE("[%2d] timeout in java bridge communication, disconnected", ctx->java_ctx.fd);
            handle_java_bridge_remove(ctx);
        }
    }
}

static void handle_c_client_conn(daemon_context_t *ctx, struct pollfd *pfd)
{
    ASSERT(ctx);
    ASSERT(pfd);

    if (pfd->revents == POLLIN) {
        /* New client connection on C control socket */
        int sock = accept(pfd->fd, 0, 0);
        if (sock >= 0) {
            for (size_t i = 0; i < ARRAY_SIZE(ctx->client_ctx); i++) {
                if (ctx->client_ctx[i].fd == -1) {
                    LOGD("[%2d] client connected", sock);
                    ctx->client_ctx[i].fd = sock;
                    ctx->client_ctx[i].msg_state = MSG_STATE_NONE;
                    ctx->client_ctx[i].wakelock_cnt = 0;
                    sock = -1;
                    break;
                }
            }
            if (sock >= 0) {
                LOGE("Too many clients connected, rejecting connection");
                close(sock);
            }
        } else {
            LOGE("Error accepting connection on C control socket");
        }
    } else {
        DASSERT(0, "Error on C control socket");
    }
}

static void handle_java_bridge_conn(daemon_context_t *ctx, struct pollfd *pfd)
{
    ASSERT(ctx);
    ASSERT(pfd);

    if (pfd->revents == POLLIN) {
        /* Java bridge connection on Java control socket */
        int sock = accept(pfd->fd, 0, 0);
        if (sock >= 0) {
            if (ctx->java_ctx.fd == -1) {
                ctx->java_ctx.fd = sock;
                ctx->java_ctx.msg_in_progress = false;
                ctx->java_ctx.wait_ack = false;
                ctx->java_ctx.data_sent = 0;
                ctx->java_ctx.wakelock_held = false;
                LOGD("[%2d] java bridge connected", sock);
            } else {
                LOGE("Too many bridges connected, rejecting connection");
                close(sock);
            }
        } else {
            LOGE("Error accepting connection on Java control socket");
        }
    } else {
        DASSERT(0, "Error on Java control socket");
    }
}

static void handle_java_bridge_event(daemon_context_t *ctx, struct pollfd *pfd)
{
    ASSERT(ctx);
    ASSERT(pfd);

    /* Error or data in / out on Java file descriptor */
    if (pfd->revents & (POLLIN | POLLOUT)) {
        if (pfd->revents & POLLIN) {
            ssize_t len = read(ctx->java_ctx.fd,
                               ((char *)&ctx->java_ctx.ack_reply) + ctx->java_ctx.ack_reply_pos,
                               sizeof(ctx->java_ctx.ack_reply) - ctx->java_ctx.ack_reply_pos);
            if (len <= 0) {
                LOGE("[%2d] error reading from java bridge, disconnected", pfd->fd);
                handle_java_bridge_remove(ctx);
            } else {
                ctx->java_ctx.ack_reply_pos += len;
                if (ctx->java_ctx.ack_reply_pos == sizeof(ctx->java_ctx.ack_reply)) {
                    ctx->java_ctx.ack_reply = ntohl(ctx->java_ctx.ack_reply);
                    if (ctx->java_ctx.ack_reply != ctx->java_ctx.wait_ack_count) {
                        LOGE("[%2d] mismatch in ack (%08x instead of %08x), disconnected",
                             pfd->fd, ctx->java_ctx.ack_reply, ctx->java_ctx.wait_ack_count);
                        handle_java_bridge_remove(ctx);
                    } else {
                        LOGD("[%2d] ... message acked", ctx->java_ctx.fd);
                        ctx->java_ctx.wait_ack = false;
                        if (ctx->java_ctx.msg == (char *)ctx->java_ctx.wakelock_msg) {
                            ctx->java_ctx.wakelock_held =
                                ntohl(ctx->java_ctx.wakelock_msg[2]) ==
                                TEL_BRIDGE_COMMAND_WAKELOCK_ACQUIRE;
                        } else {
                            msg_queue_t *to_free = ctx->java_ctx.msg_queue;
                            ASSERT(to_free);
                            ctx->java_ctx.msg_queue = to_free->next;
                            if (ctx->java_ctx.msg_queue == NULL)
                                ctx->java_ctx.msg_queue_tail = NULL;
                            free(to_free->msg);
                            free(to_free);
                        }
                    }
                }
            }
        } else {
            ssize_t len = write(ctx->java_ctx.fd,
                                ctx->java_ctx.msg + ctx->java_ctx.data_sent,
                                ctx->java_ctx.data_to_send - ctx->java_ctx.data_sent);
            if (len <= 0) {
                LOGE("[%2d] error writing to java bridge, disconnected", pfd->fd);
                handle_java_bridge_remove(ctx);
            } else {
                ctx->java_ctx.data_sent += len;
                if (ctx->java_ctx.data_sent == ctx->java_ctx.data_to_send) {
                    ctx->java_ctx.wait_ack = true;
                    ctx->java_ctx.wait_ack_count = ntohl(*((uint32_t *)ctx->java_ctx.msg));
                    crm_time_add_ms(&ctx->java_ctx.msg_end, MAX_JAVA_ACK_DURATION);
                    ctx->java_ctx.ack_reply_pos = 0;

                    ctx->java_ctx.msg_in_progress = false;
                    LOGD("[%2d] ... message sent, waiting ack %d",
                         ctx->java_ctx.fd, ctx->java_ctx.wait_ack_count);
                }
            }
        }
    } else if (pfd->revents & (POLLERR | POLLHUP | POLLNVAL)) {
        LOGE("[%2d] java bridge disconnected", pfd->fd);
        handle_java_bridge_remove(ctx);
    }
}

static void queue_msg(daemon_context_t *ctx, char *msg, int msg_size)
{
    ASSERT(ctx);
    ASSERT(msg);

    msg_queue_t *msg_s = malloc(sizeof(*msg_s));
    ASSERT(msg_s);
    msg_s->msg = msg;
    msg_s->msg_size = msg_size;
    msg_s->next = NULL;
    if (ctx->java_ctx.msg_queue == NULL) {
        ctx->java_ctx.msg_queue = msg_s;
        ctx->java_ctx.msg_queue_tail = msg_s;
    } else {
        ASSERT(ctx->java_ctx.msg_queue_tail && !ctx->java_ctx.msg_queue_tail->next);
        ctx->java_ctx.msg_queue_tail->next = msg_s;
        ctx->java_ctx.msg_queue_tail = msg_s;
    }
    msg_s->retries = 0;
}

static void handle_c_client_event(daemon_context_t *ctx, int client_idx, struct pollfd *pfd)
{
    ASSERT(ctx);
    ASSERT(pfd);

    client_context_t *client_ctx = &ctx->client_ctx[client_idx];

    /* Error or data in / out for a client */
    if (pfd->revents & POLLIN) {
        char *location;
        switch (client_ctx->msg_state) {
        case MSG_STATE_NONE:
            client_ctx->data_read = 0;
            client_ctx->data_to_read = sizeof(client_ctx->msg_hdr);
            crm_time_add_ms(&client_ctx->msg_read_end, MAX_CLIENT_MSG_DURATION);
            client_ctx->msg_state = MSG_STATE_IN_HDR;
        /* FALLTHROUGH */
        case MSG_STATE_IN_HDR:
            location = (char *)client_ctx->msg_hdr;
            break;

        case MSG_STATE_IN_MSG:
            location = client_ctx->msg_buffer;
            break;

        default:
            ASSERT(0);
            break;
        }
        ssize_t len = read(pfd->fd, location + client_ctx->data_read,
                           client_ctx->data_to_read - client_ctx->data_read);
        if (len <= 0) {
            LOGE("[%2d] error reading on socket, client disconnected", pfd->fd);
            handle_c_client_remove(ctx, client_idx);
        } else {
            client_ctx->data_read += len;
            if (client_ctx->data_read == client_ctx->data_to_read) {
                if (client_ctx->msg_state == MSG_STATE_IN_HDR) {
                    uint32_t msg_size = ntohl(client_ctx->msg_hdr[0]);
                    uint32_t msg_type = ntohl(client_ctx->msg_hdr[1]);

                    if ((msg_type == TEL_BRIDGE_COMMAND_WAKELOCK_ACQUIRE) ||
                        (msg_type == TEL_BRIDGE_COMMAND_WAKELOCK_RELEASE)) {
                        if (msg_size != 0) {
                            LOGE("[%2d] client sending data for wakelock message (%d)"
                                 ", client disconnected", pfd->fd, msg_size);
                            handle_c_client_remove(ctx, client_idx);
                        } else {
                            client_ctx->wakelock_cnt +=
                                msg_type == TEL_BRIDGE_COMMAND_WAKELOCK_ACQUIRE ? 1 : -1;
                            if (client_ctx->wakelock_cnt < 0) {
                                LOGE("[%2d] client released an unacquired wakelock"
                                     ", client disconnected", pfd->fd);
                                handle_c_client_remove(ctx, client_idx);
                            } else {
                                LOGD("[%2d] client msg: %s", pfd->fd, msg_to_string(msg_type));
                                handle_wakelock_change(ctx);
                                client_ctx->msg_state = MSG_STATE_NONE;
                            }
                        }
                    } else {
                        if ((msg_type >= TEL_BRIDGE_COMMAND_NUM) ||
                            (msg_size >= MAX_MSG_SIZE)) {
                            LOGE("[%2d] client sent an invalid message (%d / %d)"
                                 ", client disconnected", pfd->fd, msg_size, msg_type);
                            handle_c_client_remove(ctx, client_idx);
                        } else {
                            client_ctx->msg_buffer = malloc(3 * sizeof(uint32_t) + msg_size);
                            ASSERT(client_ctx->msg_buffer);
                            *((uint32_t *)client_ctx->msg_buffer) =
                                htonl(ctx->java_ctx.msg_count++);
                            memcpy(client_ctx->msg_buffer + sizeof(uint32_t),
                                   client_ctx->msg_hdr,
                                   sizeof(client_ctx->msg_hdr));
                            client_ctx->data_read = 3 * sizeof(uint32_t);
                            client_ctx->data_to_read = msg_size + 3 * sizeof(uint32_t);
                            client_ctx->msg_state = MSG_STATE_IN_MSG;
                        }
                    }
                } else {
                    uint32_t msg_type = ntohl(client_ctx->msg_hdr[1]);
                    LOGD("[%2d] client msg: %s", pfd->fd, msg_to_string(msg_type));
                    queue_msg(ctx, client_ctx->msg_buffer, client_ctx->data_to_read);
                    client_ctx->msg_state = MSG_STATE_NONE;
                    client_ctx->msg_buffer = NULL;
                }
            }
        }
    } else if (pfd->revents & (POLLERR | POLLHUP | POLLNVAL)) {
        LOGE("[%2d] client disconnected", pfd->fd);
        handle_c_client_remove(ctx, client_idx);
    }
}

int main(void)
{
    daemon_context_t ctx;

#ifdef HOST_BUILD
    // When testing on host, need to create the sockets...
    unlink("/tmp/" BRIDGE_SOCKET_C);
    unlink("/tmp/" BRIDGE_SOCKET_JAVA);
    CRM_TEST_get_control_socket_android(BRIDGE_SOCKET_C);
    CRM_TEST_get_control_socket_android(BRIDGE_SOCKET_JAVA);
#endif

    // Open the two control sockets
    ctx.c_sock_l = socket_create(BRIDGE_SOCKET_C, MAX_CLIENTS);
#ifdef HOST_BUILD
    ctx.j_sock_l = inet_socket_create(BRIDGE_SOCKET_JAVA, 1);
#else
    ctx.j_sock_l = socket_create(BRIDGE_SOCKET_JAVA, 1);
#endif

    // Initialize client contexts
    for (size_t i = 0; i < ARRAY_SIZE(ctx.client_ctx); i++) {
        ctx.client_ctx[i].fd = -1;
        ctx.client_ctx[i].msg_buffer = NULL;
    }

    // Initialize java context
    ctx.java_ctx.fd = -1;
    ctx.java_ctx.msg_count = 0;
    ctx.java_ctx.msg_queue = NULL;
    ctx.java_ctx.wait_ack = false;

    // Initialize wakelock state
    ctx.wakelock_held = false;

    // Main loop
    while (true) {
        struct pollfd pfd[2 + MAX_CLIENTS + 1] = {
            { .fd = ctx.c_sock_l, .events = POLLIN },
            { .fd = ctx.j_sock_l, .events = POLLIN }
        };
        int timeout = fill_pollfd(&ctx, &pfd[2]);
        int ret = poll(pfd, ARRAY_SIZE(pfd), timeout);
        ASSERT(ret >= 0);
        if (ret == 0) {
            handle_timeout(&ctx);
        } else {
            for (size_t i = 0; i < ARRAY_SIZE(pfd); i++) {
                if (pfd[i].revents == 0)
                    continue;

                if (i == 0)
                    handle_c_client_conn(&ctx, &pfd[i]);
                else if (i == 1)
                    handle_java_bridge_conn(&ctx, &pfd[i]);
                else if (i == 2 + MAX_CLIENTS)
                    handle_java_bridge_event(&ctx, &pfd[i]);
                else
                    handle_c_client_event(&ctx, i - 2, &pfd[i]);
            }
        }
    }

    return 0;
}
