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

#include <errno.h>
#include <poll.h>
#include <stdio.h>

#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <cutils/sockets.h>

#include "libmdmcli/mdm_cli.h"

#define CRM_MODULE_TAG "CLA"
#include "utils/common.h"
#include "utils/debug.h"
#include "utils/fsm.h"
#include "utils/ipc.h"
#include "utils/thread.h"
#include "utils/time.h"
#include "utils/string_helpers.h"
#include "utils/socket.h"
#include "utils/wakelock.h"
#include "plugins/client_abstraction.h"
#include "plugins/control.h"
#include "plugins/mdmcli_wire.h"

/**
 * @TODO (?) remove this define and make it dynamic ?
 */
#define MAX_CLIENTS 16

typedef struct crm_client {
    bool registered;
    char name[MDM_CLI_NAME_LEN];
    int events_bitmap;
    bool acquired;
    bool waiting_cold_reset_ack;
    bool waiting_shutdown_ack;
} crm_client_t;

typedef struct crm_cli_abs_internal_ctx {
    crm_cli_abs_ctx_t ctx; // Needs to be first

    /* Plugin contexts */
    crm_ctrl_ctx_t *control_ctx;
    crm_ipc_ctx_t *ipc_ctx;
    crm_thread_ctx_t *thread_ctx;
    crm_mdmcli_wire_ctx_t *wire_ctx;
    crm_fsm_ctx_t *fsm_ctx;
    crm_wakelock_t *wakelock;

    /* Configuration */
    bool enable_fmmo;

    /* Clients management */
    bool sanity_test_mode;
    int num_clients;
    crm_client_t clients[MAX_CLIENTS];
    struct pollfd pfd[2 + MAX_CLIENTS];
    unsigned int to_clean;

    /* Interface with CTRL */
    crm_cli_abs_mdm_state_t modem_state;
    crm_cli_abs_mdm_state_t real_modem_state;
    bool fake_modem_state;
    bool request_in_progress;

    /* Pending procedure handling */
    bool reject_requests;
    int num_acquired;
    int num_waiting_cold_reset_ack;
    int num_waiting_shutdown_ack;
    crm_ctrl_restart_type_t restart_type;
    bool dbg_info_present;
    mdm_cli_dbg_info_t dbg_info;
    char dbg_strings[MDM_CLI_MAX_NB_DATA][MDM_CLI_MAX_LEN_DATA];
    const char *dbg_strings_ptr[MDM_CLI_MAX_NB_DATA];

    /* Timers */
    struct timespec timer_ack_end;
    struct timespec timer_boot_end;
    bool timer_boot_armed;
} crm_cli_abs_internal_ctx_t;


#define CLIENT_FORMAT "%-16s"
#define MSG_EVT_FORMAT "%-15s"

#define CLOGE(ctx, idx, format, ...) LOGE("[" CLIENT_FORMAT "](%2d) " format, \
                                          ctx->clients[idx].name, ctx->pfd[2 + idx].fd, \
                                          ## __VA_ARGS__)
#define CLOGD(ctx, idx, format, ...) LOGD("[" CLIENT_FORMAT "](%2d) " format, \
                                          ctx->clients[idx].name, ctx->pfd[2 + idx].fd, \
                                          ## __VA_ARGS__)
#define CLOGV(ctx, idx, format, ...) LOGV("[" CLIENT_FORMAT "](%2d) " format, \
                                          ctx->clients[idx].name, ctx->pfd[2 + idx].fd, \
                                          ## __VA_ARGS__)
#define CLOGI(ctx, idx, format, ...) LOGI("[" CLIENT_FORMAT "](%2d) " format, \
                                          ctx->clients[idx].name, ctx->pfd[2 + idx].fd, \
                                          ## __VA_ARGS__)

enum crm_cli_abs_msg_type {
    CLI_ABS_MSG_NOTIFY_OPERATION_RESULT = 1,
    CLI_ABS_MSG_NOTIFY_CLIENT,
    CLI_ABS_MSG_NOTIFY_MODEM_STATE,
};

/*
 * As the scalar is a 64 bits field, use:
 *  = 8 MSb for the type of operation (result, client, modem state)
 *  = 56 LSb for the operation parameter
 */
#define CLI_ABS_GEN_SCALAR(t, id) ((((long long)(t)) << 56) | (id))
#define CLI_ABS_GET_MSG_TYPE(s) (((s) >> 56) & 0xFF)
#define CLI_ABS_GET_MSG_PAYLOAD(s) ((s) & 0xFFFFFFFFFFFFFF)

#define TIMEOUT_ACK_IDX (1u << 0)
#define TIMEOUT_BOOT_IDX (1u << 1)

static mdm_cli_event_t map_modem_state_to_cli_event(crm_cli_abs_mdm_state_t mdm_state)
{
    switch (mdm_state) {
    case MDM_STATE_OFF: return MDM_DOWN;
    case MDM_STATE_UNRESP: return MDM_OOS;
    case MDM_STATE_PLATFORM_REBOOT: return MDM_OOS;
    case MDM_STATE_BUSY: return MDM_DOWN;
    case MDM_STATE_READY: return MDM_UP;
    case MDM_STATE_UNKNOWN: return MDM_DOWN;
    default: ASSERT(0);
    }
}

/* State machine types */
typedef enum cl_abs_evt {
    EV_NONE = -1,

    /* Success / failure notification */
    EV_SUCCESS = 0,
    EV_FAILURE,

    /* Modem state nofications */
    EV_MDM_OFF,
    EV_MDM_UNRESP,
    EV_MDM_BUSY,
    EV_MDM_READY,

    /* Client requests */
    EV_CLI_ACQUIRE,
    EV_CLI_RELEASE,
    EV_CLI_RESTART,
    EV_CLI_ACKED,
    EV_NUM
} cl_abs_evt_t;

typedef enum cl_abs_state {
    ST_INITIAL,
    ST_OFF,
    ST_STARTING,
    ST_UP,
    ST_ACK_WAITING_COLD,
    ST_ACK_WAITING_SHTDWN,
    ST_STOPPING,
    ST_NUM
} cl_abs_state_t;

static const char *get_state_txt(int state)
{
    switch (state) {
    case ST_INITIAL:            return "INITIAL";
    case ST_OFF:                return "OFF";
    case ST_STARTING:           return "STARTING";
    case ST_UP:                 return "UP";
    case ST_ACK_WAITING_COLD:   return "WAITING C ACK";
    case ST_ACK_WAITING_SHTDWN: return "WAITING S ACK";
    case ST_STOPPING:           return "STOPPING";
    default: ASSERT(0);
    }
}

