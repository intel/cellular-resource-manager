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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdbool.h>
#include <poll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "libmdmcli/mdm_cli.h"

#define CRM_MODULE_TAG "TEST_CLIENT"
#include "utils/common.h"
#include "utils/string_helpers.h"
#include "utils/logs.h"
#include "utils/property.h"
#include "utils/thread.h"
#include "utils/time.h"
#include "plugins/mdmcli_wire.h"

/* Note: this is needed as long as we do not use the "real" client library that loads the server
 *       specific library.
 */
extern mdm_cli_hdle_t *mdm_cli_connect(const char *client_name, int inst_id, int nb_evts,
                                       const mdm_cli_register_t evts[]);
extern int mdm_cli_disconnect(mdm_cli_hdle_t *hdle);
extern int mdm_cli_acquire(mdm_cli_hdle_t *hdle);
extern int mdm_cli_release(mdm_cli_hdle_t *hdle);
extern int mdm_cli_restart(mdm_cli_hdle_t *hdle, mdm_cli_restart_cause_t cause,
                           const mdm_cli_dbg_info_t *data);
extern int mdm_cli_shutdown(mdm_cli_hdle_t *hdle);
extern int mdm_cli_nvm_bckup(mdm_cli_hdle_t *hdle);
extern int mdm_cli_ack_cold_reset(mdm_cli_hdle_t *hdle);
extern int mdm_cli_ack_shutdown(mdm_cli_hdle_t *hdle);
extern int mdm_cli_notify_dbg(mdm_cli_hdle_t *hdle, const mdm_cli_dbg_info_t *data);

int scenario_state;

bool sock_ko = false;
int instance_id;
crm_thread_ctx_t *thread_ctx;
crm_mdmcli_wire_ctx_t *wire_ctx;
crm_ipc_ctx_t *ipc_ctx;

#define EVENTS_BITMAP ((1 << MDM_NUM_EVENTS) - 2 - (1 << MDM_TLV_SYNCING))

/* Override the 'local client' function in the test library for the unit testing to use a simple
 * pipe here */
int socket_local_client(const char *name, int namespaceId, int type)
{
    (void)namespaceId;
    (void)type;

    char check_name[16];
    snprintf(check_name, sizeof(check_name), "crm%d", instance_id);

    ASSERT(strcmp(name, check_name) == 0);

    if (sock_ko) {
        return -1;
    } else {
        int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_fd < 0)
            return -1;
        struct sockaddr_un server = { .sun_family = AF_UNIX };
        snprintf(server.sun_path, sizeof(server.sun_path), "/tmp/%s", name);
        if (connect(sock_fd, (struct sockaddr *)&server, sizeof(struct sockaddr_un)) < 0) {
            close(sock_fd);
            return -1;
        }

        return sock_fd;
    }
}

