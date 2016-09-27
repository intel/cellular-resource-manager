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

#ifdef HOST_BUILD
#define _GNU_SOURCE // needed for strcasestr
#endif
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/netlink.h>

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <stdio.h>

#define CRM_MODULE_TAG "HAL"
#include "utils/at.h"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/file.h"
#include "utils/thread.h"
#include "utils/time.h"

#include "common.h"
#include "modem.h"
#include "ping.h"

#define RECV_BUFFER 1024

/* commands used to control the modem */
#define STOP_MODEM "0"
#define START_MODEM "1"

static int open_debug_socket(const char *name)
{
    ASSERT(name != NULL);

    errno = 0;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    DASSERT(fd >= 0, "Failed to open socket (%s)", strerror(errno));

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", name);

    int count = 0;
    while ((connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) && (count++ < 100))
        usleep(50000);

    DASSERT(count <= 100, "failed to connect to socket (%s)", strerror(errno));
    return fd;
}

static int open_uevent_netlink(void)
{
    errno = 0;
    int fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    DASSERT(fd >= 0, "Failed to open netlink socket (%s)", strerror(errno));

    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_pid = getpid();
    sa.nl_groups = NETLINK_KOBJECT_UEVENT;

    DASSERT(bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0, "Failed to bind socket (%s)",
            strerror(errno));

    return fd;
}

static int get_mdm_state_from_sysfs(const char *vmodem_sysfs_mdm_state)
{
    int ret = -1;
    int err = -1;

    ASSERT(vmodem_sysfs_mdm_state != NULL);
    char tmp[15];
    err = crm_file_read(vmodem_sysfs_mdm_state, tmp, sizeof(tmp));
    DASSERT(err == 0, "Failed to read SYSFS modem state value");
    LOGD("[VMODEM] sysfs (%s) read: %s", vmodem_sysfs_mdm_state, tmp);

    if (!err) {
        if (strcasestr(tmp, "trap"))
            ret = EV_MDM_TRAP;
        else if (strcasestr(tmp, "turning_on") || strcasestr(tmp, "shutting_down"))
            ret = -1;
        else if (strcasestr(tmp, "on"))
            ret = EV_MDM_ON;
        else if (strcasestr(tmp, "off"))
            ret = EV_MDM_OFF;
        else if (strcasestr(tmp, "verify-fail"))
            ret = EV_MDM_FW_FAIL;
        else
            DASSERT(0, "unknown sysfs value: %s", tmp);
    }
    return ret;
}

static bool is_modem_event(crm_hal_ctx_internal_t *i_ctx)
{
    bool ret = false;
    struct pollfd pfd = { .fd = i_ctx->s_fd, .events = POLLIN };

    ASSERT(i_ctx != NULL);

    int err = poll(&pfd, 1, 0);
    ASSERT(err != 0);

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        return ret;

    if (pfd.revents & POLLIN) {
        char tmp[RECV_BUFFER];
        ssize_t len = recv(i_ctx->s_fd, tmp, sizeof(tmp), 0);
        if (len > 0) {
            tmp[len] = '\0';
            if (0 == strcmp(tmp, i_ctx->uevent_vmodem)) {
                LOGD("[VMODEM] UEVENT event: %s", i_ctx->uevent_vmodem);
                ret = true;
            }
        }
    }

    return ret;
}

void *crm_hal_ping_modem_thread(crm_thread_ctx_t *thread_ctx, void *param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)param;

    ASSERT(i_ctx != NULL);
    ASSERT(thread_ctx != NULL);

    int fd = crm_hal_ping_modem(i_ctx->ping_node, i_ctx->ping_timeout,
                                thread_ctx->get_poll_fd(thread_ctx), false);
    /* ping operation can be aborted by writing in thread poll fd.
     * in that case, returned value is -2 */
    if (fd > -2) {
        crm_ipc_msg_t msg = { .scalar = (-1 == fd) ? EV_TIMEOUT : EV_MDM_RUN };
        thread_ctx->send_msg(thread_ctx, &msg);
        if (fd >= 0)
            close(fd);
    }

    return NULL;
}

/**
 * @see modem.h
 */
int crm_hal_start_modem(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    LOGD("[VMODEM] sysfs (%s) writing: %s", i_ctx->vmodem_sysfs_mdm_ctrl, START_MODEM);
    return crm_file_write(i_ctx->vmodem_sysfs_mdm_ctrl, START_MODEM);
}

/**
 * @see modem.h
 */
int crm_hal_stop_modem(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    LOGD("[VMODEM] sysfs (%s) writing: %s", i_ctx->vmodem_sysfs_mdm_ctrl, STOP_MODEM);
    return crm_file_write(i_ctx->vmodem_sysfs_mdm_ctrl, STOP_MODEM);
}

/**
 * @see modem.h
 */
void crm_hal_init_modem(crm_hal_ctx_internal_t *i_ctx, bool force_stop)
{
    ASSERT(i_ctx != NULL);

    if (i_ctx->silent_reset_enabled) {
        ASSERT(crm_file_write(i_ctx->dump_node, "set_coredump_config silent_reset=1") == 0);
        LOGV("modem silent reset ENABLED");
    }

    i_ctx->mdm_state = get_mdm_state_from_sysfs(i_ctx->vmodem_sysfs_mdm_state);

    bool stop_modem = force_stop || !i_ctx->support_mdm_up_on_start ||
                      i_ctx->mdm_state == EV_MDM_TRAP ||
                      i_ctx->mdm_state == EV_MDM_FW_FAIL ||
                      i_ctx->mdm_state == -1;

    if (stop_modem) {
        DASSERT(crm_hal_stop_modem(i_ctx) == 0, "Failed to write (%s) writing: %s",
                i_ctx->vmodem_sysfs_mdm_ctrl, STOP_MODEM);
        struct pollfd pfd = { .fd = i_ctx->s_fd, .events = POLLIN };

        i_ctx->mdm_state = EV_MDM_OFF;

        /* at CRM boot, modem can be up or down. if modem is UP, an UEVENT will be received,
         * otherwise, timeout will expire */
        struct timespec timer_end;
        crm_time_add_ms(&timer_end, TIMEOUT_MDM_OFF);

        int timeout;
        while ((timeout = crm_time_get_remain_ms(&timer_end)) > 0) {
            int err = poll(&pfd, 1, timeout);
            if (0 == err) {
                break;
            } else if (pfd.revents & POLLIN) {
                int evt = crm_hal_get_mdm_state(i_ctx);
                if (evt != -1) {
                    i_ctx->mdm_state = evt;
                    if ((EV_MDM_OFF == i_ctx->mdm_state) || (EV_MDM_TRAP == i_ctx->mdm_state))
                        break;
                }
            }
        }
    }
}

/**
 * @see modem.h
 */
int crm_hal_get_poll_mdm_fd(const char *host_socket_name)
{
    if (!host_socket_name)
        return open_uevent_netlink();
    else
        return open_debug_socket(host_socket_name);
}

/**
 * @see modem.h
 */
int crm_hal_get_mdm_state(crm_hal_ctx_internal_t *i_ctx)
{
    if (is_modem_event(i_ctx))
        return get_mdm_state_from_sysfs(i_ctx->vmodem_sysfs_mdm_state);
    else
        return -1;
}
