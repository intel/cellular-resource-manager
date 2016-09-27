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
#include <stdio.h>
#include <unistd.h>

#define CRM_MODULE_TAG "HAL"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/file.h"
#include "utils/thread.h"
#include "utils/fsm.h"
#include "utils/keys.h"
#include "utils/property.h"
#include "utils/time.h"

#include "common.h"
#include "fsm_hal.h"
#include "modem.h"
#include "rpcd.h"

#include "libmdmcli/mdm_cli_dbg.h"

typedef enum hal_sofia_states {
    ST_INITIAL,
    ST_OFF,
    ST_BOOTING,
    ST_PINGING,
    ST_WAITING_RPC,
    ST_RUN,
    ST_STOPPING,
    ST_NEED_RESETTING,
    ST_NUM,
} hal_sofia_states_t;

static const char *get_event_txt(int evt)
{
    switch (evt) {
    case EV_POWER:         return "REQ: power";
    case EV_BOOT:          return "REQ: boot";
    case EV_STOP:          return "REQ: stop";
    case EV_RESET:         return "REQ: reset";

    case EV_MDM_OFF:       return "MDM: off";
    case EV_MDM_ON:        return "MDM: on";
    case EV_MDM_TRAP:      return "MDM: trap";
    case EV_MDM_RUN:       return "MDM: run";
    case EV_MDM_FW_FAIL:   return "MDM: bad firmware";
    case EV_TIMEOUT:       return "TIMEOUT";

    case EV_RPC_DEAD:      return "RPC: dead";
    case EV_RPC_RUN:       return "RPC: run";

    default: ASSERT(0);
    }
}

static const char *get_state_txt(int state)
{
    switch (state) {
    case ST_INITIAL:        return "INITIAL";
    case ST_OFF:            return "OFF";
    case ST_BOOTING:        return "BOOTING";
    case ST_PINGING:        return "PINGING";
    case ST_WAITING_RPC:    return "WAIT RPC";
    case ST_RUN:            return "RUN";
    case ST_STOPPING:       return "STOPPING";
    case ST_NEED_RESETTING: return "NEED RESET";
    default: ASSERT(0);
    }
}

static inline void notify_ctrl_event_only(crm_ctrl_ctx_t *control, crm_hal_evt_type_t evt)
{
    crm_hal_evt_t event = { evt, "", NULL };

    control->notify_hal_event(control, &event);
}

static void timer_start(crm_hal_ctx_internal_t *i_ctx, int ms)
{
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->timer_armed == false);

    i_ctx->timer_armed = true;
    crm_time_add_ms(&i_ctx->timer_end, ms);
}

static int get_timeout(crm_hal_ctx_internal_t *i_ctx)
{
    if (i_ctx->timer_armed == false)
        return -1;

    return crm_time_get_remain_ms(&i_ctx->timer_end);
}

static int notify_run(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param; // UNUSED

    crm_hal_evt_t event = { HAL_MDM_RUN, "", NULL };
    i_ctx->control->notify_hal_event(i_ctx->control, &event);
    char value[CRM_PROPERTY_VALUE_MAX];
    crm_property_get(CRM_KEY_NET_DEVICE_STATE, value, "");

    if (*value != '1') {
        LOGD("setting NET device state to registered");
        crm_property_set(CRM_KEY_NET_DEVICE_STATE, "1");
    }

    return ST_RUN;
}

static int notify_flash(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param; // UNUSED

    notify_ctrl_event_only(i_ctx->control, HAL_MDM_BUSY);

    crm_hal_evt_t event = { HAL_MDM_FLASH, "", NULL };
    snprintf(event.nodes, sizeof(event.nodes), "%s", i_ctx->flash_node);
    i_ctx->control->notify_hal_event(i_ctx->control, &event);

    return ST_OFF;
}

static int notify_off(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param; // UNUSED

    notify_ctrl_event_only(i_ctx->control, HAL_MDM_BUSY);
    notify_ctrl_event_only(i_ctx->control, HAL_MDM_OFF);

    return ST_OFF;
}

static int notify_off_only(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param; // UNUSED

    notify_ctrl_event_only(i_ctx->control, HAL_MDM_OFF);

    return ST_OFF;
}

static int notify_off_or_flash(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->mdm_state == EV_MDM_OFF);

    if (i_ctx->stopping) {
        i_ctx->stopping = false;
        return notify_off(fsm_param, evt_param);
    } else {
        return notify_flash(fsm_param, evt_param);
    }
}