void scenario_checker(crm_mdmcli_wire_msg_t *msg)
{
    crm_ipc_msg_t i_msg = { .scalar = 0 };

    switch (scenario_state) {
    case 0:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_REGISTER);
        ASSERT(strcmp(msg->msg.register_client.name, "test") == 0);
        ASSERT(msg->msg.register_client.events_bitmap == EVENTS_BITMAP);
        scenario_state += 1;
        break;

    case 1:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_ACQUIRE);
        scenario_state += 1;
        break;

    case 2:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_RELEASE);
        scenario_state += 1;
        break;

    case 3:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_SHUTDOWN);
        scenario_state += 1;
        break;

    case 4:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_NVM_BACKUP);
        scenario_state += 1;
        break;

    case 5:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_ACK_COLD_RESET);
        scenario_state += 1;
        break;

    case 6:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_ACK_SHUTDOWN);
        scenario_state += 1;
        break;

    case 7:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_RESTART);
        ASSERT(msg->msg.restart.cause == RESTART_MDM_ERR);
        ASSERT(msg->msg.restart.debug->type == DBG_TYPE_APIMR);
        ASSERT(msg->msg.restart.debug->ap_logs_size == 1234);
        ASSERT(msg->msg.restart.debug->bp_logs_size == 5678);
        ASSERT(msg->msg.restart.debug->bp_logs_time == 9012);
        ASSERT(msg->msg.restart.debug->nb_data == 4);
        ASSERT(strcmp(msg->msg.restart.debug->data[0], "First") == 0);
        ASSERT(strcmp(msg->msg.restart.debug->data[1], "Second") == 0);
        ASSERT(strcmp(msg->msg.restart.debug->data[2], "Third") == 0);
        ASSERT(strcmp(msg->msg.restart.debug->data[3], "Fourth") == 0);
        scenario_state += 1;
        break;

    case 8:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_NOTIFY_DBG);
        ASSERT(msg->msg.debug->type == DBG_TYPE_APIMR);
        ASSERT(msg->msg.debug->ap_logs_size == 1234);
        ASSERT(msg->msg.debug->bp_logs_size == 5678);
        ASSERT(msg->msg.debug->bp_logs_time == 9012);
        ASSERT(msg->msg.debug->nb_data == 4);
        ASSERT(strcmp(msg->msg.debug->data[0], "First") == 0);
        ASSERT(strcmp(msg->msg.debug->data[1], "Second") == 0);
        ASSERT(strcmp(msg->msg.debug->data[2], "Third") == 0);
        ASSERT(strcmp(msg->msg.debug->data[3], "New Fourth :)") == 0);
        scenario_state += 1;
        break;

    case 9:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_ACQUIRE);
        scenario_state += 1;
        break;

    case 10:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_REGISTER);
        ASSERT(strcmp(msg->msg.register_client.name, "test") == 0);
        ASSERT(msg->msg.register_client.events_bitmap == EVENTS_BITMAP);
        scenario_state += 1;
        break;

    case 11:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_ACQUIRE);
        i_msg.scalar = -scenario_state;
        ipc_ctx->send_msg(ipc_ctx, &i_msg);
        scenario_state += 1;
        break;

    case 12:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_RELEASE);
        scenario_state += 1;
        break;

    case 13:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_REGISTER);
        ASSERT(strcmp(msg->msg.register_client.name, "test") == 0);
        ASSERT(msg->msg.register_client.events_bitmap == EVENTS_BITMAP);
        i_msg.scalar = -scenario_state;
        ipc_ctx->send_msg(ipc_ctx, &i_msg);
        scenario_state += 1;
        break;

    case 14:
        ASSERT(msg == NULL);
        i_msg.scalar = -scenario_state;
        ipc_ctx->send_msg(ipc_ctx, &i_msg);
        scenario_state += 1;
        break;

    case 15:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_REGISTER);
        ASSERT(strcmp(msg->msg.register_client.name, "test ack") == 0);
        ASSERT(msg->msg.register_client.events_bitmap ==
               ((1 << MDM_COLD_RESET) | (1 << MDM_SHUTDOWN)));
        scenario_state += 1;
        ipc_ctx->send_msg(ipc_ctx, &i_msg);
        break;

    case 16:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_ACK_COLD_RESET);
        i_msg.scalar = -scenario_state;
        ipc_ctx->send_msg(ipc_ctx, &i_msg);
        scenario_state += 1;
        break;

    case 17:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_ACK_SHUTDOWN);
        i_msg.scalar = -scenario_state;
        ipc_ctx->send_msg(ipc_ctx, &i_msg);
        scenario_state += 1;
        break;

    case 18:
        ASSERT(msg == NULL);
        i_msg.scalar = -scenario_state;
        ipc_ctx->send_msg(ipc_ctx, &i_msg);
        scenario_state += 1;
        break;
    }
}

