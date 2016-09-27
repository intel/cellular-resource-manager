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
#include <dlfcn.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

#define CRM_MODULE_TAG "FACT"
#include "utils/common.h"
#include "utils/process_factory.h"

typedef enum factory_events {
    CREATE,
    CLEAN,
    KILL,
    DEAD,
    DISPOSE
} factory_events_t;

typedef struct crm_process {
    pid_t pid;
    int events;

    crm_ipc_ctx_t *ipc_p2c; // parent to child
    crm_ipc_ctx_t *ipc_c2p; // child to parent
} crm_process_t;

typedef struct crm_process_factory_ctx_internal {
    crm_process_factory_ctx_t ctx; // Needs to be first

    /* configuration */
    int nb_processes;
    int created;

    pid_t pid_parent;
    pid_t pid_self;
    pthread_mutex_t lock;
    crm_ipc_ctx_t *ipc_ctrl;  // pipe used to control the main factory from factories
                              // running in children processes
    crm_ipc_ctx_t *ipc_evt;   // pipe used to notify the main factory from factories
                              // running in children processes
    crm_process_t *processes;
} crm_process_factory_ctx_internal_t;

crm_process_factory_ctx_internal_t *g_factory = NULL;

static inline long long gen_scalar(factory_events_t evt, int id)
{
    return (((long long)id) & 0x7FFFFFFF) << 8 | (evt & 0xFF);
}

static inline int get_id(long long scalar)
{
    return (scalar >> 8) & 0x7FFFFFFF;
}

static inline int get_event(long long scalar)
{
    return scalar & 0xFF;
}

static int running_process(crm_process_factory_ctx_internal_t *factory)
{
    int count = 0;

    ASSERT(pthread_mutex_lock(&factory->lock) == 0);
    for (int i = 0; i < factory->nb_processes; i++) {
        if (factory->processes[i].pid != -1)
            count++;
    }

    ASSERT(pthread_mutex_unlock(&factory->lock) == 0);
    return count;
}

static void kill_all(crm_process_factory_ctx_internal_t *factory)
{
    ASSERT(factory);

    for (int i = 0; i < factory->nb_processes; i++) {
        if (factory->processes[i].pid > 0)
            kill(factory->processes[i].pid, SIGKILL);
    }
    kill(factory->pid_parent, SIGKILL);
    exit(-1);
}

static void sig_handler(int sig)
{
    if (g_factory) {
        if ((SIGABRT == sig) || (SIGTERM == sig)) {
            crm_process_factory_ctx_internal_t *factory = g_factory;
            g_factory = NULL;
            kill_all(factory);
        } else if (SIGCHLD == sig) {
            crm_ipc_msg_t msg = { .scalar = gen_scalar(DEAD, 0) };
            g_factory->ipc_ctrl->send_msg(g_factory->ipc_ctrl, &msg);
        }
    }
}

static void free_memory(crm_process_factory_ctx_internal_t *factory)
{
    ASSERT(factory);

    factory->ipc_ctrl->dispose(factory->ipc_ctrl, NULL);
    factory->ipc_evt->dispose(factory->ipc_evt, NULL);

    for (int i = 0; i < factory->nb_processes; i++) {
        factory->processes[i].ipc_p2c->dispose(factory->processes[i].ipc_p2c, NULL);
        factory->processes[i].ipc_c2p->dispose(factory->processes[i].ipc_c2p, NULL);
    }
    free(factory->processes);
    free(factory);
}

/**
 * ================================================================================================
 * the following code runs in a dedicated process. it's the 'real' factory controlling children
 * processes.
 * ================================================================================================
 */
static void forked_flush_ipcs(crm_process_t *process)
{
    ASSERT(process);

    crm_ipc_ctx_t *ipcs[2] = { process->ipc_p2c, process->ipc_c2p };
    for (size_t i = 0; i < ARRAY_SIZE(ipcs); i++) {
        struct pollfd pfd = { .fd = ipcs[i]->get_poll_fd(ipcs[i]), .events = POLLIN };
        while (poll(&pfd, 1, 0) > 0) {
            DASSERT(process->events & (1u << KILL),
                    "remaining data while client has requested a clean");
            crm_ipc_msg_t msg;
            ASSERT(ipcs[i]->get_msg(ipcs[i], &msg));
            free(msg.data);
        }
    }
}

