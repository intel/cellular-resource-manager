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
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdbool.h>
#include <poll.h>
#include <unistd.h>

#include "libmdmcli/mdm_cli.h"

#define CRM_MODULE_TAG "CLFA"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/ipc.h"
#include "utils/thread.h"
#include "test/client_factory.h"

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

typedef struct crm_client_factory_internal_ctx {
    crm_client_factory_t ctx; // Needs to be first

    int instance_id;
    void *context;
    crm_client_stub_callback_t callback;

    // To be used later
    crm_thread_ctx_t *thread_ctx;
    crm_ipc_ctx_t *ipc_ctx;
} crm_client_factory_internal_ctx_t;

typedef struct crm_client_stub_internal_ctx {
    crm_client_stub_t ctx; // Needs to be first

    char *name;
    int event_bitmap;
    mdm_cli_hdle_t *handle;
    crm_client_factory_internal_ctx_t *factory_ctx;
} crm_client_stub_internal_ctx_t;

static int client_callback(const mdm_cli_callback_data_t *cb_data)
{
    ASSERT(cb_data != NULL);
    crm_client_stub_internal_ctx_t *i_ctx = (crm_client_stub_internal_ctx_t *)cb_data->context;
    ASSERT(i_ctx != NULL);
    crm_client_factory_internal_ctx_t *i_factory_ctx = i_ctx->factory_ctx;
    ASSERT(i_factory_ctx != NULL);


    if (i_factory_ctx->callback) {
        mdm_cli_callback_data_t cb_data_bis = *cb_data;
        cb_data_bis.context = i_factory_ctx->context;
        return i_factory_ctx->callback(&i_ctx->ctx, &cb_data_bis);
    } else {
        return 0;
    }
}

/**
 * @see client_factory.h
 */
static int client_connect(crm_client_stub_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_client_stub_internal_ctx_t *i_ctx = (crm_client_stub_internal_ctx_t *)ctx;

    mdm_cli_register_t callbacks[MDM_NUM_EVENTS];
    int num_evts = 0;
    for (int i = 0; i < MDM_NUM_EVENTS; i++) {
        if ((1u << i) & i_ctx->event_bitmap) {
            callbacks[num_evts].id = i;
            callbacks[num_evts].callback = client_callback;
            callbacks[num_evts].context = i_ctx;
            num_evts += 1;
        }
    }

    i_ctx->handle = mdm_cli_connect(i_ctx->name, i_ctx->factory_ctx->instance_id,
                                    num_evts, callbacks);
    if (i_ctx->handle != NULL)
        return 0;
    else
        return -1;
}

/**
 * @see client_factory.h
 */
static int client_disconnect(crm_client_stub_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_client_stub_internal_ctx_t *i_ctx = (crm_client_stub_internal_ctx_t *)ctx;

    int ret = 0;
    if (i_ctx->handle)
        ret = mdm_cli_disconnect(i_ctx->handle);

    free(i_ctx->name);
    free(i_ctx);

    return ret;
}

/**
 * @see client_factory.h
 */
static int client_acquire(crm_client_stub_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_client_stub_internal_ctx_t *i_ctx = (crm_client_stub_internal_ctx_t *)ctx;
    return mdm_cli_acquire(i_ctx->handle);
}

/**
 * @see client_factory.h
 */
static int client_release(crm_client_stub_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_client_stub_internal_ctx_t *i_ctx = (crm_client_stub_internal_ctx_t *)ctx;
    return mdm_cli_release(i_ctx->handle);
}

/**
 * @see client_factory.h
 */
static int client_restart(crm_client_stub_t *ctx, mdm_cli_restart_cause_t cause,
                          const mdm_cli_dbg_info_t *data)
{
    ASSERT(ctx != NULL);
    crm_client_stub_internal_ctx_t *i_ctx = (crm_client_stub_internal_ctx_t *)ctx;
    return mdm_cli_restart(i_ctx->handle, cause, data);
}

/**
 * @see client_factory.h
 */
static int client_shutdown(crm_client_stub_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_client_stub_internal_ctx_t *i_ctx = (crm_client_stub_internal_ctx_t *)ctx;
    return mdm_cli_shutdown(i_ctx->handle);
}

/**
 * @see client_factory.h
 */
static int client_nvm_bckup(crm_client_stub_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_client_stub_internal_ctx_t *i_ctx = (crm_client_stub_internal_ctx_t *)ctx;
    return mdm_cli_nvm_bckup(i_ctx->handle);
}

