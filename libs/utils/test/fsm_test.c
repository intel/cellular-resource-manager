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

#define CRM_MODULE_TAG "FSMT"
#include "utils/common.h"
#include "utils/fsm.h"

typedef enum states {
    ST_DOWN,
    ST_STARTING,
    ST_DUMPING,
    ST_ON,
    ST_NUM,
} states_t;

typedef enum events {
    EV_OFF,
    EV_STARTING,
    EV_DUMPING,
    EV_ON,
    EV_NUM
} events_t;

typedef struct fsm_test_ctx {
    int test;
} fsm_test_ctx_t;

static int off(void *fsm_param, void *evt_param)
{
    (void)fsm_param; /* unused */
    (void)evt_param; /* unused */

    LOGD("off...");
    return ST_DOWN;
}

static int starting(void *fsm_param, void *evt_param)
{
    (void)fsm_param; /* unused */
    (void)evt_param; /* unused */

    LOGD("starting...");
    return ST_STARTING;
}

static int dump(void *fsm_param, void *evt_param)
{
    (void)fsm_param; /* unused */
    (void)evt_param; /* unused */

    LOGD("dumping...");
    //return ST_DUMPING;
    return -2;
}

static int on(void *fsm_param, void *evt_param)
{
    (void)fsm_param; /* unused */
    (void)evt_param; /* unused */

    LOGD("declaring UP...");
    return ST_ON;
}

static const char *get_event_txt(int evt)
{
    switch (evt) {
    case EV_OFF:      return "event off";
    case EV_STARTING: return "event starting";
    case EV_DUMPING:  return "event dumping";
    case EV_ON:       return "event on";
    default: ASSERT(0);
    }
}

static const char *get_state_txt(int evt)
{
    switch (evt) {
    case ST_DOWN:     return "OFF";
    case ST_STARTING: return "STARTING";
    case ST_DUMPING:  return "DUMPING";
    case ST_ON:       return "ON";
    default: ASSERT(0);
    }
}

static void state_trans(int prev_state, int new_state, int evt, void *fsm_param, void *evt_param)
{
    (void)new_state;
    (void)fsm_param;
    (void)evt_param;
    LOGD("Exit state: %s, evt: %s", get_state_txt(prev_state), get_event_txt(evt));
}

/* *INDENT-OFF* */
static const crm_fsm_ops_t fsm[EV_NUM * ST_NUM] = {
                   /* ST_DOWN */   /* ST_STARTING */  /* ST_DUMPING */  /* ST_ON */
/* EV_OFF */       {-1, NULL},      {-1, off},        {-1, off},        {-1, off},
/* EV_STARTING */  {-1, starting},  {-1, NULL},       {-1, NULL},       {-1, NULL},
/* EV_DUMPING */   {-1, dump},      {-1, dump},       {-1, dump},       {-1, dump},
/* EV_ON */        {-1, off},       {-1, on},         {-1, on},         {-1, NULL},
};
/* *INDENT-ON* */

static void test_mdm_up(crm_fsm_ctx_t *fsm_ctx)
{
    ASSERT(fsm_ctx != NULL);

    events_t evts[] = { EV_OFF, EV_STARTING, EV_ON, EV_DUMPING, EV_OFF, EV_STARTING, EV_ON };

    for (size_t i = 0; i < ARRAY_SIZE(evts); i++) {
        LOGD("\n");
        LOGD("Sending event %%%zu: %s", i, get_event_txt(evts[i]));
        fsm_ctx->notify_event(fsm_ctx, evts[i], NULL);
    }
}

int main()
{
    fsm_test_ctx_t ctx;
    crm_fsm_ctx_t *fsm_ctx = crm_fsm_init(fsm, EV_NUM, ST_NUM, ST_DOWN, NULL, state_trans, off,
                                          &ctx, CRM_MODULE_TAG, get_state_txt, get_event_txt);

    test_mdm_up(fsm_ctx);

    fsm_ctx->dispose(fsm_ctx);

    LOGD("\n");
    LOGD("done");
    return 0;
}