void *server_thread_stress(crm_thread_ctx_t *ctx, void *data)
{
    int sock_fd = *(int *)data;

    free(data);
    int client_fd = accept(sock_fd, 0, 0);
    bool registered = false;
    int dbg_count_send = 0;
    int dbg_count_recv = 0;

    while (1) {
        struct pollfd pfd[2] = { { .fd = ctx->get_poll_fd(ctx), .events = POLLIN },
                                 { .fd = client_fd, .events = POLLIN } };

        int timeout = registered ? rand() % 500 : -1;
        int ret = poll(pfd, 2, timeout);
        ASSERT(ret >= 0);

        if (ret == 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%d", dbg_count_send);
            dbg_count_send += 1;
            const char *dbg_data[5] = { "First", "Second", "Third", "Fourth", buf };
            mdm_cli_dbg_info_t dbg_info = { .type = DBG_TYPE_APIMR,
                                            .ap_logs_size = 1234,
                                            .bp_logs_size = 5678,
                                            .bp_logs_time = 9012,
                                            .nb_data = 5,
                                            .data = dbg_data };
            crm_mdmcli_wire_msg_t w_msg = { .id = MDM_DBG_INFO, .msg.debug = &dbg_info };
            wire_ctx->send_msg(wire_ctx, &w_msg, client_fd);
        } else {
            if (pfd[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                DASSERT(0, "Client should not stop before server !");
                break;
            } else if (pfd[1].revents & POLLIN) {
                crm_mdmcli_wire_msg_t *msg = wire_ctx->recv_msg(wire_ctx, client_fd);
                ASSERT(msg != NULL);
                switch (msg->id) {
                case CRM_REQ_REGISTER:
                    LOGD("message received [%2d]: %-15s(%08x,'%s')", scenario_state,
                         crm_mdmcli_wire_req_to_string(
                             msg->id), msg->msg.register_client.events_bitmap,
                         msg->msg.register_client.name);
                    registered = true;
                    break;
                case CRM_REQ_RESTART:
                    LOGD("message received [%2d]: %-15s()", scenario_state,
                         crm_mdmcli_wire_req_to_string(msg->id));
                    ASSERT(msg->msg.restart.cause == RESTART_MDM_ERR);
                    ASSERT(msg->msg.restart.debug->type == DBG_TYPE_APIMR);
                    ASSERT(msg->msg.restart.debug->ap_logs_size == 1234);
                    ASSERT(msg->msg.restart.debug->bp_logs_size == 5678);
                    ASSERT(msg->msg.restart.debug->bp_logs_time == 9012);
                    ASSERT(msg->msg.restart.debug->nb_data == 5);
                    ASSERT(strcmp(msg->msg.restart.debug->data[0], "First") == 0);
                    ASSERT(strcmp(msg->msg.restart.debug->data[1], "Second") == 0);
                    ASSERT(strcmp(msg->msg.restart.debug->data[2], "Third") == 0);
                    ASSERT(strcmp(msg->msg.restart.debug->data[3], "Fourth") == 0);
                    int value = atoi(msg->msg.restart.debug->data[4]);
                    ASSERT(value == dbg_count_recv);
                    dbg_count_recv += 1;
                    break;
                default:
                    LOGD("message received [%2d]: %-15s()", scenario_state,
                         crm_mdmcli_wire_req_to_string(msg->id));
                    ASSERT(0);
                    break;
                }
            }

            if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL | POLLIN))
                break;
        }
    }

    close(client_fd);
    close(sock_fd);
    wire_ctx->dispose(wire_ctx);

    return NULL;
}

void *server_thread(crm_thread_ctx_t *ctx, void *data)
{
    int sock_fd = *(int *)data;

    free(data);
    int client_fd = accept(sock_fd, 0, 0);

    while (1) {
        struct pollfd pfd[2] = { { .fd = ctx->get_poll_fd(ctx), .events = POLLIN },
                                 { .fd = client_fd, .events = POLLIN } };

        int ret = poll(pfd, 2, 15000);
        ASSERT(ret > 0);
        if (pfd[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            LOGD("socket closed");
            scenario_checker(NULL);
            break;
        } else if (pfd[1].revents & POLLIN) {
            crm_mdmcli_wire_msg_t *msg = wire_ctx->recv_msg(wire_ctx, client_fd);
            ASSERT(msg != NULL);
            switch (msg->id) {
            case CRM_REQ_REGISTER:
                LOGD("message received [%2d]: %-15s(%08x,'%s')", scenario_state,
                     crm_mdmcli_wire_req_to_string(msg->id), msg->msg.register_client.events_bitmap,
                     msg->msg.register_client.name);
                break;
            default:
                LOGD("message received [%2d]: %-15s()", scenario_state,
                     crm_mdmcli_wire_req_to_string(msg->id));
                break;
            }
            scenario_checker(msg);
        }

        if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        } else if (pfd[0].revents & POLLIN) {
            crm_ipc_msg_t msg;
            ctx->get_msg(ctx, &msg);
            crm_mdmcli_wire_msg_t w_msg = { .id = msg.scalar };
            const char *dbg_data[4] = { "First", "Second", "Third", "Fourth" };
            mdm_cli_dbg_info_t dbg_info = { .type = DBG_TYPE_APIMR,
                                            .ap_logs_size = 1234,
                                            .bp_logs_size = 5678,
                                            .bp_logs_time = 9012,
                                            .nb_data = 4,
                                            .data = dbg_data };
            if (msg.scalar == MDM_DBG_INFO)
                w_msg.msg.debug = &dbg_info;
            wire_ctx->send_msg(wire_ctx, &w_msg, client_fd);
        }
    }

    close(client_fd);
    close(sock_fd);
    wire_ctx->dispose(wire_ctx);

    return NULL;
}

