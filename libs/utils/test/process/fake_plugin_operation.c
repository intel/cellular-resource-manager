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
#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>

#define CRM_MODULE_TAG "FAKEP"
#include "utils/common.h"
#include "utils/ipc.h"

void start_process(crm_ipc_ctx_t *ipc_in, crm_ipc_ctx_t *ipc_out, void *data, size_t data_size)
{
    ASSERT(data && data_size > 0);
    ASSERT(ipc_in);
    ASSERT(ipc_out);

    char *param = (char *)data;
    bool deadlock = (*param == '1');

    int txt_len = data_size - 2;
    char *txt = malloc(sizeof(char) * (txt_len + 1));
    ASSERT(txt);
    memcpy(txt, param + 2, txt_len);
    txt[txt_len] = '\0';

    LOGD("->%s(%s)", __FUNCTION__, txt);
    free(txt);

    crm_ipc_msg_t msg = { .scalar = 0 };

    struct pollfd pfd = { .fd = ipc_in->get_poll_fd(ipc_in), .events = POLLIN };

    srand(getpid() | time(NULL));
    int max = rand() % 100;

    int ret = -1;
    bool first_msg = true;
    while (msg.scalar < max) {
        ASSERT(ipc_out->send_msg(ipc_out, &msg));

        int err = poll(&pfd, 1, first_msg ? 20000 : 3000);
        if (err <= 0) {
            LOGD("TIMEOUT");
            ret = -2;
            break;
        }
        first_msg = false;

        ASSERT(ipc_in->get_msg(ipc_in, &msg));
    }

    /* deadlock */
    if (deadlock) {
        LOGD("deadlock");
        while (true)
            pause();
    }

    msg.scalar = ret;
    ipc_out->send_msg(ipc_out, &msg);
}