static int notify_need_reset(crm_hal_ctx_internal_t *i_ctx, mdm_cli_dbg_info_t *dbg_info)
{
    ASSERT(i_ctx);
    ASSERT(dbg_info);

    notify_ctrl_event_only(i_ctx->control, HAL_MDM_BUSY);

    crm_hal_evt_t event = { HAL_MDM_NEED_RESET, "", dbg_info };
    i_ctx->control->notify_hal_event(i_ctx->control, &event);

    if (EV_MDM_OFF == i_ctx->mdm_state)
        return ST_OFF;
    else
        return ST_NEED_RESETTING;
}

static int self_reset(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param; // UNUSED

    /* This function is called when an unexpected modem off event coming from VMODEM is received.
     * In that case, CRM requests a modem shutdown to properly shutdown the VM. Otherwise, modem
     * behavior is not guaranteed at next boot: no answer to PING request, camp issue, etc.
     * This shutdown request will not trig another modem off event, that is why the timer is not
     * armed here */
    crm_hal_stop_modem(i_ctx);

    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_SELF_RESET, DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_NO_LOG,
                                    DBG_DEFAULT_NO_LOG, 0, NULL };
    return notify_need_reset(i_ctx, &dbg_info);
}

static int notify_trap_evt(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param; // UNUSED

    if (i_ctx->dump_enabled) {
        notify_ctrl_event_only(i_ctx->control, HAL_MDM_BUSY);

        crm_hal_evt_t event = { HAL_MDM_DUMP, "", NULL };
        snprintf(event.nodes, sizeof(event.nodes), "%s", i_ctx->dump_node);
        i_ctx->control->notify_hal_event(i_ctx->control, &event);

        return ST_NEED_RESETTING;
    } else {
        /* notify a self-reset if core dump feature is disabled */
        mdm_cli_dbg_info_t dbg = { DBG_TYPE_SELF_RESET, DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_NO_LOG,
                                   DBG_DEFAULT_NO_LOG, 0, NULL };
        return notify_need_reset(i_ctx, &dbg);
    }
}

static int notify_fw_failure(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    (void)evt_param; //UNUSED
    ASSERT(i_ctx != NULL);

    notify_ctrl_event_only(i_ctx->control, HAL_MDM_BUSY);

    const char *data[] = { "firmware is corrupted" };
    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_FW_FAILURE, DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_NO_LOG,
                                    DBG_DEFAULT_NO_LOG, ARRAY_SIZE(data), data };
    crm_hal_evt_t event = { HAL_MDM_UNRESPONSIVE, "", &dbg_info };
    i_ctx->control->notify_hal_event(i_ctx->control, &event);

    return ST_OFF;
}

static int notify_rpc_dead(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    const char *data[] = { "RPC dead" };
    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_ERROR, DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_NO_LOG,
                                    DBG_DEFAULT_NO_LOG, ARRAY_SIZE(data), data };
    return notify_need_reset(i_ctx, &dbg_info);
}

static int notify_timeout(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    const char *data[] = { "HAL timeout" };
    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_ERROR, DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_NO_LOG,
                                    DBG_DEFAULT_NO_LOG, ARRAY_SIZE(data), data };
    return notify_need_reset(i_ctx, &dbg_info);
}

static int stop_mdm(crm_hal_ctx_internal_t *i_ctx, bool stopping)
{
    ASSERT(i_ctx != NULL);

    if (EV_MDM_OFF == i_ctx->mdm_state) {
        return ST_OFF;
    } else if (!crm_hal_stop_modem(i_ctx)) {
        timer_start(i_ctx, TIMEOUT_MDM_OFF);
        i_ctx->stopping = stopping;
        return ST_STOPPING;
    } else {
        return -2;
    }
}

static int request_stop(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    (void)evt_param; // UNUSED
    return stop_mdm(i_ctx, true);
}

static int request_reset(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    (void)evt_param; // UNUSED
    return stop_mdm(i_ctx, false);
}

static int start_ping(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param; // UNUSED
    ASSERT(i_ctx->thread_ping == NULL);

    i_ctx->thread_ping = crm_thread_init(crm_hal_ping_modem_thread, i_ctx, true, false);

    return ST_PINGING;
}

static int request_boot(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param; // UNUSED

    if (i_ctx->secvm_flash) {
        if (i_ctx->mdm_state == EV_MDM_ON) {
            /* Modem already started before CTRL sent the 'boot' command */
            return start_ping(fsm_param, evt_param);
        } else {
            /* Otherwise, wait for the SecVM to start the modem VM */
            timer_start(i_ctx, TIMEOUT_MDM_ON);
            return ST_BOOTING;
        }
    } else {
        if (!crm_hal_start_modem(i_ctx)) {
            timer_start(i_ctx, TIMEOUT_MDM_ON);
            return ST_BOOTING;
        } else {
            return -2;
        }
    }
}

