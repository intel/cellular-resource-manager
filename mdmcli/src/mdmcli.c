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
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>

#include <utils/Log.h>

#include "libmdmcli/mdm_cli.h"

#define CRM_MODULE_TAG "CLI"
#include "utils/debug.h"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/ipc.h"
#include "utils/string_helpers.h"
#include "utils/thread.h"
#include "utils/time.h"
#include "utils/property.h"
#include "utils/socket.h"
#include "plugins/mdmcli_wire.h"

#define CLIENT_FORMAT "%-16s"
#define MSG_EVT_FORMAT "%-15s"

#define CLOGE(ctx, format, ...) LOGE("[" CLIENT_FORMAT "] " format, ctx->name, ## __VA_ARGS__)
#define CLOGD(ctx, format, ...) LOGD("[" CLIENT_FORMAT "] " format, ctx->name, ## __VA_ARGS__)
#define CLOGV(ctx, format, ...) LOGV("[" CLIENT_FORMAT "] " format, ctx->name, ## __VA_ARGS__)
#define CLOGI(ctx, format, ...) LOGI("[" CLIENT_FORMAT "] " format, ctx->name, ## __VA_ARGS__)

typedef struct crm_mdm_cli_ctx {
    int nb_evts;
    mdm_cli_register_t *evts;
    int events_bitmap;

    crm_mdmcli_wire_ctx_t *wire;
    crm_ipc_ctx_t *ipc;
    crm_thread_ctx_t *thread;

    int sock_fd;

    pthread_mutex_t lock;

    char *name;

    bool reconnect;
    bool disconnect;
    bool acquired;
    int register_id;
} crm_mdm_cli_ctx_t;

static void dispose(crm_mdm_cli_ctx_t *ctx)
{
    if (ctx->thread)
        ctx->thread->dispose(ctx->thread, NULL);
    if (ctx->ipc)
        ctx->ipc->dispose(ctx->ipc, NULL);
    pthread_mutex_destroy(&ctx->lock);
    ctx->wire->dispose(ctx->wire);
    if (ctx->sock_fd >= 0)
        close(ctx->sock_fd);
    free(ctx->name);
    free(ctx->evts);
    free(ctx);
}

static void reconnect(crm_mdm_cli_ctx_t *ctx)
{
    ASSERT(ctx != NULL);
    ASSERT(ctx->reconnect);

    ctx->sock_fd = crm_socket_connect(ctx->wire->get_socket_name(ctx->wire));
    if (ctx->sock_fd >= 0) {
        ASSERT(pthread_mutex_lock(&ctx->lock) == 0);
        crm_mdmcli_wire_msg_t msg;
        msg.id = ctx->register_id;
        msg.msg.register_client.events_bitmap = ctx->events_bitmap;
        msg.msg.register_client.name = ctx->name;
        CLOGD(ctx, "=> " MSG_EVT_FORMAT "(0x%08x,'%s')", "REGISTER*", ctx->events_bitmap,
              ctx->name);
        if (ctx->wire->send_msg(ctx->wire, &msg, ctx->sock_fd)) {
            CLOGE(ctx, "failed to send message");
            goto error;
        }

        if (ctx->acquired) {
            msg.id = CRM_REQ_ACQUIRE;
            CLOGD(ctx, "=> " MSG_EVT_FORMAT "()", "ACQUIRE*");
            if (ctx->wire->send_msg(ctx->wire, &msg, ctx->sock_fd)) {
                CLOGE(ctx, "failed to send message");
                goto error;
            }
        }

        ctx->reconnect = false;

error:
        if (ctx->reconnect) {
            close(ctx->sock_fd);
            ctx->sock_fd = -1;
        } else {
            CLOGD(ctx, "reconnected to CRM server");
        }
        ASSERT(pthread_mutex_unlock(&ctx->lock) == 0);
    }
}