static const char *get_event_txt(int event)
{
    switch (event) {
    case EV_SUCCESS:     return "CTRL: success";
    case EV_FAILURE:     return "CTRL: failure";
    case EV_MDM_OFF:     return "CTRL: mdm off";
    case EV_MDM_UNRESP:  return "CTRL: mdm unresp";
    case EV_MDM_BUSY:    return "CTRL: mdm busy";
    case EV_MDM_READY:   return "CTRL: mdm ready";
    case EV_CLI_ACQUIRE: return "CLI : acquire";
    case EV_CLI_RELEASE: return "CLI : release";
    case EV_CLI_RESTART: return "CLI : restart";
    case EV_CLI_ACKED:   return "CLI : acked";
    default: ASSERT(0);
    }
}

static inline crm_ctrl_restart_type_t get_restart_type(mdm_cli_restart_cause_t cause)
{
    switch (cause) {
    case RESTART_MDM_ERR:      return CTRL_MODEM_RESTART;
    case RESTART_APPLY_UPDATE: return CTRL_MODEM_UPDATE;
    default: ASSERT(0);
    }
}

static void handle_client_unregister(crm_cli_abs_internal_ctx_t *i_ctx, int client_idx)
{
    ASSERT((i_ctx->to_clean & (1u << client_idx)) == 0);
    if (i_ctx->clients[client_idx].registered) {
        CLOGD(i_ctx, client_idx, "client unregistered");

        if (i_ctx->clients[client_idx].acquired) {
            i_ctx->clients[client_idx].acquired = false;
            i_ctx->num_acquired -= 1;
            ASSERT(i_ctx->num_acquired >= 0);
        }
        if (i_ctx->clients[client_idx].waiting_cold_reset_ack) {
            i_ctx->clients[client_idx].waiting_cold_reset_ack = false;
            i_ctx->num_waiting_cold_reset_ack -= 1;
            ASSERT(i_ctx->num_waiting_cold_reset_ack >= 0);
        }
        if (i_ctx->clients[client_idx].waiting_shutdown_ack) {
            i_ctx->clients[client_idx].waiting_shutdown_ack = false;
            i_ctx->num_waiting_shutdown_ack -= 1;
            ASSERT(i_ctx->num_waiting_shutdown_ack >= 0);
        }
    } else {
        LOGD("unregistered client disconnected on socket %d", i_ctx->pfd[2 + client_idx].fd);
    }
    i_ctx->to_clean |= 1u << client_idx;
}

static void notify_cli_event_single(crm_cli_abs_internal_ctx_t *i_ctx, int client_idx,
                                    mdm_cli_event_t event, void *serialized_msg)
{
    ASSERT((i_ctx->to_clean & (1u << client_idx)) == 0);
    if ((1u << event) & i_ctx->clients[client_idx].events_bitmap) {
        CLOGD(i_ctx, client_idx, "=> " MSG_EVT_FORMAT "()", crm_mdmcli_wire_req_to_string(event));
        bool failure;
        if (event == MDM_COLD_RESET) {
            ASSERT(i_ctx->num_waiting_shutdown_ack == 0);
            i_ctx->clients[client_idx].waiting_cold_reset_ack = true;
            i_ctx->num_waiting_cold_reset_ack += 1;
        } else if (event == MDM_SHUTDOWN) {
            ASSERT(i_ctx->num_waiting_cold_reset_ack == 0);
            i_ctx->clients[client_idx].waiting_shutdown_ack = true;
            i_ctx->num_waiting_shutdown_ack += 1;
        }
        if (serialized_msg) {
            failure = i_ctx->wire_ctx->send_serialized_msg(i_ctx->wire_ctx, serialized_msg,
                                                           i_ctx->pfd[2 + client_idx].fd) != 0;
        } else {
            crm_mdmcli_wire_msg_t msg = { .id = event };
            failure =
                i_ctx->wire_ctx->send_msg(i_ctx->wire_ctx, &msg,
                                          i_ctx->pfd[2 + client_idx].fd) != 0;
        }
        if (failure) {
            CLOGE(i_ctx, client_idx, "failure to send message to client");
            handle_client_unregister(i_ctx, client_idx);
        }
    }
}

static void notify_cli_event_all(crm_cli_abs_internal_ctx_t *i_ctx, mdm_cli_event_t event,
                                 void *serialized_msg)
{
    LOGV("notifying event %d [%s] to up to %d client(s)", event,
         crm_mdmcli_wire_req_to_string(event), i_ctx->num_clients);
    for (int client_idx = 0; client_idx < i_ctx->num_clients; client_idx++)
        if ((i_ctx->to_clean & (1u << client_idx)) == 0)
            notify_cli_event_single(i_ctx, client_idx, event, serialized_msg);
}

static int failsafe(void *fsm_param, void *evt_param)
{
    (void)fsm_param;
    (void)evt_param;
    DASSERT(0, "not implemented");
}

static int assert(void *fsm_param, void *evt_param)
{
    (void)fsm_param;
    (void)evt_param;
    ASSERT(0);
}

static int client_acquire(void *fsm_param, void *evt_param)
{
    (void)evt_param;

    crm_cli_abs_internal_ctx_t *i_ctx = fsm_param;
    ASSERT(i_ctx);
    ASSERT(i_ctx->request_in_progress == false);

    i_ctx->request_in_progress = true;
    i_ctx->control_ctx->start(i_ctx->control_ctx);

    return ST_STARTING;
}

static void start_ack_timer(crm_cli_abs_internal_ctx_t *i_ctx)
{
    /* @TODO: add TCS configuration of time-out */
    crm_time_add_ms(&i_ctx->timer_ack_end, 1000);

    if (!i_ctx->wakelock->is_held_by_module(i_ctx->wakelock, WAKELOCK_CLA))
        i_ctx->wakelock->acquire(i_ctx->wakelock, WAKELOCK_CLA);
}

static int get_timeout(crm_cli_abs_internal_ctx_t *i_ctx, int *idx)
{
    int timeout = -1;

    ASSERT(idx != NULL);
    *idx = 0;

    if (i_ctx->num_waiting_cold_reset_ack != 0 || i_ctx->num_waiting_shutdown_ack != 0) {
        timeout = crm_time_get_remain_ms(&i_ctx->timer_ack_end);
        *idx = TIMEOUT_ACK_IDX;
    }

    if (i_ctx->timer_boot_armed) {
        int tmp = crm_time_get_remain_ms(&i_ctx->timer_boot_end);
        if (timeout == tmp) {
            *idx |= TIMEOUT_BOOT_IDX;
        } else if ((timeout < 0) || ((timeout > 0) && (tmp < timeout))) {
            timeout = tmp;
            *idx = TIMEOUT_BOOT_IDX;
        }
    }

    return timeout;
}

