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

#include <unistd.h>

#define CRM_MODULE_TAG "WATT"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/thread.h"
#include "utils/wakelock.h"
#include "test/test_utils.h"
#include "libmdmcli/mdm_cli.h"

#include "watchdog.h"

int main()
{
    int ping_period;
    crm_wakelock_t *wakelock = crm_wakelock_init("test");

    ASSERT(wakelock != NULL);

    tcs_ctx_t *tcs = CRM_TEST_tcs_init("host_sofia", MDM_CLI_DEFAULT_INSTANCE);
    ASSERT(tcs->select_group(tcs, ".control") == 0);
    ASSERT(tcs->get_int(tcs, "ping_period", &ping_period) == 0);
    tcs->dispose(tcs);

    watchdog_param_t *cfg_watch = malloc(sizeof(watchdog_param_t));
    ASSERT(cfg_watch != NULL);
    cfg_watch->wakelock = wakelock;
    cfg_watch->ping_period = ping_period;

    crm_thread_ctx_t *watchdog = crm_thread_init(crm_watchdog_loop, cfg_watch, true, false);

    struct pollfd pfd = { .fd = watchdog->get_poll_fd(watchdog), .events = POLLIN };

    bool running = true;
    bool send = true;

    enum watchdog_requests request = CRM_WATCH_PING;
    int timeout = MAX_TIMEOUT;
    int id = MAX_REQ_ID;
    long long scalar = watchdog_gen_scalar(request, timeout, id);

    ASSERT(request == watchdog_get_request(scalar));
    ASSERT(timeout == watchdog_get_timeout(scalar));
    ASSERT(id == watchdog_get_id(scalar));

    timeout = 300;
    id = -1;
    while (running) {
        int err = poll(&pfd, 1, timeout);

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            running = false;
            continue;
        }

        if (0 == err) {
            enum watchdog_requests request;
            if (send) {
                id = watchdog_get_new_id(id);
                request = CRM_WATCH_START;
                send = false;
                LOGD("START %d", id);
            } else {
                request = CRM_WATCH_STOP;
                send = true;
                LOGD("STOP %d", id);
            }

            crm_ipc_msg_t msg =
            { .scalar = watchdog_gen_scalar(request, timeout + 1000, id) };
            watchdog->send_msg(watchdog, &msg);
        }

        if (pfd.revents & POLLIN) {
            crm_ipc_msg_t msg;
            static int count = 1;
            while (watchdog->get_msg(watchdog, &msg)) {
                int sleep_ms = (MAX_PING_ELAPSED / 3) * count++;
                enum watchdog_requests request = watchdog_get_request(msg.scalar);
                int id_ping = watchdog_get_id(msg.scalar);
                ASSERT(CRM_WATCH_PING == request);
                LOGD("====> PING %d", id_ping);
                if (sleep_ms > MAX_PING_ELAPSED)
                    LOGD("EXPECTED BEHAVIOR: watchdog will crash");

                msg.scalar = watchdog_gen_scalar(CRM_WATCH_PONG, 0, id_ping);
                usleep(sleep_ms * 1000);
                watchdog->send_msg(watchdog, &msg);
                LOGD("<==== PONG");
            }
        }
    }

    wakelock->dispose(wakelock);
    watchdog->dispose(watchdog, NULL);

    return 0;
}
