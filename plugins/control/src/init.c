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

#include <stdio.h>
#include <string.h>

#define CRM_MODULE_TAG "CTRL"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/keys.h"
#include "utils/property.h"
#include "utils/wakelock.h"
#include "utils/debug.h"

#include "common.h"
#include "fsm_ctrl.h"
#include "notify.h"
#include "request.h"
#include "watchdog.h"

static void load_plugins(crm_control_ctx_internal_t *i_ctx, tcs_ctx_t *tcs)
{
    ASSERT(i_ctx != NULL);
    ASSERT(tcs);

    char value[CRM_PROPERTY_VALUE_MAX];
    crm_property_get(CRM_KEY_DBG_LOAD_STUB, value, "false");
    bool load_hw_stub = strcmp(value, "true") == 0;

    ASSERT(tcs->select_group(tcs, ".control.plugins") == 0);

    if (!load_hw_stub)
        ASSERT(!crm_plugin_load(tcs, "hal", CRM_HAL_INIT, &i_ctx->plugins[PLUGIN_HAL]));
    else
        ASSERT(!crm_plugin_load(tcs, "hal_stub", CRM_HAL_INIT, &i_ctx->plugins[PLUGIN_HAL]));

    ASSERT(!crm_plugin_load(tcs, "clients", CRM_CLI_ABS_INIT, &i_ctx->plugins[PLUGIN_CLIENTS]));
    ASSERT(!crm_plugin_load(tcs, "fw_upload", CRM_FW_UPLOAD_INIT,
                            &i_ctx->plugins[PLUGIN_FW_UPLOAD]));
    ASSERT(!crm_plugin_load(tcs, "fw_elector", CRM_FW_ELECTOR_INIT,
                            &i_ctx->plugins[PLUGIN_FW_ELECTOR]));
    ASSERT(!crm_plugin_load(tcs, "customization", CRM_CUSTOMIZATION_INIT,
                            &i_ctx->plugins[PLUGIN_CUSTOMIZATION]));
    ASSERT(!crm_plugin_load(tcs, "wakelock", CRM_WAKELOCK_INIT, &i_ctx->plugins[PLUGIN_WAKELOCK]));
    ASSERT(!crm_plugin_load(tcs, "escalation", CRM_ESCALATION_INIT,
                            &i_ctx->plugins[PLUGIN_ESCALATION]));

    /* Dump plugin is optional: no ASSERT */
    crm_plugin_load(tcs, "dump", CRM_DUMP_INIT, &i_ctx->plugins[PLUGIN_DUMP]);
}

static void unload_plugins(crm_control_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    for (size_t i = 0; i < PLUGIN_NB; i++) {
        if (i_ctx->plugins[i].handle)
            crm_plugin_unload(&i_ctx->plugins[i]);
    }
}

static void start_plugins(crm_control_ctx_internal_t *i_ctx, int ping_period, tcs_ctx_t *tcs,
                          crm_process_factory_ctx_t *factory)
{
    ASSERT(i_ctx);
    ASSERT(tcs);
    ASSERT(factory);

    /* === STATIC PLUGINS === */
    i_ctx->ipc = crm_ipc_init(CRM_IPC_THREAD); /* Better to init IPC first */

    /* === INTERNAL SERVICES === */
    char wakelock_name[5];
    snprintf(wakelock_name, sizeof(wakelock_name), "crm%d", i_ctx->inst_id);
    i_ctx->wakelock = ((crm_wakelock_init_t)i_ctx->plugins[PLUGIN_WAKELOCK].init)(wakelock_name);

    watchdog_param_t *cfg_watch = malloc(sizeof(watchdog_param_t));
    ASSERT(cfg_watch != NULL);
    cfg_watch->wakelock = i_ctx->wakelock;
    cfg_watch->ping_period = ping_period;

    i_ctx->watchdog = crm_thread_init(crm_watchdog_loop, cfg_watch, true, false);

    /* === DYNAMIC PLUGINS === */
    bool sanity_mode = crm_is_in_sanity_test_mode();
    bool dump_enabled = i_ctx->plugins[PLUGIN_DUMP].init != NULL;

    char value[CRM_PROPERTY_VALUE_MAX];
    crm_property_get(CRM_KEY_DBG_HOST, value, "false");
    bool host_debug = strcmp(value, "true") == 0;

    i_ctx->clients = ((crm_cli_abs_init_t)i_ctx->plugins[PLUGIN_CLIENTS].init)
                         (i_ctx->inst_id, sanity_mode, tcs, &i_ctx->ctx, i_ctx->wakelock);
    i_ctx->hal = ((crm_hal_init_t)i_ctx->plugins[PLUGIN_HAL].init)
                     (i_ctx->inst_id, host_debug, dump_enabled, tcs, &i_ctx->ctx);
    i_ctx->upload = ((crm_fw_upload_init_t)i_ctx->plugins[PLUGIN_FW_UPLOAD].init)
                        (i_ctx->inst_id, true, tcs, &i_ctx->ctx, factory);
    i_ctx->elector = ((crm_fw_elector_init_t)i_ctx->plugins[PLUGIN_FW_ELECTOR].init)
                         (tcs, i_ctx->inst_id);
    i_ctx->customization = ((crm_customization_init_t)i_ctx->plugins[PLUGIN_CUSTOMIZATION].init)
                               (tcs, &i_ctx->ctx);
    i_ctx->escalation = ((crm_escalation_init_t)i_ctx->plugins[PLUGIN_ESCALATION].init)
                            (sanity_mode, tcs);
    if (dump_enabled)
        i_ctx->dump = ((crm_dump_init_t)i_ctx->plugins[PLUGIN_DUMP].init)
                          (tcs, &i_ctx->ctx, factory, host_debug);
}

