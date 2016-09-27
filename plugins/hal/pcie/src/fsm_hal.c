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

#include <string.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#define CRM_MODULE_TAG "HAL"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/at.h"
#include "utils/thread.h"
#include "utils/fsm.h"
#include "utils/file.h"
#include "utils/time.h"
#include "utils/property.h"
#include "utils/keys.h"

#include "common.h"
#include "fsm_hal.h"
#include "modem.h"
#include "rpcd.h"
#include "daemons.h"
#include "nvm_manager.h"

#define FSM_HAL_FDS_TO_POLL 5

typedef enum hal_pcie_states {
    ST_OFF,
    ST_BOOTING,
    ST_FLASHING,
    ST_CONFIGURING,
    ST_STARTING_DAEMONS,
    ST_RUN,
    ST_WAITING_DUMP,
    ST_STOPPING_DAEMONS,
    ST_WAITING_LINK,
    ST_NUM
} hal_pcie_states_t;

static const char *get_event_txt(int evt)
{
    switch (evt) {
    case EV_POWER:     return "REQ: power";
    case EV_BOOT:      return "REQ: boot";
    case EV_STOP:      return "REQ: stop";
    case EV_RESET:     return "REQ: reset";
    case EV_BACKUP:    return "REQ: backup";

    case EV_MDM_OFF:        return "MDM: off";
    case EV_MDM_FLASH:      return "MDM: flash";
    case EV_MDM_CRASH:      return "MDM: crash";
    case EV_MDM_DUMP_READY: return "MDM: dump";
    case EV_MDM_RUN:        return "MDM: run";
    case EV_MDM_LINK_DOWN:  return "LINK: down";

    case EV_MDM_CONFIGURED: return "MDM: configured";
    case EV_MUX_ERR:        return "MUX: hangout";
    case EV_MUX_DEAD:       return "MUX: dead";
    case EV_TIMEOUT:        return "TIMEOUT";

    case EV_RPC_DEAD:  return "RPC: dead";
    case EV_RPC_RUN:   return "RPC: run";

    case EV_NVM_RUN:   return "NVM: run";
    case EV_NVM_STOP:  return "NVM: stop";

    default: ASSERT(0);
    }
}

static const char *get_state_txt(int state)
{
    switch (state) {
    case ST_OFF:              return "OFF";
    case ST_BOOTING:          return "BOOTING";
    case ST_FLASHING:         return "FLASHING";
    case ST_CONFIGURING:      return "CONFIGURING";
    case ST_STARTING_DAEMONS: return "STARTING DAEMONS";
    case ST_STOPPING_DAEMONS: return "STOPPING DAEMONS";
    case ST_RUN:              return "RUN";
    case ST_WAITING_DUMP:     return "WAITING DUMP";
    case ST_WAITING_LINK:     return "WAITING LINK";
    default: ASSERT(0);
    }
}

static inline void notify_ctrl_event_only(crm_ctrl_ctx_t *control, crm_hal_evt_type_t evt)
{
    crm_hal_evt_t event = { .type = evt, "", NULL };

    control->notify_hal_event(control, &event);
}

static void timer_start(crm_hal_ctx_internal_t *i_ctx, int ms, bool update)
{
    ASSERT(i_ctx != NULL);

    if (i_ctx->timer_armed)
        ASSERT(update);

    i_ctx->timer_armed = true;
    crm_time_add_ms(&i_ctx->timer_end, ms);
}

static int get_timeout(crm_hal_ctx_internal_t *i_ctx)
{
    if (i_ctx->timer_armed == false)
        return -1;

    return crm_time_get_remain_ms(&i_ctx->timer_end);
}

static void stop_daemons(crm_hal_ctx_internal_t *i_ctx)
{
    crm_hal_nvm_on_modem_down(i_ctx);
    timer_start(i_ctx, 10000, false); // @TODO: fix timeout
}

