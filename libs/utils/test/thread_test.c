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

#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <stdlib.h>

#define CRM_MODULE_TAG "THREAD_TEST"
#include "utils/common.h"
#include "utils/thread.h"

enum TEST_ID {
    TEST_JOIN,
    TEST_PARENT_TO_CHILD,
    TEST_CHILD_TO_PARENT,
    TEST_DISPOSE_WITH_FREE,
    TEST_DETACHED,
    TEST_RANDOM,
};

#define MAX_THREADS_RANDOM_TEST 16
#define MAX_RANDOM_TEST_DURATION 900 // 15 minutes

void *detached_test_routine(crm_thread_ctx_t *ctx, void *param)
{
    ASSERT(ctx != NULL);
    (void)param;  // UNUSED
    LOGD("detached thread started");
    ctx->dispose(ctx, NULL);
    LOGD("detached thread stopped");

    return NULL;
}

void *test_routine(crm_thread_ctx_t *ctx, void *param)
{
    enum TEST_ID test = *(enum TEST_ID *)param;

    if (test == TEST_JOIN) {
        /* Basic 'sleep / join' test */
        sleep(1);
    } else if (test == TEST_PARENT_TO_CHILD) {
        /* Mirroring where messages come from parent */
        int fd = ctx->get_poll_fd(ctx);
        while (fd != -1) {
            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            poll(&pfd, 1, -1);
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                break;
            } else {
                crm_ipc_msg_t msg;
                while (ctx->get_msg(ctx, &msg)) {
                    msg.scalar |= 1 << 16;
                    ctx->send_msg(ctx, &msg);
                }
            }
        }
    } else if (test == TEST_CHILD_TO_PARENT) {
        int fd = ctx->get_poll_fd(ctx);
        for (int i = 0; (i < 32) && (fd != -1); i++) {
            crm_ipc_msg_t msg;
            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            for (int j = 0; j < (i % 4) + 1; j++) {
                msg.scalar = j << 8 | i;
                LOGD("Sending message : %06llx", msg.scalar);
                ctx->send_msg(ctx, &msg);
            }
            poll(&pfd, 1, -1);
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
                break;
            else
                while (ctx->get_msg(ctx, &msg))
                    LOGD("Received message: %06llx", msg.scalar);
        }
    } else if (test == TEST_RANDOM) {
        /* Mirroring where messages come from parent + some unsolicited */
        int fd = ctx->get_poll_fd(ctx);
        while (fd != -1) {
            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            int to = rand() % 15;
            if (poll(&pfd, 1, to) == 0) {
                crm_ipc_msg_t msg = { .scalar = -rand() };
                ctx->send_msg(ctx, &msg);
            } else if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                break;
            } else {
                crm_ipc_msg_t msg;
                while (ctx->get_msg(ctx, &msg)) {
                    poll(NULL, 0, rand() % 50);
                    ctx->send_msg(ctx, &msg);
                }
            }
        }
    } else if (test == TEST_DISPOSE_WITH_FREE) {
        /* Doing nothing :) */
        int fd = ctx->get_poll_fd(ctx);
        while (fd != -1) {
            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            poll(&pfd, 1, -1);
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
                break;
            else
                sleep(2);
        }
    }

    return NULL;
}

void free_func(const crm_ipc_msg_t *msg)
{
    free(msg->data);
}

