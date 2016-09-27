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
#include <string.h>
#include <unistd.h>

#include "libmdmcli/mdm_cli.h"

#define CRM_MODULE_TAG "CTRL"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/fsm.h"
#include "utils/time.h"
#include "utils/string_helpers.h"
#include "plugins/client_abstraction.h"
#include "teljavabridge/tel_java_bridge.h"

#include "common.h"
#include "watchdog.h"

// The time CTRL waits for a HAL reset before acting on the CLA reset request
#define MAX_RESET_TIMEOUT 100 // time in ms

typedef enum ctrl_states {
    ST_INITIAL = 0,
    ST_DOWN,
    ST_PACKAGING,
    ST_FLASHING,
    ST_CUSTOMIZING,
    ST_UP,
    ST_WAITING,
    ST_DUMPING,
    ST_NUM,
} ctrl_states_t;

static const char *get_event_txt(int evt)
{
    switch (evt) {
    case EV_CLI_START:            return "CLI: start";
    case EV_CLI_STOP:             return "CLI: stop";
    case EV_CLI_RESET:            return "CLI: reset";
    case EV_CLI_UPDATE:           return "CLI: update";
    case EV_CLI_NVM_BACKUP:       return "CLI: backup";

    case EV_HAL_MDM_OFF:          return "HAL: off";
    case EV_HAL_MDM_RUN:          return "HAL: run";
    case EV_HAL_MDM_UNRESPONSIVE: return "HAL: unresp";
    case EV_HAL_MDM_BUSY:         return "HAL: busy";
    case EV_HAL_MDM_NEED_RESET:   return "HAL: need_reset";
    case EV_HAL_MDM_FLASH:        return "HAL: flash";
    case EV_HAL_MDM_DUMP:         return "HAL: dump";

    case EV_NVM_SUCCESS:          return "OP : nvm ok";
    case EV_FW_SUCCESS:           return "OP : fw ok";
    case EV_DUMP_SUCCESS:         return "OP : dump ok";
    case EV_FAILURE:              return "OP : err";
    case EV_TIMEOUT:              return "OP : timeout";
    default: ASSERT(0);
    }
}

static const char *get_state_txt(int state)
{
    switch (state) {
    case ST_INITIAL:      return "INITIAL";
    case ST_DOWN:         return "DOWN";
    case ST_PACKAGING:    return "PACKAGING";
    case ST_FLASHING:     return "FLASHING";
    case ST_CUSTOMIZING:  return "CUSTOMIZING";
    case ST_UP:           return "UP";
    case ST_WAITING:      return "WAITING";
    case ST_DUMPING:      return "DUMPING";
    default: ASSERT(0);
    }
}

static void start_timer(crm_control_ctx_internal_t *i_ctx, int timeout)
{
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->timer_armed == false);
    ASSERT(timeout >= 0);

    i_ctx->timer_armed = true;
    crm_time_add_ms(&i_ctx->timer_end, timeout);
}

static int get_timeout(crm_control_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);
    if (i_ctx->timer_armed == false)
        return -1;

    return crm_time_get_remain_ms(&i_ctx->timer_end);
}

static void clear_internal_state(crm_control_ctx_internal_t *i_ctx)
{
    free(i_ctx->state.hal_evt);
    memset(&i_ctx->state, 0, sizeof(i_ctx->state));
}

static inline void watchdog_start(crm_control_ctx_internal_t *i_ctx, int timeout)
{
    i_ctx->watch_id = watchdog_get_new_id(i_ctx->watch_id);
    crm_ipc_msg_t msg =
    { .scalar = watchdog_gen_scalar(CRM_WATCH_START, timeout, i_ctx->watch_id) };
    i_ctx->watchdog->send_msg(i_ctx->watchdog, &msg);
    LOGD("watchdog armed, id: %d", i_ctx->watch_id);
}

static inline void watchdog_stop(crm_control_ctx_internal_t *i_ctx)
{
    crm_ipc_msg_t msg = { .scalar = watchdog_gen_scalar(CRM_WATCH_STOP, 0, i_ctx->watch_id) };

    i_ctx->watchdog->send_msg(i_ctx->watchdog, &msg);
    LOGD("watchdog disarmed, id: %d", i_ctx->watch_id);
}

