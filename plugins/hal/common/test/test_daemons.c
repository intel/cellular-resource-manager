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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define CRM_MODULE_TAG "HALDT"
#include "utils/common.h"
#include "utils/keys.h"
#include "utils/logs.h"
#include "utils/property.h"
#include "utils/socket.h"
#include "utils/thread.h"
#include "test/test_utils.h"

#include "daemons.h"

crm_ipc_ctx_t *ipc;

typedef struct {
    char *client;
    int msg_id;
    char *msg;
    int msg_len;
    int id;
} evt_msg;

enum {
    SERVICE_STARTED,
    SERVICE_STOPPED,
    SERVICE_MSG,
    CALLBACK_CONNECT,
    CALLBACK_DISCONNECT,
    CALLBACK_MSG
};

int test_callback(int id, void *ctx,
                  crm_hal_daemon_evt_t evt,
                  int msg_id, size_t msg_len)
{
    int scalar = -1;

    LOGD("%d: %d", id, evt);
    if (evt == HAL_DAEMON_CONNECTED)
        scalar = CALLBACK_CONNECT;
    else if (evt == HAL_DAEMON_DISCONNECTED)
        scalar = CALLBACK_DISCONNECT;
    if (scalar >= 0) {
        int *data = malloc(sizeof(int));
        *data = id;
        crm_ipc_msg_t msg = { .scalar = scalar,
                              .data_size = sizeof(int),
                              .data = data };
        ASSERT(ipc->send_msg(ipc, &msg));
    } else {
        ASSERT(evt == HAL_DAEMON_DATA_IN);
        evt_msg *emsg = malloc(sizeof(evt_msg));
        ASSERT(emsg != NULL);
        emsg->id = id;
        emsg->msg_id = msg_id;
        emsg->msg_len = msg_len;
        void *imsg = NULL;
        if (msg_len > 0) {
            imsg = malloc(msg_len);
            ASSERT(imsg != NULL);
            crm_hal_daemon_msg_read(ctx, id, imsg, msg_len);
        }
        emsg->msg = imsg;
        crm_ipc_msg_t msg = { .scalar = CALLBACK_MSG,
                              .data_size = sizeof(*emsg),
                              .data = emsg };
        ASSERT(ipc->send_msg(ipc, &msg));
    }
    return 0;
}

