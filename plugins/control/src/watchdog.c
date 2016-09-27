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
#include <string.h>
#include <time.h>
#include <limits.h>

#define CRM_MODULE_TAG "CTRL"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/thread.h"
#include "utils/wakelock.h"
#include "utils/time.h"

#include "watchdog.h"


typedef struct watchdog_timer {
    int id;
    bool armed;
    struct timespec end;
    bool waiting_pong; // only used by PING/PONG request
} watchdog_timer_t;

#define PING_IDX 0
#define REQ_IDX 1

static int get_timeout(watchdog_timer_t *timer, int *idx)
{
    int timeout = INT_MAX;

    ASSERT(idx != NULL);

    *idx = -1;
    int i;
    for (i = 0; i < 2; i++) {
        if (timer[i].armed) {
            int remain = crm_time_get_remain_ms(&timer[i].end);
            if (remain <= 1) {
                timeout = 0;
                *idx = i;
                break;
            } else if (remain < timeout) {
                *idx = i;
                timeout = remain;
            }
        }
    }

    ASSERT(timeout != INT_MAX);
    ASSERT(*idx != -1);

    return timeout;
}

static void handle_msg(watchdog_timer_t *timer, crm_ipc_msg_t *msg, int ping_period,
                       crm_wakelock_t *wakelock)
{
    enum watchdog_requests request = watchdog_get_request(msg->scalar);
    int timeout = watchdog_get_timeout(msg->scalar);
    int id = watchdog_get_id(msg->scalar);

    switch (request) {
    case CRM_WATCH_START:
        /* If a request is already pending, the new one overwrites the previous one */
        if (timer[REQ_IDX].id < MAX_REQ_ID)
            ASSERT(id == (timer[REQ_IDX].id + 1));
        else
            ASSERT(id == 0);

        /* Make sure that wakelock is held if timer is armed or not if unarmed */
        bool is_held = wakelock->is_held_by_module(wakelock, WAKELOCK_WATCHDOG_REQ);
        ASSERT(timer[REQ_IDX].armed == is_held);
        if (!is_held)
            wakelock->acquire(wakelock, WAKELOCK_WATCHDOG_REQ);

        timer[REQ_IDX].id = id;
        timer[REQ_IDX].armed = true;
        crm_time_add_ms(&timer[REQ_IDX].end, timeout);

        break;
    case CRM_WATCH_STOP:
        ASSERT(timer[REQ_IDX].armed == true);

        if (timer[REQ_IDX].id != id)
            break;

        wakelock->release(wakelock, WAKELOCK_WATCHDOG_REQ);
        ASSERT(wakelock->is_held_by_module(wakelock, WAKELOCK_WATCHDOG_REQ) == false);

        timer[REQ_IDX].armed = false;
        break;
    case CRM_WATCH_PONG:
        ASSERT(timer[PING_IDX].waiting_pong == true);
        ASSERT(timer[PING_IDX].id == id);

        wakelock->release(wakelock, WAKELOCK_WATCHDOG_PING);
        ASSERT(wakelock->is_held_by_module(wakelock, WAKELOCK_WATCHDOG_PING) == false);

        // New PING timer configuration: armed to send the PING request
        timer[PING_IDX].waiting_pong = false;
        crm_time_add_ms(&timer[PING_IDX].end, ping_period);
        break;
    default: ASSERT(0);
    }
}

/**
 * @see watchdog.h
 */
void *crm_watchdog_loop(crm_thread_ctx_t *thread_ctx, void *param)
{
    watchdog_timer_t timer[2];
    struct pollfd pfd = { .fd = thread_ctx->get_poll_fd(thread_ctx), .events = POLLIN };

    watchdog_param_t *cfg = (watchdog_param_t *)param;

    ASSERT(cfg != NULL);

    int ping_period = cfg->ping_period;
    crm_wakelock_t *wakelock = cfg->wakelock;
    ASSERT(ping_period > 0);
    ASSERT(wakelock != NULL);

    free(cfg);
    cfg = NULL;

    timer[PING_IDX].id = -1;
    timer[PING_IDX].armed = true;
    timer[PING_IDX].waiting_pong = false;
    crm_time_add_ms(&timer[PING_IDX].end, ping_period);

    timer[REQ_IDX].id = -1;
    timer[REQ_IDX].armed = false;

    bool run = true;
    while (run) {
        int idx;
        int err = poll(&pfd, 1, get_timeout(timer, &idx));
        if ((-1 == err) || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
            DASSERT(0, "error in control socket");

        if (0 == err) {
            if (PING_IDX == idx) {
                if (timer[PING_IDX].waiting_pong) {
                    wakelock->release(wakelock, WAKELOCK_WATCHDOG_PING);
                    ASSERT(wakelock->is_held_by_module(wakelock, WAKELOCK_WATCHDOG_PING) == false);
                    DASSERT(0, "PONG not received. watchdog expiration");
                } else {
                    wakelock->acquire(wakelock, WAKELOCK_WATCHDOG_PING);

                    // New PING timer configuration: armed to wait for PING answer
                    timer[PING_IDX].id = watchdog_get_new_id(timer[PING_IDX].id);
                    timer[PING_IDX].waiting_pong = true;
                    crm_time_add_ms(&timer[PING_IDX].end, MAX_PING_ELAPSED);

                    crm_ipc_msg_t msg =
                    { .scalar = watchdog_gen_scalar(CRM_WATCH_PING, 0, timer[PING_IDX].id) };
                    thread_ctx->send_msg(thread_ctx, &msg);
                }
            } else if (REQ_IDX == idx) {
                wakelock->release(wakelock, WAKELOCK_WATCHDOG_REQ);
                ASSERT(wakelock->is_held_by_module(wakelock, WAKELOCK_WATCHDOG_REQ) == false);
                DASSERT(0, "Answer not received. watchdog expiration");
            }
        } else if (pfd.revents & POLLIN) {
            crm_ipc_msg_t msg;
            while (thread_ctx->get_msg(thread_ctx, &msg)) {
                if (msg.scalar == -1)
                    run = false;
                else
                    handle_msg(timer, &msg, ping_period, wakelock);
            }
        }
    }

    return NULL;
}