/**
 * @see client_factory.h
 */
static int client_ack_cold_reset(crm_client_stub_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_client_stub_internal_ctx_t *i_ctx = (crm_client_stub_internal_ctx_t *)ctx;
    return mdm_cli_ack_cold_reset(i_ctx->handle);
}

/**
 * @see client_factory.h
 */
static int client_ack_shutdown(crm_client_stub_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_client_stub_internal_ctx_t *i_ctx = (crm_client_stub_internal_ctx_t *)ctx;
    return mdm_cli_ack_shutdown(i_ctx->handle);
}

/**
 * @see client_factory.h
 */
static int client_notify_dbg(crm_client_stub_t *ctx, const mdm_cli_dbg_info_t *data)
{
    ASSERT(ctx != NULL);
    crm_client_stub_internal_ctx_t *i_ctx = (crm_client_stub_internal_ctx_t *)ctx;
    return mdm_cli_notify_dbg(i_ctx->handle, data);
}

/**
 * @see client_factory.h
 */
static void client_kill(crm_client_stub_t *ctx)
{
    // As long as the factory is thread based, kill and disconnect are the same
    client_disconnect(ctx);
}

/**
 * @see client_factory.h
 */
static void dispose(crm_client_factory_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_client_factory_internal_ctx_t *i_ctx = (crm_client_factory_internal_ctx_t *)ctx;
    i_ctx->ipc_ctx->dispose(i_ctx->ipc_ctx, NULL);
    i_ctx->thread_ctx->dispose(i_ctx->thread_ctx, NULL);
    free(i_ctx);
}

/**
 * @see client_factory.h
 */
static crm_client_stub_t *add_client(crm_client_factory_t *ctx, const char *name, int event_bitmap)
{
    ASSERT(ctx != NULL);
    crm_client_factory_internal_ctx_t *i_ctx = (crm_client_factory_internal_ctx_t *)ctx;
    (void)i_ctx;

    crm_client_stub_internal_ctx_t *client_i_ctx = calloc(1, sizeof(*client_i_ctx));
    ASSERT(client_i_ctx);

    char name_buf[256];
    const char *c_name = name;
    if (c_name == NULL) {
        c_name = name_buf;
        snprintf(name_buf, sizeof(name_buf), "CLIENT_%p", client_i_ctx);
    }

    client_i_ctx->name = strdup(c_name);
    ASSERT(client_i_ctx->name);
    client_i_ctx->event_bitmap = event_bitmap;
    client_i_ctx->factory_ctx = i_ctx;

    client_i_ctx->ctx.kill = client_kill;
    client_i_ctx->ctx.connect = client_connect;
    client_i_ctx->ctx.disconnect = client_disconnect;
    client_i_ctx->ctx.acquire = client_acquire;
    client_i_ctx->ctx.release = client_release;
    client_i_ctx->ctx.restart = client_restart;
    client_i_ctx->ctx.shutdown = client_shutdown;
    client_i_ctx->ctx.nvm_bckup = client_nvm_bckup;
    client_i_ctx->ctx.ack_cold_reset = client_ack_cold_reset;
    client_i_ctx->ctx.ack_shutdown = client_ack_shutdown;
    client_i_ctx->ctx.notify_dbg = client_notify_dbg;

    return &client_i_ctx->ctx;
}

static void *factory_thread(crm_thread_ctx_t *dummy, void *ctx)
{
    (void)dummy;

    ASSERT(ctx);
    crm_client_factory_internal_ctx_t *i_ctx = ctx;

    struct pollfd pfd = { .fd = i_ctx->ipc_ctx->get_poll_fd(i_ctx->ipc_ctx), .events = POLLIN };

    // Placeholder for now :)
    while (1) {
        int ret = poll(&pfd, 1, -1);
        if (ret < 0) break;
    }

    return NULL;
}

/**
 * @see client_factory.h
 */
crm_client_factory_t *crm_client_factory_init(int instance_id,
                                              crm_client_stub_callback_t callback, void *context)
{
    crm_client_factory_internal_ctx_t *i_ctx = calloc(1, sizeof(*i_ctx));

    ASSERT(i_ctx != NULL);

    i_ctx->instance_id = instance_id;
    i_ctx->context = context;
    i_ctx->callback = callback;

    i_ctx->ipc_ctx = crm_ipc_init(CRM_IPC_THREAD);
    i_ctx->thread_ctx = crm_thread_init(factory_thread, i_ctx, false, false);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.add_client = add_client;

    return &i_ctx->ctx;
}