static int cfg_modem(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    if (i_ctx->request == REQ_BOOT) {
        ASSERT(!i_ctx->thread_cfg);
        crm_property_set(CRM_KEY_SERVICE_WWAN, "ready");
        i_ctx->request = REQ_NONE;
        i_ctx->timer_armed = false;
        i_ctx->thread_cfg = crm_thread_init(crm_hal_cfg_modem, i_ctx, true, false);

        return ST_CONFIGURING;
    } else {
        return -1;
    }
}

static int link_down_evt(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;

    ASSERT(i_ctx->request == REQ_STOP || i_ctx->request == REQ_RESET);
    ASSERT(i_ctx->timer_armed);

    /* let's notify MDM_BUSY ASAP. By doing this, multiple MDM_BUSY can be sent but control
     * supports it */
    notify_ctrl_event_only(i_ctx->control, HAL_MDM_BUSY);

    if (i_ctx->request == REQ_STOP) {
        if (crm_hal_stop_modem(i_ctx))
            return -2;
        timer_start(i_ctx, 1000, true); // @TODO: fix timeout
    }

    return -1;
}

static int timeout_waiting_link(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    ASSERT(i_ctx->timer_armed == true);

    if (i_ctx->first_expiration) {
        i_ctx->first_expiration = false;

        int err;
        if (i_ctx->request == REQ_STOP)
            err = crm_hal_stop_modem(i_ctx);
        else
            err = crm_hal_cold_reset_modem(i_ctx);

        if (!err) {
            timer_start(i_ctx, 1000, true); // @TODO: fix timeout
            return -1;
        }
    }

    return -2;
}

static int reset_or_stop(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param;

    if (i_ctx->nvm_daemon_syncing)
        return -1;

    if (i_ctx->backup) {
        i_ctx->backup = false;
        ASSERT(!crm_file_copy(i_ctx->nvm_calib_file, i_ctx->nvm_calib_bkup_file,
                              false, i_ctx->nvm_calib_bkup_is_raw, NVM_FILE_PERMISSION));
        LOGV("Calibration backed up in (%s)", i_ctx->nvm_calib_bkup_file);
        mdm_cli_dbg_info_t dbg_info =
        { DBG_TYPE_NVM_BACKUP_SUCCESS, DBG_DEFAULT_NO_LOG, DBG_DEFAULT_NO_LOG,
          DBG_DEFAULT_NO_LOG, 0, NULL };
        i_ctx->control->notify_client(i_ctx->control, MDM_DBG_INFO, sizeof(dbg_info), &dbg_info);
    }

    i_ctx->timer_armed = false;

    switch (i_ctx->request) {
    case REQ_RESET:
        ASSERT(!crm_hal_cold_reset_modem(i_ctx));
        timer_start(i_ctx, 100000, true); // @TODO: fix timeout
        return ST_WAITING_LINK;
    case REQ_STOP:
        ASSERT(!crm_hal_stop_modem(i_ctx));
        if ((EV_MDM_LINK_DOWN == i_ctx->mdm_state) || (EV_MDM_OFF == i_ctx->mdm_state)) {
            return ST_OFF;
        } else {
            timer_start(i_ctx, 1000, true); // @TODO: fix timeout
            return ST_WAITING_LINK;
        }
        break;
    default:
        return -1;
    }
}

static int timeout_daemons(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    if (i_ctx->first_expiration) {
        i_ctx->first_expiration = false;
        ASSERT(i_ctx->nvm_daemon_connected);
        crm_hal_nvm_on_manager_crash(i_ctx);
        i_ctx->timer_armed = false;
        return reset_or_stop(fsm_param, evt_param);
    }

    return -2;
}

static int mdm_off_evt(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;

    ASSERT(i_ctx->request == REQ_STOP || i_ctx->request == REQ_RESET);
    ASSERT(i_ctx->timer_armed);

    notify_ctrl_event_only(i_ctx->control, HAL_MDM_BUSY);

    if (i_ctx->request == REQ_STOP) {
        crm_hal_stop_modem(i_ctx);
        notify_ctrl_event_only(i_ctx->control, HAL_MDM_OFF);

        i_ctx->timer_armed = false;
        i_ctx->request = REQ_NONE;
        return ST_OFF;
    }

    return -1;
}

