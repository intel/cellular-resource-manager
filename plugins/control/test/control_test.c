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

#include <string.h>
#include <stdio.h>

#define CRM_MODULE_TAG "CTRLT"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/process_factory.h"
#include "utils/keys.h"
#include "utils/property.h"
#include "plugins/control.h"
#include "test/test_utils.h"

int main()
{
    unlink(CRM_PROPERTY_PIPE_NAME);

    crm_logs_init(MDM_CLI_DEFAULT_INSTANCE);
    crm_property_init(MDM_CLI_DEFAULT_INSTANCE);

    tcs_ctx_t *tcs = CRM_TEST_tcs_init("host_sofia_control_testu", MDM_CLI_DEFAULT_INSTANCE);

    crm_property_set(CRM_KEY_DBG_LOAD_STUB, "true");
    crm_property_set(CRM_KEY_DBG_HOST, "true");

    crm_process_factory_ctx_t *factory = crm_process_factory_init(2);
    ASSERT(factory);
    crm_ctrl_ctx_t *ctrl = crm_ctrl_init(MDM_CLI_DEFAULT_INSTANCE, tcs, factory);

    tcs->dispose(tcs);

    ctrl->event_loop(ctrl);

    LOGE("An error happened. Let's dispose everything...");
    ctrl->dispose(ctrl);
    ASSERT(0);

    return 0;
}
