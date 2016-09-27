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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <stdarg.h>

#include <utils/Log.h>

#include "common.h"

static void print_dbg(char *buf)
{
    char *save = strdup(buf);

    ASSERT(save != NULL);
    char *tmp = strtok(save, "\r\n");
    while (tmp) {
        my_printf("<== %s", tmp);
        tmp = strtok(NULL, "\r\n");
    }
    free(save);
}

char *send_at(int fd, char *at, int timeout)
{
    char buf[65536];
    int pos = 0;
    ssize_t ret;

    my_printf("==> %s", at);

    snprintf(buf, sizeof(buf), "%s\r", at);

    struct pollfd pfd = { .fd = fd, .events = POLLOUT };
    int poll_ret = poll(&pfd, 1, timeout);
    if (poll_ret <= 0) {
        my_printf("Timeout on POLLOUT event");
        return NULL;
    }
    if (write(fd, buf, strlen(buf)) != ((ssize_t)strlen(buf)))
        return NULL;

    while (1) {
        pfd.events = POLLIN;
        int poll_ret = poll(&pfd, 1, timeout);
        if (poll_ret <= 0) {
            my_printf("Timeout on POLLIN event");
            return NULL;
        }
        ret = read(fd, &buf[pos], sizeof(buf) - 1 - pos);
        if (ret > 0) {
            ASSERT(pos + ret < (ssize_t)sizeof(buf));
            buf[pos + ret] = '\0';
            pos += ret;
            if (strstr(buf, "OK") || strstr(buf, "ERROR")) break;
        } else {
            return NULL;
        }
    }
    print_dbg(buf);
    return strdup(buf);
}

void send_evt(int fd, int evt)
{
    struct pollfd pfd = { .fd = fd, .events = POLLOUT };
    int poll_ret = poll(&pfd, 1, 0);

    ASSERT(poll_ret == 1);
    ASSERT((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0);
    ASSERT(write(fd, &evt, sizeof(evt)) == sizeof(evt));
}

int my_printf(const char *format, ...)
{
    char buf[1024];
    va_list args;

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

#ifndef HOST_BUILD
    __android_log_buf_write(LOG_ID_RADIO, ANDROID_LOG_DEBUG, "SANITY", buf);
#endif
    return printf("%s\n", buf);
}
