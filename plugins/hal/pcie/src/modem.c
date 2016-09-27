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

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <linux/netlink.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>

#ifndef HOST_BUILD
#include <linux/mdm_ctrl.h>
#include <linux/gsmmux.h>
#else
#include "mux_mcd_stubs.h"
#endif

#define CRM_MODULE_TAG "HAL"
#include "utils/at.h"
#include "utils/common.h"
#include "utils/file.h"
#include "utils/logs.h"
#include "utils/time.h"

#include "common.h"
#include "modem.h"
#include "ping.h"

#define TIMEOUT_MDM_OFF  1000

/**
 * @TODO remove those inline functions OR use TCS to get the path
 */
static inline void pcie_rescan(crm_hal_ctx_internal_t *i_ctx)
{
    if (i_ctx->pcie_rescan != NULL)
        crm_file_write(i_ctx->pcie_rescan, "1");
}

static inline void pcie_remove(crm_hal_ctx_internal_t *i_ctx)
{
    if (i_ctx->pcie_rm != NULL)
        crm_file_write(i_ctx->pcie_rm, "1");
}

static inline void pcie_force_power_on(crm_hal_ctx_internal_t *i_ctx)
{
    crm_file_write(i_ctx->pcie_pwr_ctrl, "on");
}

static inline void pcie_enable_lpm(crm_hal_ctx_internal_t *i_ctx)
{
    if (i_ctx->pcie_rtpm != NULL)
        crm_file_write(i_ctx->pcie_rtpm, "1");
}

static inline void pcie_disable_lpm(crm_hal_ctx_internal_t *i_ctx)
{
    if (i_ctx->pcie_rtpm != NULL)
        crm_file_write(i_ctx->pcie_rtpm, "0");
}

//@TODO: merge this function with sofia code?
static int open_debug_socket(const char *name)
{
    ASSERT(name != NULL);

    errno = 0;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    DASSERT(fd >= 0, "failed to open socket (%s)", strerror(errno));

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

static int open_netlink(void)
{
    errno = 0;
    int fd = socket(AF_NETLINK, SOCK_RAW, MAX_LINKS - 1);
    DASSERT(fd >= 0, "failed to open netlink socket (%s)", strerror(errno));

    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_pid = getpid();
    sa.nl_groups = -1;

    DASSERT(bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0, "failed to bind socket (%s)",
            strerror(errno));

    return fd;
}

/**
 * @see modem.h
 */
int crm_hal_start_modem(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->mux_fd == -1);

    LOGD("[MCD] Modem POWER ON");
    int ret = ioctl(i_ctx->mcd_fd, MDM_CTRL_POWER_ON);

    // START HACK
    if (!ret) {
        sleep(1);
        pcie_rescan(i_ctx);
        sleep(1);
        pcie_force_power_on(i_ctx);
        sleep(1);
        pcie_enable_lpm(i_ctx);
    }
    // END HACK

    return ret;
}

/**
 * @see modem.h
 */
int crm_hal_stop_modem(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    if (i_ctx->mux_fd > 0) {
        close(i_ctx->mux_fd);
        i_ctx->mux_fd = -1;
    }

    /* Force wake up before modem power off */
    pcie_force_power_on(i_ctx);
    pcie_disable_lpm(i_ctx);

    LOGD("[MCD] Modem POWER OFF");
    int ret = ioctl(i_ctx->mcd_fd, MDM_CTRL_POWER_OFF);

    // START HACK
    if (!ret) {
        pcie_remove(i_ctx);
        pcie_rescan(i_ctx);
    }
    // END HACK

    return ret;
}

/**
 * @see modem.h
 */
int crm_hal_warm_reset_modem(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    if (i_ctx->mux_fd > 0) {
        close(i_ctx->mux_fd);
        i_ctx->mux_fd = -1;
    }

    // START HACK
    pcie_disable_lpm(i_ctx);
    sleep(2);
    // END HACK

    LOGD("[MCD] Modem WARM RESET");
    int ret = ioctl(i_ctx->mcd_fd, MDM_CTRL_WARM_RESET);

    // START HACK
    if (!ret) {
        pcie_remove(i_ctx);
        pcie_rescan(i_ctx);
        sleep(5);
        pcie_enable_lpm(i_ctx);
        sleep(2);
    }
    // END HACK

    return ret;
}