static void *daemon_test(crm_thread_ctx_t *thread_ctx, void *arg)
{
    ASSERT(thread_ctx);
    ASSERT(arg);

    bool *stopping = (bool *)arg;
    bool running = true;
    int property_fifo_fd = open(CRM_PROPERTY_PIPE_NAME, O_RDONLY);
    ASSERT(property_fifo_fd >= 0);

    struct {
        int fd;
        char *name;
    } daemons[CRM_HAL_MAX_DAEMONS];
    for (int i = 0; i < CRM_HAL_MAX_DAEMONS; i++) {
        daemons[i].fd = -1;
        daemons[i].name = NULL;
    }

    while (running) {
        struct pollfd pfd[3 + CRM_HAL_MAX_DAEMONS] = {
            { .fd = property_fifo_fd, .events = POLLIN },
            { .fd = thread_ctx->get_poll_fd(thread_ctx), .events = POLLIN }
        };
        int num_fd = 2;
        for (int i = 0; i < CRM_HAL_MAX_DAEMONS; i++) {
            if (daemons[i].fd >= 0) {
                pfd[num_fd].fd = daemons[i].fd;
                pfd[num_fd].events = POLLIN;
                num_fd += 1;
            }
        }
        poll(pfd, num_fd, -1);
        if (pfd[0].revents & POLLIN) {
            char buf[CRM_PROPERTY_VALUE_MAX + CRM_PROPERTY_KEY_MAX + 2];
            ssize_t ret = read(pfd[0].fd, buf, sizeof(buf) - 1);
            ASSERT(ret >= 0);
            buf[ret] = '\0';
            LOGD("prop read: %s", buf);
            char *service = strchr(buf, '=');
            ASSERT(service);
            *service = '\0';
            service += 1;
            if (!strcmp(buf, "ctl.stop")) {
                crm_ipc_msg_t msg = { .scalar = SERVICE_STOPPED,
                                      .data_size = strlen(service) + 1,
                                      .data = strdup(service) };
                ASSERT(ipc->send_msg(ipc, &msg));
                for (int i = 0; i < CRM_HAL_MAX_DAEMONS; i++) {
                    if (daemons[i].name && !strcmp(daemons[i].name, service)) {
                        close(daemons[i].fd);
                        daemons[i].fd = -1;
                        free(daemons[i].name);
                        daemons[i].name = NULL;
                        break;
                    }
                }
            } else if (!strcmp(buf, "ctl.start")) {
                crm_ipc_msg_t msg = { .scalar = SERVICE_STARTED,
                                      .data_size = strlen(service) + 1,
                                      .data = strdup(service) };
                ASSERT(ipc->send_msg(ipc, &msg));

                int i;
                for (i = 0; i < CRM_HAL_MAX_DAEMONS; i++)
                    if (daemons[i].fd == -1)
                        break;
                ASSERT(i < CRM_HAL_MAX_DAEMONS);
                ASSERT(daemons[i].name == NULL);
                daemons[i].name = strdup(service);
                daemons[i].fd = crm_socket_connect("crmctrl0");
                ASSERT(daemons[i].fd >= 0);
                int smsg[1 + (CRM_PROPERTY_KEY_MAX + 3) / 4];
                smsg[0] = strlen(service);
                memcpy(&smsg[1], service, strlen(service));
                ASSERT(crm_socket_write(daemons[i].fd, 1000, smsg, sizeof(smsg[0]) + strlen(
                                            service)) == 0);
            } else {
                DASSERT(0, "Bad property: '%s'", buf);
            }
        } else if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (!*stopping)
                ASSERT(0);
        }

        if (pfd[1].revents & POLLIN) {
            crm_ipc_msg_t msg;
            while (thread_ctx->get_msg(thread_ctx, &msg)) {
                char *name;
                if (msg.scalar == 0)
                    name = msg.data;
                else
                    name = ((evt_msg *)msg.data)->client;

                int i;
                for (i = 0; i < CRM_HAL_MAX_DAEMONS; i++)
                    if (!strcmp(daemons[i].name, name))
                        break;

                ASSERT(i < CRM_HAL_MAX_DAEMONS);

                if (msg.scalar == 0) {
                    close(daemons[i].fd);
                    daemons[i].fd = -1;
                    free(daemons[i].name);
                    daemons[i].name = NULL;
                } else {
                    evt_msg *emsg = (evt_msg *)msg.data;
                    int msg_hdr[2] = { emsg->msg_id, emsg->msg_len };
                    ASSERT(crm_socket_write(daemons[i].fd, 1000, msg_hdr, sizeof(msg_hdr)) == 0);
                    if (emsg->msg_len > 0)
                        ASSERT(crm_socket_write(daemons[i].fd, 1000, emsg->msg,
                                                emsg->msg_len) == 0);
                }
            }
        } else if (pfd[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }

        for (int i = 2; i < num_fd; i++) {
            if (pfd[i].revents == POLLIN) {
                int msg_hdr[2];
                char *msg = NULL;
                ASSERT(crm_socket_read(pfd[i].fd, 1000, msg_hdr, sizeof(msg_hdr)) == 0);
                if (msg_hdr[1] > 0) {
                    msg = malloc(msg_hdr[1]);
                    ASSERT(msg);
                    ASSERT(crm_socket_read(pfd[i].fd, 1000, msg, msg_hdr[1]) == 0);
                }
                evt_msg *imsg = malloc(sizeof(evt_msg));
                ASSERT(imsg);
                for (int j = 0; j < CRM_HAL_MAX_DAEMONS; j++)
                    if (daemons[j].fd == pfd[i].fd) {
                        imsg->client = strdup(daemons[j].name);
                        break;
                    }
                imsg->msg_id = msg_hdr[0];
                imsg->msg = msg;
                imsg->msg_len = msg_hdr[1];
                crm_ipc_msg_t smsg = { .scalar = SERVICE_MSG,
                                       .data_size = sizeof(*imsg),
                                       .data = imsg };
                ASSERT(ipc->send_msg(ipc, &smsg));
            }
        }
    }
    for (int i = 0; i < CRM_HAL_MAX_DAEMONS; i++)
        free(daemons[i].name);

    return NULL;
}

int num_evts = 0;
struct {
    int id;
    void *data;
} evts[4];
void add_evt(int id, void *data)
{
    evts[num_evts].id = id;
    evts[num_evts].data = data;
    num_evts += 1;
}