static inline void notify_op_result_if_needed(crm_control_ctx_internal_t *i_ctx, int status)
{
    if (REQ_NONE != i_ctx->state.client_request) {
        i_ctx->state.client_request = REQ_NONE;
        i_ctx->clients->notify_operation_result(i_ctx->clients, status);
    }
}

static int mdm_flash(crm_control_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    if (i_ctx->state.hal_evt && i_ctx->state.fw_ready) {
        ASSERT(i_ctx->state.hal_evt->type == HAL_MDM_FLASH);
        i_ctx->upload->flash(i_ctx->upload, i_ctx->state.hal_evt->nodes);

        free(i_ctx->state.hal_evt);
        i_ctx->state.hal_evt = NULL;
        i_ctx->state.fw_ready = false;

        return ST_FLASHING;
    } else {
        return -1;
    }
}

static int flash_evt(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;
    crm_hal_evt_t **hal_evt_ptr = (crm_hal_evt_t **)evt_param;

    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->state.hal_evt == NULL);

    i_ctx->state.hal_evt = *hal_evt_ptr;
    *hal_evt_ptr = NULL;

    return mdm_flash(i_ctx);
}

static int fw_ready_evt(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // unused

    i_ctx->state.fw_ready = true;
    return mdm_flash(i_ctx);
}

static int mdm_cfg(crm_control_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    if (i_ctx->state.flash_done && i_ctx->state.run_ipc) {
        i_ctx->state.flash_done = false;
        i_ctx->state.run_ipc = false;

        int nb;
        const char *const *tlvs = i_ctx->elector->get_tlv_list(i_ctx->elector, &nb);
        if (tlvs) {
            ASSERT(nb > 0);
            i_ctx->customization->send(i_ctx->customization, tlvs, nb);
            return ST_CUSTOMIZING;
        } else {
            ASSERT(nb == 0);
            notify_op_result_if_needed(i_ctx, 0);

            i_ctx->clients->notify_modem_state(i_ctx->clients, MDM_STATE_READY);
            return ST_UP;
        }
    } else {
        return -1;
    }
}

static int run_evt_initial(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // unused

    i_ctx->state.run_ipc = true;
    i_ctx->state.flash_done = true;

    return mdm_cfg(i_ctx);
}

static int run_evt(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // unused

    i_ctx->state.run_ipc = true;
    return mdm_cfg(i_ctx);
}

static int flash_success_evt(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // unused

    if (!i_ctx->state.run_ipc)
        i_ctx->hal->boot(i_ctx->hal);

    i_ctx->elector->notify_fw_flashed(i_ctx->elector, 0);
    i_ctx->state.flash_done = true;
    return mdm_cfg(i_ctx);
}

static int mdm_stop(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // UNUSED

    i_ctx->hal->shutdown(i_ctx->hal);

    return ST_WAITING;
}

static void free_dbg_info(mdm_cli_dbg_info_t *dbg_info)
{
    if (dbg_info) {
        for (size_t i = 0; i < dbg_info->nb_data; i++)
            free((char *)dbg_info->data[i]);

        free(dbg_info->data);
        free(dbg_info);
    }
}

static void store_dbg_info(mdm_cli_dbg_info_t **dest, void *evt_param, bool overwrite)
{
    mdm_cli_dbg_info_t **src = (mdm_cli_dbg_info_t **)evt_param;

    if (src && *src) {
        ASSERT(dest);
        if (*dest && overwrite) {
            free_dbg_info(*dest);
            *dest = NULL;
        }
        if (!*dest)
            *dest = *src;
        else
            free_dbg_info(*src);
        *src = NULL;
    }
}

