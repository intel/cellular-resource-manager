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

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define CRM_MODULE_TAG "THD"
#include "utils/common.h"
#include "utils/thread.h"
#include "utils/ipc.h"

typedef struct crm_thread_ctx_internal {
    crm_thread_ctx_t ctx; // Needs to be first

    /* Internal variables */
    pthread_mutex_t lock;
    pthread_t thread;
    pthread_t parent;
    bool detached;

    void *(*start_routine)(crm_thread_ctx_t *, void *);
    void *arg;

    crm_ipc_ctx_t *ipc[2];
} crm_thread_ctx_internal_t;

#define CHILD_IDX 0
#define PARENT_IDX 1
#define OTHER_IDX 2

static void *internal_start_routine(void *arg)
{
    crm_thread_ctx_internal_t *i_ctx = arg;

    return i_ctx->start_routine(&i_ctx->ctx, i_ctx->arg);
}

static int get_idx(crm_thread_ctx_internal_t *i_ctx)
{
    pthread_t self = pthread_self();

    if (pthread_equal(self, i_ctx->thread))
        return CHILD_IDX;   // called from child thread
    else if (pthread_equal(self, i_ctx->parent))
        return PARENT_IDX;  // called from parent
    else
        return OTHER_IDX;   // called from any other thread
}

/**
 * @see ipc.h
 */
static void dispose(crm_thread_ctx_t *ctx, void (*free_routine)(const crm_ipc_msg_t *))
{
    ASSERT(ctx != NULL);
    crm_thread_ctx_internal_t *i_ctx = (crm_thread_ctx_internal_t *)ctx;

    if (i_ctx->detached)
        ASSERT(get_idx(i_ctx) == CHILD_IDX);
    else
        ASSERT(get_idx(i_ctx) != CHILD_IDX);

    ASSERT(pthread_mutex_lock(&i_ctx->lock) == 0);
    for (size_t i = 0; i < ARRAY_SIZE(i_ctx->ipc); i++) {
        if (i_ctx->ipc[i])
            i_ctx->ipc[i]->dispose(i_ctx->ipc[i], free_routine);
        i_ctx->ipc[i] = NULL;
    }
    ASSERT(pthread_mutex_unlock(&i_ctx->lock) == 0);

    if (!i_ctx->detached)
        pthread_join(i_ctx->thread, NULL);

    pthread_mutex_destroy(&i_ctx->lock);

    free(i_ctx);
}

/**
 * @see ipc.h
 */
static int get_poll_fd(crm_thread_ctx_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_thread_ctx_internal_t *i_ctx = (crm_thread_ctx_internal_t *)ctx;

    int idx = get_idx(i_ctx);
    ASSERT(idx != OTHER_IDX);

    if (i_ctx->ipc[idx] != NULL)
        return i_ctx->ipc[idx]->get_poll_fd(i_ctx->ipc[idx]);
    else
        return -1;
}

/**
 * @see ipc.h
 */
static bool get_msg(crm_thread_ctx_t *ctx, crm_ipc_msg_t *msg)
{
    ASSERT(ctx != NULL);
    crm_thread_ctx_internal_t *i_ctx = (crm_thread_ctx_internal_t *)ctx;

    /* The child (idx 0) will read from index 0 of the ipc array (opposite for parent) */
    int idx = get_idx(i_ctx);
    ASSERT(idx != OTHER_IDX);
    bool ret = false;

    ASSERT(pthread_mutex_lock(&i_ctx->lock) == 0);
    if (i_ctx->ipc[idx] != NULL)
        ret = i_ctx->ipc[idx]->get_msg(i_ctx->ipc[idx], msg);
    ASSERT(pthread_mutex_unlock(&i_ctx->lock) == 0);

    return ret;
}

/**
 * @see ipc.h
 */
static bool send_msg(crm_thread_ctx_t *ctx, const crm_ipc_msg_t *msg)
{
    ASSERT(ctx != NULL);
    crm_thread_ctx_internal_t *i_ctx = (crm_thread_ctx_internal_t *)ctx;

    /* The parent (idx 1) will write in index 0 of the ipc array (opposite for child) */
    int idx = get_idx(i_ctx);
    ASSERT(idx != OTHER_IDX);
    idx = 1 - idx;
    bool ret = false;

    ASSERT(pthread_mutex_lock(&i_ctx->lock) == 0);
    if (i_ctx->ipc[idx] != NULL)
        ret = i_ctx->ipc[idx]->send_msg(i_ctx->ipc[idx], msg);
    ASSERT(pthread_mutex_unlock(&i_ctx->lock) == 0);

    return ret;
}

/**
 * @see ipc.h
 */
crm_thread_ctx_t *crm_thread_init(void *(*start_routine)(crm_thread_ctx_t *, void *),
                                  void *arg, bool create_ipc, bool detached)
{
    crm_thread_ctx_internal_t *i_ctx = calloc(1, sizeof(*i_ctx));

    ASSERT(i_ctx != NULL);
    ASSERT(pthread_mutex_init(&i_ctx->lock, NULL) == 0);

    i_ctx->parent = pthread_self();

    if (create_ipc)
        for (size_t i = 0; i < ARRAY_SIZE(i_ctx->ipc); i++)
            i_ctx->ipc[i] = crm_ipc_init(CRM_IPC_THREAD);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.get_poll_fd = get_poll_fd;
    i_ctx->ctx.get_msg = get_msg;
    i_ctx->ctx.send_msg = send_msg;

    i_ctx->start_routine = start_routine;
    i_ctx->arg = arg;
    i_ctx->detached = detached;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (detached)
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    else
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ASSERT(pthread_create(&i_ctx->thread, &attr, internal_start_routine, i_ctx) == 0);
    pthread_attr_destroy(&attr);

    return &i_ctx->ctx;
}