static int start_shutdown_procedure(crm_cli_abs_internal_ctx_t *i_ctx)
{
    crm_mdmcli_wire_msg_t msg;
    void *serialized_msg;

    msg.id = MDM_SHUTDOWN;
    serialized_msg = i_ctx->wire_ctx->serialize_msg(i_ctx->wire_ctx, &msg, false);
    notify_cli_event_all(i_ctx, MDM_SHUTDOWN, serialized_msg);

    if (!i_ctx->fake_modem_state) {
        msg.id = MDM_DOWN;
        serialized_msg = i_ctx->wire_ctx->serialize_msg(i_ctx->wire_ctx, &msg, false);
        notify_cli_event_all(i_ctx, MDM_DOWN, serialized_msg);
    }

    i_ctx->fake_modem_state = true;
    i_ctx->modem_state = MDM_STATE_OFF;

    if (i_ctx->num_waiting_shutdown_ack == 0) {
        ASSERT(i_ctx->request_in_progress == false);
        i_ctx->request_in_progress = true;
        i_ctx->control_ctx->stop(i_ctx->control_ctx);
        return ST_STOPPING;
    } else {
        start_ack_timer(i_ctx);
        return ST_ACK_WAITING_SHTDWN;
    }
}

static int start_restart_procedure(crm_cli_abs_internal_ctx_t *i_ctx)
{
    crm_mdmcli_wire_msg_t msg;
    void *serialized_msg;

    if (i_ctx->modem_state != MDM_STATE_BUSY) {
        msg.id = MDM_DOWN;
        serialized_msg = i_ctx->wire_ctx->serialize_msg(i_ctx->wire_ctx, &msg, false);
        notify_cli_event_all(i_ctx, MDM_DOWN, serialized_msg);

        ASSERT(!i_ctx->fake_modem_state);
        i_ctx->modem_state = MDM_STATE_BUSY;
        i_ctx->fake_modem_state = true;
    }

    msg.id = MDM_COLD_RESET;
    serialized_msg = i_ctx->wire_ctx->serialize_msg(i_ctx->wire_ctx, &msg, false);
    notify_cli_event_all(i_ctx, MDM_COLD_RESET, serialized_msg);

    if (i_ctx->num_waiting_cold_reset_ack == 0) {
        ASSERT(i_ctx->request_in_progress == false);
        i_ctx->request_in_progress = true;
        i_ctx->control_ctx->restart(i_ctx->control_ctx, i_ctx->restart_type,
                                    i_ctx->dbg_info_present ? &i_ctx->dbg_info : NULL);

        return ST_STARTING;
    } else {
        start_ack_timer(i_ctx);
        return ST_ACK_WAITING_COLD;
    }
}

static int mdm_busy(void *fsm_param, void *evt_param)
{
    (void)evt_param;

    crm_cli_abs_internal_ctx_t *i_ctx = fsm_param;
    ASSERT(i_ctx);

    i_ctx->restart_type = CTRL_MODEM_RESTART;
    return start_restart_procedure(i_ctx);
}

static int check_pending_up(void *fsm_param, void *evt_param)
{
    (void)evt_param;

    crm_cli_abs_internal_ctx_t *i_ctx = fsm_param;
    ASSERT(i_ctx);

    /* If request still pending or modem not up, do nothing */
    if (i_ctx->request_in_progress || i_ctx->modem_state != MDM_STATE_READY)
        return -1;

    /* Then check first if there are still clients */
    if (i_ctx->num_acquired == 0)
        return start_shutdown_procedure(i_ctx);

    /* Then if a restart has been asked */
    if (i_ctx->restart_type != 0)
        return start_restart_procedure(i_ctx);

    /* Nothing pending, go to UP state */
    return ST_UP;
}

static int check_pending_down(void *fsm_param, void *evt_param)
{
    (void)evt_param;

    crm_cli_abs_internal_ctx_t *i_ctx = fsm_param;
    ASSERT(i_ctx);

    /* If request still pending, do nothing */
    if (i_ctx->request_in_progress || i_ctx->modem_state != MDM_STATE_OFF)
        return -1;

    if (i_ctx->num_acquired > 0) {
        i_ctx->request_in_progress = true;
        i_ctx->control_ctx->start(i_ctx->control_ctx);
        return ST_STARTING;
    } else {
        return ST_OFF;
    }
}

static int client_release(void *fsm_param, void *evt_param)
{
    (void)evt_param;

    crm_cli_abs_internal_ctx_t *i_ctx = fsm_param;
    ASSERT(i_ctx);

    return start_shutdown_procedure(i_ctx);
}

static int client_restart(void *fsm_param, void *evt_param)
{
    (void)evt_param;

    crm_cli_abs_internal_ctx_t *i_ctx = fsm_param;
    ASSERT(i_ctx);

    return start_restart_procedure(i_ctx);
}

static int client_acked_cold(void *fsm_param, void *evt_param)
{
    (void)evt_param;

    crm_cli_abs_internal_ctx_t *i_ctx = fsm_param;
    ASSERT(i_ctx);

    if (i_ctx->modem_state == MDM_STATE_UNRESP) {
        return ST_OFF;
    } else if (i_ctx->num_acquired == 0) {
        return start_shutdown_procedure(i_ctx);
    } else if (i_ctx->restart_type != 0) {
        ASSERT(i_ctx->request_in_progress == false);
        i_ctx->request_in_progress = true;
        i_ctx->control_ctx->restart(i_ctx->control_ctx, i_ctx->restart_type,
                                    i_ctx->dbg_info_present ? &i_ctx->dbg_info : NULL);
        return ST_STARTING;
    } else {
        ASSERT(i_ctx->request_in_progress == false);
        ASSERT(i_ctx->real_modem_state == MDM_STATE_BUSY);
        i_ctx->request_in_progress = true;
        i_ctx->control_ctx->restart(i_ctx->control_ctx, CTRL_MODEM_RESTART, NULL);
        return ST_STARTING;
    }
}