bool wait_evts(crm_hal_daemon_ctx_t *ctx)
{
    while (1) {
        struct pollfd pfd[1 + 1 +
                          CRM_HAL_MAX_DAEMONS] =
        { { .fd = ipc->get_poll_fd(ipc), .events = POLLIN } };
        int num_fd = 1 + crm_hal_daemon_get_sockets(ctx, &pfd[1]);
        int ret = poll(pfd, num_fd, 50);
        ASSERT(ret >= 0);
        if (ret == 0) {
            return num_evts == 0;
        } else {
            if (pfd[0].revents == POLLIN) {
                crm_ipc_msg_t msg;
                while (ipc->get_msg(ipc, &msg)) {
                    int i;
                    for (i = 0; i < num_evts; i++) {
                        if (msg.scalar == evts[i].id) {
                            if ((msg.scalar == SERVICE_STARTED) ||
                                (msg.scalar == SERVICE_STOPPED)) {
                                if (!strcmp(msg.data, evts[i].data)) {
                                    free(msg.data);
                                    break;
                                }
                            } else if ((msg.scalar == CALLBACK_CONNECT) ||
                                       (msg.scalar == CALLBACK_DISCONNECT)) {
                                if (*((int *)msg.data) == *((int *)evts[i].data)) {
                                    free(msg.data);
                                    break;
                                }
                            } else if (msg.scalar == CALLBACK_MSG) {
                                evt_msg *evt_q = (evt_msg *)evts[i].data;
                                evt_msg *evt_m = (evt_msg *)msg.data;
                                if ((evt_q->id == evt_m->id) &&
                                    (evt_q->msg_id == evt_m->msg_id) &&
                                    (evt_q->msg_len == evt_m->msg_len)) {
                                    bool match = false;
                                    if (evt_q->msg != NULL) {
                                        if (!memcmp(evt_q->msg, evt_m->msg, evt_q->msg_len))
                                            match = true;
                                    } else {
                                        match = evt_m->msg == NULL;
                                    }
                                    if (match) {
                                        free(evt_m->msg);
                                        free(evt_m);
                                        break;
                                    }
                                }
                            } else if (msg.scalar == SERVICE_MSG) {
                                evt_msg *evt_q = (evt_msg *)evts[i].data;
                                evt_msg *evt_m = (evt_msg *)msg.data;
                                if ((!strcmp(evt_q->client, evt_m->client)) &&
                                    (evt_q->msg_id == evt_m->msg_id) &&
                                    (evt_q->msg_len == evt_m->msg_len)) {
                                    bool match = false;
                                    if (evt_q->msg != NULL) {
                                        if (!memcmp(evt_q->msg, evt_m->msg, evt_q->msg_len))
                                            match = true;
                                    } else {
                                        match = evt_m->msg == NULL;
                                    }
                                    if (match) {
                                        free(evt_m->msg);
                                        free(evt_m->client);
                                        free(evt_m);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    DASSERT(i < num_evts, "Event: %lld", msg.scalar);
                    memmove(&evts[i], &evts[i + 1], (num_evts - i - 1) * sizeof(evts[0]));
                    num_evts -= 1;
                }
            } else if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                ASSERT(0);
            }

            for (int i = 1; i < num_fd; i++) {
                if (pfd[i].revents)
                    ASSERT(crm_hal_daemon_handle_poll(ctx, &pfd[i],
                                                      NULL) != HAL_DAEMON_POLL_NOT_HANDLED);
            }
        }
    }
}

int main()
{
    bool stopping = false;
    crm_hal_daemon_ctx_t ctx;

    unlink(CRM_PROPERTY_PIPE_NAME);
    ASSERT(mkfifo(CRM_PROPERTY_PIPE_NAME, 0666) == 0);

    ipc = crm_ipc_init(CRM_IPC_THREAD);
    crm_thread_ctx_t *tst = crm_thread_init(daemon_test, &stopping, true, false);

    crm_logs_init(0);
    crm_property_init(0);

    CRM_TEST_get_control_socket_android("crmctrl0");
    crm_hal_daemon_init(&ctx, "crmctrl0");

    /* Check addition / starting of daemons */
    LOGD("===== Adding service 'foo'");
    int id0;
    ASSERT((id0 = crm_hal_daemon_add(&ctx, "foo", test_callback, &ctx)) >= 0);
    add_evt(SERVICE_STOPPED, "foo");
    ASSERT(wait_evts(&ctx));

    LOGD("===== Starting service 'foo'");
    crm_hal_daemon_start(&ctx, id0);
    add_evt(SERVICE_STARTED, "foo");
    add_evt(CALLBACK_CONNECT, &id0);
    ASSERT(wait_evts(&ctx));

    LOGD("===== Stopping service 'foo'");
    crm_hal_daemon_stop(&ctx, id0);
    add_evt(SERVICE_STOPPED, "foo");
    ASSERT(wait_evts(&ctx));

    LOGD("===== Starting service 'foo'");
    crm_hal_daemon_start(&ctx, id0);
    add_evt(SERVICE_STARTED, "foo");
    add_evt(CALLBACK_CONNECT, &id0);
    ASSERT(wait_evts(&ctx));

    LOGD("===== Adding service 'bar'");
    int id1;
    ASSERT((id1 = crm_hal_daemon_add(&ctx, "bar", test_callback, &ctx)) >= 0);
    add_evt(SERVICE_STOPPED, "bar");
    ASSERT(wait_evts(&ctx));

    LOGD("===== Killing service 'foo'");
    /* Check daemon that is 'killed' (here simulated by closing the socket) */
    crm_ipc_msg_t msg = { .scalar = 0,
                          .data_size = strlen("foo"),
                          .data = "foo" };
    ASSERT(tst->send_msg(tst, &msg));
    add_evt(SERVICE_STOPPED, "foo");
    add_evt(CALLBACK_DISCONNECT, &id0);
    ASSERT(wait_evts(&ctx));

    LOGD("===== Restarting 'foo' and starting 'bar'");
    crm_hal_daemon_start(&ctx, id0);
    add_evt(SERVICE_STARTED, "foo");
    add_evt(CALLBACK_CONNECT, &id0);
    ASSERT(wait_evts(&ctx));
    crm_hal_daemon_start(&ctx, id1);
    add_evt(SERVICE_STARTED, "bar");
    add_evt(CALLBACK_CONNECT, &id1);
    ASSERT(wait_evts(&ctx));

    LOGD("===== Sending message to 'foo' and 'bar'");
    crm_hal_daemon_msg_send(&ctx, id0, 1234, NULL, 0);
    evt_msg emsg;
    emsg.client = "foo";
    emsg.msg_id = 1234;
    emsg.msg = NULL;
    emsg.msg_len = 0;
    add_evt(SERVICE_MSG, &emsg);
    ASSERT(wait_evts(&ctx));

    crm_hal_daemon_msg_send(&ctx, id1, 4321, "Test", strlen("Test"));
    emsg.client = "bar";
    emsg.msg_id = 4321;
    emsg.msg = "Test";
    emsg.msg_len = strlen(emsg.msg);
    add_evt(SERVICE_MSG, &emsg);
    ASSERT(wait_evts(&ctx));

    crm_hal_daemon_msg_send(&ctx, id0, 4321, "Test", strlen("Test"));
    emsg.client = "foo";
    emsg.msg_id = 4321;
    emsg.msg = "Test";
    emsg.msg_len = strlen(emsg.msg);
    add_evt(SERVICE_MSG, &emsg);
    ASSERT(wait_evts(&ctx));

    LOGD("===== Receiving message from 'foo' and 'bar'");
    emsg.client = "foo";
    emsg.msg_id = 4321234;
    emsg.msg = "TestTest";
    emsg.msg_len = strlen(emsg.msg);
    emsg.id = id0;

    msg.scalar = 1;
    msg.data_size = sizeof(emsg);
    msg.data = &emsg;
    ASSERT(tst->send_msg(tst, &msg));

    add_evt(CALLBACK_MSG, &emsg);
    ASSERT(wait_evts(&ctx));

    emsg.client = "bar";
    emsg.msg_id = 42;
    emsg.msg = "Tess ?";
    emsg.msg_len = strlen(emsg.msg);
    emsg.id = id1;
    msg.scalar = 1;
    msg.data_size = sizeof(emsg);
    msg.data = &emsg;
    ASSERT(tst->send_msg(tst, &msg));
    evt_msg emsg2;
    emsg2.client = "foo";
    emsg2.msg_id = 424;
    emsg2.msg = "? tseT";
    emsg2.msg_len = strlen(emsg2.msg);
    emsg2.id = id0;
    msg.scalar = 1;
    msg.data_size = sizeof(emsg2);
    msg.data = &emsg2;
    ASSERT(tst->send_msg(tst, &msg));

    add_evt(CALLBACK_MSG, &emsg);
    add_evt(CALLBACK_MSG, &emsg2);
    ASSERT(wait_evts(&ctx));

    tst->dispose(tst, NULL);
    ipc->dispose(ipc, NULL);
    LOGD("Test successful !");
}