void start_server(void *(*server_func)(crm_thread_ctx_t *, void *))
{
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    int *sock_ptr = malloc(sizeof(int));

    ASSERT(sock_fd >= 0);
    ASSERT(sock_ptr != NULL);
    errno = 0;
    struct sockaddr_un server = { .sun_family = AF_UNIX };
    snprintf(server.sun_path, sizeof(server.sun_path), "/tmp/crm%d", instance_id);
    unlink(server.sun_path);
    DASSERT(bind(sock_fd, (struct sockaddr *)&server, sizeof(struct sockaddr_un)) == 0,
            "Failed to bind socket (%s)", strerror(errno));
    DASSERT(listen(sock_fd, 1) == 0, "Failed to listen for connections on socket (%s)",
            strerror(errno));

    *sock_ptr = sock_fd;
    wire_ctx = crm_mdmcli_wire_init(CRM_SERVER_TO_CLIENT, instance_id);
    ASSERT(wire_ctx != NULL);
    thread_ctx = crm_thread_init(server_func, sock_ptr, true, false);
    ASSERT(thread_ctx != NULL);
}

int dbg_callback(const mdm_cli_callback_data_t *cb_data)
{
    crm_ipc_msg_t msg = { .scalar = cb_data->id };

    LOGD("modem status received: %-15s()", crm_mdmcli_wire_req_to_string(cb_data->id));

    switch (cb_data->id) {
    case MDM_DOWN:
        ASSERT(cb_data->context == (void *)0x1234);
        ASSERT(cb_data->data_size == 0);
        ASSERT(cb_data->data == NULL);
        ipc_ctx->send_msg(ipc_ctx, &msg);
        break;
    case MDM_ON:
        ASSERT(cb_data->context == (void *)0x5678);
        ASSERT(cb_data->data_size == 0);
        ASSERT(cb_data->data == NULL);
        ipc_ctx->send_msg(ipc_ctx, &msg);
        break;
    default:
        ASSERT(0);
    }
    return 0;
}

int dbg_callback_1(const mdm_cli_callback_data_t *cb_data)
{
    crm_ipc_msg_t msg = { .scalar = cb_data->id };
    int ret = 0;

    LOGD("modem status received: %-15s()", crm_mdmcli_wire_req_to_string(cb_data->id));

    switch (cb_data->id) {
    case MDM_SHUTDOWN:
        ASSERT(cb_data->context == (void *)0xabcd);
        ASSERT(cb_data->data_size == 0);
        ASSERT(cb_data->data == NULL);
        ipc_ctx->send_msg(ipc_ctx, &msg);
        ret = -1;
        break;
    default:
        ASSERT(0);
    }
    return ret;
}

int dbg_callback_2(const mdm_cli_callback_data_t *cb_data)
{
    crm_ipc_msg_t msg = { .scalar = cb_data->id };
    int ret = 0;

    LOGD("modem status received: %-15s()", crm_mdmcli_wire_req_to_string(cb_data->id));

    switch (cb_data->id) {
    case MDM_UP:
        ASSERT(cb_data->context == (void *)0x4321);
        ASSERT(cb_data->data_size == 0);
        ASSERT(cb_data->data == NULL);
        ipc_ctx->send_msg(ipc_ctx, &msg);
        break;
    case MDM_COLD_RESET:
        ASSERT(cb_data->context == (void *)0x23);
        ASSERT(cb_data->data_size == 0);
        ASSERT(cb_data->data == NULL);
        ipc_ctx->send_msg(ipc_ctx, &msg);
        ret = -1;
        break;
    default:
        ASSERT(0);
    }
    return ret;
}

