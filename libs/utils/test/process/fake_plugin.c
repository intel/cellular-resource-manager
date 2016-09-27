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
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

#define CRM_MODULE_TAG "FAKE"
#include "utils/common.h"
#include "utils/thread.h"

#include "fake_plugin.h"

typedef struct crm_fake_plugin_ctx_internal {
    crm_fake_plugin_ctx_t ctx; // Must be first

    crm_process_factory_ctx_t *factory;
    crm_thread_ctx_t *ctrl_thread;
    int idx;
    bool deadlock;
    void (*notify)(void);
} crm_fake_plugin_ctx_internal_t;

static void *proces_control(crm_thread_ctx_t *thread_ctx, void *args)
{
    crm_fake_plugin_ctx_internal_t *i_ctx = (crm_fake_plugin_ctx_internal_t *)args;

    ASSERT(i_ctx);
    ASSERT(thread_ctx);

    bool first_msg = true;
    struct pollfd pfd[] = {
        { .fd = i_ctx->factory->get_poll_fd(i_ctx->factory, i_ctx->idx), .events = POLLIN },
        { .fd = thread_ctx->get_poll_fd(thread_ctx), .events = POLLIN }
    };

    srand(getpid() | time(NULL));

    while (true) {
        int err = poll(pfd, ARRAY_SIZE(pfd), first_msg ? 20000 : 3000);
        if ((err == 0) && (!i_ctx->deadlock))
            break;
        else if ((err == -1) && (errno == EINTR))
            continue;
        first_msg = false;

        if (pfd[0].revents & POLLIN) {
            crm_ipc_msg_t msg;
            ASSERT(i_ctx->factory->get_msg(i_ctx->factory, i_ctx->idx, &msg));
            if (msg.scalar <= -1)
                break;
            msg.scalar++;
            usleep(rand() % 10 * 1000);
            ASSERT(i_ctx->factory->send_msg(i_ctx->factory, i_ctx->idx, &msg));
        } else if (pfd[1].revents & POLLIN) {
            break;
        } else {
            ASSERT(0);
        }
    }

    thread_ctx->dispose(thread_ctx, NULL);
    i_ctx->ctrl_thread = NULL;
    i_ctx->notify();
    i_ctx->factory->clean(i_ctx->factory, i_ctx->idx);
    i_ctx->idx = -1;
    return NULL;
}

static int start(crm_fake_plugin_ctx_t *ctx, bool deadlock)
{
    crm_fake_plugin_ctx_internal_t *i_ctx = (crm_fake_plugin_ctx_internal_t *)ctx;
    int ret = -1;

    ASSERT(i_ctx);

    LOGD("->%s()", __FUNCTION__);

    if (i_ctx->idx == -1) {
        char args[20];
        const char *lib_name = "libcrm_test_fake_plugin_operation.so";
        int args_len = snprintf(args, sizeof(args), "%c;hello world", deadlock ? '1' : '0');

        ASSERT(i_ctx->factory);
        i_ctx->deadlock = deadlock;
        i_ctx->idx = i_ctx->factory->create(i_ctx->factory, lib_name, args, args_len);

        if (i_ctx->idx >= 0) {
            i_ctx->ctrl_thread = crm_thread_init(proces_control, i_ctx, true, true);
            ASSERT(i_ctx->ctrl_thread);
            ret = 0;
        }
    }

    return ret;
}

static void kill_process(crm_fake_plugin_ctx_t *ctx)
{
    crm_fake_plugin_ctx_internal_t *i_ctx = (crm_fake_plugin_ctx_internal_t *)ctx;

    ASSERT(i_ctx);

    LOGD("->%s()", __FUNCTION__);

    if (i_ctx->idx != -1) {
        i_ctx->factory->kill(i_ctx->factory, i_ctx->idx);
        ASSERT(i_ctx->ctrl_thread);
        crm_ipc_msg_t msg = { .scalar = 0 };
        i_ctx->ctrl_thread->send_msg(i_ctx->ctrl_thread, &msg);
    }
}

static void dispose(crm_fake_plugin_ctx_t *ctx)
{
    crm_fake_plugin_ctx_internal_t *i_ctx = (crm_fake_plugin_ctx_internal_t *)ctx;

    ASSERT(i_ctx);
    i_ctx->factory = NULL;

    free(i_ctx);
}

crm_fake_plugin_ctx_t *crm_fake_plugin_init(crm_process_factory_ctx_t *factory,
                                            void (*notify)(void))
{
    crm_fake_plugin_ctx_internal_t *i_ctx = calloc(1, sizeof(crm_fake_plugin_ctx_internal_t));

    ASSERT(i_ctx);
    ASSERT(factory);
    ASSERT(notify);

    i_ctx->ctx.start = start;
    i_ctx->ctx.kill = kill_process;
    i_ctx->ctx.dispose = dispose;

    i_ctx->notify = notify;
    i_ctx->factory = factory;

    i_ctx->idx = -1;

    return &i_ctx->ctx;
}
