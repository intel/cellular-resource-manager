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
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define CRM_MODULE_TAG "UTILS"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/at.h"

/**
 * @see at.h
 */
int crm_send_at(int fd, const char *tag, const char *at_cmd, int timeout, int fd_abort)
{
    char buffer[2048];

    ASSERT(at_cmd != NULL);
    ASSERT(fd >= 0);

    struct pollfd pfd[] = {
        { .fd = fd, .events = POLLIN },
        { .fd = fd_abort, .events = POLLIN }
    };


    LOGD("[AT-%s]  sending: %s", tag, at_cmd);
    int lenb = snprintf(buffer, sizeof(buffer), "%s\r\n", at_cmd);
    DASSERT(lenb < (int)sizeof(buffer), "internal buffer too small");

    ssize_t len = write(fd, buffer, lenb);
    if (len != lenb) {
        LOGE("Write failure. %zd/%d written", len, lenb);
        return -1;
    }

    size_t idx = 0;

    int ret = 1;
    while (ret > 0) {
        int err = poll(pfd, ARRAY_SIZE(pfd), timeout);

        if ((err == 0) || (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            LOGE("[AT-%s] no answer from modem. timeout: %dms", tag, timeout);
            ret = -1;
            break;
        } else if (pfd[1].revents) {
            LOGD("[AT-%s] aborted", tag);
            ret = -2;
            break;
        }

        if (pfd[0].revents & POLLIN) {
            int len = read(fd, &buffer[idx], sizeof(buffer) - idx);
            if (len <= 0) {
                LOGE("failed to read answer. errno: %d/%s", errno, strerror(errno));
                ret = -1;
                break;
            }

            idx += len;
            DASSERT(idx < sizeof(buffer), "internal buffer too small. idx:%zd", idx);
            buffer[idx] = '\0';

            char *cr;
            while ((cr = strstr(buffer, "\r\n")) != NULL) {
                *cr = '\0';
                if (buffer[0] != '\0')
                    LOGD("[AT-%s] received: %s", tag, buffer);

                if (strcmp(buffer, "OK") == 0) {
                    ret = 0;
                    break;
                } else if (strcmp(buffer, "ERROR") == 0) {
                    ret = -1;
                    break;
                }

                cr += 2; // skipping \r\n
                idx = &buffer[idx] - cr;
                memmove(buffer, cr, idx);
            }
        }
    }

    return ret;
}
