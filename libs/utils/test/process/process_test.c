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
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <unistd.h>

#define CRM_MODULE_TAG "FACTT"
#include "utils/common.h"
#include "utils/process_factory.h"
#include "utils/ipc.h"

#include "libmdmcli/mdm_cli.h"

#include "fake_plugin.h"

crm_ipc_ctx_t *g_ipc = NULL;
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
crm_process_factory_ctx_t *g_factory = NULL;

#define NB_PROCESSES 10
#define NB_FAKES (NB_PROCESSES + 20)

static void notify(void)
{
    ASSERT(g_ipc);
    ASSERT(pthread_mutex_lock(&g_lock) == 0);
    crm_ipc_msg_t msg = { .scalar = 1 };
    while (g_ipc->send_msg(g_ipc, &msg) != true)
        usleep(500 * 1000);
    ASSERT(pthread_mutex_unlock(&g_lock) == 0);
}

static void sig_handler(int sig)
{
    (void)sig;
    if (g_factory) {
        g_factory->dispose(g_factory);
        kill(getpid(), SIGKILL);
        exit(0);
    }
}

int main(void)
{
    crm_logs_init(MDM_CLI_DEFAULT_INSTANCE);
    LOGD("starting...");

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    ASSERT(!sigaction(SIGABRT, &sa, NULL));
    ASSERT(!sigaction(SIGCHLD, &sa, NULL));

    crm_process_factory_ctx_t *factory = crm_process_factory_init(NB_PROCESSES);
    ASSERT(factory);
    g_factory = factory;

    g_ipc = crm_ipc_init(CRM_IPC_THREAD);
    struct pollfd pfd = { .fd = g_ipc->get_poll_fd(g_ipc), .events = POLLIN };

    crm_fake_plugin_ctx_t **fakes = malloc(sizeof(crm_fake_plugin_ctx_t *) * NB_FAKES);
    ASSERT(fakes);

    for (int i = 0; i < NB_FAKES; i++) {
        fakes[i] = crm_fake_plugin_init(factory, notify);
        ASSERT(fakes[i]);
    }

    for (int loop = 0; loop < 20; loop++) {
        int f_idx = 0;
        int nb_dead = 0;
        do {
            /*create processes till it's possible */
            while (f_idx < NB_FAKES && fakes[f_idx]->start(fakes[f_idx], false) == 0)
                f_idx++;

            for (;; ) {
                int err = poll(&pfd, 1, 500);
                if (err == 0)
                    break;
                ASSERT(err != -1);

                crm_ipc_msg_t msg;
                ASSERT(g_ipc->get_msg(g_ipc, &msg));
                nb_dead++;
            }
            int created = loop * NB_FAKES + f_idx;
            int terminated = loop * NB_FAKES + nb_dead;
            LOGD("STATS. created: %d. terminated: %d", created, terminated);
        } while (nb_dead < NB_FAKES);
        LOGD("all processes are stopped");
    }

    LOGD("testing kill API...");
    for (int i = 0; i < 5; i++) {
        while (fakes[0]->start(fakes[0], true) != 0)
            usleep(500 * 1000);
        ASSERT(!poll(&pfd, 1, 2000));

        fakes[0]->kill(fakes[0]);
        ASSERT(poll(&pfd, 1, 500));
        crm_ipc_msg_t msg;
        ASSERT(g_ipc->get_msg(g_ipc, &msg));
    }

    LOGD("cleaning...");

    for (int i = 0; i < NB_FAKES; i++)
        fakes[i]->dispose(fakes[i]);
    free(fakes);

    g_factory = NULL;
    factory->dispose(factory);
    g_ipc->dispose(g_ipc, NULL);

    LOGD("success");
    return 0;
}
