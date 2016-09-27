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

#ifndef __CRM_UTILS_LOADER_HEADER__
#define __CRM_UTILS_LOADER_HEADER__

#include "libtcs2/tcs.h"

typedef struct crm_plugin {
    void *handle;
    void *init;
} crm_plugin_t;

/**
 * Loads the plugin
 *
 * NB: User of this function _SHALL_ select the proper TCS group. Otherwise, plugin name
 * will not be retrieved
 *
 * @param [in] tcs         TCS context
 * @param [in] plugin_name Key to be used with TCS to get the name of the library to be loaded
 * @param [in] init_name   Name of the init function of the plugin
 * @param [out] plugin     Plugin context. Contains the handle and the init function pointer
 *
 * @return 0 if the plugin is loaded successfully
 */
int crm_plugin_load(tcs_ctx_t *tcs, const char *plugin_name, const char *init_name,
                    crm_plugin_t *plugin);

/**
 * Unloads the plugin
 *
 * @param [in] plugin Plugin context.
 */
void crm_plugin_unload(crm_plugin_t *plugin);

#endif /* __CRM_UTILS_LOADER_HEADER__ */