static int notify_need_reset(crm_hal_ctx_internal_t *i_ctx, mdm_cli_dbg_info_t *dbg_info)
{
    ASSERT(i_ctx);
    ASSERT(dbg_info);

    notify_ctrl_event_only(i_ctx->control, HAL_MDM_BUSY);

    crm_hal_evt_t event = { HAL_MDM_NEED_RESET, "", dbg_info };
    i_ctx->control->notify_hal_event(i_ctx->control, &event);

    crm_hal_nvm_on_modem_down(i_ctx);
    timer_start(i_ctx, 10000, true); // @TODO: fix timeout

    return ST_STOPPING_DAEMONS;
}

static int notify_mux_err(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    const char *data[] = { "MUX hangup" };
    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_ERROR, DBG_DEFAULT_LOG_SIZE,
                                    DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_LOG_TIME, ARRAY_SIZE(data),
                                    data };

    return notify_need_reset(i_ctx, &dbg_info);
}

static int notify_timeout(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    const char *data[] = { "timeout" };
    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_ERROR, DBG_DEFAULT_LOG_SIZE,
                                    DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_LOG_TIME, ARRAY_SIZE(data),
                                    data };

    return notify_need_reset(i_ctx, &dbg_info);
}

static int notify_dump(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    crm_hal_evt_t event = { HAL_MDM_DUMP, "", NULL };
    snprintf(event.nodes, sizeof(event.nodes), "%s", i_ctx->flash_node);
    i_ctx->control->notify_hal_event(i_ctx->control, &event);

    return -1;
}

static int notify_self(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_SELF_RESET, DBG_DEFAULT_LOG_SIZE,
                                    DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_LOG_TIME, 0, NULL };

    return notify_need_reset(i_ctx, &dbg_info);
}

static int notify_crash(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    notify_ctrl_event_only(i_ctx->control, HAL_MDM_BUSY);
    return ST_WAITING_DUMP;
}

static int prepare_dump(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    if (i_ctx->dump_disabled)
        return notify_self(fsm_param, evt_param);
    else
        crm_hal_warm_reset_modem(i_ctx);
    return -1;
}

static int notify_flash(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    ASSERT(i_ctx->timer_armed);
    i_ctx->timer_armed = false;

    notify_ctrl_event_only(i_ctx->control, HAL_MDM_BUSY);

    crm_hal_evt_t event = { HAL_MDM_FLASH, "", NULL };
    snprintf(event.nodes, sizeof(event.nodes), "%s", i_ctx->flash_node);
    i_ctx->control->notify_hal_event(i_ctx->control, &event);

    return ST_FLASHING;
}

static int start_daemons(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    (void)evt_param; // UNUSED
    ASSERT(i_ctx);

    ASSERT(!i_ctx->timer_armed);
    timer_start(i_ctx, 5000, false); // @TODO: fix timeout

    /** @TODO handle the case where NVM manager takes ages to start (?) */
    ASSERT(i_ctx->nvm_daemon_connected && !i_ctx->nvm_daemon_syncing);
    crm_hal_nvm_on_modem_up(i_ctx);

    return ST_STARTING_DAEMONS;
}

static int notify_run(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    (void)evt_param; // UNUSED
    ASSERT(i_ctx);

    ASSERT(i_ctx->timer_armed);
    i_ctx->timer_armed = false;

    notify_ctrl_event_only(i_ctx->control, HAL_MDM_RUN);

    return ST_RUN;
}

static int notify_dead(crm_hal_ctx_internal_t *i_ctx, const char *reason)
{
    ASSERT(i_ctx);
    ASSERT(reason);

    crm_hal_stop_modem(i_ctx);

    notify_ctrl_event_only(i_ctx->control, HAL_MDM_BUSY);

    const char *data[] = { reason };
    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_ERROR, DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_NO_LOG,
                                    DBG_DEFAULT_NO_LOG, ARRAY_SIZE(data), data };
    crm_hal_evt_t event = { HAL_MDM_UNRESPONSIVE, "", &dbg_info };
    i_ctx->control->notify_hal_event(i_ctx->control, &event);

    return ST_OFF;
}