static void handle_error(crm_mdm_cli_ctx_t *ctx)
{
    ASSERT(ctx != NULL);

    ASSERT(pthread_mutex_lock(&ctx->lock) == 0);
    close(ctx->sock_fd);
    ctx->sock_fd = -1;
    ctx->reconnect = true;
    ASSERT(pthread_mutex_unlock(&ctx->lock) == 0);

    for (int i = 0; i < ctx->nb_evts; i++) {
        if (ctx->evts[i].id == MDM_DOWN) {
            CLOGD(ctx, "<= " MSG_EVT_FORMAT "()", "MDM_DOWN*");
            mdm_cli_callback_data_t data = { .id = MDM_DOWN,
                                             .context = ctx->evts[i].context };
            ctx->evts[i].callback(&data);
            break;
        }
    }
    for (int i = 0; i < ctx->nb_evts; i++) {
        if (ctx->evts[i].id == MDM_COLD_RESET) {
            CLOGD(ctx, "<= " MSG_EVT_FORMAT "()", "MDM_COLD_RESET*");
            mdm_cli_callback_data_t data = { .id = MDM_COLD_RESET,
                                             .context = ctx->evts[i].context };
            ctx->evts[i].callback(&data);
            break;
        }
    }

    if (!crm_is_in_sanity_test_mode() || ctx->register_id == CRM_REQ_REGISTER_DBG)
        reconnect(ctx);
}

static int mdm_cli_send_simple_msg(mdm_cli_hdle_t *hdle, int message)
{
    crm_mdm_cli_ctx_t *ctx = hdle;

    ASSERT(ctx != NULL);
    ASSERT(ctx->wire != NULL);

    ASSERT(pthread_mutex_lock(&ctx->lock) == 0);
    crm_mdmcli_wire_msg_t msg = { .id = message };
    CLOGD(ctx, "=> " MSG_EVT_FORMAT "()%s", crm_mdmcli_wire_req_to_string(msg.id),
          ctx->reconnect ? " [ignored]" : "");
    int ret;
    if (ctx->reconnect) {
        ret = -1;
    } else {
        ret = ctx->wire->send_msg(ctx->wire, &msg, ctx->sock_fd);
        if (ret)
            CLOGE(ctx, "failed to send message");
    }
    ASSERT(pthread_mutex_unlock(&ctx->lock) == 0);

    return ret;
}

static void *mdmcli_event_loop(crm_thread_ctx_t *thread_ctx, void *ctx_)
{
    crm_mdm_cli_ctx_t *ctx = ctx_;

    ASSERT(thread_ctx);
    ASSERT(ctx);

    int thread_fd = ctx->ipc->get_poll_fd(ctx->ipc);

    while (true) {
        struct pollfd pfd[2] = { { .fd = thread_fd, .events = POLLIN },
                                 { .fd = ctx->sock_fd, .events = POLLIN } };
        int timeout = -1;
        int num_fds = 2;

        if (ctx->reconnect) {
            if (crm_is_in_sanity_test_mode() && ctx->register_id == CRM_REQ_REGISTER)
                timeout = 10000; // Sanity tests take some time. Don't need to retry too often...
            else
                timeout = 1000;
            num_fds = 1;
        }

        struct timespec timer_end;
        if (timeout > 0)
            crm_time_add_ms(&timer_end, timeout);
        errno = 0;
        int ret = 0;
        do
            ret = poll(pfd, num_fds, timeout > 0 ? crm_time_get_remain_ms(&timer_end) : -1);
        while ((ret < 0) && (errno == EINTR));

        if (ret < 0) {
            CLOGE(ctx, "error in poll system call (%d / %d / %s), no recovery possible !",
                  ret, errno, strerror(errno));
            ASSERT(0);
        } else if (ret == 0) {
            reconnect(ctx);
        } else {
            /* Handle events on IPC socket */
            if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (!ctx->disconnect) {
                    CLOGE(ctx,
                          "error on thread IPC pipe, should never happen, no recovery possible !");
                    ASSERT(0);
                } else {
                    break;
                }
            } else if (pfd[0].revents & POLLIN) {
                break;
            }
            /* Handle events on server socket */
            if (pfd[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                CLOGE(ctx, "error on server communication socket");
                handle_error(ctx);
            } else if (pfd[1].revents & POLLIN) {
                crm_mdmcli_wire_msg_t *msg;

                msg = ctx->wire->recv_msg(ctx->wire, ctx->sock_fd);
                if (msg == NULL) {
                    CLOGE(ctx, "error retrieving message from server socket");
                    handle_error(ctx);
                } else {
                    int i;
                    if (msg->id == MDM_DBG_INFO) {
                        CLOGD(ctx, "<= " MSG_EVT_FORMAT "(%s,ApLogsSize:%dMB,BpLogsSize:%dMB,"
                              "BpLogsTime:%ds,%zd)", crm_mdmcli_wire_req_to_string(msg->id),
                              crm_mdmcli_dbg_type_to_string(msg->msg.debug->type),
                              msg->msg.debug->ap_logs_size, msg->msg.debug->bp_logs_size,
                              msg->msg.debug->bp_logs_time, msg->msg.debug->nb_data);
                    } else {
                        CLOGD(ctx, "<= " MSG_EVT_FORMAT "()",
                              crm_mdmcli_wire_req_to_string(msg->id));
                    }
                    for (i = 0; i < ctx->nb_evts; i++) {
                        /**
                         * @TODO: understand why I need this (int) cast !
                         */
                        if ((int)ctx->evts[i].id == msg->id) {
                            mdm_cli_callback_data_t data = { .id = msg->id,
                                                             .context = ctx->evts[i].context };
                            if (msg->id == MDM_DBG_INFO) {
                                data.data_size = sizeof(*msg->msg.debug);
                                data.data = (void *)msg->msg.debug;
                            }
                            int ret = ctx->evts[i].callback(&data);
                            if (msg->id == MDM_COLD_RESET && ret == 0)
                                mdm_cli_send_simple_msg(ctx, CRM_REQ_ACK_COLD_RESET);
                            else if (msg->id == MDM_SHUTDOWN && ret == 0)
                                mdm_cli_send_simple_msg(ctx, CRM_REQ_ACK_SHUTDOWN);
                            break;
                        }
                    }
                    ASSERT(i < ctx->nb_evts);
                }
            }
        }
    }

    return NULL;
}