static void notify_dbg_info(crm_control_ctx_internal_t *i_ctx, bool report)
{
    ASSERT(i_ctx != NULL);

    if (report && !i_ctx->dbg_info.do_not_report) {
        if (!i_ctx->dbg_info.evt && i_ctx->dbg_info.reset_initiated_by_cla) {
            mdm_cli_dbg_info_t dbg_info_apimr = { DBG_TYPE_APIMR, DBG_DEFAULT_LOG_SIZE,
                                                  DBG_DEFAULT_NO_LOG, DBG_DEFAULT_NO_LOG, 0, NULL };

            i_ctx->dbg_info.evt = malloc(sizeof(mdm_cli_dbg_info_t));
            ASSERT(i_ctx->dbg_info.evt);
            *i_ctx->dbg_info.evt = dbg_info_apimr;
        }

        ASSERT(i_ctx->dbg_info.evt);
        i_ctx->clients->notify_client(i_ctx->clients, MDM_DBG_INFO, sizeof(mdm_cli_dbg_info_t),
                                      i_ctx->dbg_info.evt);
    }

    i_ctx->dbg_info.do_not_report = false;
    i_ctx->dbg_info.reset_initiated_by_cla = false;

    free_dbg_info(i_ctx->dbg_info.evt);
    i_ctx->dbg_info.evt = NULL;
}

static int mdm_restart(crm_control_ctx_internal_t *i_ctx, bool report_dbg_info, bool backup)
{
    ASSERT(i_ctx != NULL);

    notify_dbg_info(i_ctx, report_dbg_info);

    i_ctx->upload->package(i_ctx->upload, i_ctx->elector->get_fw_path(i_ctx->elector));

    i_ctx->hal->reset(i_ctx->hal, backup ? RESET_BACKUP : RESET_COLD);

    tel_java_brige_broadcast_intent("com.intel.action.MODEM_COLD_RESET", "instId%d",
                                    i_ctx->inst_id);

    return ST_PACKAGING;
}

static int set_oos(crm_control_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->is_mdm_oos != true);

    notify_dbg_info(i_ctx, true);

    i_ctx->clients->notify_modem_state(i_ctx->clients, MDM_STATE_UNRESP);
    notify_op_result_if_needed(i_ctx, -1);
    i_ctx->is_mdm_oos = true;

    tel_java_brige_broadcast_intent("com.intel.action.MODEM_OUT_OF_SERVICE", "instId%d",
                                    i_ctx->inst_id);

    return ST_DOWN;
}

static int fw_flash_failure(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    store_dbg_info(&i_ctx->dbg_info.evt, evt_param, true);

    return set_oos(i_ctx);
}

static int platform_reboot(crm_control_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    notify_dbg_info(i_ctx, true);

    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_PLATFORM_REBOOT, DBG_DEFAULT_LOG_SIZE,
                                    DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_LOG_TIME, 0, NULL };
    i_ctx->clients->notify_client(i_ctx->clients, MDM_DBG_INFO, sizeof(dbg_info), &dbg_info);

    i_ctx->clients->notify_modem_state(i_ctx->clients, MDM_STATE_PLATFORM_REBOOT);
    notify_op_result_if_needed(i_ctx, -1);
    i_ctx->is_mdm_oos = true;

    tel_java_brige_broadcast_intent("com.intel.action.PLATFORM_REBOOT", "instId%d", i_ctx->inst_id);

    while (!tel_java_brige_broadcast_intent("android.intent.action.REBOOT", "nowait%d", 1))
        usleep(500 * 1000);

    return ST_DOWN;
}

static int escalation(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;
    int ret = -1;

    ASSERT(i_ctx != NULL);

    store_dbg_info(&i_ctx->dbg_info.evt, evt_param, true);

    crm_escalation_next_step_t step = i_ctx->escalation->get_next_step(i_ctx->escalation);

    switch (step) {
    case STEP_MDM_WARM_RESET:
        // @TODO: Add handling for warm reset
        DASSERT(0, "not implemented");
        break;
    case STEP_MDM_COLD_RESET:
        ret = mdm_restart(i_ctx, true, false);
        break;
    case STEP_PLATFORM_REBOOT:
        ret = platform_reboot(i_ctx);
        break;
    case STEP_OOS:
        ret = set_oos(i_ctx);
        break;
    default:
        ASSERT(0);
    }
    return ret;
}

