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
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "libmdmcli/mdm_cli.h"

#define CRM_MODULE_TAG "CLAS"
#include "utils/common.h"
#include "utils/string_helpers.h"
#include "plugins/client_abstraction.h"
#include "plugins/control.h"
#include "plugins/hal.h"

#define TEST_DBG_DATA "test debug data"

typedef struct crm_cli_abs_internal_ctx {
    crm_cli_abs_ctx_t ctx; // Needs to be first
    bool done;

    crm_ctrl_ctx_t *control_ctx;
    mdm_cli_dbg_type_t type;
} crm_cli_abs_internal_ctx_t;


static void check_result(crm_cli_abs_internal_ctx_t *i_ctx, int *count)
{
    ASSERT(i_ctx != NULL);

    if (i_ctx->done == false) {
        LOGE("failure during test %d", *count);
        ASSERT(0);
    }
    i_ctx->done = false;
    (*count)++;
}

/**
 * @see client_abstraction.h
 */
static void notify_operation_result(crm_cli_abs_ctx_t *ctx, int result)
{
    crm_cli_abs_internal_ctx_t *i_ctx = (crm_cli_abs_internal_ctx_t *)ctx;

    ASSERT(i_ctx != NULL);

    if (0 == result)
        i_ctx->done = true;
    else
        i_ctx->done = false;
}

/**
 * @see client_abstraction.h
 */
static void notify_client(crm_cli_abs_ctx_t *ctx, mdm_cli_event_t evt_id, size_t data_size,
                          const void *data)
{
    LOGD("(%p, %d [%s], %zd, %p)", ctx, evt_id,
         crm_mdmcli_wire_req_to_string(evt_id), data_size, data);

    crm_cli_abs_internal_ctx_t *i_ctx = (crm_cli_abs_internal_ctx_t *)ctx;
    ASSERT(i_ctx != NULL);
    ASSERT(evt_id == MDM_DBG_INFO);

    mdm_cli_dbg_info_t *dbg = (mdm_cli_dbg_info_t *)data;
    ASSERT(dbg != NULL);

    int status = -1;

    DASSERT(i_ctx->type == dbg->type, "type %d received instead of %d", dbg->type, i_ctx->type);

    if (dbg->type == DBG_TYPE_SELF_RESET) {
        if (dbg->nb_data > 0) {
            for (size_t i = 0; i < dbg->nb_data; i++) {
                ASSERT(dbg->data[i] != NULL);
                ASSERT(strcmp((char *)dbg->data[i], TEST_DBG_DATA) == 0);
            }
            LOGD("Timer expired, Debug info sent by CLA ");
            status = 0;
        }
    }
    notify_operation_result(ctx, status);
}

static void notify_ctrl(crm_ctrl_ctx_t *control, crm_hal_evt_type_t *evt, size_t nb)
{
    for (size_t i = 0; i < nb; i++) {
        crm_hal_evt_t event = { evt[i], "", NULL };
        control->notify_hal_event(control, &event);
    }
}

static void fake_mdm_reset(crm_ctrl_ctx_t *control)
{
    ASSERT(control != NULL);
    crm_hal_evt_type_t evt[] = { HAL_MDM_BUSY };
    notify_ctrl(control, evt, ARRAY_SIZE(evt));

    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_SELF_RESET, DBG_DEFAULT_LOG_SIZE,
                                    DBG_DEFAULT_NO_LOG, DBG_DEFAULT_NO_LOG, 0, NULL };
    crm_hal_evt_t event = { HAL_MDM_NEED_RESET, "", &dbg_info };
    control->notify_hal_event(control, &event);
}

/**
 * @see client_abstraction.h
 */