int main(void)
{
    crm_thread_ctx_t *ctx;
    int test;
    int fd;

    /* Basic 'join' test */
    test = TEST_JOIN;
    LOGD("Join test");
    ctx = crm_thread_init(test_routine, &test, true, false);
    ctx->dispose(ctx, NULL);

    /* Basic 'mirror' P=>C test */
    test = TEST_PARENT_TO_CHILD;
    LOGD("P => C test");
    ctx = crm_thread_init(test_routine, &test, true, false);
    fd = ctx->get_poll_fd(ctx);
    bool last_received = false;
    for (int i = 0; !last_received; i++) {
        crm_ipc_msg_t msg;
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        if (i < 32) {
            for (int j = 0; j < (i % 4) + 1; j++) {
                msg.scalar = j << 8 | i;
                LOGD("Sending message : %06llx", msg.scalar);
                ctx->send_msg(ctx, &msg);
            }
        }
        poll(&pfd, 1, -1);
        while (ctx->get_msg(ctx, &msg)) {
            last_received = msg.scalar == ((1 << 16) | (31 % 4) << 8 | 31);
            LOGD("Received message: %06llx", msg.scalar);
        }
    }
    ctx->dispose(ctx, NULL);

    /* Basic 'mirror' C=>P test */
    test = TEST_CHILD_TO_PARENT;
    LOGD("C => P test");
    ctx = crm_thread_init(test_routine, &test, true, false);
    fd = ctx->get_poll_fd(ctx);
    while (1) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        crm_ipc_msg_t msg;
        poll(&pfd, 1, -1);
        while (ctx->get_msg(ctx, &msg)) {
            msg.scalar |= 1 << 16;
            ctx->send_msg(ctx, &msg);
        }
        if (msg.scalar == ((1 << 16) | (31 % 4) << 8 | 31))
            break;
    }
    ctx->dispose(ctx, NULL);

    /* Memory free test */
    test = TEST_DISPOSE_WITH_FREE;
    LOGD("Mem free test");
    ctx = crm_thread_init(test_routine, &test, true, false);
    for (int i = 0; i < 5; i++) {
        crm_ipc_msg_t msg = { .data = malloc(50) };
        ctx->send_msg(ctx, &msg);
    }
    ctx->dispose(ctx, free_func);

    /* Detached test */
    test = TEST_DETACHED;
    LOGD("detached thread");
    crm_thread_init(detached_test_routine, &test, false, true);

    /* Stress test with multiple threads and a single 'controller' that simulates command / response
     * and unsolicited messages.
     *
     * Test ends after around 15 minutes, it's a random test :)
     */
    test = TEST_RANDOM;
    LOGD("Random test");
    time_t s_time = time(NULL);
    srand(s_time);
    while ((time(NULL) - s_time) <= MAX_RANDOM_TEST_DURATION) {
        int num_ctx = rand() % MAX_THREADS_RANDOM_TEST;
        num_ctx += 1; // To have at least one context :)

        crm_thread_ctx_t *ctx_array[MAX_THREADS_RANDOM_TEST];
        int fd_array[MAX_THREADS_RANDOM_TEST];
        struct pollfd pfd_array[MAX_THREADS_RANDOM_TEST];
        int num_msg[MAX_THREADS_RANDOM_TEST];
        int wait_reply[MAX_THREADS_RANDOM_TEST];
        bool all_wait = false;

        for (int i = 0; i < num_ctx; i++) {
            ctx_array[i] = crm_thread_init(test_routine, &test, true, false);
            fd_array[i] = ctx_array[i]->get_poll_fd(ctx_array[i]);
            pfd_array[i].fd = fd_array[i];
            pfd_array[i].events = POLLIN;
            num_msg[i] = 10 + rand() % 256;
            wait_reply[i] = -1;
        }

        while (1) {
            int timeout = rand() % 30;
            if (all_wait)
                timeout = -1;

            int ret = poll(pfd_array, num_ctx, timeout);
            if (ret == 0) {
                /* Timeout during the poll */
                int j = rand() % num_ctx;
                int k;
                for (k = 0; k < num_ctx; k++) {
                    if ((num_msg[(k + j) % num_ctx] != 0) && (wait_reply[(k + j) % num_ctx] == -1))
                        break;
                }
                ASSERT(k != num_ctx);
                k = (k + j) % num_ctx;
                crm_ipc_msg_t msg = { .scalar = rand() };
                wait_reply[k] = msg.scalar;
                LOGD(">[%02d] REQ   %lld", k, msg.scalar);
                ctx_array[k]->send_msg(ctx_array[k], &msg);
            } else if (ret > 0) {
                /* Message received from one of the threads */
                for (int i = 0; i < num_ctx; i++) {
                    if (pfd_array[i].revents & POLLIN) {
                        crm_ipc_msg_t msg;
                        while (ctx_array[i]->get_msg(ctx_array[i], &msg)) {
                            if (msg.scalar < 0) {
                                LOGD("<[%02d] UNSOL %lld", i, -msg.scalar);
                            } else if (msg.scalar == wait_reply[i]) {
                                wait_reply[i] = -1;
                                num_msg[i] -= 1;
                                LOGD("<[%02d] RESP  %lld", i, msg.scalar);
                            } else {
                                LOGD("<[%02d] UNEXP %lld", i, msg.scalar);
                                ASSERT(0);
                            }
                        }
                    }
                }
            } else {
                ASSERT(0);
            }

            bool all_NULL = true;
            all_wait = true;
            for (int i = 0; i < num_ctx; i++) {
                if (num_msg[i] == 0) {
                    if (ctx_array[i] != NULL) {
                        ctx_array[i]->dispose(ctx_array[i], NULL);
                        ctx_array[i] = NULL;
                        fd_array[i] = -1;
                        pfd_array[i].fd = -1;
                        pfd_array[i].events = 0;
                    }
                } else {
                    all_NULL = false;
                }
                if ((num_msg[i] != 0) && (wait_reply[i] == -1))
                    all_wait = false;
            }
            if (all_NULL)
                break;
        }
    }

    return 0;
}