static int mux_dead(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    return notify_dead(i_ctx, "MUX is dead");
}

static int modem_dead(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    return notify_dead(i_ctx, "failsafe");
}

static int request_stop(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    ASSERT(i_ctx->request == REQ_NONE);
    i_ctx->request = REQ_STOP;

    stop_daemons(i_ctx);

    if (EV_MDM_RUN == i_ctx->mdm_state) {
        int fd = open(i_ctx->shutdown_node, O_WRONLY);
        if (fd >= 0) {
            /* modem NEVER answers to this command. That is why the timeout 0 and returned value
             * is not checked */
            crm_send_at(fd, CRM_MODULE_TAG, "AT+CFUN=0", 0, -1);
            close(fd);
        } else {
            LOGD("No AT+CFUN command sent. Modem is probably already dead");
        }
    }

    return ST_STOPPING_DAEMONS;
}

static int request_reset(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param;

    ASSERT(i_ctx->request == REQ_NONE);
    i_ctx->request = REQ_RESET;

    stop_daemons(i_ctx);

    return ST_STOPPING_DAEMONS;
}

static int request_backup(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param;
    i_ctx->backup = true;

    return request_reset(fsm_param, evt_param);
}

static int request_power(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    ASSERT(i_ctx->request == REQ_NONE);
    i_ctx->request = REQ_POWER;

    if (!crm_hal_start_modem(i_ctx)) {
        timer_start(i_ctx, 3000, false); //@TODO: fix timeout
        return ST_BOOTING;
    } else {
        return -2;
    }
}

static int request_boot(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    ASSERT(i_ctx->request == REQ_POWER || i_ctx->request == REQ_RESET);
    i_ctx->request = REQ_BOOT;

    if (i_ctx->mdm_state == EV_MDM_RUN) {
        return cfg_modem(fsm_param, evt_param);
    } else {
        timer_start(i_ctx, 1000, false); //@TODO: fix timeout
        return -1;
    }
}

static int accept_stop(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    i_ctx->request = REQ_STOP;
    return reset_or_stop(fsm_param, evt_param);
}

static int accept_reset(void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)evt_param; // UNUSED

    i_ctx->request = REQ_RESET;
    return reset_or_stop(fsm_param, evt_param);
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

static int todo(void *fsm_param, void *evt_param)
{
    (void)fsm_param; // UNUSED
    (void)evt_param; // UNUSED
    ASSERT(0);
}

static void state_trans(int prev_state, int new_state, int evt, void *fsm_param, void *evt_param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);
    (void)prev_state;
    (void)new_state;
    (void)evt;
    (void)evt_param;

    i_ctx->first_expiration = true;

    switch (prev_state) {
    case ST_RUN:
        LOGD("Stopping RPC daemon");
        crm_property_set(CRM_KEY_SERVICE_STOP, CRM_KEY_CONTENT_SERVICE_RPCD);
        break;
    }
}