static int forked_create_process(crm_process_factory_ctx_internal_t *factory, crm_ipc_msg_t *msg)
{
    ASSERT(factory);
    ASSERT(msg);

    int idx = 0;
    for (; idx < factory->nb_processes && factory->processes[idx].pid != -1; idx++) ;
    if (idx >= factory->nb_processes)
        return -1;

    crm_process_t *process = &factory->processes[idx];
    ASSERT(process);

    process->pid = fork();
    process->events = 0;
    ASSERT(process->pid >= 0);

    if (!process->pid) {
        char *lib_name = msg->data;
        char *find = memchr(msg->data, ';', msg->data_size);
        ASSERT(find);
        *find = '\0';

        dlerror(); // clear previous errors
        void *handle = dlopen(lib_name, RTLD_LAZY);
        DASSERT(handle != NULL, "Failed to load %s: %s", lib_name, dlerror());

        static const char *func_name = "start_process";
        void (*start_process)(crm_ipc_ctx_t *, crm_ipc_ctx_t *, void *, size_t) = NULL;
        start_process = dlsym(handle, func_name);
        DASSERT(!dlerror() && start_process, "%s function not found in library (%s)", func_name,
                lib_name);

        uint32_t data_len = msg->data_size - (++find - lib_name);
        char *data = (data_len > 0) ? find : NULL;

        start_process(process->ipc_p2c, process->ipc_c2p, data, data_len);

        free_memory(factory);
        free(msg->data);
        dlclose(handle);
        exit(0);
    } else {
        LOGD("process {id[%d],pid[%d]} is started", idx, process->pid);
    }

    return idx;
}

static void forked_handle_events(crm_process_factory_ctx_internal_t *factory)
{
    ASSERT(factory);

    struct pollfd pfd = { .fd = factory->ipc_ctrl->get_poll_fd(factory->ipc_ctrl),
                          .events = POLLIN };

    bool stopping = false;
    while (true) {
        ASSERT(pthread_mutex_lock(&factory->lock) == 0);
        int timeout = -1;
        for (int i = 0; i < factory->nb_processes; i++) {
            if ((factory->processes[i].pid > 0) && (factory->processes[i].events & (1u << DEAD))) {
                timeout = 500;
                break;
            }
        }
        ASSERT(pthread_mutex_unlock(&factory->lock) == 0);

        int err = poll(&pfd, 1, timeout);
        if (err == -1) {
            DASSERT((errno == EINTR) || (errno == ECHILD), "error: %d %s", errno, strerror(errno));
            continue;
        } else if (err == 0) {
            for (int i = 0; i < factory->nb_processes; i++) {
                if ((factory->processes[i].pid > 0) &&
                    (factory->processes[i].events & (1u << DEAD)))
                    LOGE("Timeout. id[%d] not cleaned", i);
            }
            ASSERT(0);
        }

        ASSERT(pthread_mutex_lock(&factory->lock) == 0);
        if (pfd.revents & POLLIN) {
            crm_ipc_msg_t msg;
            ASSERT(factory->ipc_ctrl->get_msg(factory->ipc_ctrl, &msg));
            int event = get_event(msg.scalar);
            switch (event) {
            case CREATE: {
                int idx = forked_create_process(factory, &msg);

                crm_ipc_msg_t process_id_msg = { .scalar = idx };
                ASSERT(factory->ipc_evt->send_msg(factory->ipc_evt, &process_id_msg));
                free(msg.data);
            }
            break;
            case DISPOSE:
                ASSERT(msg.data_size == 0 && !msg.data);
                stopping = true;
                for (int i = 0; i < factory->nb_processes; i++) {
                    if (factory->processes[i].pid > 0) {
                        kill(factory->processes[i].pid, SIGKILL);
                        LOGD("process {id[%d],pid[%d]} is killed", i, factory->processes[i].pid);
                    }
                }
                break;
            case DEAD:
                ASSERT(msg.data_size == 0 && !msg.data);
                pid_t pid;
                int status;
                while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                    for (int i = 0; i < factory->nb_processes; i++) {
                        if (factory->processes[i].pid == pid) {
                            crm_process_t *p = &factory->processes[i];
                            LOGD("process {id[%d],pid[%d]} is stopped", i, p->pid);

                            if ((p->events & (1u << KILL)) || (p->events & (1u << CLEAN))) {
                                forked_flush_ipcs(p);
                                p->pid = -1;
                            } else if (WIFSIGNALED(status)) {
                                g_factory = NULL;
                                kill_all(factory);
                            } else {
                                p->events |= (1u << DEAD);
                            }
                            break;
                        }
                    }
                }
                break;
            case CLEAN: {
                ASSERT(msg.data_size == 0 && !msg.data);
                int idx = get_id(msg.scalar);
                ASSERT(idx >= 0 && idx < factory->nb_processes);
                ASSERT(factory->processes[idx].pid > 0);
                if (factory->processes[idx].events & (1u << DEAD)) {
                    forked_flush_ipcs(&factory->processes[idx]);
                    factory->processes[idx].pid = -1;
                } else {
                    factory->processes[idx].events |= (1u << CLEAN);
                }
            }
            break;
            case KILL: {
                ASSERT(msg.data_size == 0 && !msg.data);
                int idx = get_id(msg.scalar);
                ASSERT(idx >= 0 && idx < factory->nb_processes);
                if (factory->processes[idx].pid > 0) {
                    if (factory->processes[idx].events & (1u << DEAD)) {
                        forked_flush_ipcs(&factory->processes[idx]);
                        factory->processes[idx].pid = -1;
                    } else {
                        LOGD("process {id[%d],pid[%d]} is killed", idx,
                             factory->processes[idx].pid);
                        factory->processes[idx].events |= (1u << KILL);
                        kill(factory->processes[idx].pid, SIGKILL);
                    }
                }
            }
            break;
            default:
                DASSERT(0, "command %d not supported", event);
            }
        } else {
            DASSERT(0, "event err: %d", err);
        }

        ASSERT(pthread_mutex_unlock(&factory->lock) == 0);
        if (stopping && running_process(factory) == 0)
            break;
    }

    free_memory(factory);
}

