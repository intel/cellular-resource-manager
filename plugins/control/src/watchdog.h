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

#ifndef __CRM_CONTROL_WATCHDOG_HEADER__
#define __CRM_CONTROL_WATCHDOG_HEADER__

#include "utils/thread.h"
#include "utils/wakelock.h"

#define MAX_PING_ELAPSED 10000 /* ms */

/**
 * Watchdog functionality:
 *
 * - START/STOP
 *   ---------
 *   Watchdog can be armed by user with the START request.
 *   User must provide a timeout (in milliseconds) and a valid ID (a value between 0 and MAX_REQ_ID
 *   and higher than previous ID. use watchdog_get_new_id()).
 *   Watchdog is disarmed with STOP request. User must provide the same ID.
 *
 *   If a new request is sent while a previous one is pending, new request overwrites the previous
 *   one.
 *   If timer expires, watchdog asserts.
 *
 * - PING/PONG:
 *   ---------
 *   Watchdog will ping its user periodically by sending PING request. A ping ID is provided
 *   User must answer by sending PONG request with the same ID.
 *
 *   If client answer is received too late, watchdog asserts.
 */

enum watchdog_requests {
    CRM_WATCH_START = 1, // starts a request timer
    CRM_WATCH_STOP,      // stops the request timer
    CRM_WATCH_PING,      // starts a PING timer
    CRM_WATCH_PONG,      // stops the PING timer
};

#define MAX_REQ_ID 2147483647 // ((2^(8*4))/2)-1
#define MAX_TIMEOUT 8388607   // ((2^(8*3))/2)-1

/* Message is mapped as follow:
 *
 * bits  : data
 * 7-0   : request (1 byte)
 * 39-8  : id      (4 bytes)
 * 63-40 : timeout (3 bytes)
 */

/**
 * Generates a message to notify a watchdog event
 *
 * @param [in] request type of request
 * @param [in] timeout used only for CRM_WATCH_START request. in ms
 * @param [in] id      id of the request. For the same type of request (PING or REQUEST)
 *                     must be higher than previous one
 */
static inline long long watchdog_gen_scalar(enum watchdog_requests request, int timeout, int id)
{
    ASSERT(timeout <= MAX_TIMEOUT);
    ASSERT(id <= MAX_REQ_ID);
    return (((long long)timeout) & 0x7FFFFF) << 40 |
           (((long long)id) & 0x7FFFFFFF) << 8 | (request & 0xFF);
}

static inline enum watchdog_requests watchdog_get_request(long long scalar)
{
    return scalar & 0xFF;
}

static inline int watchdog_get_id(long long scalar)
{
    return (scalar >> 8) & 0x7FFFFFFF;
}

static inline int watchdog_get_timeout(long long scalar)
{
    return (scalar >> 40) & 0x7FFFFF;
}

static inline int watchdog_get_new_id(int id)
{
    if (id < MAX_REQ_ID)
        return id + 1;
    else
        return 0;
}

typedef struct watchdog_param {
    int ping_period;
    crm_wakelock_t *wakelock;
} watchdog_param_t;

/**
 * Starts the watchdog
 * Must be called with crm_thread_init
 *
 * @param [in] thread_ctx Parameter provided by crm_thread_init
 * @param [in] param      watchdog_param_t pointer
 */
void *crm_watchdog_loop(crm_thread_ctx_t *thread_ctx, void *param);

#endif /* __CRM_CONTROL_WATCHDOG_HEADER__ */
