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

#define CRM_MODULE_TAG "CLAT"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/ipc.h"
#include "utils/wakelock.h"
#include "test/test_utils.h"
#include "plugins/client_abstraction.h"
#include "plugins/control.h"
#include "plugins/mdmcli_wire.h"

crm_ipc_ctx_t *ipc;
crm_mdmcli_wire_ctx_t *wire;
crm_cli_abs_ctx_t *client_abs;

typedef enum {
    CLA_REQ_START = 1,
    CLA_REQ_STOP = 2,
    CLA_REQ_RESTART = 3,
    CLA_REQ_BACKUP = 4
} cla_req_type;

typedef struct {
    crm_ctrl_restart_type_t type;
    bool dbg_info_present;
    mdm_cli_dbg_info_t dbg_info;
    char dbg_info_strings[MDM_CLI_MAX_NB_DATA][MDM_CLI_MAX_LEN_DATA];
} restart_info_t;

typedef struct {
    enum {
        EVT_CLIENT,
        EVT_CTRL
    } type;
    union {
        struct {
            cla_req_type req;
            restart_info_t info;
        } ctrl;
        struct {
            int fd;
            bool expect_err;
            mdm_cli_event_t id;
            restart_info_t info;
        } client;
    };
} evt_wait_t;

static inline mdm_cli_restart_cause_t get_cause(crm_ctrl_restart_type_t type)
{
    switch (type) {
    case CTRL_MODEM_RESTART: return RESTART_MDM_ERR;
    case CTRL_MODEM_UPDATE:  return RESTART_APPLY_UPDATE;
    default: ASSERT(0);
    }
}

static void start(crm_ctrl_ctx_t *ctx)
{
    (void)ctx;  // UNUSED
    LOGD("received start");
    crm_ipc_msg_t msg;
    msg.scalar = CLA_REQ_START;
    ASSERT(ipc->send_msg(ipc, &msg));
}

static void stop(crm_ctrl_ctx_t *ctx)
{
    (void)ctx;  // UNUSED
    LOGD("received stop");
    crm_ipc_msg_t msg;
    msg.scalar = CLA_REQ_STOP;
    ASSERT(ipc->send_msg(ipc, &msg));
}

static void restart(crm_ctrl_ctx_t *ctx, crm_ctrl_restart_type_t type,
                    const mdm_cli_dbg_info_t *dbg)
{
    (void)ctx;   // UNUSED
    crm_ipc_msg_t msg;
    switch (type) {
    case CTRL_MODEM_RESTART:
    case CTRL_MODEM_UPDATE:
        msg.scalar = CLA_REQ_RESTART;
        LOGD("received restart");
        break;
    case CTRL_BACKUP_NVM:
        msg.scalar = CLA_REQ_BACKUP;
        LOGD("received restart with backup");
        break;
    default: DASSERT(0, "received: %d", type);
    }

    restart_info_t *info = malloc(sizeof(restart_info_t));
    ASSERT(info);
    info->type = type;
    info->dbg_info_present = dbg != NULL;
    if (dbg) {
        info->dbg_info = *dbg;
        for (size_t i = 0; i < info->dbg_info.nb_data; i++) {
            strncpy(info->dbg_info_strings[i], dbg->data[i],
                    MDM_CLI_MAX_LEN_DATA);
            info->dbg_info_strings[i][MDM_CLI_MAX_LEN_DATA - 1] = '\0';
        }
    }
    msg.data = info;
    ASSERT(ipc->send_msg(ipc, &msg));
}

int connect_to_server(const char *name)
{
    struct sockaddr_un server = { .sun_family = AF_UNIX };

    snprintf(server.sun_path, sizeof(server.sun_path), "/tmp/%s", name);
    for (int i = 0; i < 5; i++) {
        int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        ASSERT(sock_fd >= 0);
        if (connect(sock_fd, (struct sockaddr *)&server, sizeof(struct sockaddr_un)) >= 0)
            return sock_fd;
        else
            close(sock_fd);
        usleep(250000);
    }
    ASSERT(0);
}

struct pollfd pfd[32];
int num_fds;

static void add_fd(int fd)
{
    ASSERT(num_fds < 32);
    pfd[num_fds].fd = fd;
    pfd[num_fds].events = POLLIN;
    num_fds += 1;
}
static void del_fd(int fd)
{
    int i;

    for (i = 0; i < num_fds && pfd[i].fd != fd; i++) ;
    ASSERT(i < num_fds);
    if (i != (num_fds - 1))
        memmove(&pfd[i], &pfd[i + 1], num_fds - i - 1);
    num_fds -= 1;
}

