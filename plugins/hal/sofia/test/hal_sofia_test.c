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

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>

#define CRM_MODULE_TAG "HALT"
#include "utils/common.h"
#include "utils/keys.h"
#include "utils/logs.h"
#include "utils/thread.h"
#include "utils/property.h"
#include "test/mdm_stub.h"
#include "test/test_utils.h"
#include "plugins/control.h"
#include "plugins/hal.h"

#include "common.h"
#include "libmdmcli/mdm_cli.h"

crm_ipc_ctx_t *g_ipc;
int g_control_id = MCTRL_OFF;

enum hal_ops {
    SHUTDOWN,
    POWER_ON,
    BOOT,
    RESET,
};

static void hal_event(crm_ctrl_ctx_t *ctx, const crm_hal_evt_t *event)
{
    (void)ctx;   // UNUSED
    ASSERT(event != NULL);

    crm_ipc_msg_t msg = { .scalar = event->type };
    g_ipc->send_msg(g_ipc, &msg);
}

static bool check_operation(crm_hal_ctx_t *hal, enum hal_ops hal_op,
                            mdm_stub_control_t control_event, crm_hal_evt_type_t expected_event)
{
    bool ret = false;

    g_control_id = control_event;

    switch (hal_op) {
    case SHUTDOWN:
        hal->shutdown(hal);
        break;
    case POWER_ON:
        hal->power_on(hal);
        break;
    case BOOT:
        hal->boot(hal);
        break;
    case RESET:
        hal->reset(hal, RESET_WARM);
        break;
    default: ASSERT(0);
    }

    struct pollfd pfd = { .fd = g_ipc->get_poll_fd(g_ipc), .events = POLLIN };
    bool wait = true;
    while (wait) {
        int err = poll(&pfd, 1, 60000);

        if (0 == err) {
            LOGE("timeout");
            wait = false;
        } else {
            crm_ipc_msg_t msg = { .scalar = 0 };
            while (g_ipc->get_msg(g_ipc, &msg)) {
                if (msg.scalar == expected_event) {
                    wait = false;
                    ret = true;
                }
            }
        }
    }

    return ret;
}

static void *mdm_ctrl(crm_thread_ctx_t *thread_ctx, void *arg)
{
    ASSERT(thread_ctx != NULL);
    bool *stopping = (bool *)arg;

    int c_fd = CRM_TEST_connect_socket(MDM_STUB_SOFIA_CTRL);
    ASSERT(c_fd >= 0);

    int property_fifo_fd = open(CRM_PROPERTY_PIPE_NAME, O_RDONLY);
    ASSERT(property_fifo_fd >= 0);

    struct pollfd pfd[] = {
        { .fd = c_fd, .events = POLLIN },
        { .fd = thread_ctx->get_poll_fd(thread_ctx), .events = POLLIN },
        { .fd = property_fifo_fd, .events = POLLIN },
    };

    /* Set it to true as CRM should stop RPCD as soon as it starts */
    bool rpcd_running = true;
    bool running = true;
    while (running) {
        poll(pfd, ARRAY_SIZE(pfd), -1);

        if (pfd[0].revents & POLLIN) {
            int id;
            ssize_t len = recv(c_fd, &id, sizeof(id), 0);
            ASSERT(len == sizeof(id));

            if (MREQ_READY == id) {
                crm_ipc_msg_t msg = { .scalar = id };
                thread_ctx->send_msg(thread_ctx, &msg);
            } else {
                ASSERT(send(c_fd, &g_control_id, sizeof(g_control_id), MSG_NOSIGNAL) != 0);
            }
        } else if (pfd[1].revents & POLLIN) {
            crm_ipc_msg_t msg;
            while (thread_ctx->get_msg(thread_ctx, &msg))
                if (-1 == msg.scalar)
                    running = false;
        } else if (pfd[2].revents & POLLIN) {
            char buf[CRM_PROPERTY_VALUE_MAX + CRM_PROPERTY_KEY_MAX + 2];
            ssize_t ret = read(pfd[2].fd, buf, sizeof(buf) - 1);
            ASSERT(ret >= 0);
            buf[ret] = '\0';
            LOGD("prop read: %s", buf);
            if (!strcmp(buf, "ctl.start=rpc-daemon")) {
                ASSERT(rpcd_running == false);
                rpcd_running = true;
                int fd = open("/tmp/crm/rpcd_started", O_CREAT | O_WRONLY, 0666);
                ASSERT(fd >= 0);
                write(fd, &fd, sizeof(fd)); // Just write dummy value :)
                close(fd);
            } else if (!strcmp(buf, "ctl.stop=rpc-daemon")) {
                ASSERT(rpcd_running == true);
                rpcd_running = false;
            }
        } else if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            ASSERT(0);
        } else if (pfd[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (*stopping)
                running = false;
            else
                ASSERT(0);
        } else if (pfd[2].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (!*stopping)
                ASSERT(0);
        }
    }

    int id = MCTRL_STOP;
    ASSERT(send(c_fd, &id, sizeof(id), MSG_NOSIGNAL) != 0);
    close(c_fd);

    return NULL;
}