/**
 * ================================================================================================
 * END of forked section
 * ================================================================================================
 */

/**
 * @see process_factory.h
 */
static int create(crm_process_factory_ctx_t *ctx, const char *plugin_name, void *data,
                  size_t data_len)
{
    crm_process_factory_ctx_internal_t *factory = (crm_process_factory_ctx_internal_t *)ctx;

    ASSERT(factory);
    ASSERT(plugin_name);

    ASSERT(!((data == NULL) ^ (data_len == 0)));

    LOGD("->%s(%s)", __FUNCTION__, plugin_name);

    ASSERT(pthread_mutex_lock(&factory->lock) == 0);

    int buffer_size = strlen(plugin_name) + data_len + 1;
    char *buffer = malloc(sizeof(char) * buffer_size);
    int len = snprintf(buffer, strlen(plugin_name) + 1, "%s", plugin_name);
    ASSERT(len > 0 && len < buffer_size);
    memcpy(buffer + len + 1, data, data_len);
    buffer[len] = ';';

    crm_ipc_msg_t msg = { gen_scalar(CREATE, 0), buffer_size, buffer };
    ASSERT(factory->ipc_ctrl->send_msg(factory->ipc_ctrl, &msg));
    free(buffer);

    struct pollfd pfd = { .fd = factory->ipc_evt->get_poll_fd(factory->ipc_evt), .events = POLLIN };
    int err = poll(&pfd, 1, 30000);
    DASSERT(err > 0, "err=%d", err);

    factory->ipc_evt->get_msg(factory->ipc_evt, &msg);
    ASSERT(pthread_mutex_unlock(&factory->lock) == 0);

    return msg.scalar;
}

/**
 * @see process_factory.h
 */
static void clean(crm_process_factory_ctx_t *ctx, int idx)
{
    crm_process_factory_ctx_internal_t *factory = (crm_process_factory_ctx_internal_t *)ctx;

    ASSERT(factory);
    ASSERT(idx >= 0 && idx < factory->nb_processes);

    LOGD("->%s(id[%d])", __FUNCTION__, idx);

    crm_ipc_msg_t msg = { .scalar = gen_scalar(CLEAN, idx) };
    ASSERT(factory->ipc_ctrl->send_msg(factory->ipc_ctrl, &msg));
}

/**
 * @see process_factory.h
 */
static void kill_process(crm_process_factory_ctx_t *ctx, int idx)
{
    crm_process_factory_ctx_internal_t *factory = (crm_process_factory_ctx_internal_t *)ctx;

    ASSERT(factory);
    ASSERT(idx >= 0 && idx < factory->nb_processes);

    LOGD("->%s(id[%d])", __FUNCTION__, idx);

    crm_ipc_msg_t msg = { .scalar = gen_scalar(KILL, idx) };
    ASSERT(factory->ipc_ctrl->send_msg(factory->ipc_ctrl, &msg));
}

