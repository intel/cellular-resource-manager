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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/netlink.h>
#include <sys/sendfile.h>

#include "libmdmcli/mdm_cli.h"

#include "common.h"

#define RECV_BUFFER 1024

#define VMODEM_PATH "/sys/class/misc/vmodem/modem_state"
#define VMODEM_UEVENT "change@/devices/virtual/misc/vmodem"
#define MODEM_PATH "/system/vendor/firmware/telephony/modem.fls_ID0_CUST_LoadMap0.bin"
#define MODEM_VPATH "/dev/vmodem"

static int open_uevent_netlink(void)
{
    int fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);

    ASSERT(fd >= 0);

    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_pid = getpid();
    sa.nl_groups = NETLINK_KOBJECT_UEVENT;

    int err = bind(fd, (struct sockaddr *)&sa, sizeof(sa));
    ASSERT(err == 0);

    return fd;
}

static void write_sysfs(const char *d)
{
    int fd = open(VMODEM_PATH, O_WRONLY);

    my_printf("Writing '%s' to '%s'", d, VMODEM_PATH);

    ASSERT(fd >= 0);
    ASSERT(write(fd, d, strlen(d)) == ((ssize_t)strlen(d)));
    close(fd);
}

static int get_modem_state(void)
{
    int fd = open(VMODEM_PATH, O_RDONLY);

    ASSERT(fd >= 0);
    char buf[64];
    ssize_t ret = read(fd, buf, sizeof(buf));
    ASSERT((ret > 0) && (ret < (ssize_t)sizeof(buf)));
    buf[ret] = '\0';
    char *cr = strstr(buf, "\n");
    if (cr) *cr = '\0';

    my_printf("Read '%s' from '%s'", buf, VMODEM_PATH);

    int st = -1;
    if (!strcasecmp(buf, "on"))
        st = MODEM_UP;
    else if ((!strcasecmp(buf, "off")) ||
             (!strcasecmp(buf, "trap")))
        st = MODEM_DOWN;

    ASSERT(st >= 0);

    close(fd);

    return st;
}

void *restart_modem(void *ctx)
{
    (void)ctx;

    write_sysfs("0");

    my_printf("Copying file '%s' to device '%s'", MODEM_PATH, MODEM_VPATH);

    int i_fd = open(MODEM_PATH, O_RDONLY);
    ASSERT(i_fd >= 0);
    int o_fd = open(MODEM_VPATH, O_WRONLY);
    ASSERT(o_fd >= 0);

    struct stat sb;
    ASSERT(fstat(i_fd, &sb) == 0);

    ssize_t size = 0;
    struct pollfd pfd = { .fd = o_fd, .events = POLLOUT };
    ASSERT(poll(&pfd, 1, 1000) == 1);
    size = sendfile(o_fd, i_fd, NULL, sb.st_size);
    ASSERT(size == sb.st_size);
    close(i_fd);
    close(o_fd);

    write_sysfs("1");

    return NULL;
}

void *modem_ping(void *ctx)
{
    ASSERT(ctx != NULL);
    int pipe_fd = *((int *)ctx);

    bool running = true;
    while (running) {
        int fd = open(AT_PIPE, O_RDWR);
        if (fd >= 0) {
            my_printf("Opened '%s'", AT_PIPE);
            char *reply = send_at(fd, "ATE0", 500);
            if (reply) {
                if (strstr(reply, "OK"))
                    running = false;
                free(reply);
                reply = NULL;
            }
            close(fd);
        } else {
            my_printf("Failed to open '%s'", AT_PIPE);
        }
        usleep(50000);
    }

    send_evt(pipe_fd, MODEM_UP);
    return NULL;
}

void *ueventd_thread(void *ctx)
{
    ASSERT(ctx != NULL);

    int pipe_fd = *((int *)ctx);

    int state = get_modem_state();
    send_evt(pipe_fd, state);

    if (state == MODEM_DOWN) {
        pthread_t thr;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        ASSERT(pthread_create(&thr, NULL, restart_modem, NULL) == 0);
    }

    int uevent_fd = open_uevent_netlink();

    while (1) {
        struct pollfd pfd = { .fd = uevent_fd, .events = POLLIN };
        ASSERT(poll(&pfd, 1, -1) == 1);
        ASSERT((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0);
        ASSERT((pfd.revents & POLLIN) != 0);

        char tmp[RECV_BUFFER];
        ssize_t len = recv(uevent_fd, tmp, sizeof(tmp), 0);
        if ((len > 0) && (len < RECV_BUFFER)) {
            tmp[len] = '\0';
            if (!strcmp(tmp, VMODEM_UEVENT)) {
                my_printf("Received event '%s'", tmp);
                state = get_modem_state();

                if (state == MODEM_DOWN) {
                    send_evt(pipe_fd, state);

                    pthread_t thr;
                    pthread_attr_t attr;
                    pthread_attr_init(&attr);
                    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

                    ASSERT(pthread_create(&thr, NULL, restart_modem, NULL) == 0);
                } else if (state == MODEM_UP) {
                    pthread_t thr;
                    pthread_attr_t attr;
                    pthread_attr_init(&attr);
                    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

                    ASSERT(pthread_create(&thr, NULL, modem_ping, ctx) == 0);
                }
            }
        }
    }

    return NULL;
}

void init_test_SYSFS(int pipe_fd)
{
    int *ctx = malloc(sizeof(pipe_fd));

    system("stop rpcServer");
    system("stop rpc-daemon");
    system("stop crm");
    sleep(2);

    ASSERT(ctx != NULL);
    *ctx = pipe_fd;

    pthread_t thr;
    ASSERT(pthread_create(&thr, NULL, ueventd_thread, ctx) == 0);
}

void restart_modem_SYSFS(void)
{
    write_sysfs("0");
}