static mdm_cli_hdle_t *crm_connect(const char *client_name, int inst_id,
                                   int nb_evts, const mdm_cli_register_t evts[],
                                   crm_mdmcli_wire_req_ids_t request)
{
    crm_mdm_cli_ctx_t *ctx = NULL;
    mdm_cli_hdle_t *ret = NULL;

    ASSERT(client_name != NULL && strlen(client_name) < MDM_CLI_NAME_LEN);
    ASSERT(nb_evts <= MDM_NUM_EVENTS && nb_evts >= 0);
    ASSERT(nb_evts == 0 || evts != NULL);
    ASSERT(request == CRM_REQ_REGISTER || request == CRM_REQ_REGISTER_DBG);

    ctx = calloc(1, sizeof(*ctx));
    ASSERT(ctx != NULL);
    ctx->sock_fd = -1;

    ASSERT(pthread_mutex_init(&ctx->lock, NULL) == 0);
    ctx->name = strdup(client_name);
    ASSERT(ctx->name != NULL);

    crm_logs_init(inst_id);
    crm_property_init(inst_id);
    ctx->wire = crm_mdmcli_wire_init(CRM_CLIENT_TO_SERVER, inst_id);
    ASSERT(ctx->wire);

    ctx->register_id = request;

    int events_bitmap = 0;
    ctx->nb_evts = nb_evts;
    if (nb_evts > 0) {
        ctx->evts = malloc(nb_evts * sizeof(ctx->evts[0]));
        ASSERT(ctx->evts != NULL);
        for (int i = 0; i < nb_evts; i++) {
            int event_bit = 1 << evts[i].id;
            ASSERT((event_bit & events_bitmap) == 0);
            ASSERT(evts[i].callback != NULL);
            ctx->evts[i] = evts[i];
            events_bitmap |= event_bit;
        }
    }
    ctx->events_bitmap = events_bitmap;

    ctx->sock_fd = crm_socket_connect(ctx->wire->get_socket_name(ctx->wire));
    if (ctx->sock_fd < 0) {
        CLOGE(ctx, "could not connect to CRM daemon");
        goto exit;
    }

    ctx->ipc = crm_ipc_init(CRM_IPC_THREAD);
    ASSERT(ctx->ipc != NULL);
    ctx->thread = crm_thread_init(mdmcli_event_loop, ctx, false, false);
    ASSERT(ctx->thread != NULL);

    crm_mdmcli_wire_msg_t msg;
    msg.id = request;
    msg.msg.register_client.events_bitmap = events_bitmap;
    msg.msg.register_client.name = client_name;
    CLOGD(ctx, "=> " MSG_EVT_FORMAT "(0x%08x,'%s')",
          crm_mdmcli_wire_req_to_string(msg.id), events_bitmap, client_name);
    if (!ctx->wire->send_msg(ctx->wire, &msg, ctx->sock_fd))
        ret = ctx;
    else
        CLOGE(ctx, "failure to send REGISTER message");

exit:
    if (ret == NULL)
        dispose(ctx);

    return ret;
}

