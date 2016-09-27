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

#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <termios.h>

#define CRM_MODULE_TAG "HAL"
#include "utils/at.h"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/time.h"

#include "ping.h"

/**
 * @see ping.h
 */
int crm_hal_ping_modem(const char *ping_node, int ping_timeout, int fd_abort, bool is_tty)
{
    ASSERT(ping_node);
    ASSERT(ping_timeout > 0);

    struct timespec timer_end;
    crm_time_add_ms(&timer_end, ping_timeout);
    LOGD("node: %s", ping_node);

    int fd;
    for (;; ) {
        fd = open(ping_node, O_RDWR | O_NONBLOCK);
        if (fd >= 0) {
            if (is_tty) {
                struct termios tio;
                ASSERT(!tcgetattr(fd, &tio));
                cfmakeraw(&tio);
                ASSERT(!tcsetattr(fd, TCSANOW, &tio));
            }

            int err = crm_send_at(fd, CRM_MODULE_TAG, "ATE0", 500, fd_abort);
            if (!err)
                return fd;
            else if (-2 == err)
                return -2;
            close(fd);
        } else {
            LOGE("failed to open ping node (%s). errno: %d/%s", ping_node, errno, strerror(errno));
        }

        int timeout = crm_time_get_remain_ms(&timer_end);
        if (timeout <= 0)
            break;

        struct pollfd pfd = { .fd = fd_abort, .events = POLLIN };
        poll(&pfd, 1, 500); // retry every 500ms

        /* something is received only if the loop needs to be stopped */
        if (pfd.revents) {
            LOGD("aborted");
            return -2;
        }
    }

    LOGE("Timeout. failed to ping modem");

    return -1;
}