int dbg_callback_3(const mdm_cli_callback_data_t *cb_data)
{
    crm_ipc_msg_t msg = { .scalar = cb_data->id };

    LOGD("modem status received: %-15s()", crm_mdmcli_wire_req_to_string(cb_data->id));

    switch (cb_data->id) {
    case MDM_OOS:
        ASSERT(cb_data->context == (void *)0x43);
        ASSERT(cb_data->data_size == 0);
        ASSERT(cb_data->data == NULL);
        ipc_ctx->send_msg(ipc_ctx, &msg);
        break;
    default:
        ASSERT(0);
    }
    return 0;
}

int dbg_callback_ack(const mdm_cli_callback_data_t *cb_data)
{
    crm_ipc_msg_t msg = { .scalar = cb_data->id };

    LOGD("modem status received: %-15s()", crm_mdmcli_wire_req_to_string(cb_data->id));

    switch (cb_data->id) {
    case MDM_COLD_RESET:
        ASSERT(cb_data->context == (void *)0x1122);
        ASSERT(cb_data->data_size == 0);
        ASSERT(cb_data->data == NULL);
        ipc_ctx->send_msg(ipc_ctx, &msg);
        break;
    case MDM_SHUTDOWN:
        ASSERT(cb_data->context == (void *)0x2211);
        ASSERT(cb_data->data_size == 0);
        ASSERT(cb_data->data == NULL);
        ipc_ctx->send_msg(ipc_ctx, &msg);
        break;
    default:
        ASSERT(0);
    }
    return 0;
}


int dbg_callback_dbg(const mdm_cli_callback_data_t *cb_data)
{
    crm_ipc_msg_t msg = { .scalar = cb_data->id };

    LOGD("modem status received: %-15s()", crm_mdmcli_wire_req_to_string(cb_data->id));
    ASSERT(cb_data->id == MDM_DBG_INFO);
    ASSERT(cb_data->context == (void *)0xbebe);
    ASSERT(cb_data->data_size == sizeof(mdm_cli_dbg_info_t));
    ASSERT(cb_data->data != NULL);
    ASSERT(((mdm_cli_dbg_info_t *)cb_data->data)->type == DBG_TYPE_APIMR);
    ASSERT(((mdm_cli_dbg_info_t *)cb_data->data)->ap_logs_size == 1234);
    ASSERT(((mdm_cli_dbg_info_t *)cb_data->data)->bp_logs_size == 5678);
    ASSERT(((mdm_cli_dbg_info_t *)cb_data->data)->bp_logs_time == 9012);
    ASSERT(((mdm_cli_dbg_info_t *)cb_data->data)->nb_data == 4);
    ASSERT(strcmp(((mdm_cli_dbg_info_t *)cb_data->data)->data[0], "First") == 0);
    ASSERT(strcmp(((mdm_cli_dbg_info_t *)cb_data->data)->data[1], "Second") == 0);
    ASSERT(strcmp(((mdm_cli_dbg_info_t *)cb_data->data)->data[2], "Third") == 0);
    ASSERT(strcmp(((mdm_cli_dbg_info_t *)cb_data->data)->data[3], "Fourth") == 0);
    ipc_ctx->send_msg(ipc_ctx, &msg);
    return 0;
}

