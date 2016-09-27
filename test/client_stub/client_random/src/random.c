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
#include <time.h>

#include "libmdmcli/mdm_cli.h"

#define CRM_MODULE_TAG "CLIENT"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/thread.h"
#include "test/client_factory.h"

#define DELAY 250000

typedef struct test_context {
    char name[64];
    crm_ipc_ctx_t *ipc;
} test_context_t;

int callback(crm_client_stub_t *client, const mdm_cli_callback_data_t *callback_data)
{
    (void)client;
    ASSERT(callback_data);
    test_context_t *ctx = callback_data->context;
    ASSERT(ctx);

    crm_ipc_msg_t msg = { .scalar = callback_data->id };
    int ret = 0;

    switch (callback_data->id) {
    case MDM_SHUTDOWN:
    case MDM_COLD_RESET:
        if (rand() & 0x01) {
            ret = -1;
            ASSERT(ctx->ipc->send_msg(ctx->ipc, &msg));
        }
        break;
    case MDM_DOWN:
    case MDM_UP:
    case MDM_OOS:
        ASSERT(ctx->ipc->send_msg(ctx->ipc, &msg));
        break;

    case MDM_DBG_INFO:
        break;

    default:
        ASSERT(0);
        break;
    }

    return ret;
}

int main(int argc, char *argv[])
{
    int instance_id = MDM_CLI_DEFAULT_INSTANCE;
    test_context_t ctx;
    bool is_full = true;

    srand(getpid() + time(NULL));

    if (argc > 1)
        instance_id = atoi(argv[1]);
    if (argc > 2)
        is_full = false;

    LOGD("Starting stress test on instance %d", instance_id);

    crm_client_factory_t *factory_ctx = crm_client_factory_init(instance_id, callback, &ctx);
    ASSERT(factory_ctx != NULL);
    ctx.ipc = crm_ipc_init(CRM_IPC_THREAD);

    int event_bitmap;
    if (is_full) {
        event_bitmap = (1u << MDM_DOWN) | (1u << MDM_UP) | (1u << MDM_OOS);
        if (rand() & 1)
            event_bitmap |= (1u << MDM_COLD_RESET);
        if (rand() & 1)
            event_bitmap |= (1u << MDM_SHUTDOWN);
    } else {
        event_bitmap = (1u << MDM_DOWN) | (1u << MDM_UP) | (1u << MDM_DBG_INFO);
    }
    snprintf(ctx.name, sizeof(ctx.name), "clt_%05d_%s%s%s", getpid(),
             is_full ? "fl" : "st",
             event_bitmap & (1u << MDM_COLD_RESET) ? "c" : "",
             event_bitmap & (1u << MDM_SHUTDOWN) ? "s" : "");

    crm_client_stub_t *client_ctx = NULL;
    bool connected = false;
    bool acquired = false;
    bool to_ack_cold = false;
    bool to_ack_shut = false;
    bool modem_up = false;
    while (1) {
        if (client_ctx == NULL)
            client_ctx = factory_ctx->add_client(factory_ctx, ctx.name, event_bitmap);
        ASSERT(client_ctx != NULL);

        if (connected) {
            if ((rand() % 128) == 0) {
                client_ctx->disconnect(client_ctx);
                client_ctx = NULL;
                connected = false;
                acquired = false;
                to_ack_shut = false;
                to_ack_cold = false;
            } else {
                int to = rand() % DELAY;
                if (modem_up)
                    to += DELAY;
                to /= 1000;
                struct pollfd pfd = { .fd = ctx.ipc->get_poll_fd(ctx.ipc), .events = POLLIN };
                int ret = poll(&pfd, 1, to);
                ASSERT(ret >= 0);
                if (ret == 1) {
                    ASSERT(pfd.revents & POLLIN);
                    crm_ipc_msg_t msg;
                    while (ctx.ipc->get_msg(ctx.ipc, &msg)) {
                        switch (msg.scalar) {
                        case MDM_DOWN: modem_up = false; break;
                        case MDM_UP: modem_up = true; break;
                        case MDM_OOS: LOGD("modem out of service, exiting client !"); exit(0);
                            break;
                        case MDM_SHUTDOWN: to_ack_shut = (rand() % 4) == 0; break;
                        case MDM_COLD_RESET: to_ack_cold = (rand() % 4) == 0; break;
                        }
                    }
                } else {
                    if (to_ack_shut && ((rand() % 4) == 0)) {
                        to_ack_shut = false;
                        client_ctx->ack_shutdown(client_ctx);
                    }
                    if (to_ack_cold && ((rand() % 4) == 0)) {
                        to_ack_cold = false;
                        client_ctx->ack_cold_reset(client_ctx);
                    }
                    if (!acquired) {
                        if ((rand() % 8) == 0) {
                            client_ctx->acquire(client_ctx);
                            acquired = true;
                        }
                    } else {
                        if ((rand() % 8) == 0) {
                            client_ctx->release(client_ctx);
                            acquired = false;
                        }
                    }
                    if ((rand() % 16) == 0) {
                        const char *dbg_data[5];
                        int num_str = rand() % 5;
                        for (int i = 0; i < num_str; i++) {
                            dbg_data[i] = malloc(50);
                            snprintf((char *)dbg_data[i], 50, "Random garbage %08x", rand());
                        }

                        mdm_cli_dbg_info_t dbg_info =
                        { .type = DBG_TYPE_ERROR, .ap_logs_size = DBG_DEFAULT_NO_LOG,
                          .bp_logs_size = DBG_DEFAULT_LOG_SIZE,
                          .bp_logs_time = DBG_DEFAULT_LOG_TIME,
                          .nb_data = num_str, .data = dbg_data };
                        client_ctx->notify_dbg(client_ctx, &dbg_info);

                        for (int i = 0; i < num_str; i++)
                            free((char *)dbg_data[i]);
                    }
                }
            }
        } else {
            connected = client_ctx->connect(client_ctx) == 0;
        }
        if (!connected)
            usleep(DELAY + (rand() % DELAY));
    }

    return 0;
}
