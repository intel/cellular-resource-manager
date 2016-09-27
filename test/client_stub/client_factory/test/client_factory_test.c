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

#include "libmdmcli/mdm_cli.h"

#define CRM_MODULE_TAG "CLFAT"
#include "utils/common.h"
#include "utils/string_helpers.h"
#include "utils/logs.h"
#include "utils/thread.h"
#include "plugins/mdmcli_wire.h"
#include "test/client_factory.h"

int scenario_state;

int instance_id;
crm_thread_ctx_t *thread_ctx;
crm_mdmcli_wire_ctx_t *wire_ctx;
crm_ipc_ctx_t *ipc_ctx;

crm_client_stub_t *client_ctx;

void scenario_checker(crm_mdmcli_wire_msg_t *msg)
{
    crm_ipc_msg_t i_msg = { .scalar = 0 };

    switch (scenario_state) {
    case 0:
        ASSERT(msg != NULL);
        ASSERT(msg->id == CRM_REQ_REGISTER);
        ASSERT(strcmp(msg->msg.register_client.name, "test") == 0);
        ASSERT(msg->msg.register_client.events_bitmap == ((1 << MDM_NUM_EVENTS) - 2));
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
        ASSERT(msg->msg.register_client.events_bitmap == ((1 << MDM_NUM_EVENTS) - 2));
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
        ASSERT(msg->msg.register_client.events_bitmap == ((1 << MDM_NUM_EVENTS) - 2));
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

void start_server(void)
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
    thread_ctx = crm_thread_init(server_thread, sock_ptr, true, false);
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

void send_and_check(int id)
{
    crm_ipc_msg_t msg = { .scalar = id };

    thread_ctx->send_msg(thread_ctx, &msg);
    struct pollfd pfd = { .fd = ipc_ctx->get_poll_fd(ipc_ctx), .events = POLLIN };
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == id);
}

int test_callback(crm_client_stub_t *client,
                  const mdm_cli_callback_data_t *callback_data)
{
    mdm_cli_register_t callbacks_ack[] = {
        { .id = MDM_COLD_RESET, .callback = dbg_callback_ack, (void *)0x1122 },
        { .id = MDM_SHUTDOWN, .callback = dbg_callback_ack, (void *)0x2211 },
    };

    mdm_cli_register_t callbacks[] = {
        { .id = MDM_DOWN, .callback = dbg_callback, (void *)0x1234 },
        { .id = MDM_ON, .callback = dbg_callback, (void *)0x5678 },
        { .id = MDM_UP, .callback = dbg_callback_2, (void *)0x4321 },
        { .id = MDM_OOS, .callback = dbg_callback_3, (void *)0x43 },
        { .id = MDM_COLD_RESET, .callback = dbg_callback_2, (void *)0x23 },
        { .id = MDM_SHUTDOWN, .callback = dbg_callback_1, (void *)0xabcd },
        { .id = MDM_DBG_INFO, .callback = dbg_callback_dbg, (void *)0xbebe },
    };

    size_t array_size;
    mdm_cli_register_t *array_ptr;

    ASSERT(client == client_ctx);
    ASSERT(callback_data->context != NULL);

    int type = *(int *)callback_data->context;
    if (type == 0x1234) {
        array_size = ARRAY_SIZE(callbacks);
        array_ptr = callbacks;
    } else if (type == 0x4321) {
        array_size = ARRAY_SIZE(callbacks_ack);
        array_ptr = callbacks_ack;
    } else {
        ASSERT(0);
    }
    for (size_t i = 0; i < array_size; i++) {
        if (callback_data->id == array_ptr[i].id) {
            mdm_cli_callback_data_t cb_data = *callback_data;
            cb_data.context = array_ptr[i].context;
            return array_ptr[i].callback(&cb_data);
        }
    }
    ASSERT(0);
    return 0;
}

int main(void)
{
    int test_type;

    instance_id = 1;

    test_type = 0x1234;
    crm_client_factory_t *factory_ctx = crm_client_factory_init(instance_id, test_callback,
                                                                &test_type);

    ASSERT((ipc_ctx = crm_ipc_init(CRM_IPC_THREAD)) != NULL);

    LOGD("Testing failure to connect to server");
    client_ctx = factory_ctx->add_client(factory_ctx, "test",
                                         (1u << MDM_DOWN) | (1u << MDM_SHUTDOWN) |
                                         (1u << MDM_ON) | (1u << MDM_UP) | (1u << MDM_OOS) |
                                         (1u << MDM_COLD_RESET) | (1u << MDM_DBG_INFO));
    ASSERT(client_ctx->connect(client_ctx) != 0);

    LOGD("Full test (connection / requests / replies / error + reconnection / deconnection)");
    scenario_state = 0;
    start_server();
    usleep(250000);

    ASSERT(client_ctx->connect(client_ctx) == 0);
    usleep(250000);
    client_ctx->acquire(client_ctx);
    usleep(250000);
    client_ctx->release(client_ctx);
    usleep(250000);
    client_ctx->shutdown(client_ctx);
    usleep(250000);
    client_ctx->nvm_bckup(client_ctx);
    usleep(250000);
    client_ctx->ack_cold_reset(client_ctx);
    usleep(250000);
    client_ctx->ack_shutdown(client_ctx);
    usleep(250000);

    const char *dbg_data[4] = { "First", "Second", "Third", "Fourth" };
    mdm_cli_dbg_info_t dbg_info = { .type = DBG_TYPE_APIMR,
                                    .ap_logs_size = 1234,
                                    .bp_logs_size = 5678,
                                    .bp_logs_time = 9012,
                                    .nb_data = 4,
                                    .data = dbg_data };

    client_ctx->restart(client_ctx, RESTART_MDM_ERR, &dbg_info);
    usleep(250000);

    dbg_data[3] = "New Fourth :)";
    client_ctx->notify_dbg(client_ctx, &dbg_info);
    usleep(250000);

    send_and_check(MDM_DOWN);
    send_and_check(MDM_ON);
    send_and_check(MDM_UP);
    send_and_check(MDM_OOS);
    send_and_check(MDM_COLD_RESET);
    send_and_check(MDM_SHUTDOWN);
    send_and_check(MDM_DBG_INFO);

    client_ctx->acquire(client_ctx);
    usleep(250000);

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

    start_server();
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == -11);

    send_and_check(MDM_UP);

    client_ctx->release(client_ctx);
    usleep(250000);

    thread_ctx->dispose(thread_ctx, NULL);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == MDM_DOWN);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == MDM_COLD_RESET);
    sleep(2);
    start_server();
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == -13);

    client_ctx->disconnect(client_ctx);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == -14);
    thread_ctx->dispose(thread_ctx, NULL);

    sleep(1);
    start_server();
    test_type = 0x4321;
    usleep(250000);

    client_ctx = factory_ctx->add_client(factory_ctx, "test ack",
                                         (1u << MDM_COLD_RESET) | (1u << MDM_SHUTDOWN));
    ASSERT(client_ctx != NULL);
    ASSERT(client_ctx->connect(client_ctx) == 0);
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

    client_ctx->disconnect(client_ctx);
    ASSERT(poll(&pfd, 1, 5000) == 1);
    ASSERT(ipc_ctx->get_msg(ipc_ctx, &msg));
    ASSERT(msg.scalar == -18);

    thread_ctx->dispose(thread_ctx, NULL);
    ipc_ctx->dispose(ipc_ctx, NULL);
    LOGD("Test passed !");

    return 0;
}