static int requested_operation(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);

    store_dbg_info(&i_ctx->dbg_info.evt, evt_param, true);

    if (REQ_NONE == i_ctx->state.client_request)
        return ST_WAITING;
    else if ((REQ_RESET == i_ctx->state.client_request) ||
             (REQ_START == i_ctx->state.client_request))
        return escalation(fsm_param, evt_param);
    else if (REQ_STOP == i_ctx->state.client_request)
        return mdm_stop(fsm_param, evt_param);
    else
        ASSERT(0);
}

static int accept_stop(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    (void)evt_param;  // UNUSED
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->state.client_request == REQ_NONE);

    i_ctx->state.client_request = REQ_STOP;

    return -1;
}

static int accept_reset(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // UNUSED
    ASSERT(i_ctx->state.client_request == REQ_NONE);

    /* CLA debug info is not stored in this UC */
    i_ctx->state.client_request = REQ_RESET;

    return -1;
}

static int request_start(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->state.client_request == REQ_NONE);
    (void)evt_param; // UNUSED

    i_ctx->state.client_request = REQ_START;
    if (i_ctx->is_mdm_oos) {
        notify_op_result_if_needed(i_ctx, -1);
        return -1;
    } else {
        i_ctx->upload->package(i_ctx->upload, i_ctx->elector->get_fw_path(i_ctx->elector));
        i_ctx->hal->power_on(i_ctx->hal);
        return ST_PACKAGING;
    }
}

static int request_reset(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);

    store_dbg_info(&i_ctx->dbg_info.evt, evt_param, false);

    ASSERT(i_ctx->state.client_request == REQ_NONE);
    i_ctx->state.client_request = REQ_RESET;

    if (i_ctx->state.waiting_hal_busy_reason)
        return -1;
    else
        return escalation(fsm_param, NULL);
}

static int request_reset_timer(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);

    store_dbg_info(&i_ctx->dbg_info.evt, evt_param, false);

    ASSERT(i_ctx->state.client_request == REQ_NONE);
    i_ctx->state.client_request = REQ_RESET;

    i_ctx->dbg_info.reset_initiated_by_cla = true;
    start_timer(i_ctx, MAX_RESET_TIMEOUT); // @TODO: configure timeout

    return -1;
}

static int request_update(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    (void)evt_param;  // UNUSED
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->state.client_request == REQ_NONE);

    i_ctx->dbg_info.do_not_report = true;
    i_ctx->state.client_request = REQ_RESET;

    if (i_ctx->state.waiting_hal_busy_reason)
        return -1;
    else
        return mdm_restart(i_ctx, false, false);
}

static int request_backup(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    (void)evt_param;  // UNUSED
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->state.client_request == REQ_NONE);

    i_ctx->state.client_request = REQ_RESET;
    return mdm_restart(i_ctx, false, true);
}

static int request_stop(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->state.client_request == REQ_NONE);

    i_ctx->state.client_request = REQ_STOP;
    if (i_ctx->state.waiting_hal_busy_reason)
        return -1;
    else
        return mdm_stop(fsm_param, evt_param);
}

static int reset_after_tlv(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // UNUSED

    tel_java_brige_broadcast_intent("com.intel.action.MODEM_TLV_APPLY_SUCCESS", "instId%d",
                                    i_ctx->inst_id);

    i_ctx->elector->notify_tlv_applied(i_ctx->elector, 0);

    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_TLV_SUCCESS, DBG_DEFAULT_NO_LOG, DBG_DEFAULT_NO_LOG,
                                    DBG_DEFAULT_NO_LOG, 0, NULL };
    i_ctx->clients->notify_client(i_ctx->clients, MDM_DBG_INFO, sizeof(dbg_info), &dbg_info);

    return mdm_restart(i_ctx, false, false);
}

static int flash_failure(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // UNUSED

    LOGD("Flashing failure");
    //@TODO: fix the failure here
    i_ctx->elector->notify_fw_flashed(i_ctx->elector, -1);
    return -2;
}

