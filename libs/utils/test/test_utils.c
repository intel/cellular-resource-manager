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

#define CRM_MODULE_TAG "UTILST"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/property.h"

static void test_properties()
{
    const char *keys[] = { "indexed_@_key", "not_indexed_key" };

    for (size_t idx = 0; idx < ARRAY_SIZE(keys); idx++) {
        int max = 2;
        if (!strstr(keys[idx], "@"))
            max = 1;

        for (int i = 0; i < max; i++) {
            const char *default_value = "default_value";
            const char *new_value = "new_value";
            char read[CRM_PROPERTY_VALUE_MAX];

            crm_property_init(i);

            char *key = strdup(keys[idx]);
            ASSERT(key != NULL);

            crm_property_get(key, read, NULL);
            ASSERT(read[0] == '\0');
            crm_property_get(key, read, default_value);
            ASSERT(0 == (strcmp(read, default_value)));

            crm_property_set(key, new_value);

            crm_property_get(key, read, default_value);
            ASSERT(0 == (strcmp(read, new_value)));

            free(key);
        }
    }
}

static void test_log()
{
    crm_logs_init(0);
    int a = 1;
    int b = 2;
    int c = 3;
    LOGD("SHORT TEST");
    LOGV("VERBOSE: LONG TEST a = %d, b = %d, c = %d, str = %s", a, b, c, "hello world!");
    LOGE("ERROR: LONG TEST a = %d, b = %d, c = %d, str = %s", a, b, c, "hello world!");
    LOGI("INFO: LONG TEST a = %d, b = %d, c = %d, str = %s", a, b, c, "hello world!");
    LOGD("DEBUG: LONG TEST a = %d, b = %d, c = %d, str = %s", a, b, c, "hello world!");
}

int main()
{
    test_log();
    test_properties();

    LOGD("success");
    int a = 0;
    DASSERT(0, "expected assert (%d). %s", a, "*** TEST HAS SUCCEED ***");
    return 0;
}