static void wait_evt(int to, size_t num_evt, evt_wait_t *evts)
{
    unsigned int todo_bitmap = (1u << num_evt) - 1;

    if (todo_bitmap) {
        while (todo_bitmap) {
            // Note: as timeouts are approximative, do not complexify the code to recompute them :)
            int ret = poll(pfd, num_fds, to);
            ASSERT(ret > 0);

            for (int j = 0; j < num_fds; j++) {
                if (pfd[j].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    ASSERT(j > 0);
                    bool handled = false;
                    for (size_t i = 0; i < num_evt; i++) {
                        if ((1u << i) & todo_bitmap) {
                            if ((evts[i].type == EVT_CLIENT) && (evts[i].client.fd == pfd[j].fd)) {
                                handled = true;
                                todo_bitmap &= ~(1u << i);
                                ASSERT(evts[i].client.expect_err == true);
                            }
                        }
                    }
                    ASSERT(handled);
                } else if (pfd[j].revents & POLLIN) {
                    bool handled = false;
                    for (size_t i = 0; i < num_evt; i++) {
                        if ((1u << i) & todo_bitmap) {
                            if ((evts[i].type == EVT_CTRL) && (j == 0)) {
                                handled = true;
                                todo_bitmap &= ~(1u << i);
                                crm_ipc_msg_t msg_ipc;
                                ASSERT(pfd[0].revents == POLLIN);
                                ASSERT(ipc->get_msg(ipc, &msg_ipc) == true);
                                ASSERT(msg_ipc.scalar == evts[i].ctrl.req);
                                if (msg_ipc.scalar == CLA_REQ_RESTART) {
                                    ASSERT(msg_ipc.data != NULL);
                                    restart_info_t *r_info = msg_ipc.data;
                                    ASSERT(r_info->type == evts[i].ctrl.info.type);
                                    ASSERT(r_info->dbg_info_present ==
                                           evts[i].ctrl.info.dbg_info_present);
                                    if (r_info->dbg_info_present) {
                                        ASSERT(r_info->dbg_info.type ==
                                               evts[i].ctrl.info.dbg_info.type);
                                        ASSERT(r_info->dbg_info.ap_logs_size ==
                                               evts[i].ctrl.info.dbg_info.ap_logs_size);
                                        ASSERT(r_info->dbg_info.bp_logs_size ==
                                               evts[i].ctrl.info.dbg_info.bp_logs_size);
                                        ASSERT(r_info->dbg_info.bp_logs_time ==
                                               evts[i].ctrl.info.dbg_info.bp_logs_time);
                                        ASSERT(r_info->dbg_info.nb_data ==
                                               evts[i].ctrl.info.dbg_info.nb_data);
                                        for (size_t j = 0; j < r_info->dbg_info.nb_data; j++)
                                            ASSERT(strcmp(r_info->dbg_info_strings[j],
                                                          evts[i].ctrl.info.dbg_info_strings[j]) ==
                                                   0);
                                    }
                                    free(msg_ipc.data);
                                }
                            } else if ((evts[i].type == EVT_CLIENT) &&
                                       (evts[i].client.fd == pfd[j].fd)) {
                                handled = true;
                                todo_bitmap &= ~(1u << i);
                                ASSERT(evts[i].client.expect_err == false);
                                crm_mdmcli_wire_msg_t *r_msg = wire->recv_msg(wire, pfd[j].fd);
                                ASSERT(r_msg != NULL);
                                ASSERT(r_msg->id == (int)evts[i].client.id);
                                if (r_msg->id == MDM_DBG_INFO) {
                                    ASSERT(
                                        (r_msg->msg.debug !=
                                         NULL) == evts[i].client.info.dbg_info_present);
                                    if (r_msg->msg.debug) {
                                        ASSERT(r_msg->msg.debug->type ==
                                               evts[i].client.info.dbg_info.type);
                                        ASSERT(r_msg->msg.debug->ap_logs_size ==
                                               evts[i].client.info.dbg_info.ap_logs_size);
                                        ASSERT(r_msg->msg.debug->bp_logs_size ==
                                               evts[i].client.info.dbg_info.bp_logs_size);
                                        ASSERT(r_msg->msg.debug->bp_logs_time ==
                                               evts[i].client.info.dbg_info.bp_logs_time);
                                        ASSERT(r_msg->msg.debug->nb_data ==
                                               evts[i].client.info.dbg_info.nb_data);
                                        for (size_t k = 0; k < r_msg->msg.debug->nb_data; k++)
                                            ASSERT(strcmp(r_msg->msg.debug->data[k],
                                                          evts[i].client.info.dbg_info_strings[k]) ==
                                                   0);
                                    }
                                }
                            }
                        }
                    }
                    ASSERT(handled);
                }
            }
        }
    } else {
        int ret = poll(pfd, num_fds, to);
        ASSERT(ret == 0);
    }
}

