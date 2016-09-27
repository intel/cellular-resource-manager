/*
 * Copyright (C) Intel 2016
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

#define CRM_MODULE_TAG "CTRL"
#include "utils/common.h"
#include "utils/logs.h"

#include "utils.h"

mdm_cli_dbg_info_t *copy_dbg_info(const mdm_cli_dbg_info_t *dbg_info)
{
    ASSERT(dbg_info);

    mdm_cli_dbg_info_t *copy = malloc(sizeof(mdm_cli_dbg_info_t));
    ASSERT(copy);

    *copy = *dbg_info;
    copy->data = malloc(dbg_info->nb_data * sizeof(char *));
    ASSERT(copy->data);
    for (size_t i = 0; i < dbg_info->nb_data; i++) {
        copy->data[i] = strdup(dbg_info->data[i]);
        ASSERT(copy->data[i]);
    }

    return copy;
}
