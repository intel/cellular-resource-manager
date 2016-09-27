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

#include <poll.h>

#define CRM_MODULE_TAG "HALT"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/ipc.h"

#include "plugins/control.h"
#include "plugins/hal.h"

crm_ipc_ctx_t *ipc;

static void hal_event(crm_ctrl_ctx_t *ctx, const crm_hal_evt_t *event)
{
    (void)ctx;  /* unused */
    ASSERT(event != NULL);

    crm_ipc_msg_t msg = { .scalar = event->type };
    ipc->send_msg(ipc, &msg);
}

static bool wait_events(int nb_evts, int expected)
{
    struct pollfd pfd = { .fd = ipc->get_poll_fd(ipc), .events = POLLIN };
    int received = 0;
    int i;

    for (i = 0; i < nb_evts + 1; i++) {
        /* HAL stub guarantees a maximum time between two events of 200ms */
        int ret = poll(&pfd, 1, 220);

        if (ret == 0)
            break;

        crm_ipc_msg_t msg;
        ipc->get_msg(ipc, &msg);

        LOGD("Event %d", i);
        switch (msg.scalar) {
        case -1:
            LOGD("success");
            break;
        case HAL_MDM_FLASH:
            LOGD("modem flashing");
            break;
        case HAL_MDM_RUN:
            LOGD("modem running");
            break;
        case HAL_MDM_BUSY:
            LOGD("modem busy");
            break;
        case HAL_MDM_OFF:
            LOGD("modem off");
            break;
        }
        received += msg.scalar;
        LOGD("\n");
    }

    return (received == expected) && (i == nb_evts);
}

int main()
{
    /* Fake control context, just for testing */
    crm_ctrl_ctx_t control = {
        .notify_hal_event = hal_event,
    };

    ipc = crm_ipc_init(CRM_IPC_THREAD);

    crm_hal_ctx_t *hal = crm_hal_init(0, true, true, NULL, &control);

    hal->shutdown(hal);
    ASSERT(wait_events(2, HAL_MDM_BUSY + HAL_MDM_OFF) != false);

    hal->power_on(hal);
    ASSERT(wait_events(1, HAL_MDM_FLASH) != false);

    hal->boot(hal);
    ASSERT(wait_events(1, HAL_MDM_RUN) != false);

    hal->reset(hal, RESET_WARM);
    ASSERT(wait_events(2, HAL_MDM_BUSY + HAL_MDM_FLASH) != false);

    hal->boot(hal);
    ASSERT(wait_events(1, HAL_MDM_RUN) != false);

    hal->dispose(hal);
    LOGD("success");
    return 0;
}
