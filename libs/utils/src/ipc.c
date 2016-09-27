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
#include <string.h>
#include <unistd.h>

#define CRM_MODULE_TAG "IPC"
#include "utils/common.h"
#include "utils/ipc.h"

#define MSG_QUEUE_SIZE 8

typedef struct crm_ipc_ctx_internal {
    crm_ipc_ctx_t ctx; // Needs to be first

    /* Internal variables */
    pthread_mutex_t lock;
    pthread_mutex_t lock_w;
    int r_fd;
    int w_fd;

    crm_ipc_msg_t msg_queue[MSG_QUEUE_SIZE];
    int num_msgs_in_queue;
    int msg_r_idx;
    int msg_w_idx;
} crm_ipc_ctx_internal_t;

/**
 * @see ipc.h
 */
static void dispose(crm_ipc_ctx_t *ctx, void (*free_routine)(const crm_ipc_msg_t *))
{
    ASSERT(ctx != NULL);
    crm_ipc_ctx_internal_t *i_ctx = (crm_ipc_ctx_internal_t *)ctx;

    close(i_ctx->r_fd);
    close(i_ctx->w_fd);
    i_ctx->r_fd = -1;
    i_ctx->w_fd = -1;

    if (free_routine) {
        while (i_ctx->num_msgs_in_queue) {
            free_routine(&i_ctx->msg_queue[i_ctx->msg_r_idx]);
            i_ctx->msg_r_idx += 1;
            if (i_ctx->msg_r_idx == MSG_QUEUE_SIZE)
                i_ctx->msg_r_idx = 0;
            i_ctx->num_msgs_in_queue -= 1;
        }
    }
    free(i_ctx);
}

/**
 * @see ipc.h
 */
static int get_poll_fd(crm_ipc_ctx_t *ctx)
{
    ASSERT(ctx != NULL);
    crm_ipc_ctx_internal_t *i_ctx = (crm_ipc_ctx_internal_t *)ctx;

    return i_ctx->r_fd;
}

/**
 * @see ipc.h
 */
static bool get_msg_process(crm_ipc_ctx_t *ctx, crm_ipc_msg_t *msg)
{
    crm_ipc_ctx_internal_t *i_ctx = (crm_ipc_ctx_internal_t *)ctx;

    ASSERT(i_ctx);
    ASSERT(msg);

    ASSERT(pthread_mutex_lock(&i_ctx->lock) == 0);
    int msg_hdr[2];
    ASSERT(read(i_ctx->r_fd, &msg_hdr, sizeof(msg_hdr)) == sizeof(msg_hdr));
    msg->scalar = msg_hdr[0];
    msg->data_size = msg_hdr[1];
    if (msg->data_size > 0) {
        msg->data = malloc(msg->data_size);
        ASSERT(msg->data);
        ASSERT(read(i_ctx->r_fd, msg->data, msg->data_size) == (int)msg->data_size);
    } else {
        msg->data = NULL;
    }
    ASSERT(pthread_mutex_unlock(&i_ctx->lock) == 0);

    return true;
}

/**
 * @see ipc.h
 */
static bool send_msg_process(crm_ipc_ctx_t *ctx, const crm_ipc_msg_t *msg)
{
    crm_ipc_ctx_internal_t *i_ctx = (crm_ipc_ctx_internal_t *)ctx;

    ASSERT(i_ctx);
    ASSERT(msg);

    int msg_hdr[2] = { msg->scalar, msg->data_size };

    ASSERT(pthread_mutex_lock(&i_ctx->lock_w) == 0);
    ASSERT(write(i_ctx->w_fd, msg_hdr, sizeof(msg_hdr)) == sizeof(msg_hdr));
    if (msg->data_size > 0)
        ASSERT(write(i_ctx->w_fd, msg->data, msg->data_size) == (int)msg->data_size);
    ASSERT(pthread_mutex_unlock(&i_ctx->lock_w) == 0);

    return true;
}

/**
 * @see ipc.h
 */
static bool get_msg_thread(crm_ipc_ctx_t *ctx, crm_ipc_msg_t *msg)
{
    ASSERT(ctx != NULL);
    crm_ipc_ctx_internal_t *i_ctx = (crm_ipc_ctx_internal_t *)ctx;

    bool ret = false;

    ASSERT(pthread_mutex_lock(&i_ctx->lock) == 0);
    if ((i_ctx->num_msgs_in_queue > 0) && (i_ctx->r_fd != -1)) {
        ret = true;
        *msg = i_ctx->msg_queue[i_ctx->msg_r_idx];

        char dummy;
        ASSERT(read(i_ctx->r_fd, &dummy, sizeof(dummy)) == sizeof(dummy));

        i_ctx->num_msgs_in_queue -= 1;
        i_ctx->msg_r_idx += 1;
        if (i_ctx->msg_r_idx == MSG_QUEUE_SIZE)
            i_ctx->msg_r_idx = 0;
    }
    ASSERT(pthread_mutex_unlock(&i_ctx->lock) == 0);

    return ret;
}

/**
 * @see ipc.h
 */
static bool send_msg_thread(crm_ipc_ctx_t *ctx, const crm_ipc_msg_t *msg)
{
    ASSERT(ctx != NULL);
    crm_ipc_ctx_internal_t *i_ctx = (crm_ipc_ctx_internal_t *)ctx;

    bool ret = false;

    ASSERT(pthread_mutex_lock(&i_ctx->lock) == 0);
    if ((i_ctx->num_msgs_in_queue < MSG_QUEUE_SIZE) && (i_ctx->w_fd != -1)) {
        ret = true;
        i_ctx->msg_queue[i_ctx->msg_w_idx] = *msg;

        static char dummy;
        ASSERT(write(i_ctx->w_fd, &dummy, sizeof(dummy)) == sizeof(dummy));
        dummy += 1; // Can be useful for debugging to tag messages

        i_ctx->num_msgs_in_queue += 1;
        i_ctx->msg_w_idx += 1;
        if (i_ctx->msg_w_idx == MSG_QUEUE_SIZE)
            i_ctx->msg_w_idx = 0;
    }
    ASSERT(pthread_mutex_unlock(&i_ctx->lock) == 0);

    return ret;
}

/**
 * @see ipc.h
 */
crm_ipc_ctx_t *crm_ipc_init(crm_ipc_type_t type)
{
    crm_ipc_ctx_internal_t *i_ctx = calloc(1, sizeof(*i_ctx));

    ASSERT(i_ctx != NULL);
    ASSERT(pthread_mutex_init(&i_ctx->lock, NULL) == 0);

    int pipes[2];
    ASSERT(pipe(pipes) == 0);
    i_ctx->r_fd = pipes[0];
    i_ctx->w_fd = pipes[1];
    i_ctx->num_msgs_in_queue = 0;
    i_ctx->msg_r_idx = 0;
    i_ctx->msg_w_idx = 0;

    if (CRM_IPC_THREAD == type) {
        i_ctx->ctx.get_msg = get_msg_thread;
        i_ctx->ctx.send_msg = send_msg_thread;
    } else if (CRM_IPC_PROCESS == type) {
        ASSERT(pthread_mutex_init(&i_ctx->lock_w, NULL) == 0);
        i_ctx->ctx.get_msg = get_msg_process;
        i_ctx->ctx.send_msg = send_msg_process;
    } else {
        ASSERT(0);
    }

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.get_poll_fd = get_poll_fd;

    return &i_ctx->ctx;
}
