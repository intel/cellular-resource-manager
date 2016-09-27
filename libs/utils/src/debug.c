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

#include "utils/property.h"
#include "utils/keys.h"

#include "utils/debug.h"

bool crm_is_in_sanity_test_mode(void)
{
    bool sanity = false;

    char value[CRM_PROPERTY_VALUE_MAX];

    crm_property_get(CRM_KEY_BUILD_TYPE, value, "");

    if (strcmp(value, "user")) {
        crm_property_get(CRM_KEY_DBG_SANITY_TEST_MODE, value, "");
        if (strcmp(value, "true") == 0)
            sanity = true;
    }

    return sanity;
}