static void run_test(tcs_ctx_t *tcs)
{
    bool stopping = false;
    /* Fake control context, just for testing */
    crm_ctrl_ctx_t control = {
        .notify_hal_event = hal_event,
    };

    /* Create FIFO to capture RPCD handling */
    unlink(CRM_PROPERTY_PIPE_NAME);
    ASSERT(mkfifo(CRM_PROPERTY_PIPE_NAME, 0666) == 0);
    unlink("/tmp/crm/rpcd_started");
    unlink("/tmp/crm/rpcd_stopped");

    crm_thread_ctx_t *mdm = crm_thread_init(mdm_ctrl, &stopping, true, false);

    g_ipc = crm_ipc_init(CRM_IPC_THREAD);

    crm_property_init(MDM_CLI_DEFAULT_INSTANCE); // Needed as HAL uses properties to control RPCD
    crm_property_set(CRM_KEY_DBG_ENABLE_SILENT_RESET, "true");
    crm_hal_ctx_t *hal = crm_hal_init(0, true, true, tcs, &control);

    crm_ipc_msg_t msg;
    struct pollfd pfd = { .fd = mdm->get_poll_fd(mdm), .events = POLLIN };
    bool wait = true;
    while (wait) {
        poll(&pfd, 1, -1);
        while (mdm->get_msg(mdm, &msg))
            if (MREQ_READY == msg.scalar)
                wait = false;
    }

    /* normal cases */
    ASSERT(check_operation(hal, SHUTDOWN, MCTRL_OFF, HAL_MDM_OFF) == true);
    ASSERT(check_operation(hal, POWER_ON, MCTRL_OFF, HAL_MDM_FLASH) == true);
    ASSERT(check_operation(hal, BOOT, MCTRL_RUN, HAL_MDM_RUN) == true);
    ASSERT(check_operation(hal, RESET, MCTRL_OFF, HAL_MDM_FLASH) == true);
    ASSERT(check_operation(hal, BOOT, MCTRL_RUN, HAL_MDM_RUN) == true);

    /* firmware corrupted use case*/
    ASSERT(check_operation(hal, SHUTDOWN, MCTRL_OFF, HAL_MDM_OFF) == true);
    ASSERT(check_operation(hal, POWER_ON, MCTRL_OFF, HAL_MDM_FLASH) == true);
    ASSERT(check_operation(hal, BOOT, MCTRL_FW_FAILURE, HAL_MDM_UNRESPONSIVE) == true);
    ASSERT(check_operation(hal, RESET, MCTRL_OFF, HAL_MDM_FLASH) == true);
    ASSERT(check_operation(hal, BOOT, MCTRL_RUN, HAL_MDM_RUN) == true);

    /* error injection */
    ASSERT(check_operation(hal, RESET, MCTRL_DUMP, HAL_MDM_DUMP) == true);
    ASSERT(check_operation(hal, SHUTDOWN, MCTRL_OFF, HAL_MDM_OFF) == true);

    stopping = true;

    hal->dispose(hal);

    msg.scalar = -1;
    mdm->send_msg(mdm, &msg);

    mdm->dispose(mdm, NULL);
    g_ipc->dispose(g_ipc, NULL);
}

int main()
{
    tcs_ctx_t *tcs = CRM_TEST_tcs_init("host_sofia", MDM_CLI_DEFAULT_INSTANCE);

    ASSERT(tcs);

    // @TODO: configure the modem to load
    pid_t pid_mdm = CRM_TEST_start_stub_sofia_mdm(tcs);

    /* do not start the test till modem nodes are not available. on HOST, SYSFS availability is
     * guaranteed, that is why this is not checked by HAL and needs to be done here before
     * starting it. Otherwise, HAL will crash */
    struct stat st;

    ASSERT(tcs->select_group(tcs, ".hal") == 0);
    char *vmodem_sysfs_mdm_state = tcs->get_string(tcs, "vmodem_sysfs_mdm_state");
    ASSERT(vmodem_sysfs_mdm_state);

    char *vmodem_sysfs_mdm_ctrl = tcs->get_string(tcs, "vmodem_sysfs_mdm_ctrl");
    ASSERT(vmodem_sysfs_mdm_ctrl);

    while (stat(vmodem_sysfs_mdm_state, &st) != 0 && S_ISREG(st.st_mode) != true)
        usleep(5000);

    while (stat(vmodem_sysfs_mdm_ctrl, &st) != 0 && S_ISREG(st.st_mode) != true)
        usleep(5000);


    run_test(tcs);

    pid_t status;
    waitpid(pid_mdm, &status, 0);
    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) != 0)
            LOGE("modem has exit with error code: %d", WEXITSTATUS(status));
        else
            LOGD("modem has exited successfully");
    } else if (WIFSIGNALED(status)) {
        LOGE("modem terminated by signal %d", WTERMSIG(status));
    }

    free(vmodem_sysfs_mdm_state);
    free(vmodem_sysfs_mdm_ctrl);
    tcs->dispose(tcs);
    LOGD("success");

    return 0;
}