int dbg_callback_stress(const mdm_cli_callback_data_t *cb_data)
{
    LOGD("modem status received: %-15s()", crm_mdmcli_wire_req_to_string(cb_data->id));

    switch (cb_data->id) {
    case MDM_DOWN:
        exit(0);
        break;

    case MDM_DBG_INFO:
        ASSERT(cb_data->context != NULL);
        int dbg_count = *((int *)cb_data->context);
        ASSERT(cb_data->data_size == sizeof(mdm_cli_dbg_info_t));
        ASSERT(cb_data->data != NULL);
        ASSERT(((mdm_cli_dbg_info_t *)cb_data->data)->type == DBG_TYPE_APIMR);
        ASSERT(((mdm_cli_dbg_info_t *)cb_data->data)->ap_logs_size == 1234);
        ASSERT(((mdm_cli_dbg_info_t *)cb_data->data)->bp_logs_size == 5678);
        ASSERT(((mdm_cli_dbg_info_t *)cb_data->data)->bp_logs_time == 9012);
        ASSERT(((mdm_cli_dbg_info_t *)cb_data->data)->nb_data == 5);
        ASSERT(strcmp(((mdm_cli_dbg_info_t *)cb_data->data)->data[0], "First") == 0);
        ASSERT(strcmp(((mdm_cli_dbg_info_t *)cb_data->data)->data[1], "Second") == 0);
        ASSERT(strcmp(((mdm_cli_dbg_info_t *)cb_data->data)->data[2], "Third") == 0);
        ASSERT(strcmp(((mdm_cli_dbg_info_t *)cb_data->data)->data[3], "Fourth") == 0);
        int dbg_count_msg = atoi(((mdm_cli_dbg_info_t *)cb_data->data)->data[4]);
        ASSERT(dbg_count_msg == dbg_count);
        *((int *)cb_data->context) += 1;
        break;

    default:
        ASSERT(0);
        break;
    }

    return 0;
}

void send_and_check(int id)
{
    crm_ipc_msg_t msg = { .scalar = id };

    thread_ctx->send_msg(thread_ctx, &msg);
    struct pollfd pfd = { .fd = ipc_ctx->get_poll_fd(ipc_ctx), .events = POLLIN };
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == id);
}

void ignore_signal(int sig)
{
    (void)sig;
    usleep(rand() % 5000);
}

