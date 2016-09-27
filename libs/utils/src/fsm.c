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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRM_MODULE_TAG "FSM"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/fsm.h"

#define MAX_TAG_LEN 5
#define PRINT_FSM "<CRM%-" xstr(MAX_TAG_LEN) "s>"
#define PRINT_EVENT "[%-20s]"
#define PRINT_STATE "{%-16s}"


typedef struct crm_fsm_ctx_internal {
    crm_fsm_ctx_t ctx; //Needs to be first

    int state;

    void (*pre_op)(int event, void *fsm_param);
    void (*state_trans)(int prev_state, int new_state, int event, void *fsm_param, void *evt_param);
    int (*failsafe)(void *fsm_param, void *evt_param);

    const crm_fsm_ops_t *fsm;
    int num_states;
    int num_events;

    const char *logging_tag;
    void *fsm_param;
    const char *(*get_state_txt)(int);
    const char *(*get_event_txt)(int);
} crm_fsm_ctx_internal_t;

static inline void check_state(crm_fsm_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx->state >= 0);
    ASSERT(i_ctx->state < i_ctx->num_states);
}

/**
 * @see fsm.h
 */
static void notify_event(crm_fsm_ctx_t *ctx, int evt, void *evt_param)
{
    crm_fsm_ctx_internal_t *i_ctx = (crm_fsm_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);
    ASSERT(evt < i_ctx->num_events);

    bool error = false;
    int new_state = i_ctx->state;
    int idx = i_ctx->state + (evt * i_ctx->num_states);
    const crm_fsm_ops_t *op = i_ctx->fsm + idx;

    LOGD(PRINT_FSM " =IN=  " PRINT_EVENT " " PRINT_STATE, i_ctx->logging_tag,
         i_ctx->get_event_txt(evt), i_ctx->get_state_txt(i_ctx->state));

    if (i_ctx->pre_op)
        i_ctx->pre_op(evt, i_ctx->fsm_param);

    if (op->operation) {
        new_state = op->operation(i_ctx->fsm_param, evt_param);
        if (new_state == -1) {
            // keep current state
            new_state = i_ctx->state;
        } else if (new_state <= -2) {
            LOGE("%s - Error detected. Failsafe operation", i_ctx->logging_tag);
            new_state = i_ctx->failsafe(i_ctx->fsm_param, evt_param);
            error = true;
        }
    }

    if (!error && (-1 != op->force_new_state)) {
        LOGD("%s - State forced", i_ctx->logging_tag);
        new_state = op->force_new_state;
    }

    LOGI(PRINT_FSM " =OUT= " PRINT_EVENT " " PRINT_STATE " => " PRINT_STATE,
         i_ctx->logging_tag,
         i_ctx->get_event_txt(evt),
         i_ctx->get_state_txt(i_ctx->state),
         i_ctx->get_state_txt(new_state));

    if (i_ctx->state != new_state)
        if (i_ctx->state_trans)
            i_ctx->state_trans(i_ctx->state, new_state, evt, i_ctx->fsm_param, evt_param);

    i_ctx->state = new_state;
    check_state(i_ctx);
}

/**
 * @see fsm.h
 */
static void dispose(crm_fsm_ctx_t *ctx)
{
    crm_fsm_ctx_internal_t *i_ctx = (crm_fsm_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    free(i_ctx);
}

/**
 * @see fsm.h
 */
crm_fsm_ctx_t *crm_fsm_init(const crm_fsm_ops_t *fsm, int num_events, int num_states,
                            int initial_state,
                            void (*pre_op)(int event, void *fsm_param),
                            void (*state_trans)(int prev_state, int new_state, int event,
                                                void *fsm_param, void *evt_param),
                            int (*failsafe_op)(void *fsm_param, void *evt_param),
                            void *fsm_param, const char *logging_tag,
                            const char *(*get_state_txt)(int), const char *(*get_event_txt)(int))
{
    ASSERT(fsm_param != NULL);
    ASSERT(fsm != NULL);
    ASSERT(logging_tag != NULL);
    ASSERT(failsafe_op != NULL);
    ASSERT(get_state_txt != NULL);
    ASSERT(get_event_txt != NULL);

    crm_fsm_ctx_internal_t *i_ctx = calloc(1, sizeof(crm_fsm_ctx_internal_t));
    ASSERT(i_ctx != NULL);

    i_ctx->fsm = fsm;
    i_ctx->num_events = num_events;
    i_ctx->num_states = num_states;
    i_ctx->failsafe = failsafe_op;
    i_ctx->pre_op = pre_op;
    i_ctx->state_trans = state_trans;
    i_ctx->logging_tag = logging_tag;
    ASSERT(strlen(logging_tag) < MAX_TAG_LEN);

    i_ctx->state = initial_state;
    i_ctx->fsm_param = fsm_param;

    i_ctx->get_state_txt = get_state_txt;
    i_ctx->get_event_txt = get_event_txt;

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.notify_event = notify_event;

    check_state(i_ctx);

    return &i_ctx->ctx;
}