static int custo_failure(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // UNUSED

    tel_java_brige_broadcast_intent("com.intel.action.MODEM_TLV_APPLY_ERROR", "instId%d",
                                    i_ctx->inst_id);

    LOGD("Customization failure");
    //@TODO: fix the failure here
    i_ctx->elector->notify_tlv_applied(i_ctx->elector, -1);

    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_TLV_FAILURE, DBG_DEFAULT_NO_LOG, DBG_DEFAULT_NO_LOG,
                                    DBG_DEFAULT_NO_LOG, 0, NULL };
    i_ctx->clients->notify_client(i_ctx->clients, MDM_DBG_INFO, sizeof(dbg_info), &dbg_info);
    return -2;
}

static int pack_failure(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // UNUSED

    LOGD("Packaging failure");
    //@TODO: fix the failure here
    i_ctx->elector->notify_fw_flashed(i_ctx->elector, -1);
    return -2;
}

static int notify_off(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // UNUSED

    notify_op_result_if_needed(i_ctx, 0);
    i_ctx->clients->notify_modem_state(i_ctx->clients, MDM_STATE_OFF);

    return ST_DOWN;
}

static int notify_unresp(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);

    store_dbg_info(&i_ctx->dbg_info.evt, evt_param, true);

    crm_escalation_next_step_t last_step = i_ctx->escalation->get_last_step(i_ctx->escalation);
    if (STEP_PLATFORM_REBOOT == last_step)
        return platform_reboot(i_ctx);
    else if (STEP_OOS == last_step)
        return set_oos(i_ctx);
    else
        ASSERT(0);

    return -2;
}

static int notify_busy(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // UNUSED

    i_ctx->clients->notify_modem_state(i_ctx->clients, MDM_STATE_BUSY);

    return -1;
}

static int dump_start(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;
    crm_hal_evt_t **dump_evt_ptr = (crm_hal_evt_t **)evt_param;

    ASSERT(i_ctx != NULL);
    ASSERT(*dump_evt_ptr != NULL);

    i_ctx->dbg_info.do_not_report = true;

    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_DUMP_START, DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_NO_LOG,
                                    DBG_DEFAULT_NO_LOG, 0, NULL };
    i_ctx->clients->notify_client(i_ctx->clients, MDM_DBG_INFO, sizeof(dbg_info), &dbg_info);

    tel_java_brige_broadcast_intent("com.intel.action.CORE_DUMP_WARNING", "instId%d",
                                    i_ctx->inst_id);

    ASSERT(i_ctx->dump);
    i_ctx->dump->read(i_ctx->dump, (*dump_evt_ptr)->nodes,
                      i_ctx->elector->get_fw_path(i_ctx->elector));

    return ST_DUMPING;
}

static int dump_end(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);

    tel_java_brige_broadcast_intent("com.intel.action.CORE_DUMP_COMPLETE", "instId%d",
                                    i_ctx->inst_id);

    return requested_operation(fsm_param, evt_param);
}

static int dump_error(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param; // UNUSED

    LOGE("core dump interrupted by HAL event");
    i_ctx->dump->stop(i_ctx->dump);

    const char *data[] = { DUMP_STR_LINK_ERR };
    mdm_cli_dbg_info_t dbg = { DBG_TYPE_ERROR, DBG_DEFAULT_LOG_SIZE,
                               DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_LOG_TIME,
                               ARRAY_SIZE(data), data };
    i_ctx->clients->notify_client(i_ctx->clients, MDM_DBG_INFO, sizeof(dbg), &dbg);

    return requested_operation(fsm_param, NULL);
}

static int todo(void *fsm_param, void *evt_param)
{
    (void)fsm_param;  // UNUSED
    (void)evt_param;  // UNUSED
    DASSERT(0, "transition not handled");
}

static int assert(void *fsm_param, void *evt_param)
{
    (void)fsm_param;  // UNUSED
    (void)evt_param;  // UNUSED
    DASSERT(0, "transition not supported");
}

static int failsafe(void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // UNUSED

    LOGD("************* FAILSAFE *****************");
    notify_op_result_if_needed(i_ctx, -1);

    clear_internal_state(i_ctx);

    return request_stop(fsm_param, evt_param);
}

