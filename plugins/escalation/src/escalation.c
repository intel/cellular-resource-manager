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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRM_MODULE_TAG "ESC"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/property.h"
#include "utils/time.h"
#include "utils/keys.h"
#include "utils/string_helpers.h"
#include "plugins/escalation.h"

/* Keep IDX_* enum in sync with enum crm_escalation_next_step_t */
enum {
    IDX_WARM = STEP_MDM_WARM_RESET - STEP_MDM_WARM_RESET,
    IDX_COLD = STEP_MDM_COLD_RESET - STEP_MDM_WARM_RESET,
    IDX_REBOOT = STEP_PLATFORM_REBOOT - STEP_MDM_WARM_RESET,
    IDX_OOS = STEP_OOS - STEP_MDM_WARM_RESET,
    IDX_NUM = STEP_NUM - STEP_MDM_WARM_RESET
};

typedef struct crm_escalation_ctx_internal {
    crm_escalation_ctx_t ctx; //Needs to be first

    /* Configuration */
    int cfg[IDX_NUM];
    int modem_stability_timeout;

    /* Internal variables */
    int cfg_idx;
    int counter;
    struct timespec timer_end;
    bool escalation_disabled;
} crm_escalation_ctx_internal_t;

static void go_next_step(crm_escalation_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);
    do {
        i_ctx->cfg_idx++;
        DASSERT((size_t)i_ctx->cfg_idx <= ARRAY_SIZE(i_ctx->cfg),
                "Invalid escalation level reached");
    } while ((i_ctx->cfg[i_ctx->cfg_idx] <= 0) && (i_ctx->cfg_idx != IDX_OOS));

    i_ctx->counter = i_ctx->cfg[i_ctx->cfg_idx];
}

static void update_reboot_counter(crm_escalation_ctx_internal_t *i_ctx)
{
    char value[CRM_PROPERTY_VALUE_MAX];

    crm_property_get(CRM_KEY_REBOOT_COUNTER, value, "0");
    errno = 0;
    long reboot_counter = strtol(value, NULL, 0);
    ASSERT(errno == 0);

    ASSERT(i_ctx);

    if (reboot_counter >= i_ctx->cfg[IDX_REBOOT]) {
        LOGV("modem OUT OF SERVICE state reached");
        i_ctx->cfg_idx = IDX_OOS;
    } else {
        char value[CRM_PROPERTY_VALUE_MAX];
        snprintf(value, sizeof(value), "%ld", ++reboot_counter);
        crm_property_set(CRM_KEY_REBOOT_COUNTER, value);
    }
}

/**
 * @see escalation.h
 */
static crm_escalation_next_step_t get_next_step(crm_escalation_ctx_t *ctx)
{
    crm_escalation_ctx_internal_t *i_ctx = (crm_escalation_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    if (i_ctx->escalation_disabled) {
        LOGD("->%s() level: %s", __FUNCTION__, crm_escalation_level_to_string(STEP_MDM_COLD_RESET));
        return STEP_MDM_COLD_RESET;
    }

    if (i_ctx->cfg_idx != IDX_OOS) {
        if (crm_time_get_remain_ms(&i_ctx->timer_end) <= 0) {
            LOGD("Escalation recovery reset. Modem was stable during at least %d ms",
                 i_ctx->modem_stability_timeout);

            i_ctx->cfg_idx = 0;
            i_ctx->counter = i_ctx->cfg[i_ctx->cfg_idx];
            crm_property_set(CRM_KEY_REBOOT_COUNTER, "0");
        }

        if (i_ctx->counter <= 0)
            go_next_step(i_ctx);

        if (i_ctx->cfg_idx != IDX_REBOOT)
            i_ctx->counter--;
        else
            update_reboot_counter(i_ctx);

        crm_time_add_ms(&i_ctx->timer_end, i_ctx->modem_stability_timeout);
    }

    if (i_ctx->cfg_idx != IDX_OOS)
        LOGD("->%s() level: %s. remaining: %d", __FUNCTION__,
             crm_escalation_level_to_string(i_ctx->cfg_idx + STEP_MDM_WARM_RESET), i_ctx->counter);
    else
        LOGD("->%s() level: %s", __FUNCTION__,
             crm_escalation_level_to_string(i_ctx->cfg_idx + STEP_MDM_WARM_RESET));

    /* convert index to enum value (see index definition) */
    return i_ctx->cfg_idx + STEP_MDM_WARM_RESET;
}

/**
 * @see escalation.h
 */
static crm_escalation_next_step_t get_last_step(crm_escalation_ctx_t *ctx)
{
    crm_escalation_ctx_internal_t *i_ctx = (crm_escalation_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    i_ctx->cfg_idx = IDX_REBOOT;
    update_reboot_counter(i_ctx);

    LOGD("->%s() level: %s", __FUNCTION__,
         crm_escalation_level_to_string(i_ctx->cfg_idx + STEP_MDM_WARM_RESET));

    /* convert index to enum value (see index definition) */
    return i_ctx->cfg_idx + STEP_MDM_WARM_RESET;
}

/**
 * @see escalation.h
 */
static void dispose(crm_escalation_ctx_t *ctx)
{
    crm_escalation_ctx_internal_t *i_ctx = (crm_escalation_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    free(i_ctx);
}

/**
 * @see escalation.h
 */
crm_escalation_ctx_t *crm_escalation_init(bool sanity_mode, tcs_ctx_t *tcs)
{
    crm_escalation_ctx_internal_t *i_ctx = calloc(1, sizeof(crm_escalation_ctx_internal_t));

    ASSERT(i_ctx);
    ASSERT(tcs);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.get_next_step = get_next_step;
    i_ctx->ctx.get_last_step = get_last_step;

    i_ctx->modem_stability_timeout = -1;

    char value[CRM_PROPERTY_VALUE_MAX];
    crm_property_get(CRM_KEY_DBG_DISABLE_ESCALATION, value, "false");

    if (!strcmp(value, "false")) {
        ASSERT(!tcs->select_group(tcs, ".escalation"));

        /* Warm reset is optional */
        int tmp;
        if (!tcs->get_int(tcs, "warm_reset", &tmp)) {
            ASSERT(tmp >= 0);
            i_ctx->cfg[IDX_WARM] = tmp;
        }

        ASSERT(!tcs->get_int(tcs, "cold_reset", &i_ctx->cfg[IDX_COLD]));
        ASSERT(!tcs->get_int(tcs, "reboot", &i_ctx->cfg[IDX_REBOOT]));
        ASSERT(i_ctx->cfg[IDX_COLD] >= 0 && i_ctx->cfg[IDX_REBOOT] >= 0);

        if (!sanity_mode)
            ASSERT(!tcs->get_int(tcs, "timeout", &i_ctx->modem_stability_timeout));
        else
            ASSERT(!tcs->get_int(tcs, "timeout_sanity", &i_ctx->modem_stability_timeout));

        i_ctx->counter = i_ctx->cfg[IDX_WARM];
        if (i_ctx->counter <= 0)
            go_next_step(i_ctx);

        crm_time_add_ms(&i_ctx->timer_end, i_ctx->modem_stability_timeout);

        /* @TODO: what to do if we reboot with the counter already at its maximum value:
         * do we enter OOS directly or do one attempt to restart the modem ? */
    } else {
        i_ctx->escalation_disabled = true;
        LOGV("Escalation disabled");
    }

    return &i_ctx->ctx;
}
