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

#include <pthread.h>
#include <poll.h>
#include <unistd.h>

#define CRM_MODULE_TAG "WAKE"
#include "utils/common.h"
#include "utils/thread.h"
#include "utils/wakelock.h"

#include "teljavabridge/tel_java_bridge.h"

#define MAX_MODULE 5

typedef struct crm_wakelock_internal_ctx {
    crm_wakelock_t ctx; // Must be first

    tel_java_bridge_ctx_t *bridge;
    crm_ipc_ctx_t *ipc;
    crm_thread_ctx_t *wakelock_thread;

    int count[MAX_MODULE];
    pthread_mutex_t lock;
} crm_fw_upload_internal_ctx_t;

/**
 * @see wakelock.h
 */
static bool is_held_by_module(crm_wakelock_t *ctx, int module_id)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    ASSERT(i_ctx);
    ASSERT(module_id >= 0 && module_id < (int)ARRAY_SIZE(i_ctx->count));

    ASSERT(pthread_mutex_lock(&i_ctx->lock) == 0);
    bool held = i_ctx->count[module_id] > 0;
    ASSERT(pthread_mutex_unlock(&i_ctx->lock) == 0);

    return held;
}

/**
 * @see wakelock.h
 */
static bool is_held(crm_wakelock_t *ctx)
{
    bool held = false;
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    ASSERT(i_ctx);

    ASSERT(pthread_mutex_lock(&i_ctx->lock) == 0);
    for (size_t i = 0; i < ARRAY_SIZE(i_ctx->count); i++) {
        if (i_ctx->count[i] > 0) {
            held = true;
            break;
        }
    }
    ASSERT(pthread_mutex_unlock(&i_ctx->lock) == 0);

    return held;
}

/**
 * @see wakelock.h
 */
static void acquire(crm_wakelock_t *ctx, int module_id)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    ASSERT(i_ctx);
    ASSERT(module_id >= 0 && module_id < (int)ARRAY_SIZE(i_ctx->count));

    ASSERT(pthread_mutex_lock(&i_ctx->lock) == 0);
    i_ctx->count[module_id]++;
    ASSERT(pthread_mutex_unlock(&i_ctx->lock) == 0);

    crm_ipc_msg_t msg = { .scalar = 1 };
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);
}

/**
 * @see wakelock.h
 */
static void release(crm_wakelock_t *ctx, int module_id)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    ASSERT(i_ctx);
    ASSERT(module_id >= 0 && module_id < (int)ARRAY_SIZE(i_ctx->count));

    ASSERT(pthread_mutex_lock(&i_ctx->lock) == 0);
    if (i_ctx->count[module_id] > 0)
        i_ctx->count[module_id]--;
    ASSERT(pthread_mutex_unlock(&i_ctx->lock) == 0);

    crm_ipc_msg_t msg = { .scalar = 1 };
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);
}

static void *wakelock_thread(crm_thread_ctx_t *thread_ctx, void *args)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)args;

    ASSERT(thread_ctx);
    ASSERT(i_ctx);

    int i_fd = i_ctx->ipc->get_poll_fd(i_ctx->ipc);
    int b_fd = -1;
    bool wakelock_acquired = false;
    bool to_update = false;

    while (true) {
        if (b_fd < 0) {
            wakelock_acquired = false;
            if (!i_ctx->bridge->connect(i_ctx->bridge)) {
                b_fd = i_ctx->bridge->get_poll_fd(i_ctx->bridge);
                to_update = true;
            }
        }

        if (to_update) {
            to_update = false;

            bool acquire = is_held((crm_wakelock_t *)i_ctx);
            if (acquire != wakelock_acquired) {
                if (!i_ctx->bridge->wakelock(i_ctx->bridge, acquire)) {
                    LOGV("[WAKELOCK] %s", acquire ? "acquired" : "released");
                    wakelock_acquired = acquire;
                } else {
                    i_ctx->bridge->disconnect(i_ctx->bridge);
                    b_fd = -1;
                }
            }
        }

        struct pollfd pfd[] = {
            { .fd = i_fd, .events = POLLIN },
            { .fd = b_fd, .events = POLLIN },
        };

        if (!poll(pfd, ARRAY_SIZE(pfd), (b_fd < 0) ? 500 : -1))
            continue;

        if (pfd[0].revents) {
            crm_ipc_msg_t msg;
            ASSERT(i_ctx->ipc->get_msg(i_ctx->ipc, &msg));
            if (msg.scalar != -1)
                to_update = true;
            else
                break;
        } else if (pfd[1].revents) {
            if (i_ctx->bridge->handle_poll_event(i_ctx->bridge, pfd[1].revents) == -1) {
                b_fd = -1;
                i_ctx->bridge->disconnect(i_ctx->bridge);
                if (is_held((crm_wakelock_t *)i_ctx))
                    LOGV("[WAKELOCK] released");
            }
        }
    }

    return NULL;
}

/**
 * @see wakelock.h
 */
static void dispose(crm_wakelock_t *ctx)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)ctx;

    ASSERT(i_ctx);

    crm_ipc_msg_t msg = { .scalar = -1 };
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);
    i_ctx->wakelock_thread->dispose(i_ctx->wakelock_thread, NULL);
    i_ctx->ipc->dispose(i_ctx->ipc, NULL);

    /* by disconnecting to the bridge, wakelock will be released. Let's print it here */
    if (is_held(ctx))
        LOGV("[WAKELOCK] released");

    i_ctx->bridge->dispose(i_ctx->bridge);
    free(i_ctx);
}

/**
 * @see wakelock.h
 */
crm_wakelock_t *crm_wakelock_init(const char *name)
{
    crm_fw_upload_internal_ctx_t *i_ctx = (crm_fw_upload_internal_ctx_t *)
                                          calloc(1, sizeof(crm_fw_upload_internal_ctx_t));

    ASSERT(i_ctx);
    (void)name;

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.acquire = acquire;
    i_ctx->ctx.release = release;
    i_ctx->ctx.is_held = is_held;
    i_ctx->ctx.is_held_by_module = is_held_by_module;

    ASSERT(pthread_mutex_init(&i_ctx->lock, NULL) == 0);

    i_ctx->bridge = tel_java_bridge_init();
    i_ctx->ipc = crm_ipc_init(CRM_IPC_THREAD);
    ASSERT(i_ctx->bridge);

    i_ctx->wakelock_thread = crm_thread_init(wakelock_thread, i_ctx, false, false);
    ASSERT(i_ctx->wakelock_thread);

    return &i_ctx->ctx;
}