static void wait_single(int type, int evt, int fd)
{
    if (type == EVT_CTRL) {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = evt } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    } else {
        evt_wait_t w[] = {
            { .type = EVT_CLIENT,
              .client = { .fd = fd, .expect_err = evt < 0, .id = (evt >= 0) ? evt : 0 } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }
}

static void wait_multiple_client(int evt, int num_fds, int *fds)
{
    evt_wait_t *w = malloc(num_fds * sizeof(*w));

    for (int i = 0; i < num_fds; i++) {
        w[i].type = EVT_CLIENT;
        w[i].client.fd = fds[i];
        w[i].client.expect_err = evt < 0;
        w[i].client.id = (evt >= 0) ? evt : 0;
    }
    wait_evt(1000, num_fds, w);
    free(w);
}

static void wait_multiple_client_plus(int *evts, int num_fds, int *fds)
{
    evt_wait_t *w = malloc(num_fds * sizeof(*w));

    for (int i = 0; i < num_fds; i++) {
        w[i].type = EVT_CLIENT;
        w[i].client.fd = fds[i];
        w[i].client.expect_err = evts[i] < 0;
        w[i].client.id = (evts[i] >= 0) ? evts[i] : 0;
    }
    wait_evt(1000, num_fds, w);
    free(w);
}

static void send_register(int sock, int evt_bitmap, const char *name)
{
    crm_mdmcli_wire_msg_t msg;

    msg.id = CRM_REQ_REGISTER;
    msg.msg.register_client.events_bitmap = evt_bitmap;
    msg.msg.register_client.name = name;
    ASSERT(wire->send_msg(wire, &msg, sock) == 0);
}

static void send_restart(int sock, crm_ctrl_restart_type_t type,
                         const mdm_cli_dbg_info_t *dbg_info)
{
    crm_mdmcli_wire_msg_t msg;

    msg.id = CRM_REQ_RESTART;
    msg.msg.restart.cause = get_cause(type);
    msg.msg.restart.debug = dbg_info;
    ASSERT(wire->send_msg(wire, &msg, sock) == 0);
}

static void send_notify_dbg(int sock, const mdm_cli_dbg_info_t *dbg_info)
{
    crm_mdmcli_wire_msg_t msg;

    msg.id = CRM_REQ_NOTIFY_DBG;
    msg.msg.debug = dbg_info;
    ASSERT(wire->send_msg(wire, &msg, sock) == 0);
}

static void send_simple_msg(int sock, int evt)
{
    crm_mdmcli_wire_msg_t msg;

    msg.id = evt;
    ASSERT(wire->send_msg(wire, &msg, sock) == 0);
}

/* Useful for conditional breakpoints in GDB :) */
int test_step = 0;

int main(void)
{
    tcs_ctx_t *tcs = CRM_TEST_tcs_init("host_sofia", MDM_CLI_DEFAULT_INSTANCE);

    wire = crm_mdmcli_wire_init(CRM_CLIENT_TO_SERVER, 0);

    CRM_TEST_get_control_socket_android(wire->get_socket_name(wire));

    // Fake control context just for testing :)
    crm_ctrl_ctx_t control = { .start = start,
                               .stop = stop,
                               .restart = restart };

    crm_wakelock_t *wakelock = crm_wakelock_init("test");

    client_abs = crm_cli_abs_init(0, false, tcs, &control, wakelock);
    ASSERT(client_abs != NULL);

    ipc = crm_ipc_init(CRM_IPC_THREAD);
    ASSERT(ipc != NULL);
    add_fd(ipc->get_poll_fd(ipc));

    int cl1 = connect_to_server(wire->get_socket_name(wire));
    add_fd(cl1);

    /* Test multiple register of a client */
    LOGD("========== Test multiple register error case");
    send_register(cl1, 1 << MDM_DOWN, "Client1");
    wait_evt(50, 0, NULL);

    send_register(cl1, 1 << MDM_DOWN, "Client1");
    wait_single(EVT_CLIENT, -1, cl1);
    close(cl1);
    del_fd(cl1);

    /* Test basic acquire / release / dbg_info notification */
    LOGD("========== Test basic acquire / release ( + dbg info notification broadcast)");
    cl1 = connect_to_server(wire->get_socket_name(wire));
    add_fd(cl1);

    send_register(cl1, (1 << MDM_NUM_EVENTS) - 1, "Client1");
    wait_evt(50, 0, NULL);

    const char *dbg_data[] = { "Test", "Foo" };
    mdm_cli_dbg_info_t dbg_info =
    { .type = DBG_TYPE_ERROR, .ap_logs_size = 3,
      .bp_logs_size = 18, .bp_logs_time = 200,
      .nb_data = 2, .data = dbg_data };
    client_abs->notify_client(client_abs, MDM_DBG_INFO, sizeof(dbg_info), &dbg_info);
    {
        evt_wait_t w[] = {
            { .type = EVT_CLIENT,
              .client = { .fd = cl1, .expect_err = false, .id = MDM_DBG_INFO,
                          .info = { .dbg_info_present = true,
                                    .dbg_info = dbg_info,
                                    .dbg_info_strings = { "Test", "Foo" } } } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }

    send_simple_msg(cl1, CRM_REQ_ACQUIRE);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_OFF);
    wait_single(EVT_CLIENT, MDM_DOWN, cl1);
    wait_single(EVT_CTRL, CLA_REQ_START, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    wait_single(EVT_CLIENT, MDM_UP, cl1);
    wait_evt(50, 0, NULL);

    send_simple_msg(cl1, CRM_REQ_RELEASE);
    wait_single(EVT_CLIENT, MDM_SHUTDOWN, cl1);
    wait_single(EVT_CLIENT, MDM_DOWN, cl1);
    send_simple_msg(cl1, CRM_REQ_ACK_SHUTDOWN);
    wait_single(EVT_CTRL, CLA_REQ_STOP, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_OFF);
    wait_evt(50, 0, NULL);

    LOGD("========== Test dbg_info notification");
    int cl2 = connect_to_server(wire->get_socket_name(wire));
    add_fd(cl2);
    send_register(cl2, (1u << MDM_DBG_INFO), "Client2");
    wait_evt(50, 0, NULL);

    dbg_data[1] = "FooBar";
    send_notify_dbg(cl1, &dbg_info);
    {
        evt_wait_t w[] = {
            { .type = EVT_CLIENT,
              .client = { .fd = cl1, .expect_err = false, .id = MDM_DBG_INFO,
                          .info = { .dbg_info_present = true,
                                    .dbg_info = dbg_info,
                                    .dbg_info_strings = { "Test", "FooBar" } } } },
            { .type = EVT_CLIENT,
              .client = { .fd = cl2, .expect_err = false, .id = MDM_DBG_INFO,
                          .info = { .dbg_info_present = true,
                                    .dbg_info = dbg_info,
                                    .dbg_info_strings = { "Test", "FooBar" } } } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }

    close(cl2);
    del_fd(cl2);

    LOGD("========== Test basic acquire / release (inversing success / modem state)");
    send_simple_msg(cl1, CRM_REQ_ACQUIRE);
    wait_single(EVT_CTRL, CLA_REQ_START, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    wait_single(EVT_CLIENT, MDM_UP, cl1);

    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);
    wait_evt(50, 0, NULL);

    send_simple_msg(cl1, CRM_REQ_RELEASE);
    wait_single(EVT_CLIENT, MDM_SHUTDOWN, cl1);
    wait_single(EVT_CLIENT, MDM_DOWN, cl1);
    send_simple_msg(cl1, CRM_REQ_ACK_SHUTDOWN);
    wait_single(EVT_CTRL, CLA_REQ_STOP, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_OFF);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    LOGD("========== Test acquire then release through disconnect while modem starts");
    send_simple_msg(cl1, CRM_REQ_ACQUIRE);
    wait_single(EVT_CTRL, CLA_REQ_START, 0);
    wait_evt(50, 0, NULL);

    cl2 = connect_to_server(wire->get_socket_name(wire));
    add_fd(cl2);
    send_register(cl2, (1u << MDM_UP) | (1u << MDM_DOWN) | (1u << MDM_OOS), "Client2");
    wait_single(EVT_CLIENT, MDM_DOWN, cl2);

    close(cl1);
    del_fd(cl1);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    wait_single(EVT_CLIENT, MDM_UP, cl2);
    wait_evt(50, 0, NULL);

    client_abs->notify_operation_result(client_abs, 0);
    {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = CLA_REQ_STOP } },
            { .type = EVT_CLIENT,
              .client = { .fd = cl2, .expect_err = false, .id = MDM_DOWN } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_OFF);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    LOGD("========== Test multiple acquire / release");
    cl1 = connect_to_server(wire->get_socket_name(wire));
    add_fd(cl1);
    wait_evt(50, 0, NULL);

    send_register(cl1, (1 << MDM_NUM_EVENTS) - 1, "Client1");
    wait_single(EVT_CLIENT, MDM_DOWN, cl1);
    usleep(250000);

    send_simple_msg(cl2, CRM_REQ_ACQUIRE);
    wait_single(EVT_CTRL, CLA_REQ_START, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl1, cl2 };
        wait_multiple_client(MDM_UP, 2, fds);
    }

    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    send_simple_msg(cl1, CRM_REQ_ACQUIRE);
    wait_evt(50, 0, NULL);

    send_simple_msg(cl2, CRM_REQ_RELEASE);
    wait_evt(50, 0, NULL);

    LOGD("========== Test acquire while shutdown");
    send_simple_msg(cl1, CRM_REQ_RELEASE);
    {
        int fds[] = { cl1, cl1, cl2 };
        int evts[] = { MDM_SHUTDOWN, MDM_DOWN, MDM_DOWN };
        wait_multiple_client_plus(evts, 3, fds);
    }

    send_simple_msg(cl2, CRM_REQ_ACQUIRE);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACK_SHUTDOWN);

    wait_single(EVT_CTRL, CLA_REQ_STOP, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_OFF);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_single(EVT_CTRL, CLA_REQ_START, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl1, cl2 };
        wait_multiple_client(MDM_UP, 2, fds);
    }
    wait_evt(50, 0, NULL);

    LOGD("========== Test acquire / release while shutdown (bis)");
    send_simple_msg(cl2, CRM_REQ_RELEASE);
    {
        int fds[] = { cl1, cl1, cl2 };
        int evts[] = { MDM_SHUTDOWN, MDM_DOWN, MDM_DOWN };
        wait_multiple_client_plus(evts, 3, fds);
    }
    send_simple_msg(cl1, CRM_REQ_ACQUIRE);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACK_SHUTDOWN);
    wait_single(EVT_CTRL, CLA_REQ_STOP, 0);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_RELEASE);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_OFF);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl2, CRM_REQ_ACQUIRE);
    wait_single(EVT_CTRL, CLA_REQ_START, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl1, cl2 };
        wait_multiple_client(MDM_UP, 2, fds);
    }
    wait_evt(50, 0, NULL);

    LOGD("========== Test restart and check that second is ignored");
    dbg_data[1] = "FooBarBar";
    send_restart(cl1, CTRL_MODEM_RESTART, &dbg_info);
    {
        int fds[] = { cl1, cl1, cl2 };
        int evts[] = { MDM_DOWN, MDM_COLD_RESET, MDM_DOWN };
        wait_multiple_client_plus(evts, 3, fds);
    }

    dbg_info.nb_data = 1;
    send_restart(cl2, CTRL_MODEM_UPDATE, &dbg_info);
    wait_evt(50, 0, NULL);

    dbg_info.nb_data = 2;
    send_simple_msg(cl1, CRM_REQ_ACK_COLD_RESET);
    {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = CLA_REQ_RESTART,
                        .info = { .type = CTRL_MODEM_RESTART,
                                  .dbg_info_present = true,
                                  .dbg_info = dbg_info,
                                  .dbg_info_strings = { "Test", "FooBarBar" } } } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl1, cl2 };
        wait_multiple_client(MDM_UP, 2, fds);
    }
    wait_evt(50, 0, NULL);

    LOGD("========== Test restart without debug info and ack done by client disconnect");
    send_restart(cl2, CTRL_MODEM_UPDATE, NULL);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    {
        int fds[] = { cl1, cl1, cl2 };
        int evts[] = { MDM_DOWN, MDM_COLD_RESET, MDM_DOWN };
        wait_multiple_client_plus(evts, 3, fds);
    }
    wait_evt(50, 0, NULL);
    close(cl1);
    del_fd(cl1);
    {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = CLA_REQ_RESTART,
                        .info = { .type = CTRL_MODEM_UPDATE,
                                  .dbg_info_present = false } } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    wait_single(EVT_CLIENT, MDM_UP, cl2);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    LOGD("========== Test restart, ack not received due to disconnect of last acquired client");
    cl1 = connect_to_server(wire->get_socket_name(wire));
    add_fd(cl1);
    send_register(cl1, (1 << MDM_NUM_EVENTS) - 1, "Client1");
    wait_single(EVT_CLIENT, MDM_UP, cl1);

    int cl3 = connect_to_server(wire->get_socket_name(wire));
    add_fd(cl3);
    send_register(cl3, (1 << MDM_SHUTDOWN) | (1 << MDM_UP), "Client3");
    wait_single(EVT_CLIENT, MDM_UP, cl3);

    send_simple_msg(cl1, CRM_REQ_ACQUIRE);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl2, CRM_REQ_RELEASE);
    wait_evt(50, 0, NULL);
    send_restart(cl1, CTRL_MODEM_RESTART, NULL);
    {
        int fds[] = { cl1, cl1, cl2 };
        int evts[] = { MDM_DOWN, MDM_COLD_RESET, MDM_DOWN };
        wait_multiple_client_plus(evts, 3, fds);
    }
    wait_evt(50, 0, NULL);
    close(cl1);
    del_fd(cl1);
    wait_single(EVT_CLIENT, MDM_SHUTDOWN, cl3);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl3, CRM_REQ_ACK_SHUTDOWN);

    wait_single(EVT_CTRL, CLA_REQ_STOP, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_OFF);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    LOGD("========== Test restart while shutdown");
    send_simple_msg(cl2, CRM_REQ_ACQUIRE);
    wait_single(EVT_CTRL, CLA_REQ_START, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl2, cl3 };
        wait_multiple_client(MDM_UP, 2, fds);
    }
    wait_evt(50, 0, NULL);
    send_simple_msg(cl2, CRM_REQ_RELEASE);
    {
        int fds[] = { cl2, cl3 };
        int evts[] = { MDM_DOWN, MDM_SHUTDOWN };
        wait_multiple_client_plus(evts, 2, fds);
    }

    send_restart(cl2, CTRL_MODEM_RESTART, NULL);
    send_simple_msg(cl3, CRM_REQ_ACK_SHUTDOWN);
    wait_single(EVT_CTRL, CLA_REQ_STOP, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_OFF);
    wait_evt(50, 0, NULL);

    LOGD("========== Test restart after shutdown");
    send_restart(cl2, CTRL_MODEM_RESTART, NULL);
    wait_evt(50, 0, NULL);

    LOGD("========== Test restart while modem is starting");
    send_simple_msg(cl2, CRM_REQ_ACQUIRE);
    wait_single(EVT_CTRL, CLA_REQ_START, 0);
    wait_evt(50, 0, NULL);

    send_restart(cl3, CTRL_MODEM_RESTART, NULL);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl2, cl3 };
        wait_multiple_client(MDM_UP, 2, fds);
    }
    wait_evt(50, 0, NULL);

    LOGD("========== Test restart while modem is starting but after modem up");
    send_simple_msg(cl2, CRM_REQ_RELEASE);
    {
        int fds[] = { cl2, cl3 };
        int evts[] = { MDM_DOWN, MDM_SHUTDOWN };
        wait_multiple_client_plus(evts, 2, fds);
    }
    send_simple_msg(cl3, CRM_REQ_ACK_SHUTDOWN);
    wait_single(EVT_CTRL, CLA_REQ_STOP, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_OFF);
    wait_evt(50, 0, NULL);

    send_simple_msg(cl2, CRM_REQ_ACQUIRE);
    wait_single(EVT_CTRL, CLA_REQ_START, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl2, cl3 };
        wait_multiple_client(MDM_UP, 2, fds);
    }
    wait_evt(50, 0, NULL);
    send_restart(cl3, CTRL_MODEM_RESTART, NULL);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = CLA_REQ_RESTART,
                        .info = { .type = CTRL_MODEM_RESTART,
                                  .dbg_info_present = false } } },
            { .type = EVT_CLIENT,
              .client = { .fd = cl2, .expect_err = false, .id = MDM_DOWN } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl2, cl3 };
        wait_multiple_client(MDM_UP, 2, fds);
    }
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    LOGD("========== Test modem error in stable state without COLD_ACK");
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = CLA_REQ_RESTART,
                        .info = { .type = CTRL_MODEM_RESTART,
                                  .dbg_info_present = false } } },
            { .type = EVT_CLIENT,
              .client = { .fd = cl2, .expect_err = false, .id = MDM_DOWN } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }
    wait_evt(50, 0, NULL);

    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl2, cl3 };
        wait_multiple_client(MDM_UP, 2, fds);
    }
    wait_evt(50, 0, NULL);

    LOGD("========== Test modem error in stable state with COLD_ACK");
    cl1 = connect_to_server(wire->get_socket_name(wire));
    add_fd(cl1);
    send_register(cl1, (1 << MDM_NUM_EVENTS) - 1, "Client1");
    wait_single(EVT_CLIENT, MDM_UP, cl1);

    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    {
        int fds[] = { cl1, cl1, cl2 };
        int evts[] = { MDM_DOWN, MDM_COLD_RESET, MDM_DOWN };
        wait_multiple_client_plus(evts, 3, fds);
    }
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACK_COLD_RESET);
    {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = CLA_REQ_RESTART,
                        .info = { .type = CTRL_MODEM_RESTART,
                                  .dbg_info_present = false } } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl1, cl2, cl3 };
        wait_multiple_client(MDM_UP, 3, fds);
    }
    wait_evt(50, 0, NULL);


    LOGD("========== Test modem errors while resetting");
    send_restart(cl3, CTRL_MODEM_RESTART, NULL);
    {
        int fds[] = { cl1, cl1, cl2 };
        int evts[] = { MDM_DOWN, MDM_COLD_RESET, MDM_DOWN };
        wait_multiple_client_plus(evts, 3, fds);
    }
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACK_COLD_RESET);
    {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = CLA_REQ_RESTART,
                        .info = { .type = CTRL_MODEM_RESTART,
                                  .dbg_info_present = false } } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl1, cl2, cl3 };
        wait_multiple_client(MDM_UP, 3, fds);
    }
    wait_evt(50, 0, NULL);

    LOGD("========== Test modem errors while shutdown");
    send_simple_msg(cl2, CRM_REQ_RELEASE);
    {
        int fds[] = { cl1, cl1, cl2, cl3 };
        int evts[] = { MDM_SHUTDOWN, MDM_DOWN, MDM_DOWN, MDM_SHUTDOWN };
        wait_multiple_client_plus(evts, 4, fds);
    }
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACK_SHUTDOWN);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl3, CRM_REQ_ACK_SHUTDOWN);
    wait_single(EVT_CTRL, CLA_REQ_STOP, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_OFF);
    wait_evt(50, 0, NULL);

    send_simple_msg(cl1, CRM_REQ_ACQUIRE);
    wait_single(EVT_CTRL, CLA_REQ_START, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl1, cl2, cl3 };
        wait_multiple_client(MDM_UP, 3, fds);
    }
    wait_evt(50, 0, NULL);

    LOGD("========== Test modem errors while 'aborted' shutdown");
    send_simple_msg(cl1, CRM_REQ_RELEASE);
    {
        int fds[] = { cl1, cl1, cl2, cl3 };
        int evts[] = { MDM_SHUTDOWN, MDM_DOWN, MDM_DOWN, MDM_SHUTDOWN };
        wait_multiple_client_plus(evts, 4, fds);
    }
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    send_simple_msg(cl2, CRM_REQ_ACQUIRE);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACK_SHUTDOWN);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl3, CRM_REQ_ACK_SHUTDOWN);
    {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = CLA_REQ_RESTART,
                        .info = { .type = CTRL_MODEM_RESTART,
                                  .dbg_info_present = false } } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl1, cl2, cl3 };
        wait_multiple_client(MDM_UP, 3, fds);
    }
    wait_evt(50, 0, NULL);

    LOGD("========== Test release / acquire while modem restart");
    send_restart(cl3, CTRL_MODEM_RESTART, NULL);
    {
        int fds[] = { cl1, cl1, cl2 };
        int evts[] = { MDM_DOWN, MDM_COLD_RESET, MDM_DOWN };
        wait_multiple_client_plus(evts, 3, fds);
    }
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACK_COLD_RESET);
    {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = CLA_REQ_RESTART,
                        .info = { .type = CTRL_MODEM_RESTART,
                                  .dbg_info_present = false } } }
        };
        wait_evt(1000, ARRAY_SIZE(w), w);
    }
    wait_evt(50, 0, NULL);
    send_simple_msg(cl2, CRM_REQ_RELEASE);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl2, CRM_REQ_ACQUIRE);
    wait_evt(50, 0, NULL);

    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl1, cl2, cl3 };
        wait_multiple_client(MDM_UP, 3, fds);
    }
    wait_evt(50, 0, NULL);

    LOGD("========== Test COLD ACK time-outs ...");
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    {
        int fds[] = { cl1, cl1, cl2 };
        int evts[] = { MDM_DOWN, MDM_COLD_RESET, MDM_DOWN };
        wait_multiple_client_plus(evts, 3, fds);
    }
    {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = CLA_REQ_RESTART,
                        .info = { .type = CTRL_MODEM_RESTART,
                                  .dbg_info_present = false } } }
        };
        wait_evt(1500, ARRAY_SIZE(w), w);
    }
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACK_COLD_RESET);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl1, cl2, cl3 };
        wait_multiple_client(MDM_UP, 3, fds);
    }
    wait_evt(50, 0, NULL);

    LOGD("========== Test SHUTDOWN ACK time-outs ...");
    send_simple_msg(cl1, CRM_REQ_RELEASE);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl2, CRM_REQ_RELEASE);
    {
        int fds[] = { cl1, cl1, cl2, cl3 };
        int evts[] = { MDM_SHUTDOWN, MDM_DOWN, MDM_DOWN, MDM_SHUTDOWN };
        wait_multiple_client_plus(evts, 4, fds);
    }
    {
        evt_wait_t w[] = {
            { .type = EVT_CTRL,
              .ctrl = { .req = CLA_REQ_STOP } }
        };
        wait_evt(1500, ARRAY_SIZE(w), w);
    }
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACK_SHUTDOWN);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACK_SHUTDOWN);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_OFF);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);

    LOGD("========== Test NVM backup ...");
    send_simple_msg(cl1, CRM_REQ_ACQUIRE);
    wait_single(EVT_CTRL, CLA_REQ_START, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_operation_result(client_abs, 0);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    {
        int fds[] = { cl1, cl2, cl3 };
        wait_multiple_client(MDM_UP, 3, fds);
    }
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_NVM_BACKUP);
    {
        int fds[] = { cl1, cl1, cl2 };
        int evts[] = { MDM_DOWN, MDM_COLD_RESET, MDM_DOWN };
        wait_multiple_client_plus(evts, 3, fds);
    }
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACK_COLD_RESET);
    wait_single(EVT_CTRL, CLA_REQ_BACKUP, 0);
    wait_evt(50, 0, NULL);

    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    wait_evt(50, 0, NULL);
    client_abs->notify_modem_state(client_abs, MDM_STATE_READY);
    client_abs->notify_operation_result(client_abs, 0);
    {
        int fds[] = { cl1, cl2, cl3 };
        wait_multiple_client(MDM_UP, 3, fds);
    }
    wait_evt(50, 0, NULL);

    LOGD("========== Test OOS ...");
    client_abs->notify_modem_state(client_abs, MDM_STATE_BUSY);
    client_abs->notify_modem_state(client_abs, MDM_STATE_UNRESP);
    {
        int fds[] = { cl1, cl1, cl2 };
        int evts[] = { MDM_DOWN, MDM_COLD_RESET, MDM_DOWN };
        wait_multiple_client_plus(evts, 3, fds);
    }
    send_simple_msg(cl1, CRM_REQ_ACK_COLD_RESET);
    {
        int fds[] = { cl1, cl2 };
        wait_multiple_client(MDM_OOS, 2, fds);
    }

    send_simple_msg(cl2, CRM_REQ_RELEASE);
    wait_evt(50, 0, NULL);
    send_simple_msg(cl1, CRM_REQ_ACQUIRE);
    wait_evt(50, 0, NULL);
    send_restart(cl1, CTRL_MODEM_RESTART, NULL);
    wait_evt(50, 0, NULL);

    client_abs->dispose(client_abs);

    ipc->dispose(ipc, NULL);
    wire->dispose(wire);
    wakelock->dispose(wakelock);
    LOGD("========== Test successful !!!");

    return 0;
}