static void stop_plugins(crm_control_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    crm_ipc_msg_t msg = { .scalar = -1 };
    i_ctx->ipc->send_msg(i_ctx->ipc, &msg);

    i_ctx->watchdog->send_msg(i_ctx->watchdog, &msg);

    i_ctx->clients->dispose(i_ctx->clients);
    i_ctx->hal->dispose(i_ctx->hal);
    i_ctx->upload->dispose(i_ctx->upload);
    i_ctx->customization->dispose(i_ctx->customization);
    i_ctx->watchdog->dispose(i_ctx->watchdog, NULL);
    i_ctx->wakelock->dispose(i_ctx->wakelock);
    i_ctx->elector->dispose(i_ctx->elector);
    if (i_ctx->dump)
        i_ctx->dump->dispose(i_ctx->dump);

    /* Better to dispose IPC at last */
    i_ctx->ipc->dispose(i_ctx->ipc, NULL);
}

/**
 * @see control.h
 */
static void dispose(crm_ctrl_ctx_t *ctx)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    stop_plugins(i_ctx);
    unload_plugins(i_ctx);
    free(ctx);
}

/**
 * @see control.h
 */
crm_ctrl_ctx_t *crm_ctrl_init(int inst_id, tcs_ctx_t *tcs, crm_process_factory_ctx_t *factory)
{
    ASSERT(tcs);
    ASSERT(factory);

    crm_control_ctx_internal_t *i_ctx = calloc(1, sizeof(crm_control_ctx_internal_t));
    ASSERT(i_ctx != NULL);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.start = crm_ctrl_start;
    i_ctx->ctx.stop = crm_ctrl_stop;
    i_ctx->ctx.restart = crm_ctrl_restart;
    i_ctx->ctx.event_loop = event_loop;

    i_ctx->ctx.notify_hal_event = notify_hal_event;
    i_ctx->ctx.notify_nvm_status = notify_nvm_status;
    i_ctx->ctx.notify_fw_upload_status = notify_fw_upload_status;
    i_ctx->ctx.notify_customization_status = notify_customization_status;
    i_ctx->ctx.notify_dump_status = notify_dump_status;
    i_ctx->ctx.notify_client = notify_client;

    i_ctx->inst_id = inst_id;
    i_ctx->watch_id = -1;

    /* load_plugins() function will change the group. All root parameters are get here then */
    ASSERT(tcs->select_group(tcs, ".control") == 0);
    ASSERT(tcs->get_int(tcs, "watchdog_timeout", &i_ctx->timeout) == 0);

    int ping_period;
    ASSERT(tcs->get_int(tcs, "ping_period", &ping_period) == 0);

    load_plugins(i_ctx, tcs);
    start_plugins(i_ctx, ping_period, tcs, factory);

    LOGV("context %p", i_ctx);
    return &i_ctx->ctx;
}
