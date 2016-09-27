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
#include <pthread.h>

#define CRM_MODULE_TAG "IPCT"
#include "utils/common.h"
#include "utils/ipc.h"

void *test_thread(void *args)
{
    crm_ipc_ctx_t *ipc = args;
    int fd = ipc->get_poll_fd(ipc);

    ASSERT(fd > 0);

    while (1) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        poll(&pfd, 1, -1);
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        } else {
            crm_ipc_msg_t msg;
            while (ipc->get_msg(ipc, &msg)) {
                LOGD("RCV: %lld", msg.scalar);
                if (msg.scalar == 5)
                    return NULL;
            }
        }
    }

    return NULL;
}

int main(void)
{
    /* Note: only a very basic test here as most of the stress test is done as part of the
     *       thread testing (that uses two IPC modules).
     *
     *       This test is here only to validate that the IPC module works 'on its own'.
     */

    crm_ipc_ctx_t *ipc = crm_ipc_init(CRM_IPC_THREAD);

    ASSERT(ipc != NULL);

    pthread_t t;
    ASSERT(pthread_create(&t, NULL, test_thread, ipc) == 0);

    for (int i = 0; i < 6; i++) {
        crm_ipc_msg_t msg = { .scalar = i };
        ASSERT(ipc->send_msg(ipc, &msg));
    }

    pthread_join(t, NULL);

    ipc->dispose(ipc, NULL);

    return 0;
}