static void state_trans(int prev_state, int new_state, int evt, void *fsm_param, void *evt_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx != NULL);
    (void)evt_param;  // UNUSED
    (void)evt;        // UNUSED

    ASSERT(new_state != ST_INITIAL);

    /* Handling of 'exit state' */
    if ((ST_UP == prev_state) || (ST_DOWN == prev_state))
        watchdog_start(i_ctx, i_ctx->timeout);

    if (ST_UP == prev_state && i_ctx->timer_armed)
        i_ctx->timer_armed = false;

    /* Handling of 'enter state' */
    if ((ST_UP == new_state) || (ST_DOWN == new_state))
        watchdog_stop(i_ctx);
}

static void pre_op(int evt, void *fsm_param)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)fsm_param;

    ASSERT(i_ctx);

    switch (evt) {
    case EV_HAL_MDM_BUSY:
        i_ctx->state.waiting_hal_busy_reason = true;
        break;
    case EV_HAL_MDM_NEED_RESET:
    case EV_HAL_MDM_FLASH:
    case EV_HAL_MDM_DUMP:
    case EV_HAL_MDM_UNRESPONSIVE:
        ASSERT(i_ctx->state.waiting_hal_busy_reason);
        i_ctx->state.waiting_hal_busy_reason = false;
        break;
    default:
        break;
    }
}

/**
 * @TODO: not handled in the current FSM:
 *  - error cases
 *  - failsafe: this mechanism must be reviewed. it doesn't work
 */

/* *INDENT-OFF* */
static const crm_fsm_ops_t g_fsm_array[EV_NUM * ST_NUM] = {
/*                         ST_INITIAL           ST_DOWN            ST_PACKAGING       ST_FLASHING            ST_CUSTOMIZING       ST_UP                    ST_WAITING               ST_DUMPING               */
/*EV_CLI_START*/           {-1,assert},         {-1,request_start},{-1,assert},       {-1,assert},           {-1,assert},         {-1,assert},             {-1,assert},             {-1,assert},

/*EV_CLI_STOP*/            {-1,assert},         {-1,assert},       {-1,assert},       {-1,assert},           {-1,assert},         {-1,request_stop},       {-1,request_stop},       {-1,accept_stop},
/*EV_CLI_RESET*/           {-1,assert},         {-1,request_start},{-1,assert},       {-1,assert},           {-1,assert},         {-1,request_reset_timer},{-1,request_reset},      {-1,accept_reset},
/*EV_CLI_UPDATE*/          {-1,assert},         {-1,request_start},{-1,assert},       {-1,assert},           {-1,assert},         {-1,request_update},     {-1,request_update},     {-1,accept_reset},
/*EV_CLI_NVM_BACKUP*/      {-1,assert},         {-1,assert},       {-1,assert},       {-1,assert},           {-1,assert},         {-1,request_backup},     {-1,assert},             {-1,NULL},

/*EV_HAL_MDM_OFF*/         {-1,notify_off},     {-1,NULL},         {-1,escalation},   {-1,todo},             {-1,todo},           {-1,todo},               {-1,notify_off},         {-1,assert},
/*EV_HAL_MDM_RUN*/         {-1,run_evt_initial},{-1,mdm_stop},     {-1,escalation},   {-1,run_evt},          {-1,todo},           {-1,assert},             {-1,escalation},         {-1,assert},
/*EV_HAL_MDM_BUSY*/        {-1,assert},         {-1,NULL},         {-1,notify_busy},  {-1,NULL},             {-1,NULL},           {ST_WAITING,notify_busy},{-1,NULL},               {-1,dump_error},
/*EV_HAL_MDM_NEED_RESET*/  {-1,assert},         {-1,todo},         {-1,escalation},   {-1,escalation},       {-1,escalation},     {-1,assert},             {-1,requested_operation},{-1,assert},
/*EV_HAL_MDM_FLASH*/       {-1,assert},         {-1,todo},         {-1,flash_evt},    {-1,assert},           {-1,todo},           {-1,assert},             {-1,requested_operation},{-1,assert},
/*EV_HAL_MDM_DUMP*/        {-1,assert},         {-1,dump_start},   {-1,dump_start},   {-1,dump_start},       {-1,dump_start},     {-1,assert},             {-1,dump_start},         {-1,assert},
/*EV_HAL_MDM_UNRESPONSIVE*/{-1,assert},         {-1,assert},       {-1,notify_unresp},{-1,fw_flash_failure}, {-1,assert},         {-1,assert},             {-1,notify_unresp},      {-1,assert},

/*EV_NVM_SUCCESS*/         {-1,assert},         {-1,todo},         {-1,todo},         {-1,todo},             {-1,todo},           {-1,todo},               {-1,todo},               {-1,assert},
/*EV_FW_SUCCESS*/          {-1,assert},         {-1,todo},         {-1,fw_ready_evt}, {-1,flash_success_evt},{-1,reset_after_tlv},{-1,todo},               {-1,todo},               {-1,assert},
/*EV_DUMP_SUCCESS*/        {-1,assert},         {-1,todo},         {-1,todo},         {-1,todo},             {-1,todo},           {-1,todo},               {-1,todo},               {-1,dump_end},

/*EV_FAILURE*/             {-1,assert},         {-1,failsafe},     {-1,pack_failure}, {-1,flash_failure},    {-1,custo_failure},  {-1,failsafe},           {-1,failsafe},           {-1,requested_operation},
/*EV_TIMEOUT*/             {-1,assert},         {-1,assert},       {-1,assert},       {-1,assert},           {-1,assert},         {-1,escalation},         {-1,assert},             {-1,assert},
};
/* *INDENT-ON* */