/* *INDENT-OFF* */
static const crm_fsm_ops_t g_fsm_array[EV_NUM * ST_NUM] = {
/*                   ST_OFF             ST_BOOTING        ST_FLASHING         ST_CONFIGURING      ST_STARTING_DAEMONS ST_RUN              ST_WAITING_DUMP     ST_STOPPING_DAEMONS  ST_WAITING_LINK         */
/*EV_POWER*/         {-1,request_power},{-1,reject},      {-1,reject},        {-1,reject},        {-1,reject},        {-1,reject},        {-1,reject},        {-1,reject},         {-1,reject},
/*EV_BOOT*/          {-1,reject},       {-1,reject},      {-1,request_boot},  {-1,reject},        {-1,reject},        {-1,reject},        {-1,reject},        {-1,reject},         {-1,reject},
/*EV_STOP*/          {-1,reject},       {-1,reject},      {-1,reject},        {-1,reject},        {-1,reject},        {-1,request_stop},  {-1,request_stop},  {-1,accept_stop},    {-1,reject},
/*EV_RESET*/         {-1,reject},       {-1,reject},      {-1,reject},        {-1,reject},        {-1,reject},        {-1,request_reset}, {-1,request_reset}, {-1,accept_reset},   {-1,reject},
/*EV_BACKUP*/        {-1,reject},       {-1,reject},      {-1,reject},        {-1,reject},        {-1,reject},        {-1,request_backup},{-1,reject},        {-1,reject},         {-1,reject},

/*EV_MDM_OFF*/       {-1,assert},       {-1,notify_self}, {-1,notify_self},   {-1,notify_self},   {-1,notify_self},   {-1,notify_self},   {-1,NULL},          {-1,NULL},           {-1,mdm_off_evt},
/*EV_MDM_FLASH*/     {-1,assert},       {-1,notify_flash},{-1,assert},        {-1,assert},        {-1,assert},        {-1,assert},        {-1,notify_dump},   {-1,todo},           {-1,notify_flash},
/*EV_MDM_RUN*/       {-1,assert},       {-1,assert},      {-1,cfg_modem},     {-1,assert},        {-1,assert},        {-1,assert},        {-1,assert},        {-1,todo},           {-1,assert},
/*EV_MDM_CRASH*/     {-1,todo},         {-1,todo},        {-1,todo},          {-1,todo},          {-1,todo},          {-1,notify_crash},  {-1,assert},        {-1,todo},           {-1,todo},
/*EV_MDM_DUMP_READY*/{-1,assert},       {-1,assert},      {-1,assert},        {-1,assert},        {-1,assert},        {-1,assert},        {-1,prepare_dump},  {-1,todo},           {-1,assert},
/*EV_MDM_LINK_DOWN*/ {-1,assert},       {-1,notify_self}, {-1,notify_self},   {-1,notify_self},   {-1,notify_self},   {-1,notify_self},   {-1,notify_self},   {-1,todo},           {-1,link_down_evt},

/*EV_MDM_CONFIGURED*/{-1,assert},       {-1,assert},      {-1,assert},        {-1,start_daemons}, {-1,assert},        {-1,assert},        {-1,assert},        {-1,todo},           {-1,assert},
/*EV_MUX_ERR*/       {-1,assert},       {-1,assert},      {-1,assert},        {-1,notify_mux_err},{-1,notify_mux_err},{-1,notify_mux_err},{-1,assert},        {-1,NULL},           {-1,assert},
/*EV_MUX_DEAD*/      {-1,assert},       {-1,assert},      {-1,assert},        {-1,mux_dead},      {-1,assert},        {-1,assert},        {-1,assert},        {-1,todo},           {-1,assert},
/*EV_TIMEOUT*/       {-1,assert},       {-1,assert},      {-1,notify_timeout},{-1,notify_timeout},{-1,todo},          {-1,assert},        {-1,todo},          {-1,timeout_daemons},{-1,timeout_waiting_link},

/*EV_RPC_DEAD*/      {-1,assert},       {-1,assert},      {-1,assert},        {-1,assert},        {-1,todo},          {-1,todo},          {-1,assert},        {-1,todo},           {-1,assert},
/*EV_RPC_RUN*/       {-1,assert},       {-1,assert},      {-1,assert},        {-1,assert},        {-1,todo},          {-1,assert},        {-1,assert},        {-1,todo},           {-1,assert},
/*EV_NVM_RUN*/       {-1,assert},       {-1,todo},        {-1,todo},          {-1,todo},          {-1,notify_run},    {-1,todo},          {-1,todo},          {-1,NULL},           {-1,todo},
/*EV_NVM_STOP*/      {-1,assert},       {-1,todo},        {-1,todo},          {-1,todo},          {-1,todo},          {-1,todo},          {-1,todo},          {-1,reset_or_stop},  {-1,todo},
};
/* *INDENT-ON* */

/**
 * @see fsm_hal.h
 */
