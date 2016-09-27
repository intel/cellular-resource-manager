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

#define CRM_MODULE_TAG "ESCT"

#include <stdio.h>
#include <unistd.h>

#include "plugins/escalation.h"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/property.h"
#include "utils/keys.h"
#include "utils/string_helpers.h"
#include "test/test_utils.h"

#include "libtcs2/tcs.h"

void check_escalation(crm_escalation_ctx_t *escalation, int *cfg, size_t size_cfg, bool oos)
{
    ASSERT(escalation);
    ASSERT(cfg);

    for (size_t idx_cfg = 0; idx_cfg < size_cfg; idx_cfg++) {
        crm_escalation_next_step_t level = idx_cfg + STEP_MDM_WARM_RESET;
        LOGD("Checking level: %s", crm_escalation_level_to_string(level));
        for (int i = 0; i < cfg[idx_cfg]; i++) {
            crm_escalation_next_step_t next = escalation->get_next_step(escalation);
            DASSERT(next == level, "Next step is %s while it should be %s",
                    crm_escalation_level_to_string(level),
                    crm_escalation_level_to_string(next));
        }
    }

    crm_escalation_next_step_t last = escalation->get_next_step(escalation);
    if (oos)
        ASSERT(last == STEP_OOS);
    else
        ASSERT(last == STEP_PLATFORM_REBOOT);
}

int main()
{
    tcs_ctx_t *tcs = CRM_TEST_tcs_init("host_sofia", MDM_CLI_DEFAULT_INSTANCE);

    ASSERT(tcs);
    ASSERT(!tcs->select_group(tcs, ".escalation"));

    int timeout;
    int reboot;
    int cfg[2];

    int tmp;
    if (!tcs->get_int(tcs, "warm_reset", &tmp))
        cfg[0] = tmp;
    else
        cfg[0] = 0;
    ASSERT(!tcs->get_int(tcs, "cold_reset", &cfg[1]));
    ASSERT(!tcs->get_int(tcs, "reboot", &reboot));
    ASSERT(!tcs->get_int(tcs, "timeout", &timeout));
    timeout += 10;

    crm_property_set(CRM_KEY_DBG_DISABLE_ESCALATION, "false");
    crm_property_set(CRM_KEY_REBOOT_COUNTER, "0");

    crm_escalation_ctx_t *escalation = crm_escalation_init(false, tcs);
    ASSERT(escalation != NULL);

    check_escalation(escalation, cfg, ARRAY_SIZE(cfg), false);

    /* Check that reboot property has been incremented */
    char value[CRM_PROPERTY_VALUE_MAX];
    crm_property_get(CRM_KEY_REBOOT_COUNTER, value, "-1");
    DASSERT(*value == '1', "value is %s", value);

    LOGD("waiting %dms to force the reset of the escalation plugin", timeout);
    usleep(timeout * 1000);

    /* Check that reboot property has been reinitialized */
    escalation->get_next_step(escalation);
    crm_property_get(CRM_KEY_REBOOT_COUNTER, value, "-1");
    DASSERT(*value == '0', "value is %s", value);

    LOGD("Checking full escalation recovery");
    for (int i = 0; i < reboot; i++) {
        /* dispose and reload the module to simulate a CRM restart */
        escalation->dispose(escalation);
        escalation = crm_escalation_init(false, tcs);
        ASSERT(escalation);

        check_escalation(escalation, cfg, ARRAY_SIZE(cfg), i == reboot);

        crm_property_get(CRM_KEY_REBOOT_COUNTER, value, "-1");
        DASSERT(*value == '1' + i, "value is %c instead of %c", *value, '1' + i);
    }

    escalation->dispose(escalation);
    LOGD("Checking sanity mode");
    {
        ASSERT(reboot >= 1); // This test works only if at least one reboot is allowed */
        ASSERT(!tcs->get_int(tcs, "timeout_sanity", &timeout));
        escalation = crm_escalation_init(true, tcs);

        crm_property_set(CRM_KEY_REBOOT_COUNTER, "0");
        check_escalation(escalation, cfg, ARRAY_SIZE(cfg), false);
        crm_property_get(CRM_KEY_REBOOT_COUNTER, value, "-1");
        DASSERT(*value == '1', "value is %s", value);

        /* dispose and reload the module to simulate a CRM restart */
        escalation->dispose(escalation);
        escalation = crm_escalation_init(true, tcs);
        ASSERT(escalation);

        escalation->get_next_step(escalation);
        crm_property_get(CRM_KEY_REBOOT_COUNTER, value, "-1");
        DASSERT(*value == '1', "value is %s instead of 1", value);

        /* Check that reboot property is not reinitialized */
        usleep((timeout - 10) * 1000);
        escalation->get_next_step(escalation);
        crm_property_get(CRM_KEY_REBOOT_COUNTER, value, "-1");
        DASSERT(*value == '1', "value is %s instead of 1", value);

        /* Check that reboot property has been reinitialized */
        usleep((timeout + 10) * 1000);
        escalation->get_next_step(escalation);
        crm_property_get(CRM_KEY_REBOOT_COUNTER, value, "-1");
        DASSERT(*value == '0', "value is %s", value);
    }
    escalation->dispose(escalation);

    LOGD("Checking last step API");
    {
        crm_property_set(CRM_KEY_REBOOT_COUNTER, "0");
        for (int i = 0; i < reboot; i++)
            ASSERT(STEP_PLATFORM_REBOOT == escalation->get_last_step(escalation));
        ASSERT(STEP_OOS == escalation->get_last_step(escalation));
    }

    LOGD("Checking disable escalation mode");
    {
        crm_property_set(CRM_KEY_DBG_DISABLE_ESCALATION, "true");
        escalation = crm_escalation_init(false, tcs);
        ASSERT(escalation != NULL);

        for (int i = 0; i < cfg[0] + cfg[1] + 1000; i++)
            ASSERT(STEP_MDM_COLD_RESET == escalation->get_next_step(escalation));
    }

    tcs->dispose(tcs);
    escalation->dispose(escalation);

    LOGD("Test Success");
}