static int modem_on(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param; // UNUSED

    ASSERT(i_ctx->secvm_flash);
    return -1;
}

static int reject(void *fsm_param, void *evt_param)
{
    (void)fsm_param; // UNUSED
    (void)evt_param; // UNUSED

    DASSERT(0, "Control request during modem transition. There is an error in control FSM");
    return -2;
}

static int assert(void *fsm_param, void *evt_param)
{
    (void)fsm_param; // UNUSED
    (void)evt_param; // UNUSED
    ASSERT(0);
}

static int start_rpcd(void *fsm_param, void *evt_param)
{
    (void)evt_param; // UNUSED

    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    crm_hal_rpcd_start(i_ctx);
    timer_start(i_ctx, TIMEOUT_RPCD_START);

    return ST_WAITING_RPC;
}

static void state_trans(int prev_state, int new_state, int evt, void *fsm_param, void *evt_param)
{
    (void)evt;
    (void)evt_param;

    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;
    ASSERT(i_ctx != NULL);

    /* Handling of 'exit state' */
    switch (prev_state) {
    case ST_STOPPING:
    case ST_BOOTING:
        ASSERT(i_ctx->timer_armed == true);
        i_ctx->timer_armed = false;
        break;

    case ST_PINGING:
        if (i_ctx->thread_ping) {
            LOGD("stopping PING thread...");
            i_ctx->thread_ping->dispose(i_ctx->thread_ping, NULL);
            i_ctx->thread_ping = NULL;
        }
        break;

    case ST_RUN:
        crm_hal_rpcd_stop(i_ctx);
        break;

    case ST_WAITING_RPC:
        ASSERT(i_ctx->timer_armed == true);
        i_ctx->timer_armed = false;
        if (new_state != ST_RUN)
            crm_hal_rpcd_stop(i_ctx);
        break;
    }
}

/* Notes:
 *  = EV_RPC_DEAD is ignored in all states as they could be generated due to race conditions
 */

/* *INDENT-OFF* */
static const crm_fsm_ops_t g_fsm_array[EV_NUM * ST_NUM] = {
/*                 ST_INITIAL            ST_OFF                ST_BOOTING               ST_PINGING              ST_WAITING_RPC          ST_RUN                  ST_STOPPING               ST_NEED_RESETTING */
/*EV_POWER*/       {-1,assert},          {-1,notify_flash},    {-1,reject},             {-1,reject},            {-1,reject},            {-1,reject},            {-1,reject},              {-1,reject},
/*EV_BOOT*/        {-1,assert},          {-1,request_boot},    {-1,reject},             {-1,reject},            {-1,reject},            {-1,reject},            {-1,reject},              {-1,reject},
/*EV_STOP*/        {-1,assert},          {-1,notify_off},      {-1,reject},             {-1,reject},            {-1,reject},            {-1,request_stop},      {-1,reject},              {-1,request_stop},
/*EV_RESET*/       {-1,assert},          {-1,notify_flash},    {-1,reject},             {-1,reject},            {-1,reject},            {-1,request_reset},     {-1,reject},              {-1,request_reset},
/*EV_BACKUP*/      {-1,assert},          {-1,assert},          {-1,assert},             {-1,assert},            {-1,assert},            {-1,assert},            {-1,assert},              {-1,assert},

/*EV_MDM_OFF*/     {-1,notify_off_only}, {-1,assert},          {-1,self_reset},         {-1,self_reset},        {-1,self_reset},        {-1,self_reset},        {-1,notify_off_or_flash}, {-1,assert},
/*EV_MDM_ON*/      {-1,start_ping},      {-1,modem_on},        {-1,start_ping},         {-1,assert},            {-1,assert},            {-1,assert},            {-1,assert},              {-1,assert},
/*EV_MDM_TRAP*/    {-1,assert},          {-1,notify_trap_evt}, {-1,notify_trap_evt},    {-1,notify_trap_evt},   {-1,notify_trap_evt},   {-1,notify_trap_evt},   {-1,notify_trap_evt},     {-1,notify_trap_evt},
/*EV_MDM_FW_FAIL*/ {-1,assert},          {-1,assert},          {-1,notify_fw_failure},  {-1,assert},            {-1,assert},            {-1,assert},            {-1,assert},              {-1,assert},
/*EV_MDM_RUN*/     {-1,assert},          {-1,assert},          {-1,assert},             {-1,start_rpcd},        {-1,assert},            {-1,assert},            {-1,assert},              {-1,assert},
/*EV_TIMEOUT*/     {-1,assert},          {-1,assert},          {-1,notify_timeout},     {-1,notify_timeout},    {-1,notify_timeout},    {-1,assert},            {-1,assert},              {-1,assert},

/*EV_RPC_DEAD*/    {-1,assert},          {-1,NULL},            {-1,NULL},               {-1,NULL},              {-1,notify_rpc_dead},   {-1,notify_rpc_dead},   {-1,NULL},                {-1,NULL},
/*EV_RPC_RUN*/     {-1,assert},          {-1,assert},          {-1,assert},             {-1,assert},            {-1,notify_run},        {-1,assert},            {-1,assert},              {-1,assert},
};
/* *INDENT-ON* */