/*
 * @see mdm_cli.h
 */
mdm_cli_hdle_t *mdm_cli_connect(const char *client_name, int inst_id,
                                int nb_evts, const mdm_cli_register_t evts[])
{
    return crm_connect(client_name, inst_id, nb_evts, evts, CRM_REQ_REGISTER);
}

/**
 * Debug connection function. Shall be used only for sanity test purposes.
 * This function is not declared to reduce its visibility. User shall declare it as external.
 *
 * Client connection will succeed only if CRM_KEY_DBG_ENABLE property is set to true
 * and for eng and userdebug builds.
 */
mdm_cli_hdle_t *mdm_cli_connect_dbg(const char *client_name, int inst_id,
                                    int nb_evts, const mdm_cli_register_t evts[])
{
    return crm_connect(client_name, inst_id, nb_evts, evts, CRM_REQ_REGISTER_DBG);
}

/*
 * @see mdm_cli.h
 */
int mdm_cli_disconnect(mdm_cli_hdle_t *hdle)
{
    crm_mdm_cli_ctx_t *ctx = hdle;

    ASSERT(ctx != NULL);
    ASSERT(ctx->thread != NULL);

    CLOGD(ctx, "=> " MSG_EVT_FORMAT "()", "DISCONNECT");

    ctx->disconnect = true;

    crm_ipc_msg_t msg = { .scalar = 0 };
    ctx->ipc->send_msg(ctx->ipc, &msg);

    dispose(ctx);

    return 0;
}

/*
 * @see mdm_cli.h
 */
int mdm_cli_acquire(mdm_cli_hdle_t *hdle)
{
    crm_mdm_cli_ctx_t *ctx = hdle;

    ASSERT(ctx != NULL);
    ASSERT(ctx->wire != NULL);

    ASSERT(pthread_mutex_lock(&ctx->lock) == 0);
    crm_mdmcli_wire_msg_t msg = { .id = CRM_REQ_ACQUIRE };
    CLOGD(ctx, "=> " MSG_EVT_FORMAT "()", crm_mdmcli_wire_req_to_string(msg.id));
    int ret;
    if (ctx->reconnect)
        ret = 0;
    else
        ret = ctx->wire->send_msg(ctx->wire, &msg, ctx->sock_fd);
    if (ret == 0)
        ctx->acquired = true;
    else
        CLOGE(ctx, "failed to send message");
    ASSERT(pthread_mutex_unlock(&ctx->lock) == 0);

    return ret;
}

/*
 * @see mdm_cli.h
 */
int mdm_cli_release(mdm_cli_hdle_t *hdle)
{
    crm_mdm_cli_ctx_t *ctx = hdle;

    ASSERT(ctx != NULL);
    ASSERT(ctx->wire != NULL);

    ASSERT(pthread_mutex_lock(&ctx->lock) == 0);
    crm_mdmcli_wire_msg_t msg = { .id = CRM_REQ_RELEASE };
    CLOGD(ctx, "=> " MSG_EVT_FORMAT "()", crm_mdmcli_wire_req_to_string(msg.id));
    int ret;
    if (ctx->reconnect)
        ret = 0;
    else
        ret = ctx->wire->send_msg(ctx->wire, &msg, ctx->sock_fd);
    if (ret == 0)
        ctx->acquired = false;
    else
        CLOGE(ctx, "failed to send message");
    ASSERT(pthread_mutex_unlock(&ctx->lock) == 0);

    return ret;
}

/*
 * @see mdm_cli.h
 */