static void notify_modem_state(crm_cli_abs_ctx_t *ctx, crm_cli_abs_mdm_state_t mdm_state)
{
    LOGD("(%p,%d [%s])", ctx, mdm_state, crm_cli_abs_mdm_state_to_string(mdm_state));
    crm_cli_abs_internal_ctx_t *i_ctx = (crm_cli_abs_internal_ctx_t *)ctx;
    static int count = 0;
    ASSERT(i_ctx != NULL);
    const char *data[2] = { TEST_DBG_DATA, TEST_DBG_DATA };
    mdm_cli_dbg_info_t dbg = { DBG_TYPE_APIMR, DBG_DEFAULT_NO_LOG, DBG_DEFAULT_NO_LOG,
                               DBG_DEFAULT_NO_LOG, ARRAY_SIZE(data), data };

    if ((count == 0) && (MDM_STATE_READY == mdm_state)) {
        i_ctx->control_ctx->stop(i_ctx->control_ctx);
        count++;
    } else if ((count == 1) && (MDM_STATE_OFF == mdm_state)) {
        check_result(i_ctx, &count);
        i_ctx->control_ctx->restart(i_ctx->control_ctx, CTRL_MODEM_RESTART, &dbg);
    } else if ((count == 2) && (MDM_STATE_READY == mdm_state)) {
        check_result(i_ctx, &count);
        i_ctx->control_ctx->restart(i_ctx->control_ctx, CTRL_MODEM_UPDATE, NULL);
    } else if ((count == 3) && (MDM_STATE_READY == mdm_state)) {
        check_result(i_ctx, &count);
        i_ctx->control_ctx->restart(i_ctx->control_ctx, CTRL_MODEM_UPDATE, NULL);
    } else if ((count == 4) && (MDM_STATE_READY == mdm_state)) {
        check_result(i_ctx, &count);
        i_ctx->control_ctx->restart(i_ctx->control_ctx, CTRL_MODEM_RESTART, &dbg);
    } else if ((count == 5) && (MDM_STATE_READY == mdm_state)) {
        check_result(i_ctx, &count);
        i_ctx->control_ctx->stop(i_ctx->control_ctx);
    } else if ((count == 6) && (MDM_STATE_OFF == mdm_state)) {
        check_result(i_ctx, &count);
        i_ctx->control_ctx->start(i_ctx->control_ctx);
    } else if ((count == 7) && (MDM_STATE_READY == mdm_state)) {
        check_result(i_ctx, &count);
        i_ctx->control_ctx->restart(i_ctx->control_ctx, CTRL_MODEM_RESTART, &dbg);
        /* Send hal reset event after cla reset event.
         * ugly hack that sends an HAL event from the CLA thread because
         * the "HAL stub" is not designed to support this kind of operation. */
        i_ctx->type = DBG_TYPE_SELF_RESET;
        fake_mdm_reset(i_ctx->control_ctx);
    } else if ((count == 8) && (MDM_STATE_READY == mdm_state)) {
        check_result(i_ctx, &count);
        i_ctx->control_ctx->stop(i_ctx->control_ctx);
    } else if ((count == 9) && (MDM_STATE_OFF == mdm_state)) {
        check_result(i_ctx, &count);
        LOGD("****** TEST SUCCEED. Exit without cleanup. Expect memory leaks ******");
        exit(0);
    }
}

/**
 * @see client_abstraction.h
 */
static void dispose(crm_cli_abs_ctx_t *ctx)
{
    LOGV("(%p)", ctx);

    ASSERT(ctx != NULL);
    crm_cli_abs_internal_ctx_t *i_ctx = (crm_cli_abs_internal_ctx_t *)ctx;

    free(i_ctx);
}

/**
 * @see client_abstraction.h
 */
crm_cli_abs_ctx_t *crm_cli_abs_init(int inst_id, bool sanity_mode, tcs_ctx_t *tcs,
                                    crm_ctrl_ctx_t *control, crm_wakelock_t *wakelock)
{
    crm_cli_abs_internal_ctx_t *i_ctx = calloc(1, sizeof(*i_ctx));

    ASSERT(i_ctx != NULL);

    (void)inst_id;     // UNUSED
    (void)sanity_mode; // UNUSED
    (void)tcs;         // UNUSED
    (void)wakelock;    // UNUSED

    i_ctx->control_ctx = control;
    ASSERT(i_ctx->control_ctx);

    LOGV("context %p", i_ctx);

    i_ctx->type = DBG_TYPE_APIMR;

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.notify_client = notify_client;
    i_ctx->ctx.notify_modem_state = notify_modem_state;
    i_ctx->ctx.notify_operation_result = notify_operation_result;

    i_ctx->done = false;

    // Test started here by starting the modem.
    i_ctx->control_ctx->start(i_ctx->control_ctx);

    return &i_ctx->ctx;
}
