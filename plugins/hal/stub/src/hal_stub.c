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

#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils/thread.h"
#include "plugins/hal.h"
#include "plugins/control.h"
#include "utils/keys.h"
#include "utils/property.h"

#define CRM_MODULE_TAG "HAL"
#include "utils/logs.h"
#include "utils/common.h"

#define MAX_SLEEP 20

typedef enum hal_stub_ops {
    STUB_MDM_POWER,
    STUB_MDM_BOOT,
    STUB_MDM_RESET,
    STUB_MDM_STOP,
} hal_stub_ops_t;

typedef struct crm_hal_ctx_internal {
    crm_hal_ctx_t ctx; //Needs to be first

    crm_ctrl_ctx_t *control;
    crm_thread_ctx_t *thread;
} crm_hal_ctx_internal_t;

/**
 * @see hal.h
 */
static void dispose(crm_hal_ctx_t *ctx)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    i_ctx->thread->dispose(i_ctx->thread, NULL);
    free(i_ctx);
}

static void notify_ctrl(crm_ctrl_ctx_t *control, crm_hal_evt_type_t *evt, size_t nb)
{
    for (size_t i = 0; i < nb; i++) {
        crm_hal_evt_t event = { evt[i], "", NULL };

        usleep((rand() % MAX_SLEEP) * 10000);
        control->notify_hal_event(control, &event);
    }
}

static void fake_mdm_power(crm_ctrl_ctx_t *control)
{
    ASSERT(control != NULL);
    crm_hal_evt_type_t evt[] = { HAL_MDM_FLASH };
    notify_ctrl(control, evt, ARRAY_SIZE(evt));
}

static void fake_mdm_boot(crm_ctrl_ctx_t *control)
{
    ASSERT(control != NULL);
    crm_hal_evt_type_t evt[] = { HAL_MDM_RUN };
    notify_ctrl(control, evt, ARRAY_SIZE(evt));
}

static void fake_mdm_reset(crm_ctrl_ctx_t *control)
{
    ASSERT(control != NULL);
    crm_hal_evt_type_t evt[] = { HAL_MDM_BUSY, HAL_MDM_FLASH };
    notify_ctrl(control, evt, ARRAY_SIZE(evt));
}

static void fake_mdm_shutdown(crm_ctrl_ctx_t *control)
{
    ASSERT(control != NULL);
    crm_hal_evt_type_t evt[] = { HAL_MDM_BUSY, HAL_MDM_OFF };
    notify_ctrl(control, evt, ARRAY_SIZE(evt));
}

/**
 * @see hal.h
 */
static void power_on(crm_hal_ctx_t *ctx)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("POWER ON request received");
    crm_ipc_msg_t msg = { .scalar = STUB_MDM_POWER };
    i_ctx->thread->send_msg(i_ctx->thread, &msg);
}

/**
 * @see hal.h
 */
static void boot(crm_hal_ctx_t *ctx)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s()", __FUNCTION__);
    crm_ipc_msg_t msg = { .scalar = STUB_MDM_BOOT };
    i_ctx->thread->send_msg(i_ctx->thread, &msg);
}

/**
 * @see hal.h
 */
static void reset(crm_hal_ctx_t *ctx, crm_hal_reset_type_t type)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)ctx;

    (void)type;  // UNUSED
    ASSERT(i_ctx != NULL);

    LOGD("->%s()", __FUNCTION__);
    crm_ipc_msg_t msg = { .scalar = STUB_MDM_RESET };
    i_ctx->thread->send_msg(i_ctx->thread, &msg);
}

/**
 * @see hal.h
 */
static void shutdown(crm_hal_ctx_t *ctx)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s()", __FUNCTION__);

    crm_ipc_msg_t msg = { .scalar = STUB_MDM_STOP };
    i_ctx->thread->send_msg(i_ctx->thread, &msg);
}

static void *hal_stub(crm_thread_ctx_t *ctx, void *param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)param;

    ASSERT(i_ctx != NULL);
    ASSERT(ctx != NULL);

    int fd = ctx->get_poll_fd(ctx);
    ASSERT(fd != -1);
    while (1) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        poll(&pfd, 1, -1);
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        } else {
            crm_ipc_msg_t msg;
            while (ctx->get_msg(ctx, &msg)) {
                switch (msg.scalar) {
                case STUB_MDM_POWER:
                    fake_mdm_power(i_ctx->control);
                    break;
                case STUB_MDM_BOOT:
                    fake_mdm_boot(i_ctx->control);
                    break;
                case STUB_MDM_RESET:
                    fake_mdm_reset(i_ctx->control);
                    break;
                case STUB_MDM_STOP:
                    fake_mdm_shutdown(i_ctx->control);
                    break;
                }
            }
        }
    }

    return NULL;
}

/**
 * @see hal.h
 */
crm_hal_ctx_t *crm_hal_init(int inst_id, bool debug, bool dump_enabled, tcs_ctx_t *tcs,
                            crm_ctrl_ctx_t *control)
{
    crm_hal_ctx_internal_t *i_ctx = calloc(1, sizeof(crm_hal_ctx_internal_t));

    ASSERT(i_ctx != NULL);
    ASSERT(control != NULL);
    (void)inst_id;      // UNUSED
    (void)debug;        // UNUSED
    (void)dump_enabled; // UNUSED
    (void)tcs;          // Scalability not used by this plugin

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.power_on = power_on;
    i_ctx->ctx.boot = boot;
    i_ctx->ctx.shutdown = shutdown;
    i_ctx->ctx.reset = reset;

    i_ctx->control = control;

    time_t s_time = time(NULL);
    srand(s_time);

    crm_property_set(CRM_KEY_SERVICE_START, CRM_KEY_CONTENT_SERVICE_RPCD);

    i_ctx->thread = crm_thread_init(hal_stub, i_ctx, true, false);

    LOGV("context %p", i_ctx);
    return &i_ctx->ctx;
}
