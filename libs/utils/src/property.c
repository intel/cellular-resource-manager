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
#include <errno.h>

#define CRM_MODULE_TAG "UTILS"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/property.h"

static int g_inst_id = 0;

#ifdef HOST_BUILD

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

static int prop_pipe = -1;

/**
 * @see proprety.h
 */
static inline void get(const char *key, char *value, const char *default_value)
{
    char *tmp = getenv(key);

    if (tmp)
        snprintf(value, CRM_PROPERTY_VALUE_MAX, "%s", tmp);
    else if (default_value)
        snprintf(value, CRM_PROPERTY_VALUE_MAX, "%s", default_value);
    else
        value[0] = '\0';
}

/**
 * @see proprety.h
 */
static inline void set(const char *key, const char *value)
{
    errno = 0;
    DASSERT(setenv(key, value, 1) == 0, "Failed to set environment variable (%s)", strerror(errno));
    if (prop_pipe != -1) {
        char buf[CRM_PROPERTY_VALUE_MAX + CRM_PROPERTY_KEY_MAX + 2];
        ASSERT((strlen(key) + strlen(value) + 2) < sizeof(buf));
        snprintf(buf, sizeof(buf), "%s=%s", key, value);
        ssize_t len = strlen(buf);
        ASSERT(write(prop_pipe, buf, len) == len);
    }
}

#else /* HOST_BUILD */

/**
 * @see proprety.h
 */
static inline void get(const char *key, char *value, const char *default_value)
{
    DASSERT(property_get(key, value, default_value) < PROPERTY_VALUE_MAX,
            "Failed to read property (%s)", key);
}

/**
 * @see proprety.h
 */
static inline void set(const char *key, const char *value)
{
    DASSERT(property_set(key, value) == 0, "Failed to write property (%s)", key);
}

#endif /* HOST_BUILD */

static inline const char *compute_key(const char *key, char *ikey)
{
    char *find = strstr(key, "@");

    if (!find) {
        return key;
    } else {
        snprintf(ikey, CRM_PROPERTY_VALUE_MAX, "%s", key);
        ikey[find - key] = '0' + g_inst_id;
        return ikey;
    }
}

void crm_property_get(const char *key, char *value, const char *default_value)
{
    char ikey[CRM_PROPERTY_VALUE_MAX];

    ASSERT(key != NULL);
    ASSERT(value != NULL);

    get(compute_key(key, ikey), value, default_value);
}

void crm_property_set(const char *key, const char *value)
{
    char ikey[CRM_PROPERTY_VALUE_MAX];

    ASSERT(key != NULL);
    ASSERT(value != NULL);

    set(compute_key(key, ikey), value);
}

void crm_property_init(int id)
{
    g_inst_id = id;
    ASSERT(id >= 0 && id <= 9);

#ifdef HOST_BUILD
    /* Note: no assert here to be able to use 'crm_property_init' in the test itself.
     *       (i.e. in the 'test' context, this open will fail).
     */
    prop_pipe = open(CRM_PROPERTY_PIPE_NAME, O_WRONLY);
#endif
}
