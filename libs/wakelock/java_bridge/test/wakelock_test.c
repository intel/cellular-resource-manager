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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <signal.h>

#define CRM_MODULE_TAG "WAKET"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/thread.h"
#include "utils/wakelock.h"

enum {
    EV_CONNECTED = 1,
    EV_DISCONNECTED,
    EV_ACQUIRED,
    EV_RELEASED,
};

static void *fake_java_daemon(crm_thread_ctx_t *thread_ctx, void *args)
{
    ASSERT(thread_ctx);
    (void)args;

    int t_fd = thread_ctx->get_poll_fd(thread_ctx);
    ASSERT(t_fd >= 0);

    int b_fd = -1;
    crm_ipc_msg_t msg;
    while (true) {
        if (b_fd == -1) {
            b_fd = socket(AF_INET, SOCK_STREAM, 0);
            ASSERT(b_fd >= 0);

            struct sockaddr_in dest;
            memset(&dest, 0, sizeof(dest));
            dest.sin_family = AF_INET;
            dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            dest.sin_port = htons(1703);

            if (connect(b_fd, (struct sockaddr *)&dest, sizeof(struct sockaddr))) {
                close(b_fd);
                b_fd = -1;
            } else {
                msg.scalar = EV_CONNECTED;
                ASSERT(thread_ctx->send_msg(thread_ctx, &msg));
            }
        }

        struct pollfd pfd[] = {
            { .fd = t_fd, .events = POLLIN },
            { .fd = b_fd, .events = POLLIN },
        };

        if (!poll(pfd, ARRAY_SIZE(pfd), (b_fd == -1) ? 500 : -1))
            continue;

        if (pfd[0].revents) {
            break;
        } else if (pfd[1].revents & POLLIN) {
            char tmp[10240];
            ssize_t len = recv(b_fd, tmp, sizeof(tmp), 0);
            if (len == 0) {
                close(b_fd);
                b_fd = -1;
                msg.scalar = EV_DISCONNECTED;
                thread_ctx->send_msg(thread_ctx, &msg);
            } else {
                if (len >= (ssize_t)(3 * sizeof(uint32_t))) {
                    uint32_t msg_cmd;
                    memcpy(&msg_cmd, tmp + 2 * sizeof(uint32_t), sizeof(msg_cmd));
                    msg_cmd = htonl(msg_cmd);
                    send(b_fd, tmp, sizeof(uint32_t), 0);
                    if (msg_cmd == 0) {
                        msg.scalar = EV_ACQUIRED;
                        thread_ctx->send_msg(thread_ctx, &msg);
                    } else if (msg_cmd == 1) {
                        msg.scalar = EV_RELEASED;
                        thread_ctx->send_msg(thread_ctx, &msg);
                    } else {
                        ASSERT(0);
                    }
                }
            }
        }
    }

    return NULL;
}

static int start_bridge_daemon()
{
    pid_t child = fork();

    ASSERT(child != -1);

    if (0 == child) {
        const char *app_name = "teljavabridged";
        int err = execlp(app_name, app_name, NULL);
        if (err)
            KLOG("failed to start teljavabridged: %d", err);
        exit(0);
    }

    return child;
}

static void check_evt(crm_thread_ctx_t *daemon, int evt)
{
    crm_ipc_msg_t msg;
    struct pollfd pfd = { .fd = daemon->get_poll_fd(daemon), .events = POLLIN };

    DASSERT(poll(&pfd, 1, 2000) != 0, "timeout. Waiting for event %d", evt);

    ASSERT(daemon->get_msg(daemon, &msg));
    DASSERT(msg.scalar == evt, "event %lld received while expecting %d", msg.scalar, evt);
}

int main()
{
    crm_thread_ctx_t *daemon = crm_thread_init(fake_java_daemon, NULL, true, false);

    ASSERT(daemon);

    pid_t pid = start_bridge_daemon();
    check_evt(daemon, EV_CONNECTED);

    KLOG("starting");
    crm_wakelock_t *wakelock = crm_wakelock_init("test app");

    KLOG("Checking initial state...");
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == false);
    ASSERT(wakelock->is_held(wakelock) == false);

    KLOG("first module acquires a first time");
    wakelock->acquire(wakelock, 0);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == true);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == false);
    ASSERT(wakelock->is_held(wakelock) == true);
    check_evt(daemon, EV_ACQUIRED);

    KLOG("first module acquires a second time");
    wakelock->acquire(wakelock, 0);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == true);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == false);
    ASSERT(wakelock->is_held(wakelock) == true);

    KLOG("first module releases a first time");
    wakelock->release(wakelock, 0);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == true);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == false);
    ASSERT(wakelock->is_held(wakelock) == true);

    KLOG("first module releases a second time");
    wakelock->release(wakelock, 0);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == false);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == false);
    ASSERT(wakelock->is_held(wakelock) == false);
    check_evt(daemon, EV_RELEASED);

    KLOG("wakelock acquired by two modules");
    wakelock->acquire(wakelock, 0);
    wakelock->acquire(wakelock, 1);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == true);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == true);
    ASSERT(wakelock->is_held(wakelock) == true);
    check_evt(daemon, EV_ACQUIRED);

    KLOG("wakelock released by first module");
    wakelock->release(wakelock, 0);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == false);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == true);
    ASSERT(wakelock->is_held(wakelock) == true);

    KLOG("restarting java bridge. wakelock acquired at the end");
    {
        kill(pid, SIGKILL);
        check_evt(daemon, EV_DISCONNECTED);

        ASSERT(wakelock->is_held_by_module(wakelock, 0) == false);
        ASSERT(wakelock->is_held_by_module(wakelock, 1) == true);
        ASSERT(wakelock->is_held(wakelock) == true);

        for (int i = 0; i < 10; i++) {
            wakelock->release(wakelock, 1);
            wakelock->acquire(wakelock, 1);
        }

        pid = start_bridge_daemon();
        check_evt(daemon, EV_CONNECTED);
        check_evt(daemon, EV_ACQUIRED);
    }

    KLOG("restarting java bridge. wakelock released at the end");
    {
        kill(pid, SIGKILL);
        check_evt(daemon, EV_DISCONNECTED);

        for (int i = 0; i < 10; i++) {
            wakelock->release(wakelock, 1);
            wakelock->acquire(wakelock, 1);
        }
        wakelock->release(wakelock, 1);

        pid = start_bridge_daemon();
        check_evt(daemon, EV_CONNECTED);
    }

    KLOG("counter test");
    {
        wakelock->acquire(wakelock, 1);
        check_evt(daemon, EV_ACQUIRED);
        for (int i = 0; i < 5; i++)
            wakelock->acquire(wakelock, 1);
        for (int i = 0; i < 8; i++)
            wakelock->release(wakelock, 1);
        check_evt(daemon, EV_RELEASED);
    }

    KLOG("disposing...");
    wakelock->acquire(wakelock, 0);
    wakelock->acquire(wakelock, 1);
    wakelock->acquire(wakelock, 2);
    check_evt(daemon, EV_ACQUIRED);

    wakelock->dispose(wakelock);

    check_evt(daemon, EV_RELEASED);
    kill(pid, SIGKILL);
    check_evt(daemon, EV_DISCONNECTED);

    crm_ipc_msg_t msg = { .scalar = -1 };

    daemon->send_msg(daemon, &msg);
    daemon->dispose(daemon, NULL);

    KLOG("success");
    return 0;
}