static int client_acked_shtdwn(void *fsm_param, void *evt_param)
{
    (void)evt_param;

    crm_cli_abs_internal_ctx_t *i_ctx = fsm_param;
    ASSERT(i_ctx);

    if (i_ctx->num_acquired == 0) {
        ASSERT(i_ctx->request_in_progress == false);
        i_ctx->request_in_progress = true;
        i_ctx->control_ctx->stop(i_ctx->control_ctx);
        return ST_STOPPING;
    } else if (i_ctx->restart_type != 0) {
        return start_restart_procedure(i_ctx);
    } else {
        if (i_ctx->real_modem_state == MDM_STATE_READY) {
            // Even if a new client acquired the modem before last ACK, still shut modem down as we
            // informed clients of MODEM_DOWN.
            ASSERT(i_ctx->request_in_progress == false);
            i_ctx->request_in_progress = true;
            i_ctx->control_ctx->stop(i_ctx->control_ctx);
            return ST_STOPPING;
        } else {
            i_ctx->fake_modem_state = false;
            i_ctx->modem_state = i_ctx->real_modem_state;
            ASSERT(i_ctx->real_modem_state == MDM_STATE_BUSY);
            ASSERT(i_ctx->request_in_progress == false);
            i_ctx->request_in_progress = true;
            i_ctx->control_ctx->restart(i_ctx->control_ctx, CTRL_MODEM_RESTART, NULL);
            return ST_STARTING;
        }
    }
}

static int check_failure(void *fsm_param, void *evt_param)
{
    (void)evt_param;
    crm_cli_abs_internal_ctx_t *i_ctx = fsm_param;
    ASSERT(i_ctx);

    if (i_ctx->modem_state != MDM_STATE_UNRESP) {
        const char *data[2] = { "mismatch between CLA and control",
                                crm_cli_abs_mdm_state_to_string(i_ctx->modem_state) };
        mdm_cli_dbg_info_t dbg = { DBG_TYPE_ERROR, DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_NO_LOG,
                                   DBG_DEFAULT_NO_LOG, ARRAY_SIZE(data), data };
        i_ctx->ctx.notify_client(&i_ctx->ctx, MDM_DBG_INFO, sizeof(mdm_cli_dbg_info_t), &dbg);
        DASSERT(0, "Wrong modem state notification received from control");
    }

    return -1;
}

/* *INDENT-OFF* */
static const crm_fsm_ops_t cla_fsm_array[EV_NUM * ST_NUM] = {
                         /* ST_INITIAL */          /* ST_OFF */          /* ST_STARTING */       /* ST_UP */           /* ST_ACK_WAITING_COLD */ /* ST_ACK_WAITING_SHTDWN */ /* ST_STOPPING */
    /* EV_SUCCESS */     {-1, assert},             {-1, assert},         {-1, check_pending_up}, {-1, assert},         {-1, assert},             {-1, assert},               {-1, check_pending_down},
    /* EV_FAILURE */     {-1, assert},             {-1, check_failure},  {-1, assert},           {-1, assert},         {-1, assert},             {-1, assert},               {ST_OFF, NULL},
    /* EV_MDM_OFF */     {-1, check_pending_down}, {-1, assert},         {-1, assert},           {-1, assert},         {-1, assert},             {-1, assert},               {-1, check_pending_down},
    /* EV_MDM_UNRESP */  {-1, assert},             {ST_OFF, NULL},       {ST_OFF, NULL},         {-1, assert},         {-1, NULL},               {-1, assert},               {-1, NULL},
    /* EV_MDM_BUSY */    {-1, assert},             {-1, assert},         {-1, NULL},             {-1, mdm_busy},       {-1, NULL},               {-1, NULL},                 {-1, NULL},
    /* EV_MDM_READY */   {-1, check_pending_up},   {-1, assert},         {-1, check_pending_up}, {-1, assert},         {-1, assert},             {-1, assert},               {-1, assert},
    /* EV_CLI_ACQUIRE */ {-1, NULL},               {-1, client_acquire}, {-1, NULL},             {-1, assert},         {-1, NULL},               {-1, NULL},                 {-1, NULL},
    /* EV_CLI_RELEASE */ {-1, NULL},               {-1, check_failure},  {-1, NULL},             {-1, client_release}, {-1, NULL},               {-1, NULL},                 {-1, NULL},
    /* EV_CLI_RESTART */ {-1, NULL},               {-1, assert},         {-1, NULL},             {-1, client_restart}, {-1, assert},             {-1, NULL},                 {-1, assert},
    /* EV_CLI_ACKED */   {-1, NULL},               {-1, assert},         {-1, assert},           {-1, assert},         {-1, client_acked_cold},  {-1, client_acked_shtdwn},  {-1, assert}
};
/* *INDENT-ON* */


/**
 * @see client_abstraction.h
 */
static void dispose(crm_cli_abs_ctx_t *ctx)
{
    LOGV("(%p)", ctx);

    ASSERT(ctx != NULL);
    crm_cli_abs_internal_ctx_t *i_ctx = (crm_cli_abs_internal_ctx_t *)ctx;
    crm_ipc_ctx_t *ipc_ctx = i_ctx->ipc_ctx;
    crm_ipc_msg_t msg = { .scalar = -1 };

    /* Send message to worker thread to stop it (the dispose of the thread module will 'join' the
     * thread.
     */
    ASSERT(ipc_ctx->send_msg(ipc_ctx, &msg));
    /**
     * @TODO add memory free function for IPC dispose
     */
    i_ctx->thread_ctx->dispose(i_ctx->thread_ctx, NULL);
    ipc_ctx->dispose(ipc_ctx, NULL);
    i_ctx->wire_ctx->dispose(i_ctx->wire_ctx);
    i_ctx->fsm_ctx->dispose(i_ctx->fsm_ctx);
    free(i_ctx);
}

/**
 * @see client_abstraction.h
 */
static void notify_client(crm_cli_abs_ctx_t *ctx, mdm_cli_event_t evt_id, size_t data_size,
                          const void *data)
{
    LOGV("(%p, %d [%s], %zd, %p)", ctx, evt_id,
         crm_mdmcli_wire_req_to_string(evt_id), data_size, data);

    crm_cli_abs_internal_ctx_t *i_ctx = (crm_cli_abs_internal_ctx_t *)ctx;
    ASSERT(i_ctx != NULL);
    ASSERT(evt_id >= MDM_DBG_INFO || evt_id == MDM_ON);
    ASSERT(evt_id != MDM_DBG_INFO || data == NULL || data_size == sizeof(mdm_cli_dbg_info_t));

    crm_ipc_msg_t msg = { .scalar = CLI_ABS_GEN_SCALAR(CLI_ABS_MSG_NOTIFY_CLIENT, evt_id),
                          .data = NULL };
    crm_mdmcli_wire_msg_t s_msg = { .id = evt_id,
                                    .msg.debug = data };
    msg.data = i_ctx->wire_ctx->serialize_msg(i_ctx->wire_ctx, &s_msg, true);
    ASSERT(i_ctx->ipc_ctx->send_msg(i_ctx->ipc_ctx, &msg));
}