int mdm_cli_restart(mdm_cli_hdle_t *hdle, mdm_cli_restart_cause_t cause,
                    const mdm_cli_dbg_info_t *data)
{
    crm_mdm_cli_ctx_t *ctx = hdle;

    ASSERT(ctx != NULL);
    ASSERT(ctx->wire != NULL);

    ASSERT(pthread_mutex_lock(&ctx->lock) == 0);
    crm_mdmcli_wire_msg_t msg;
    msg.id = CRM_REQ_RESTART;
    msg.msg.restart.cause = cause;
    msg.msg.restart.debug = data;
    if (data) {
        CLOGD(ctx, "=> " MSG_EVT_FORMAT "(%s,%s,ApLogsSize:%dMB,BpLogsSize:%dMB"
              ",BpLogsTime:%ds,%zd)",
              crm_mdmcli_wire_req_to_string(msg.id), crm_mdmcli_restart_cause_to_string(cause),
              crm_mdmcli_dbg_type_to_string(data->type), data->ap_logs_size, data->bp_logs_size,
              data->bp_logs_time, data->nb_data);
    } else {
        CLOGD(ctx, "=> " MSG_EVT_FORMAT "(%s,<nil>)", crm_mdmcli_wire_req_to_string(msg.id),
              crm_mdmcli_restart_cause_to_string(cause));
    }
    int ret;
    if (ctx->reconnect) {
        ret = -1;
    } else {
        ret = ctx->wire->send_msg(ctx->wire, &msg, ctx->sock_fd);
        if (ret)
            CLOGE(ctx, "failed to send message");
    }
    ASSERT(pthread_mutex_unlock(&ctx->lock) == 0);

    return ret;
}

/*
 * @see mdm_cli.h
 */
int mdm_cli_shutdown(mdm_cli_hdle_t *hdle)
{
    return mdm_cli_send_simple_msg(hdle, CRM_REQ_SHUTDOWN);
}

/*
 * @see mdm_cli.h
 */
int mdm_cli_nvm_bckup(mdm_cli_hdle_t *hdle)
{
    return mdm_cli_send_simple_msg(hdle, CRM_REQ_NVM_BACKUP);
}

/*
 * @see mdm_cli.h
 */
int mdm_cli_ack_cold_reset(mdm_cli_hdle_t *hdle)
{
    return mdm_cli_send_simple_msg(hdle, CRM_REQ_ACK_COLD_RESET);
}

/*
 * @see mdm_cli.h
 */
int mdm_cli_ack_shutdown(mdm_cli_hdle_t *hdle)
{
    return mdm_cli_send_simple_msg(hdle, CRM_REQ_ACK_SHUTDOWN);
}

/*
 * @see mdm_cli.h
 */
int mdm_cli_notify_dbg(mdm_cli_hdle_t *hdle, const mdm_cli_dbg_info_t *data)
{
    crm_mdm_cli_ctx_t *ctx = hdle;

    ASSERT(ctx != NULL);
    ASSERT(ctx->wire != NULL);

    ASSERT(pthread_mutex_lock(&ctx->lock) == 0);
    crm_mdmcli_wire_msg_t msg;
    msg.id = CRM_REQ_NOTIFY_DBG;
    msg.msg.debug = data;
    if (data)
        CLOGD(ctx, "=> " MSG_EVT_FORMAT "(%s,ApLogsSize:%dMB,BpLogsSize:%dMB,BpLogsTime:%ds,%zd)",
              crm_mdmcli_wire_req_to_string(msg.id), crm_mdmcli_dbg_type_to_string(data->type),
              data->ap_logs_size, data->bp_logs_size, data->bp_logs_time, data->nb_data);
    else
        CLOGD(ctx, "=> " MSG_EVT_FORMAT "(<nil>)", crm_mdmcli_wire_req_to_string(msg.id));

    int ret;
    if (ctx->reconnect) {
        ret = -1;
    } else {
        ret = ctx->wire->send_msg(ctx->wire, &msg, ctx->sock_fd);
        if (ret)
            CLOGE(ctx, "failed to send message");
    }
    ASSERT(pthread_mutex_unlock(&ctx->lock) == 0);

    return ret;
}