/**
 * @see control.h
 */
void event_loop(crm_ctrl_ctx_t *ctx)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    crm_fsm_ctx_t *fsm = crm_fsm_init(g_fsm_array, EV_NUM, ST_NUM, ST_INITIAL, pre_op, state_trans,
                                      failsafe, i_ctx, CRM_MODULE_TAG, get_state_txt,
                                      get_event_txt);

    struct pollfd pfd[] = {
        { .fd = i_ctx->ipc->get_poll_fd(i_ctx->ipc), .events = POLLIN },
        { .fd = i_ctx->watchdog->get_poll_fd(i_ctx->watchdog), .events = POLLIN },
    };

    /* Start watchdog to not stay indefinitely in INITIAL state */
    watchdog_start(i_ctx, i_ctx->timeout);

    bool running = true;
    while (running) {
        int ret = poll(pfd, ARRAY_SIZE(pfd), get_timeout(i_ctx));

        for (size_t i = 0; i < ARRAY_SIZE(pfd); i++) {
            if (pfd[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                DASSERT(0, "error in control socket %zu", i);
        }

        if (ret == 0) {
            ASSERT(i_ctx->timer_armed == true);
            fsm->notify_event(fsm, EV_TIMEOUT, NULL);
        } else {
            if (pfd[0].revents & POLLIN) {
                crm_ipc_msg_t msg;
                while (i_ctx->ipc->get_msg(i_ctx->ipc, &msg)) {
                    if (-1 == msg.scalar) {
                        running = false;
                        continue;
                    } else {
                        void *msg_ptr = msg.data_size > 0 ? msg.data : NULL;
                        fsm->notify_event(fsm, msg.scalar, &msg_ptr);
                        free(msg_ptr);
                    }
                }
            } else if (pfd[1].revents & POLLIN) {
                crm_ipc_msg_t msg;
                while (i_ctx->watchdog->get_msg(i_ctx->watchdog, &msg)) {
                    enum watchdog_requests request = watchdog_get_request(msg.scalar);
                    int id = watchdog_get_id(msg.scalar);
                    ASSERT(CRM_WATCH_PING == request);

                    msg.scalar = watchdog_gen_scalar(CRM_WATCH_PONG, 0, id);
                    i_ctx->watchdog->send_msg(i_ctx->watchdog, &msg);
                }
            }
        }
    }
}