/**
 * @see client_abstraction.h
 */
static void notify_modem_state(crm_cli_abs_ctx_t *ctx, crm_cli_abs_mdm_state_t mdm_state)
{
    LOGV("(%p,%d [%s])", ctx, mdm_state, crm_cli_abs_mdm_state_to_string(mdm_state));
    crm_cli_abs_internal_ctx_t *i_ctx = (crm_cli_abs_internal_ctx_t *)ctx;
    ASSERT(i_ctx != NULL);

    crm_ipc_msg_t msg = { .scalar = CLI_ABS_GEN_SCALAR(CLI_ABS_MSG_NOTIFY_MODEM_STATE, mdm_state),
                          .data = NULL };
    ASSERT(i_ctx->ipc_ctx->send_msg(i_ctx->ipc_ctx, &msg));
}

/**
 * @see client_abstraction.h
 */
static void notify_operation_result(crm_cli_abs_ctx_t *ctx, int result)
{
    LOGV("(%p, %d)", ctx, result);
    crm_cli_abs_internal_ctx_t *i_ctx = (crm_cli_abs_internal_ctx_t *)ctx;
    ASSERT(i_ctx != NULL);

    crm_ipc_msg_t msg = { .scalar = CLI_ABS_GEN_SCALAR(CLI_ABS_MSG_NOTIFY_OPERATION_RESULT,
                                                       result == 0 ? 0 : 1),
                          .data = NULL };
    ASSERT(i_ctx->ipc_ctx->send_msg(i_ctx->ipc_ctx, &msg));
}

static bool handle_control_msg(crm_cli_abs_internal_ctx_t *i_ctx, crm_ipc_msg_t *msg)
{
    ASSERT(msg != NULL);

    if (msg->scalar == -1)
        return true;

    int payload = CLI_ABS_GET_MSG_PAYLOAD(msg->scalar);
    switch (CLI_ABS_GET_MSG_TYPE(msg->scalar)) {
    case CLI_ABS_MSG_NOTIFY_OPERATION_RESULT:
        LOGD("->notify_operation_result(%s)", payload ? "failure" : "success");
        i_ctx->request_in_progress = false;
        i_ctx->fsm_ctx->notify_event(i_ctx->fsm_ctx, payload ? EV_FAILURE : EV_SUCCESS, NULL);
        break;

    case CLI_ABS_MSG_NOTIFY_CLIENT:
        /* This is a pass-through message, it does not impact the state machine */
        LOGD("->notify_client(%d [%s])", payload, crm_mdmcli_wire_req_to_string(payload));
        ASSERT(msg->data != NULL);
        notify_cli_event_all(i_ctx, payload, msg->data);
        free(msg->data);
        break;

    case CLI_ABS_MSG_NOTIFY_MODEM_STATE: {
        crm_cli_abs_mdm_state_t modem_state = (crm_cli_abs_mdm_state_t)payload;
        bool send_cli_msg = true;

        /* MDM_STATE_PLATFORM_REBOOT is handled as MDM_STATE_UNRESP in CLA.
         * On MDM_STATE_UNRESP and failure, CLA notify MDM_OOS event to clients.
         * On PLATFORM_REBOOT and failure, CLA doesn't notify MDM_OOS to clients. */
        if (modem_state == MDM_STATE_PLATFORM_REBOOT) {
            modem_state = MDM_STATE_UNRESP;
            send_cli_msg = false;
        }

        i_ctx->real_modem_state = modem_state;

        if ((modem_state == MDM_STATE_READY) || (modem_state == MDM_STATE_OFF))
            i_ctx->restart_type = 0;
        else if (modem_state == MDM_STATE_UNRESP)
            i_ctx->reject_requests = true;

        if (send_cli_msg) {
            if (i_ctx->fake_modem_state) {
                LOGD("->notify_modem_state(%d [%s]) [faking %d [%s]]%s",
                     modem_state, crm_cli_abs_mdm_state_to_string(modem_state), i_ctx->modem_state,
                     crm_cli_abs_mdm_state_to_string(i_ctx->modem_state),
                     send_cli_msg ? "" : " [filtered]");
            } else {
                LOGD("->notify_modem_state(%d [%s]%s)", modem_state,
                     crm_cli_abs_mdm_state_to_string(modem_state),
                     send_cli_msg ? "" : " [filtered]");
            }
        }

        ASSERT(i_ctx->modem_state != MDM_STATE_UNRESP);

        bool filter_state = i_ctx->fake_modem_state;
        if (i_ctx->fake_modem_state) {
            /* In 'fake modem state', filter everything out waiting for the allowed events */
            ASSERT(((i_ctx->modem_state == MDM_STATE_OFF) &&
                    (i_ctx->modem_state == MDM_STATE_READY)) == false);
            ASSERT(((i_ctx->modem_state == MDM_STATE_BUSY) &&
                    ((i_ctx->modem_state == MDM_STATE_READY) ||
                     (i_ctx->modem_state == MDM_STATE_OFF))) == false);
            if (modem_state == MDM_STATE_UNRESP) {
                i_ctx->fake_modem_state = false;
                filter_state = false;
            } else if (i_ctx->modem_state == modem_state) {
                i_ctx->fake_modem_state = false;
            }
        }

        if (!filter_state && i_ctx->modem_state != modem_state) {
            mdm_cli_event_t cli_evt = map_modem_state_to_cli_event(modem_state);
            if (send_cli_msg && (i_ctx->modem_state == MDM_STATE_UNKNOWN ||
                                 cli_evt != map_modem_state_to_cli_event(i_ctx->modem_state))) {
                /* Only send message to client if the 'external' modem state actually changes or if
                 * the modem state was previously unknown.
                 */
                crm_mdmcli_wire_msg_t msg = { .id = cli_evt };
                void *serialized_msg = i_ctx->wire_ctx->serialize_msg(i_ctx->wire_ctx, &msg, false);
                notify_cli_event_all(i_ctx, cli_evt, serialized_msg);
            }

            i_ctx->modem_state = modem_state;

            cl_abs_evt_t event = (modem_state - MDM_STATE_OFF) + EV_MDM_OFF;     // Switch / case to
                                                                                 // be safer ?
            i_ctx->fsm_ctx->notify_event(i_ctx->fsm_ctx, event, NULL);
        }
    } break;
    }
    return false;
}

