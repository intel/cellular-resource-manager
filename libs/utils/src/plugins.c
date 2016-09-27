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

#include <dlfcn.h>
#include <string.h>

#define CRM_MODULE_TAG "UTILS"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/plugins.h"

int crm_plugin_load(tcs_ctx_t *tcs, const char *plugin_name, const char *init_name,
                    crm_plugin_t *plugin)
{
    int ret = 0;
    char *filename = NULL;

    ASSERT(tcs != NULL);
    ASSERT(plugin_name != NULL);
    ASSERT(init_name != NULL);
    ASSERT(plugin != NULL);

    filename = tcs->get_string(tcs, plugin_name);
    if (filename) {
        dlerror(); // clear previous errors
        plugin->handle = dlopen(filename, RTLD_LAZY);
        DASSERT(plugin->handle != NULL, "Failed to load %s: %s", plugin_name, dlerror());

        plugin->init = dlsym(plugin->handle, init_name);
        DASSERT(dlerror() == NULL && plugin->init != NULL, "Symbol (%s) not found in library (%s)",
                init_name, filename);
        LOGV("<Plugin: %-15s> - <Implementation: %-30s>", plugin_name, filename);
        free(filename);
    } else {
        LOGD("no library for plugin (%s)", plugin_name);
        ret = -1;
    }

    return ret;
}

void crm_plugin_unload(crm_plugin_t *plugin)
{
    ASSERT(plugin != NULL);
    ASSERT(dlclose(plugin->handle) == 0);
    plugin->handle = NULL;
    plugin->init = NULL;
}