void *crm_hal_pcie_fsm(crm_thread_ctx_t *thread_ctx, void *param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)param;

    (void)thread_ctx;
    ASSERT(i_ctx != NULL);

    crm_hal_init_modem(i_ctx);
    /* Note: stop RPCD after having stopped modem as stopping RPCD while modem is still running
     *       can lead to a SW watchdog as modem is entering an infinite loop. */
    //crm_hal_rpcd_init(i_ctx);

    /* WA to restart RPC daemon in case of CRM restart */
    LOGD("Stopping RPC daemon");
    crm_property_set(CRM_KEY_SERVICE_STOP, CRM_KEY_CONTENT_SERVICE_RPCD);

    /* On PCIe HAL, modem is always stopped at HAL boot in function crm_hal_init_modem. */
    notify_ctrl_event_only(i_ctx->control, HAL_MDM_OFF);

    crm_fsm_ctx_t *fsm = crm_fsm_init(g_fsm_array, EV_NUM, ST_NUM, ST_OFF, NULL, state_trans,
                                      modem_dead, i_ctx, CRM_MODULE_TAG, get_state_txt,
                                      get_event_txt);

    bool running = true;
    while (running) {
        int c_fd = -1;
        if (i_ctx->thread_cfg)
            c_fd = i_ctx->thread_cfg->get_poll_fd(i_ctx->thread_cfg);

        struct pollfd pfd[FSM_HAL_FDS_TO_POLL + 2] = {
            // requests
            { .fd = i_ctx->ipc->get_poll_fd(i_ctx->ipc), .events = POLLIN },
            // NETLINK - PCIE events
            { .fd = i_ctx->net_fd, .events = POLLIN },
            // CONFIGURE - modem event listener
            { .fd = c_fd, .events = POLLIN },
            // MUX - hangup
            { .fd = i_ctx->mux_fd, .events = POLLIN },
            // INOTIFY - RPC Daemon event listener
            //@TODO: { .fd = crm_hal_rpcd_get_fd(i_ctx), .events = POLLIN },
            { .fd = -1, .events = POLLIN },
        };
        int num_socks = FSM_HAL_FDS_TO_POLL + crm_hal_daemon_get_sockets(&i_ctx->daemon_ctx,
                                                                         &pfd[FSM_HAL_FDS_TO_POLL]);
        int err = poll(pfd, num_socks, get_timeout(i_ctx));

        for (int i = 0; i < FSM_HAL_FDS_TO_POLL; i++) {
            if (pfd[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                /* Do not assert for MUX error. This is handled below */
                DASSERT(i == 3, "error on fd: %d", i);
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
            ASSERT(i_ctx->thread_cfg != NULL);

            while (i_ctx->thread_cfg->get_msg(i_ctx->thread_cfg, &msg)) {
                /* only one message is expected here */
                ASSERT(evt == -1);
                evt = msg.scalar;
            }
            i_ctx->thread_cfg->dispose(i_ctx->thread_cfg, NULL);
            i_ctx->thread_cfg = NULL;

            fsm->notify_event(fsm, evt, NULL);
        } else if (pfd[3].revents) {
            close(i_ctx->mux_fd);
            i_ctx->mux_fd = -1;

            fsm->notify_event(fsm, EV_MUX_ERR, NULL);
        } else if (pfd[4].revents & POLLIN) {
            DASSERT(0, "TODO");
            int evt = crm_hal_rpcd_event(i_ctx);
            if (evt != -1)
                fsm->notify_event(fsm, evt, NULL);
        } else {
            bool handled = false;
            for (int i = FSM_HAL_FDS_TO_POLL; i < num_socks; i++) {
                if (pfd[i].revents) {
                    int evt;
                    crm_hal_daemon_poll_t ret =
                        crm_hal_daemon_handle_poll(&i_ctx->daemon_ctx, &pfd[i], &evt);
                    ASSERT(ret != HAL_DAEMON_POLL_NOT_HANDLED);
                    if (ret == HAL_DAEMON_CALLBACK_RETVALUE_SET && evt != -1)
                        fsm->notify_event(fsm, evt, NULL);
                    handled = true;
                }
            }
            ASSERT(handled);
        }
        //@TODO: handle EINTR errors
    }

    fsm->dispose(fsm);

    return NULL;
}
