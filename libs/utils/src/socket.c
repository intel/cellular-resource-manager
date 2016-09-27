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

#include <poll.h>
#include <unistd.h>

#include <cutils/sockets.h>

#define CRM_MODULE_TAG "UTILS"
#include "utils/debug.h"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/time.h"
#include "utils/socket.h"

/**
 * @see socket.h
 */
int crm_socket_connect(const char *socket_name)
{
    ASSERT(socket_name != NULL);

    return socket_local_client(socket_name, ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM);
}

/**
 * @see socket.h
 */
int crm_socket_create(const char *socket_name, int max_conn)
{
    ASSERT(socket_name != NULL);

    int fd = android_get_control_socket(socket_name);

    if (fd >= 0) {
        int ret = listen(fd, max_conn);
        if (ret != 0) {
            close(fd);
            fd = -1;
        }
    }
    return fd;
}

/**
 * @see socket.h
 */
int crm_socket_accept(int fd)
{
    ASSERT(fd >= 0);

    return accept(fd, 0, 0);
}

/**
 * @see socket.h
 */
int crm_socket_write(int fd, int timeout, const void *data, size_t data_size)
{
    ASSERT(fd >= 0);
    ASSERT(timeout > 0);
    ASSERT(data != NULL);
    ASSERT(data_size > 0);

    size_t data_sent = 0;
    const unsigned char *data_ptr = (const unsigned char *)data;

    struct timespec timer_end;
    int time_remaining;

    crm_time_add_ms(&timer_end, timeout);
    while ((data_sent < data_size) &&
           ((time_remaining = crm_time_get_remain_ms(&timer_end)) > 0)) {
        errno = 0;
        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        int poll_ret = poll(&pfd, 1, time_remaining);
        if ((poll_ret < 0) && (errno == EINTR))
            continue;
        if ((poll_ret <= 0) || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
            return -1;

        errno = 0;
        ssize_t ret = write(fd, &data_ptr[data_sent], data_size - data_sent);
        if ((ret < 0) && (errno == EINTR))
            continue;
        if (ret <= 0)
            return -1;
        data_sent += ret;
    }
    if (data_sent < data_size)
        return -1;
    return 0;
}

/**
 * @see socket.h
 */
int crm_socket_read(int fd, int timeout, void *data, size_t data_size)
{
    ASSERT(fd >= 0);
    ASSERT(timeout > 0);
    ASSERT(data != NULL);
    ASSERT(data_size > 0);

    char *dest_buffer = data;
    size_t data_rcvd = 0;

    struct timespec timer_end;
    int time_remaining;

    crm_time_add_ms(&timer_end, timeout);
    while ((data_rcvd < data_size) &&
           ((time_remaining = crm_time_get_remain_ms(&timer_end)) > 0)) {
        errno = 0;
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int poll_ret = poll(&pfd, 1, time_remaining);
        if ((poll_ret < 0) && (errno == EINTR))
            continue;
        if ((poll_ret <= 0) || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
            return -1;

        errno = 0;
        ssize_t ret = read(fd, &dest_buffer[data_rcvd], data_size - data_rcvd);
        if ((ret < 0) && (errno == EINTR))
            continue;
        if (ret <= 0)
            return -1;
        data_rcvd += ret;
    }
    if (data_rcvd < data_size)
        return -1;
    return 0;
}
