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

#define CRM_MODULE_TAG "FWUP"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/ipc.h"
#include "ifwd/crm_ifwd.h"

char *get_next_param(char **data, size_t *size)
{
    ASSERT(*data);
    ASSERT(size && *size > 0);

    char *param = *data;
    for (size_t i = 0; i < *size; i++) {
        if (param[i] == ';') {
            param[i] = '\0';
            *size -= i + 1;
            if (*size > 0)
                *data += i + 1;
            else
                *data = NULL;
            return param;
        }
    }
    DASSERT(0, "parameter not found");
}

void start_process(crm_ipc_ctx_t *ipc_in, crm_ipc_ctx_t *ipc_out, void *data, size_t data_size)
{
    ASSERT(data && data_size > 0);
    ASSERT(ipc_out);
    (void)ipc_in;

    char *cur = data;
    char *dev_node = get_next_param(&cur, &data_size);
    char *fw = get_next_param(&cur, &data_size);
    char *log_file = get_next_param(&cur, &data_size);

    int err = crm_ifwd_write_firmware(dev_node, fw, log_file);

    crm_ipc_msg_t msg = { .scalar = err };
    ipc_out->send_msg(ipc_out, &msg);
}