/**
 * @see modem.h
 */
int crm_hal_cold_reset_modem(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    if (i_ctx->mux_fd > 0) {
        close(i_ctx->mux_fd);
        i_ctx->mux_fd = -1;
    }

    // START HACK
    pcie_force_power_on(i_ctx);
    pcie_disable_lpm(i_ctx);
    sleep(2);
    // END HACK

    LOGD("[MCD] Modem COLD RESET");
    int ret = ioctl(i_ctx->mcd_fd, MDM_CTRL_COLD_RESET);

    // START HACK
    if (!ret) {
        sleep(5);
        pcie_enable_lpm(i_ctx);
        sleep(2);
        pcie_remove(i_ctx);
        pcie_rescan(i_ctx);
    }
    // END HACK

    return ret;
}

/**
 * @see modem.h
 */
void crm_hal_init_modem(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    char node[64];
    // @TODO: remove this - MDM_CLI_DEFAULT_INSTANCE computation when MDM_CLI_DEFAULT_INSTANCE is 0
    snprintf(node, sizeof(node), "/dev/mdm_ctrl%d", i_ctx->inst_id - MDM_CLI_DEFAULT_INSTANCE);
    i_ctx->mcd_fd = open(node, O_WRONLY);
    DASSERT(i_ctx->mcd_fd >= 0, "failed to open MCD (%s)", node);

    //@TODO: configure this dynamically if needed
    struct mdm_ctrl_cfg cfg = { BOARD_PCIE, MODEM_7360, POWER_ON_PMIC, 0 };

    errno = 0;
    int err = ioctl(i_ctx->mcd_fd, MDM_CTRL_SET_CFG, &cfg);
    /* MCD returns EBUSY if already configured */
    DASSERT(!err || errno == EBUSY, "failed to configure MCD");

    /* Disable MCD event notification mechanism */
    int filter = 0;
    DASSERT(!ioctl(i_ctx->mcd_fd, MDM_CTRL_SET_POLLED_STATES, &filter),
            "failed to configure MCD filter");

    // @TODO: disable PCIE link here

    i_ctx->mdm_state = EV_MDM_OFF;
    ASSERT(!crm_hal_stop_modem(i_ctx));
    pcie_remove(i_ctx);
    pcie_rescan(i_ctx);

    /* at CRM boot, modem can be up or down. if modem is UP, an event will be received,
     * otherwise, timeout will expire */
    struct timespec timer_end;
    int timeout;
    crm_time_add_ms(&timer_end, TIMEOUT_MDM_OFF);
    while ((timeout = crm_time_get_remain_ms(&timer_end)) > 0) {
        struct pollfd pfd = { .fd = i_ctx->net_fd, .events = POLLIN };
        int err = poll(&pfd, 1, timeout);
        if (0 == err) {
            break;
        } else if (pfd.revents & POLLIN) {
            int evt = crm_hal_get_mdm_state(i_ctx);
            if (evt != -1) {
                i_ctx->mdm_state = evt;
                if (EV_MDM_OFF == i_ctx->mdm_state)
                    break;
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
        return open_netlink();
    else
        return open_debug_socket(host_socket_name);
}

/**
 * @see modem.h
 */
int crm_hal_get_mdm_state(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);
    char tmp[200];

    struct iovec iov = { tmp, sizeof(tmp) };
    struct sockaddr_nl sa;
    struct msghdr msg = { &sa, sizeof(sa), &iov, 1, NULL, 0, 0 };

    /* @TODO: a different code might be needed to read the debug socket */

    memset(tmp, 0, sizeof(tmp));
    memset(&sa, 0, sizeof(sa));
    int len = recvmsg(i_ctx->net_fd, &msg, 0);
    if (len <= 0)
        return -1;

    struct nlmsghdr *nh = (struct nlmsghdr *)tmp;

    size_t size = NLMSG_PAYLOAD(nh, 0);
    char *state = NLMSG_DATA(nh);
    ASSERT(state);

    static struct {
        const char *msg;
        int id;
    } events[] = {
        { "MDM_NOT_READY", EV_MDM_OFF },
        { "ROM_READY", EV_MDM_FLASH },
        { "MDM_READY", EV_MDM_RUN },
        { "CRASH", EV_MDM_CRASH },
        { "CD_READY", EV_MDM_DUMP_READY },
        { "CD_READY_LINK_DOWN", EV_MDM_LINK_DOWN },
    };
    for (size_t i = 0; i < ARRAY_SIZE(events); i++) {
        if (!strncmp(state, events[i].msg, size)) {
            LOGD("[PCIE] uevent (%s) read", events[i].msg);
            return events[i].id;
        }
    }

    return -1;
}

/**
 * Mux AP and BP
 *
 * @param [in] fd File descriptor to modem node
 *
 * @return 0 if successful
 * @return -1 in case of recoverable error
 * @return -2 in case of unrecoverable error
 */
static int mount_mux(int fd)
{
    ASSERT(fd >= 0);

    /* MUX on BP side first */
    if (crm_send_at(fd, CRM_MODULE_TAG, "AT+CMUX=0,0,,1509,10,3,30,,", 2500, -1)) {
        LOGE("failed to configure MUX on BP side");
        return -1;
    }
    LOGD("MUX configured on BP side");

    /* Then, MUX on AP side */
    int ldisc = N_GSM0710;
    ASSERT(!ioctl(fd, TIOCSETD, &ldisc));
    ASSERT(!ioctl(fd, TIOCGETD, &ldisc));
    if (ldisc != N_GSM0710) {
        LOGE("failed to set line discipline");
        return -2;
    }

    struct gsm_config cfg;
    memset(&cfg, 0, sizeof(struct gsm_config));

    ASSERT(!ioctl(fd, GSMIOC_GETCONF, &cfg));
    cfg.encapsulation = 0;
    cfg.initiator = 1;
    cfg.mru = 1509;
    cfg.mtu = 1509;
    cfg.burst = 0;
    if (ioctl(fd, GSMIOC_SETCONF, &cfg)) {
        LOGE("failed to configure MUX on AP side");
        return -2;
    }

    LOGD("MUX configured on AP side");
    return 0;
}

void *crm_hal_cfg_modem(crm_thread_ctx_t *thread_ctx, void *param)
{
    crm_hal_ctx_internal_t *i_ctx = (crm_hal_ctx_internal_t *)param;
    crm_ipc_msg_t msg = { .scalar = EV_MDM_CONFIGURED };

    ASSERT(i_ctx != NULL);
    ASSERT(thread_ctx != NULL);
    ASSERT(i_ctx->mux_fd == -1);

    int fd_abort = thread_ctx->get_poll_fd(thread_ctx);
    ASSERT(fd_abort >= 0);

    int fd = crm_hal_ping_modem(i_ctx->modem_node, i_ctx->ping_timeout, fd_abort, false);
    if (fd >= 0) {
        i_ctx->mux_fd = fd;
        int err = mount_mux(i_ctx->mux_fd);
        if (!err) {
            /* @TODO: is it needed to check that all DLC exists before pinging the modem ? */
            fd = crm_hal_ping_modem(i_ctx->ping_mux_node, i_ctx->ping_timeout, fd_abort, true);
            if (fd >= 0)
                close(fd); /* success case */
            else
                msg.scalar = EV_TIMEOUT;
        } else {
            close(i_ctx->mux_fd);
            i_ctx->mux_fd = -1;
            if (-1 == err)
                msg.scalar = EV_MUX_ERR;
            else
                msg.scalar = EV_MUX_DEAD;
        }
    } else {
        msg.scalar = EV_MUX_ERR;
    }

    thread_ctx->send_msg(thread_ctx, &msg);

    return NULL;
}