static void handle_client_msg(crm_cli_abs_internal_ctx_t *i_ctx, int client_idx,
                              crm_mdmcli_wire_msg_t *msg)
{
    ASSERT(i_ctx != NULL);
    ASSERT(msg != NULL);

    if (i_ctx->to_clean & (1u << client_idx)) {
        // Filter out messages on clients that were disconnected
        CLOGD(i_ctx, client_idx, "<= " MSG_EVT_FORMAT "() ignored due to client disconnection",
              crm_mdmcli_wire_req_to_string(msg->id));
        return;
    }

    if (msg->id != CRM_REQ_REGISTER && msg->id != CRM_REQ_REGISTER_DBG &&
        msg->id != CRM_REQ_RESTART && msg->id != CRM_REQ_NOTIFY_DBG)
        // Generic logging for 'no parameter' requests
        CLOGD(i_ctx, client_idx, "<= " MSG_EVT_FORMAT "()",
              crm_mdmcli_wire_req_to_string(msg->id));

    /* Refuse registration according to sanity mode */
    if ((msg->id == CRM_REQ_REGISTER && i_ctx->sanity_test_mode) ||
        (msg->id == CRM_REQ_REGISTER_DBG && !i_ctx->sanity_test_mode)) {
        CLOGE(i_ctx, client_idx, "wrong registration ID, disconnecting the client");
        handle_client_unregister(i_ctx, client_idx);
        return;
    }

    if (msg->id != CRM_REQ_REGISTER && msg->id != CRM_REQ_REGISTER_DBG &&
        !i_ctx->clients[client_idx].registered) {
        CLOGE(i_ctx, client_idx, "message received from an unregistered client, disconnecting it");
        handle_client_unregister(i_ctx, client_idx);
        return;
    }

    switch (msg->id) {
    case CRM_REQ_REGISTER:
    case CRM_REQ_REGISTER_DBG:
        /* Note: this does not impact the state machine */
        if (i_ctx->clients[client_idx].registered) {
            CLOGD(i_ctx, client_idx, "<= " MSG_EVT_FORMAT "(0x%08x)",
                  crm_mdmcli_wire_req_to_string(msg->id),
                  msg->msg.register_client.events_bitmap);
            CLOGE(i_ctx, client_idx, "duplicate REGISTER message, disconnecting client");
            handle_client_unregister(i_ctx, client_idx);
        } else {
            i_ctx->clients[client_idx].registered = true;
            snprintf(i_ctx->clients[client_idx].name, sizeof(i_ctx->clients[client_idx].name), "%s",
                     msg->msg.register_client.name);
            i_ctx->clients[client_idx].events_bitmap = msg->msg.register_client.events_bitmap;
            CLOGD(i_ctx, client_idx, "<= " MSG_EVT_FORMAT "(0x%08x)",
                  crm_mdmcli_wire_req_to_string(msg->id),
                  msg->msg.register_client.events_bitmap);

            if (i_ctx->modem_state != MDM_STATE_UNKNOWN)
                notify_cli_event_single(i_ctx, client_idx,
                                        map_modem_state_to_cli_event(i_ctx->modem_state), NULL);
        }
        break;

    case CRM_REQ_ACQUIRE:
        if (!i_ctx->reject_requests) {
            if (i_ctx->clients[client_idx].acquired) {
                CLOGE(i_ctx, client_idx, "client has already acquired the modem");
            } else {
                i_ctx->clients[client_idx].acquired = true;
                i_ctx->num_acquired += 1;
            }
        }
        break;

    case CRM_REQ_RELEASE:
        if (!i_ctx->reject_requests) {
            if (!i_ctx->clients[client_idx].acquired) {
                CLOGE(i_ctx, client_idx, "client did not previously acquire the modem");
            } else {
                i_ctx->clients[client_idx].acquired = false;
                i_ctx->num_acquired -= 1;
                ASSERT(i_ctx->num_acquired >= 0);
            }
        }
        break;

    case CRM_REQ_RESTART: {
        bool ignore = false;
        const char *ignored = "";
        if (i_ctx->restart_type != 0) {
            ignore = true;
            ignored = " (ignored, other client reset pending)";
        } else if (i_ctx->real_modem_state != MDM_STATE_READY) {
            ignore = true;
            ignored = " (ignored, modem off or being restarted)";
        } else if (i_ctx->reject_requests) {
            ignore = true;
            ignored = " (ignored, modem in a final state (force shutdown / out of service))";
        }
        if (msg->msg.restart.debug) {
            CLOGD(i_ctx, client_idx, "<= " MSG_EVT_FORMAT "(%s,%s,ApLogsSize:%dMB,BpLogsSize:%dMB"
                  ",BpLogsTime:%ds,%zd)%s",
                  crm_mdmcli_wire_req_to_string(msg->id),
                  crm_mdmcli_restart_cause_to_string(msg->msg.restart.cause),
                  crm_mdmcli_dbg_type_to_string(msg->msg.restart.debug->type),
                  msg->msg.restart.debug->ap_logs_size, msg->msg.restart.debug->bp_logs_size,
                  msg->msg.restart.debug->bp_logs_time, msg->msg.restart.debug->nb_data, ignored);
        } else {
            CLOGD(i_ctx, client_idx, "<= " MSG_EVT_FORMAT "(%s,<nil>)%s",
                  crm_mdmcli_wire_req_to_string(msg->id),
                  crm_mdmcli_restart_cause_to_string(msg->msg.restart.cause), ignored);
        }
        if (!ignore) {
            i_ctx->restart_type = get_restart_type(msg->msg.restart.cause);
            if (msg->msg.restart.debug) {
                ASSERT(msg->msg.restart.debug->nb_data <= MDM_CLI_MAX_NB_DATA);
                ASSERT(msg->msg.restart.debug->nb_data == 0 || msg->msg.restart.debug->data !=
                       NULL);
                i_ctx->dbg_info_present = true;
                i_ctx->dbg_info = *msg->msg.restart.debug;
                i_ctx->dbg_info.data = i_ctx->dbg_strings_ptr;
                for (size_t i = 0; i < msg->msg.restart.debug->nb_data; i++) {
                    strncpy(i_ctx->dbg_strings[i], msg->msg.restart.debug->data[i],
                            MDM_CLI_MAX_LEN_DATA);
                    i_ctx->dbg_strings[i][MDM_CLI_MAX_LEN_DATA - 1] = '\0';
                }
            } else {
                i_ctx->dbg_info_present = false;
            }
            i_ctx->fsm_ctx->notify_event(i_ctx->fsm_ctx, EV_CLI_RESTART, NULL);
        }
    } break;

    case CRM_REQ_SHUTDOWN:
        i_ctx->reject_requests = true;
        i_ctx->num_acquired = 0;
        break;

    case CRM_REQ_NVM_BACKUP:
        i_ctx->restart_type = CTRL_BACKUP_NVM;
        i_ctx->fsm_ctx->notify_event(i_ctx->fsm_ctx, EV_CLI_RESTART, NULL);
        break;

    case CRM_REQ_ACK_COLD_RESET:
        if (!i_ctx->clients[client_idx].waiting_cold_reset_ack) {
            CLOGE(i_ctx, client_idx, "not waiting for client cold reset ack");
        } else {
            i_ctx->clients[client_idx].waiting_cold_reset_ack = false;
            i_ctx->num_waiting_cold_reset_ack -= 1;
            ASSERT(i_ctx->num_waiting_cold_reset_ack >= 0);
        }
        break;

    case CRM_REQ_ACK_SHUTDOWN:
        if (!i_ctx->clients[client_idx].waiting_shutdown_ack) {
            CLOGE(i_ctx, client_idx, "not waiting for client shutdown ack");
        } else {
            i_ctx->clients[client_idx].waiting_shutdown_ack = false;
            i_ctx->num_waiting_shutdown_ack -= 1;
            ASSERT(i_ctx->num_waiting_shutdown_ack >= 0);
        }
        break;

    case CRM_REQ_NOTIFY_DBG:
        if (msg->msg.debug) {
            CLOGD(i_ctx, client_idx,
                  "<= " MSG_EVT_FORMAT "(%s,ApLogsSize:%dMB,BpLogsSize:%dMB,BpLogsTime:%ds,%zd)",
                  crm_mdmcli_wire_req_to_string(msg->id),
                  crm_mdmcli_dbg_type_to_string(msg->msg.debug->type),
                  msg->msg.debug->ap_logs_size, msg->msg.debug->bp_logs_size,
                  msg->msg.debug->bp_logs_time, msg->msg.debug->nb_data);
        } else {
            CLOGD(i_ctx, client_idx, "<= " MSG_EVT_FORMAT "(<nil>)",
                  crm_mdmcli_wire_req_to_string(msg->id));
        }
        crm_mdmcli_wire_msg_t to_send_msg = { .id = MDM_DBG_INFO, .msg.debug = msg->msg.debug };
        void *serialized_msg = i_ctx->wire_ctx->serialize_msg(i_ctx->wire_ctx, &to_send_msg, false);
        notify_cli_event_all(i_ctx, MDM_DBG_INFO, serialized_msg);
        break;
    }
}