/**
 * @see fsm_hal.h
 */
void *crm_hal_sofia_fsm(crm_thread_ctx_t *thread_ctx, void *param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)param;

    (void)thread_ctx;
    ASSERT(i_ctx != NULL);

    /* For safety reasons, always restart modem (even if up) in case CRM is restarted */
    bool force_stop = true;
    char value[CRM_PROPERTY_VALUE_MAX];
    crm_property_get(CRM_KEY_FIRST_START, value, "1");
    if (*value == '1') {
        force_stop = false;
        crm_property_set(CRM_KEY_FIRST_START, "0");
    }
    crm_hal_init_modem(i_ctx, force_stop);

    /* Note: stop RPCD after having stopped modem as stopping RPCD while modem is still running
     *       can lead to a SW watchdog as modem is entering an infinite loop. */
    crm_hal_rpcd_init(i_ctx);

    crm_fsm_ctx_t *fsm = crm_fsm_init(g_fsm_array, EV_NUM, ST_NUM, ST_INITIAL, NULL, state_trans,
                                      request_stop, i_ctx, CRM_MODULE_TAG, get_state_txt,
                                      get_event_txt);

    ASSERT(i_ctx->mdm_state == EV_MDM_OFF || i_ctx->mdm_state == EV_MDM_ON);
    fsm->notify_event(fsm, i_ctx->mdm_state, NULL);

    bool running = true;
    while (running) {
        int p_fd = -1;
        if (i_ctx->thread_ping)
            p_fd = i_ctx->thread_ping->get_poll_fd(i_ctx->thread_ping);

        struct pollfd pfd[] = {
            // requests
            { .fd = i_ctx->ipc->get_poll_fd(i_ctx->ipc), .events = POLLIN },
            // UEVENT - modem event listener
            { .fd = i_ctx->s_fd, .events = POLLIN },
            // PING - modem event listener
            { .fd = p_fd, .events = POLLIN },
            // INOTIFY - RPC Daemon event listener
            { .fd = crm_hal_rpcd_get_fd(i_ctx), .events = POLLIN },
        };

        int err = poll(pfd, ARRAY_SIZE(pfd), get_timeout(i_ctx));

        for (size_t i = 0; i < ARRAY_SIZE(pfd); i++) {
            if (pfd[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                DASSERT(0, "error on fd: %zu", i);
        }

        if (0 == err) {
            ASSERT(i_ctx->timer_armed);
            crm_ipc_msg_t msg = { .scalar = EV_TIMEOUT };
            fsm->notify_event(fsm, msg.scalar, NULL);
        } else if (pfd[0].revents & POLLIN) {
            crm_ipc_msg_t msg;
            while (i_ctx->ipc->get_msg(i_ctx->ipc, &msg)) {
                if (-1 == msg.scalar) {
                    running = false;
                    continue;
                } else {
                    fsm->notify_event(fsm, msg.scalar, NULL);
                }
            }
        } else if (pfd[1].revents & POLLIN) {
            int evt = crm_hal_get_mdm_state(i_ctx);
            if (evt != -1) {
                /* mdm_state only reflects HW states (OFF, TRAP, ON).
                 * @TODO: use this variable to add sanity tests in the code */
                i_ctx->mdm_state = evt;
                fsm->notify_event(fsm, evt, NULL);
            }
        } else if (pfd[2].revents & POLLIN) {
            crm_ipc_msg_t msg;
            int evt = -1;
            ASSERT(i_ctx->thread_ping != NULL);

            while (i_ctx->thread_ping->get_msg(i_ctx->thread_ping, &msg)) {
                /* only one message is expected here */
                ASSERT(evt == -1);
                evt = msg.scalar;
            }
            i_ctx->thread_ping->dispose(i_ctx->thread_ping, NULL);
            i_ctx->thread_ping = NULL;

            fsm->notify_event(fsm, evt, NULL);
        } else if (pfd[3].revents & POLLIN) {
            int evt = crm_hal_rpcd_event(i_ctx);
            if (evt != -1)
                fsm->notify_event(fsm, evt, NULL);
        } else {
            ASSERT(0);
        }
    }

    fsm->dispose(fsm);

    return NULL;
}