/**
 * @see process_factory.h
 */
static int get_poll_fd(crm_process_factory_ctx_t *ctx, int idx)
{
    crm_process_factory_ctx_internal_t *factory = (crm_process_factory_ctx_internal_t *)ctx;

    ASSERT(factory);
    ASSERT(idx >= 0 && idx < factory->nb_processes);

    return factory->processes[idx].ipc_c2p->get_poll_fd(factory->processes[idx].ipc_c2p);
}

/**
 * @see process_factory.h
 */
static bool get_msg(crm_process_factory_ctx_t *ctx, int idx, crm_ipc_msg_t *msg)
{
    crm_process_factory_ctx_internal_t *factory = (crm_process_factory_ctx_internal_t *)ctx;

    ASSERT(factory);
    ASSERT(idx >= 0 && idx < factory->nb_processes);
    ASSERT(msg);

    crm_ipc_ctx_t *ipc = factory->processes[idx].ipc_c2p;
    return ipc->get_msg(ipc, msg);
}

/**
 * @see process_factory.h
 */
static bool send_msg(crm_process_factory_ctx_t *ctx, int idx, const crm_ipc_msg_t *msg)
{
    crm_process_factory_ctx_internal_t *factory = (crm_process_factory_ctx_internal_t *)ctx;

    ASSERT(factory);
    ASSERT(idx >= 0 && idx < factory->nb_processes);
    ASSERT(msg);

    crm_ipc_ctx_t *ipc = factory->processes[idx].ipc_p2c;
    return ipc->send_msg(ipc, msg);
}

/**
 * @see process_factory.h
 */
void dispose(crm_process_factory_ctx_t *ctx)
{
    crm_process_factory_ctx_internal_t *factory = (crm_process_factory_ctx_internal_t *)ctx;

    ASSERT(factory);

    crm_ipc_msg_t msg = { .scalar = gen_scalar(DISPOSE, 0) };
    factory->ipc_ctrl->send_msg(factory->ipc_ctrl, &msg);

    if (factory->pid_self != 0) {
        LOGD("waiting for factory process termination. pid[%d]", factory->pid_self);
        int status;
        pid_t pid;
        while ((pid = waitpid(factory->pid_self, &status, 0)) != factory->pid_self)
            if ((pid < 0) && (errno == ECHILD))
                break;
    }

    free_memory(factory);
}

/**
 * @see process_factory.h
 */
crm_process_factory_ctx_t *crm_process_factory_init(int nb)
{
    crm_process_factory_ctx_internal_t *factory = calloc(1, sizeof(*factory));

    ASSERT(factory);
    ASSERT(pthread_mutex_init(&factory->lock, NULL) == 0);

    factory->ctx.dispose = dispose;
    factory->ctx.create = create;
    factory->ctx.clean = clean;
    factory->ctx.kill = kill_process;
    factory->ctx.get_poll_fd = get_poll_fd;
    factory->ctx.send_msg = send_msg;
    factory->ctx.get_msg = get_msg;

    factory->nb_processes = nb;
    factory->processes = calloc(nb, sizeof(crm_process_t));
    ASSERT(factory->processes);

    factory->ipc_ctrl = crm_ipc_init(CRM_IPC_PROCESS);
    factory->ipc_evt = crm_ipc_init(CRM_IPC_PROCESS);
    ASSERT(factory->ipc_ctrl);
    ASSERT(factory->ipc_evt);

    for (int i = 0; i < nb; i++) {
        factory->processes[i].pid = -1;

        factory->processes[i].ipc_p2c = crm_ipc_init(CRM_IPC_PROCESS);
        factory->processes[i].ipc_c2p = crm_ipc_init(CRM_IPC_PROCESS);
        ASSERT(factory->processes[i].ipc_p2c);
        ASSERT(factory->processes[i].ipc_c2p);
    }

    factory->pid_parent = getpid();
    factory->pid_self = fork();
    ASSERT(factory->pid_self >= 0);

    if (!factory->pid_self) {
        LOGV("context %p", factory);

        g_factory = factory;

        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = sig_handler;
        ASSERT(!sigaction(SIGCHLD, &sa, NULL));
        ASSERT(!sigaction(SIGABRT, &sa, NULL));
        ASSERT(!sigaction(SIGTERM, &sa, NULL));

        forked_handle_events(factory);
        exit(0);
    }

    return &factory->ctx;
}