static void update_client_list(crm_cli_abs_internal_ctx_t *i_ctx)
{
    if (i_ctx->to_clean) {
        int j = 0;
        int num_clients = i_ctx->num_clients;
        for (int i = 0; i < i_ctx->num_clients; i++) {
            if ((1u << i) & i_ctx->to_clean) {
                close(i_ctx->pfd[2 + j].fd);
                if (j != num_clients - 1) {
                    memmove(&i_ctx->pfd[2 + j], &i_ctx->pfd[2 + j + 1],
                            sizeof(i_ctx->pfd[0]) * (num_clients - j - 1));
                    memmove(&i_ctx->clients[j], &i_ctx->clients[j + 1],
                            sizeof(i_ctx->clients[0]) * (num_clients - j - 1));
                }
                num_clients -= 1;
            } else {
                j++;
            }
        }
        i_ctx->num_clients = num_clients;
    }
}


static void *main_loop(crm_thread_ctx_t *thread_ctx, void *arg)
{
    crm_cli_abs_internal_ctx_t *i_ctx = arg;

    ASSERT(i_ctx != NULL);
    int ipc_fd = i_ctx->ipc_ctx->get_poll_fd(i_ctx->ipc_ctx);
    bool running = true;
    errno = 0;
    const char *socket_name = i_ctx->wire_ctx->get_socket_name(i_ctx->wire_ctx);
    int server_sock = crm_socket_create(socket_name, MAX_CLIENTS);
    DASSERT(server_sock >= 0, "get control socket (%s) failed (%s)", socket_name,
            strerror(errno));

    (void)thread_ctx;  // UNUSED

    i_ctx->pfd[0].fd = ipc_fd;
    i_ctx->pfd[0].events = POLLIN;
    i_ctx->pfd[1].fd = server_sock;
    i_ctx->pfd[1].events = POLLIN;

    /* At boot, wakelock is hold during 2s to receive connection requests from clients */
    ASSERT(clock_gettime(CLOCK_BOOTTIME, &i_ctx->timer_boot_end) == 0);
    i_ctx->timer_boot_armed = true;
    crm_time_add_ms(&i_ctx->timer_boot_end, 2000);
    i_ctx->wakelock->acquire(i_ctx->wakelock, WAKELOCK_CLA);

    while (running) {
        int idx;
        int timeout = get_timeout(i_ctx, &idx);
        if ((timeout == -1) && i_ctx->wakelock->is_held_by_module(i_ctx->wakelock, WAKELOCK_CLA))
            i_ctx->wakelock->release(i_ctx->wakelock, WAKELOCK_CLA);

        int ret = poll(i_ctx->pfd, 2 + i_ctx->num_clients, timeout);
        /**
         * @TODO add EINTR + error handling
         */
        if (ret == 0) {
            ASSERT(idx != 0);
            ASSERT(i_ctx->wakelock->is_held_by_module(i_ctx->wakelock, WAKELOCK_CLA) == true);

            if (TIMEOUT_BOOT_IDX & idx)
                i_ctx->timer_boot_armed = crm_time_get_remain_ms(&i_ctx->timer_boot_end) > 0;

            if (TIMEOUT_ACK_IDX & idx) {
                for (int i = 0; i < i_ctx->num_clients; i++) {
                    if (i_ctx->clients[i].waiting_cold_reset_ack) {
                        i_ctx->clients[i].waiting_cold_reset_ack = false;
                        CLOGE(i_ctx, i, "time-out waiting for COLD_RESET ack");
                    }
                    if (i_ctx->clients[i].waiting_shutdown_ack) {
                        i_ctx->clients[i].waiting_shutdown_ack = false;
                        CLOGE(i_ctx, i, "time-out waiting for SHUTDOWN ack");
                    }
                }
                i_ctx->num_waiting_cold_reset_ack = 0;
                i_ctx->num_waiting_shutdown_ack = 0;
                i_ctx->fsm_ctx->notify_event(i_ctx->fsm_ctx, EV_CLI_ACKED, NULL);
            }

            if (!i_ctx->timer_boot_armed && i_ctx->num_waiting_cold_reset_ack == 0 &&
                i_ctx->num_waiting_shutdown_ack == 0)
                i_ctx->wakelock->release(i_ctx->wakelock, WAKELOCK_CLA);
        } else {
            i_ctx->to_clean = 0;
            for (int i = 0; i < i_ctx->num_clients + 2; i++) {
                bool acquired = i_ctx->num_acquired != 0;
                bool waiting_ack = (i_ctx->num_waiting_cold_reset_ack != 0) ||
                                   (i_ctx->num_waiting_shutdown_ack != 0);

                if ((i < 2) && (i_ctx->pfd[i].revents & (POLLERR | POLLHUP | POLLNVAL)))
                    DASSERT(0, "unable to handle errors on main socket / IPC socket, aborting!");

                if (i_ctx->pfd[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    if ((i_ctx->to_clean & (1u << (i - 2))) == 0)
                        handle_client_unregister(i_ctx, i - 2);
                } else if (i_ctx->pfd[i].revents & POLLIN) {
                    if (i == 0) {
                        crm_ipc_msg_t msg;
                        bool end_thread = false;
                        while (i_ctx->ipc_ctx->get_msg(i_ctx->ipc_ctx, &msg))
                            end_thread |= handle_control_msg(i_ctx, &msg);
                        if (end_thread)
                            running = false;
                    } else if (i == 1) {
                        int client_sock = crm_socket_accept(server_sock);
                        if (client_sock >= 0) {
                            LOGD("new client connecting on socket %d", client_sock);
                            ASSERT(i_ctx->num_clients < MAX_CLIENTS);
                            i_ctx->pfd[2 + i_ctx->num_clients].fd = client_sock;
                            i_ctx->pfd[2 + i_ctx->num_clients].events = POLLIN;
                            i_ctx->pfd[2 + i_ctx->num_clients].revents = 0;

                            memset(&i_ctx->clients[i_ctx->num_clients], 0,
                                   sizeof(i_ctx->clients[i_ctx->num_clients]));

                            i_ctx->num_clients += 1;
                        } else {
                            LOGE("failure to accept client connection (%d / %s)", errno,
                                 strerror(errno));
                        }
                    } else {
                        crm_mdmcli_wire_msg_t *msg = i_ctx->wire_ctx->recv_msg(i_ctx->wire_ctx,
                                                                               i_ctx->pfd[i].fd);
                        if (msg)
                            handle_client_msg(i_ctx, i - 2, msg);
                        else if ((i_ctx->to_clean & (1u << (i - 2))) == 0)
                            handle_client_unregister(i_ctx, i - 2);
                    }
                }
                cl_abs_evt_t evt = EV_NONE;
                bool new_waiting_ack = (i_ctx->num_waiting_cold_reset_ack != 0) ||
                                       (i_ctx->num_waiting_shutdown_ack != 0);

                if (waiting_ack && !new_waiting_ack)
                    evt = EV_CLI_ACKED;

                if (acquired && i_ctx->num_acquired == 0) {
                    /* Special handling needed here as a client disconnection may lead to both the
                     * 'EV_CLI_ACKED' and 'EV_CLI_RELEASE' events (in the case where a single client
                     * is simultaneously holding the last modem resource and is waited upon for the
                     * last ACK).
                     */
                    if (evt == EV_CLI_ACKED)
                        // This is to prevent triggering a safety ASSERT in the ack handling :)
                        i_ctx->restart_type = 0;
                    else
                        evt = EV_CLI_RELEASE;
                } else if (!acquired && i_ctx->num_acquired != 0) {
                    ASSERT(evt == EV_NONE);
                    evt = EV_CLI_ACQUIRE;
                }

                if (evt != EV_NONE)
                    i_ctx->fsm_ctx->notify_event(i_ctx->fsm_ctx, evt, NULL);
            }
            update_client_list(i_ctx);
        }
    }

    return NULL;
}

static void state_trans(int prev_state, int new_state, int evt, void *fsm_param, void *evt_param)
{
    (void)fsm_param;  // UNUSED
    (void)evt_param;  // UNUSED
    (void)evt;        // UNUSED
    (void)prev_state; // UNUSED

    ASSERT(new_state != ST_INITIAL);
}

/**
 * @see client_abstraction.h
 */
crm_cli_abs_ctx_t *crm_cli_abs_init(int inst_id, bool sanity_mode, tcs_ctx_t *tcs,
                                    crm_ctrl_ctx_t *control, crm_wakelock_t *wakelock)
{
    crm_cli_abs_internal_ctx_t *i_ctx = calloc(1, sizeof(*i_ctx));

    ASSERT(i_ctx != NULL);
    ASSERT(wakelock != NULL);
    ASSERT(tcs != NULL);

    i_ctx->control_ctx = control;
    i_ctx->ipc_ctx = crm_ipc_init(CRM_IPC_THREAD);
    i_ctx->wire_ctx = crm_mdmcli_wire_init(CRM_SERVER_TO_CLIENT, inst_id);
    i_ctx->fsm_ctx = crm_fsm_init(cla_fsm_array, EV_NUM, ST_NUM, ST_INITIAL, NULL, state_trans,
                                  failsafe, i_ctx, CRM_MODULE_TAG, get_state_txt, get_event_txt);
    ASSERT(i_ctx->control_ctx);
    ASSERT(i_ctx->ipc_ctx);
    ASSERT(i_ctx->wire_ctx);
    ASSERT(i_ctx->fsm_ctx);

    i_ctx->sanity_test_mode = sanity_mode;
    if (i_ctx->sanity_test_mode)
        LOGV("CRM is running in sanity test mode");

    i_ctx->modem_state = MDM_STATE_UNKNOWN;
    i_ctx->real_modem_state = MDM_STATE_UNKNOWN;
    i_ctx->restart_type = 0;
    for (size_t i = 0; i < ARRAY_SIZE(i_ctx->dbg_strings_ptr); i++)
        i_ctx->dbg_strings_ptr[i] = i_ctx->dbg_strings[i];

    ASSERT(tcs->select_group(tcs, ".client_abstraction") == 0);
    ASSERT(tcs->get_bool(tcs, "enable_fmmo", &i_ctx->enable_fmmo) == 0);

    if (!i_ctx->enable_fmmo)
        i_ctx->num_acquired = 1;

    LOGV("context %p", i_ctx);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.notify_client = notify_client;
    i_ctx->ctx.notify_modem_state = notify_modem_state;
    i_ctx->ctx.notify_operation_result = notify_operation_result;

    i_ctx->wakelock = wakelock;

    i_ctx->thread_ctx = crm_thread_init(main_loop, i_ctx, false, false);
    ASSERT(i_ctx->thread_ctx);

    return &i_ctx->ctx;
}