int main(void)
{
    mdm_cli_hdle_t *ctx;

    /* Some clean-up before scenario start */
    unlink("/tmp/crm0");
    srand(getpid() | time(NULL));

    unlink(CRM_PROPERTY_PIPE_NAME);

    ASSERT((ipc_ctx = crm_ipc_init(CRM_IPC_THREAD)) != NULL);

    LOGD("Testing failure to connect to server");
    sock_ko = true;
    ctx = mdm_cli_connect("test", 0, 0, NULL);
    ASSERT(ctx == NULL);

    LOGD("Full test (connection / requests / replies / error + reconnection / deconnection)");
    sock_ko = false;
    scenario_state = 0;
    start_server(server_thread);

    mdm_cli_register_t callbacks[] = {
        { .id = MDM_DOWN, .callback = dbg_callback, (void *)0x1234 },
        { .id = MDM_ON, .callback = dbg_callback, (void *)0x5678 },
        { .id = MDM_UP, .callback = dbg_callback_2, (void *)0x4321 },
        { .id = MDM_OOS, .callback = dbg_callback_3, (void *)0x43 },
        { .id = MDM_COLD_RESET, .callback = dbg_callback_2, (void *)0x23 },
        { .id = MDM_SHUTDOWN, .callback = dbg_callback_1, (void *)0xabcd },
        { .id = MDM_DBG_INFO, .callback = dbg_callback_dbg, (void *)0xbebe },
    };

    ctx = mdm_cli_connect("test", 0, 7, callbacks);
    ASSERT(ctx != NULL);
    usleep(250000);
    mdm_cli_acquire(ctx);
    usleep(250000);
    mdm_cli_release(ctx);
    usleep(250000);
    mdm_cli_shutdown(ctx);
    usleep(250000);
    mdm_cli_nvm_bckup(ctx);
    usleep(250000);
    mdm_cli_ack_cold_reset(ctx);
    usleep(250000);
    mdm_cli_ack_shutdown(ctx);
    usleep(250000);

    const char *dbg_data[4] = { "First", "Second", "Third", "Fourth" };
    mdm_cli_dbg_info_t dbg_info = { .type = DBG_TYPE_APIMR,
                                    .ap_logs_size = 1234,
                                    .bp_logs_size = 5678,
                                    .bp_logs_time = 9012,
                                    .nb_data = 4,
                                    .data = dbg_data };

    mdm_cli_restart(ctx, RESTART_MDM_ERR, &dbg_info);
    usleep(250000);

    dbg_data[3] = "New Fourth :)";
    mdm_cli_notify_dbg(ctx, &dbg_info);
    usleep(250000);

    mdm_cli_acquire(ctx);
    usleep(250000);

    send_and_check(MDM_DOWN);
    send_and_check(MDM_ON);
    send_and_check(MDM_UP);
    send_and_check(MDM_OOS);
    send_and_check(MDM_COLD_RESET);
    send_and_check(MDM_SHUTDOWN);
    send_and_check(MDM_DBG_INFO);

    struct pollfd pfd = { .fd = ipc_ctx->get_poll_fd(ipc_ctx), .events = POLLIN };
    crm_ipc_msg_t msg;
    thread_ctx->dispose(thread_ctx, NULL);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == MDM_DOWN);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == MDM_COLD_RESET);
    sleep(2);

    start_server(server_thread);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == -11);

    send_and_check(MDM_UP);

    mdm_cli_release(ctx);
    usleep(250000);

    thread_ctx->dispose(thread_ctx, NULL);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == MDM_DOWN);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == MDM_COLD_RESET);
    sleep(2);
    start_server(server_thread);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == -13);

    mdm_cli_disconnect(ctx);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == -14);
    thread_ctx->dispose(thread_ctx, NULL);

    sleep(1);
    start_server(server_thread);
    mdm_cli_register_t callbacks_ack[] = {
        { .id = MDM_COLD_RESET, .callback = dbg_callback_ack, (void *)0x1122 },
        { .id = MDM_SHUTDOWN, .callback = dbg_callback_ack, (void *)0x2211 },
    };


    ctx = mdm_cli_connect("test ack", 0, 2, callbacks_ack);
    ASSERT(ctx != NULL);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    usleep(250000);

    send_and_check(MDM_COLD_RESET);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == -16);
    send_and_check(MDM_SHUTDOWN);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == -17);

    mdm_cli_disconnect(ctx);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == -18);

    thread_ctx->dispose(thread_ctx, NULL);
    ipc_ctx->dispose(ipc_ctx, NULL);

    LOGD("Starting signal stress test for 10 seconds");

    start_server(server_thread_stress);
    struct timespec timer_end;
    crm_time_add_ms(&timer_end, 10000);

    pid_t pid = fork();
    ASSERT(pid >= 0);
    if (pid == 0) {
        struct sigaction sigact;

        memset(&sigact, 0, sizeof(struct sigaction));
        ASSERT(sigemptyset(&sigact.sa_mask) == 0);
        sigact.sa_flags = 0;
        sigact.sa_handler = ignore_signal;
        ASSERT(sigaction(SIGINT, &sigact, NULL) == 0);

        int dbg_count_send = 0;
        int dbg_count_recv = 0;
        mdm_cli_register_t callbacks_stress[] = {
            { .id = MDM_DOWN, .callback = dbg_callback_stress, NULL },
            { .id = MDM_DBG_INFO, .callback = dbg_callback_stress, &dbg_count_recv },
        };
        ctx = mdm_cli_connect("test signal", 0, 2, callbacks_stress);

        /* Process exit is done on MDM_DOWN callback */
        while (1) {
            int timeout;
            crm_time_add_ms(&timer_end, 500);
            while ((timeout = crm_time_get_remain_ms(&timer_end)) > 0)
                poll(NULL, 0, timeout);

            char buf[256];
            snprintf(buf, sizeof(buf), "%d", dbg_count_send);
            dbg_count_send += 1;
            const char *dbg_data[5] = { "First", "Second", "Third", "Fourth", buf };
            mdm_cli_dbg_info_t dbg_info = { .type = DBG_TYPE_APIMR,
                                            .ap_logs_size = 1234,
                                            .bp_logs_size = 5678,
                                            .bp_logs_time = 9012,
                                            .nb_data = 5,
                                            .data = dbg_data };
            mdm_cli_restart(ctx, RESTART_MDM_ERR, &dbg_info);
        }

        return 0;
    } else {
        while (crm_time_get_remain_ms(&timer_end) > 0) {
            usleep(rand() % 5000);
            ASSERT(kill(pid, SIGINT) == 0);
        }
        crm_ipc_msg_t msg = { .scalar = 0 };
        thread_ctx->send_msg(thread_ctx, &msg);
        thread_ctx->dispose(thread_ctx, NULL);
    }
    waitpid(pid, NULL, 0);

    LOGD("Test passed !");

    return 0;
}
