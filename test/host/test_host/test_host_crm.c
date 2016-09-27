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

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <poll.h>

#define CRM_MODULE_TAG "TEST"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/keys.h"
#include "utils/property.h"
#include "utils/thread.h"
#include "utils/string_helpers.h"
#include "test/mdm_stub.h"
#include "test/test_utils.h"
#include "plugins/mdmcli_wire.h"

#include "libmdmcli/mdm_cli.h"
#define INPUT_FW "/tmp/fw.fls"

typedef struct cmd_options {
    const char short_opt;
    const char *long_opt;
    const char *description;
    bool has_arg;
} cmd_options_t;

typedef struct test_crm_cfg {
    bool load_stub;
    bool no_client;
} test_crm_cfg_t;

// Short command line options are not used, that is why the values are not important here
enum {
    SHORT_HELP = 'a',
    SHORT_NO_CLIENT,
    SHORT_LOAD_STUB,
};

enum {
    PID_MDM,
    PID_CRM,
    PID_NUM
};

#define DEFAULT_TIMEOUT 12000
#define DUMP_TIMEOUT 30000

#define MDM_FW_RANDOM "/dev/urandom"

#define MENU_HELP "Shows this message"
#define MENU_STUB "load stubs instead of real plugins"
#define MENU_NO_CLIENT "do not start client"

#define WAIT_EVENT(ipc, evt, type, ms) do { wait_evt(ipc, evt, type, ms, __LINE__); } while (0)

typedef struct test_host_ctx {
    test_crm_cfg_t cfg;

    struct sigaction sigact;
    pid_t pids[PID_NUM];
    mdm_cli_hdle_t *mdmcli;
    crm_thread_ctx_t *ctrl_stub_mdm;
    crm_ipc_ctx_t *ipc;

    bool stopping;
    bool modem_not_booting;
    bool verify_fw_failure;

    char *fw_out;
} test_host_ctx_t;

// Global values
test_host_ctx_t *g_ctx = NULL;

static void cleanup_test_host(void)
{
    if (g_ctx) {
        // @TODO: maybe add a mutex here
        test_host_ctx_t *ctx = g_ctx;
        g_ctx = NULL;

        /* unregister from SIGCHLD event */
        ASSERT(sigaction(SIGCHLD, &ctx->sigact, NULL) == 0);

        LOGD("------------- cleaning -------------");
        ctx->stopping = true;
        if (ctx->ctrl_stub_mdm) {
            /* stop stub modem and its thread control */
            crm_ipc_msg_t msg = { .scalar = -1 };
            ctx->ctrl_stub_mdm->send_msg(ctx->ctrl_stub_mdm, &msg);

            ctx->ctrl_stub_mdm->dispose(ctx->ctrl_stub_mdm, NULL);
            ctx->ctrl_stub_mdm = NULL;
        }

        if (ctx->ipc) {
            ctx->ipc->dispose(ctx->ipc, NULL);
            ctx->ipc = NULL;
        }

        if (ctx->mdmcli) {
            mdm_cli_disconnect(ctx->mdmcli);
            ctx->mdmcli = NULL;
        }

        /* A STOP message has already been sent to the stub modem. Do not kill it */
        for (size_t i = 1; i < PID_NUM; i++) {
            if (ctx->pids[i] != -1) {
                LOGD("killing PID %d...", ctx->pids[i]);
                kill(ctx->pids[i], SIGKILL);
            }
        }

        LOGD("Waiting children...");
        for (int i = 0; i < PID_NUM; i++) {
            errno = 0;
            DASSERT(waitpid(ctx->pids[i], NULL, 0) == ctx->pids[i], "Failed to kill process %d. %s",
                    ctx->pids[i], strerror(errno));
        }

        unlink(INPUT_FW);
        unlink(ctx->fw_out);
        free(ctx->fw_out);
        LOGD("*** test stopped ***");
    }
}

static void sig_handler(int sig)
{
    LOGD("signal %d caught", sig);
    cleanup_test_host();
    exit(-1);
}

static void set_signal_handler(struct sigaction *old)
{
    struct sigaction sigact;

    ASSERT(old != NULL);

    memset(&sigact, 0, sizeof(struct sigaction));
    ASSERT(sigemptyset(&sigact.sa_mask) == 0);
    sigact.sa_flags = 0;
    sigact.sa_handler = sig_handler;

    ASSERT(sigaction(SIGABRT, &sigact, NULL) == 0);
    ASSERT(sigaction(SIGTERM, &sigact, NULL) == 0);
    ASSERT(sigaction(SIGUSR1, &sigact, NULL) == 0);
    ASSERT(sigaction(SIGHUP, &sigact, NULL) == 0);
    ASSERT(sigaction(SIGINT, &sigact, NULL) == 0);
    ASSERT(sigaction(SIGCHLD, &sigact, old) == 0);
}

static void *mdm_ctrl(crm_thread_ctx_t *thread_ctx, void *arg)
{
    test_host_ctx_t *ctx = (test_host_ctx_t *)arg;

    ASSERT(thread_ctx != NULL);
    ASSERT(ctx != NULL);

    int c_fd = CRM_TEST_connect_socket(MDM_STUB_SOFIA_CTRL);
    DASSERT(c_fd >= 0, "Failed to connect to modem stub control socket");

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
            if (0 == len) {
                running = false;
                break;
            }

            ASSERT(len == sizeof(id));

            if (MREQ_READY == id) {
                crm_ipc_msg_t msg = { .scalar = id };
                thread_ctx->send_msg(thread_ctx, &msg);
            } else {
                /* @TODO: add error injection in the stub modem side here */
                if (MREQ_OFF == id) {
                    id = MCTRL_OFF;
                } else if (MREQ_ON == id) {
                    if (ctx->modem_not_booting) {
                        ctx->modem_not_booting = false;
                        LOGD("===> ERROR INJECTION: modem not booting");
                        id = -1;
                    } else if (ctx->verify_fw_failure) {
                        ctx->verify_fw_failure = false;
                        LOGD("===> ERROR INJECTION: FIRMWARE-FAILURE");
                        id = MCTRL_FW_FAILURE;
                    } else {
                        id = MCTRL_RUN;
                    }
                }

                if (id != -1)
                    ASSERT(send(c_fd, &id, sizeof(id), MSG_NOSIGNAL) != 0);
            }
        } else if (pfd[1].revents & POLLIN) {
            crm_ipc_msg_t msg;
            while (thread_ctx->get_msg(thread_ctx, &msg)) {
                if (-1 == msg.scalar) {
                    running = false;
                } else {
                    int id = (int)msg.scalar;
                    ASSERT(send(c_fd, &id, sizeof(id), MSG_NOSIGNAL) != 0);
                }
            }
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
                write(fd, &fd, sizeof(fd));                 // Just write dummy value :)
                close(fd);
            } else if (!strcmp(buf, "ctl.stop=rpc-daemon")) {
                ASSERT(rpcd_running == true);
                rpcd_running = false;
            }
        } else if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            DASSERT(0, "error in control socket");
        } else if (pfd[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (ctx->stopping)
                running = false;
            else
                DASSERT(0, "error in stub modem thread control socket");
        } else if (pfd[2].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (!ctx->stopping)
                DASSERT(0, "error in property pipe");
        }
    }

    LOGD("stopping modem...");
    int id = MCTRL_STOP;
    ASSERT(send(c_fd, &id, sizeof(id), MSG_NOSIGNAL) != 0);
    close(c_fd);

    return NULL;
}

static int start_crm(bool stubs_loaded, const char *vmodem_sysfs_mdm_state,
                     const char *vmodem_sysfs_mdm_ctrl)
{
    pid_t child = fork();

    ASSERT(child != -1);

    if (0 == child) {
        crm_mdmcli_wire_ctx_t *wire = crm_mdmcli_wire_init(CRM_CLIENT_TO_SERVER,
                                                           MDM_CLI_DEFAULT_INSTANCE);
        CRM_TEST_get_control_socket_android(wire->get_socket_name(wire));
        wire->dispose(wire);

        if (!stubs_loaded) {
            /* do not start CRM till modem nodes are not available. on HOST, SYSFS availability is
             * guaranteed, that is why this is not checked by CRM and needs to be done here before
             * starting it. Otherwise, CRM will crash */
            struct stat st;
            while (stat(vmodem_sysfs_mdm_state, &st) != 0 && S_ISREG(st.st_mode) != true)
                usleep(5000);

            while (stat(vmodem_sysfs_mdm_ctrl, &st) != 0 && S_ISREG(st.st_mode) != true)
                usleep(5000);
        }

        const char *app_name = "crm";
        const char *args[] = { app_name, NULL };

        int err = execvp(app_name, (char **)args);

        if (err)
            LOGE("Failed to start crm: %s", strerror(errno));
        exit(0);
    } else {
        return child;
    }
}

static int mdm_evt(const mdm_cli_callback_data_t *ev)
{
    ASSERT(ev != NULL);
    test_host_ctx_t *ctx = (test_host_ctx_t *)ev->context;
    ASSERT(ctx != NULL);

    crm_ipc_msg_t msg = { .scalar = ev->id };
    mdm_cli_dbg_type_t type = 0;

    if (MDM_DBG_INFO == ev->id) {
        ASSERT(ev->data_size == sizeof(mdm_cli_dbg_info_t));
        mdm_cli_dbg_info_t *dbg_info = (mdm_cli_dbg_info_t *)ev->data;
        ASSERT(dbg_info != NULL);
        type = dbg_info->type;

        if (dbg_info->type == DBG_TYPE_DUMP_END) {
            struct stat st;
            ASSERT(dbg_info->nb_data == 3);
            ASSERT(strcmp(dbg_info->data[0], DUMP_STR_SUCCEED) == 0);

            LOGD("info: %s", dbg_info->data[1]);
            ASSERT(stat(dbg_info->data[1], &st) == 0);
            ASSERT(S_ISREG(st.st_mode) == true);
            unlink(dbg_info->data[1]);

            LOGD("dump: %s", dbg_info->data[2]);
            ASSERT(stat(dbg_info->data[2], &st) == 0);
            ASSERT(S_ISREG(st.st_mode) == true);
            unlink(dbg_info->data[2]);
        }
    }

    if (ctx->ipc) {
        ctx->ipc->send_msg(ctx->ipc, &msg);
        if (type != 0) {
            msg.scalar = type;
            ctx->ipc->send_msg(ctx->ipc, &msg);
        }
    }

    return 0;
}

static void wait_evt(crm_ipc_ctx_t *ipc, mdm_cli_event_t evt, mdm_cli_dbg_type_t type, int ms,
                     int line)
{
    ASSERT(ipc != NULL);
    struct pollfd pfd = { .fd = ipc->get_poll_fd(ipc), .events = POLLIN };

    DASSERT(poll(&pfd, 1, ms), "event %s not received with timeout: %dms",
            crm_mdmcli_wire_req_to_string(evt), ms);

    crm_ipc_msg_t msg;

    ASSERT(ipc->get_msg(ipc, &msg));
    DASSERT(msg.scalar == evt, "Event %s received instead of %s (line: %d)",
            crm_mdmcli_wire_req_to_string(msg.scalar),
            crm_mdmcli_wire_req_to_string(evt), line);

    if (MDM_DBG_INFO == evt) {
        ASSERT(poll(&pfd, 1, 10));
        ASSERT(ipc->get_msg(ipc, &msg));
        DASSERT(msg.scalar == type, "Type of event %s received instead of %s (line: %d)",
                crm_mdmcli_dbg_type_to_string(msg.scalar),
                crm_mdmcli_dbg_type_to_string(type), line);
    }
}

static void inline inject_mdm_error(crm_thread_ctx_t *ctrl_stub_mdm, mdm_stub_control_t request)
{
    crm_ipc_msg_t msg = { .scalar = request };

    ctrl_stub_mdm->send_msg(ctrl_stub_mdm, &msg);
}

static void fake_client(test_host_ctx_t *ctx)
{
    ASSERT(ctx != NULL);

    mdm_cli_register_t evts[] = {
        { MDM_UP, mdm_evt, ctx },
        { MDM_DOWN, mdm_evt, ctx },
        { MDM_OOS, mdm_evt, ctx },
        { MDM_DBG_INFO, mdm_evt, ctx },
    };

    while (!(ctx->mdmcli =
                 mdm_cli_connect("test app", MDM_CLI_DEFAULT_INSTANCE, ARRAY_SIZE(evts), evts)))
        sleep(1);

    WAIT_EVENT(ctx->ipc, MDM_DOWN, 0, DEFAULT_TIMEOUT);

    /* first boot: TLV application */
    {
        mdm_cli_acquire(ctx->mdmcli);
        WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_TLV_SUCCESS, DEFAULT_TIMEOUT);
        WAIT_EVENT(ctx->ipc, MDM_UP, 0, DEFAULT_TIMEOUT);
        mdm_cli_release(ctx->mdmcli);
        WAIT_EVENT(ctx->ipc, MDM_DOWN, 0, DEFAULT_TIMEOUT);
    }

    for (int i = 0; i < 10; i++) {
        mdm_cli_dbg_info_t dbg_info;
        MDM_CLI_INIT_DBG_INFO(dbg_info);
        const char *dbg_data[4] = { "First", "Second", "Third", "Fourth" };
        dbg_info.type = DBG_TYPE_APIMR;
        dbg_info.data = dbg_data;
        dbg_info.nb_data = 4;

        /* normal boot */
        {
            mdm_cli_acquire(ctx->mdmcli);
            WAIT_EVENT(ctx->ipc, MDM_UP, 0, DEFAULT_TIMEOUT);
        }

        /* normal reset */
        {
            mdm_cli_restart(ctx->mdmcli, RESTART_MDM_ERR, &dbg_info);
            WAIT_EVENT(ctx->ipc, MDM_DOWN, 0, DEFAULT_TIMEOUT);
            WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_APIMR, DEFAULT_TIMEOUT);
            WAIT_EVENT(ctx->ipc, MDM_UP, 0, DEFAULT_TIMEOUT);
        }

        if (ctx->ctrl_stub_mdm) {
            LOGD("===> ERROR INJECTION: SELF-RESET");
            {
                bool no_ping = i % 2;
                if (no_ping)
                    inject_mdm_error(ctx->ctrl_stub_mdm, MCTRL_NO_PING);
                inject_mdm_error(ctx->ctrl_stub_mdm, MCTRL_SELF_RESET);
                WAIT_EVENT(ctx->ipc, MDM_DOWN, 0, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_SELF_RESET, DEFAULT_TIMEOUT);
                if (no_ping)
                    WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_ERROR, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_UP, 0, DEFAULT_TIMEOUT);
            }

            LOGD("===> ERROR INJECTION: CORE-DUMP");
            {
                inject_mdm_error(ctx->ctrl_stub_mdm, MCTRL_DUMP);
                WAIT_EVENT(ctx->ipc, MDM_DOWN, 0, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_DUMP_START, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_DUMP_END, DUMP_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_UP, 0, DEFAULT_TIMEOUT);
            }

            /* ERROR INJECTION: no ping */
            {
                inject_mdm_error(ctx->ctrl_stub_mdm, MCTRL_NO_PING);
                mdm_cli_restart(ctx->mdmcli, RESTART_MDM_ERR, NULL);
                WAIT_EVENT(ctx->ipc, MDM_DOWN, 0, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_APIMR, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_ERROR, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_UP, 0, DEFAULT_TIMEOUT);
            }

            /* ERROR INJECTION: self-reset during ping */
            {
                inject_mdm_error(ctx->ctrl_stub_mdm, MCTRL_PING_SELF_RESET);
                mdm_cli_restart(ctx->mdmcli, RESTART_MDM_ERR, NULL);

                WAIT_EVENT(ctx->ipc, MDM_DOWN, 0, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_APIMR, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_SELF_RESET, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_UP, 0, DEFAULT_TIMEOUT);
            }

            /* modem not booting after restart request */
            {
                ctx->modem_not_booting = true;
                mdm_cli_restart(ctx->mdmcli, RESTART_MDM_ERR, NULL);
                WAIT_EVENT(ctx->ipc, MDM_DOWN, 0, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_APIMR, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_ERROR, DEFAULT_TIMEOUT);
                WAIT_EVENT(ctx->ipc, MDM_UP, 0, DEFAULT_TIMEOUT);
            }
        }

        mdm_cli_release(ctx->mdmcli);
        WAIT_EVENT(ctx->ipc, MDM_DOWN, 0, DEFAULT_TIMEOUT);
    }

    mdm_cli_acquire(ctx->mdmcli);
    WAIT_EVENT(ctx->ipc, MDM_UP, 0, DEFAULT_TIMEOUT);

    ctx->verify_fw_failure = true;
    mdm_cli_restart(ctx->mdmcli, RESTART_MDM_ERR, NULL);
    WAIT_EVENT(ctx->ipc, MDM_DOWN, 0, DEFAULT_TIMEOUT);
    WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_APIMR, DEFAULT_TIMEOUT);
    WAIT_EVENT(ctx->ipc, MDM_DBG_INFO, DBG_TYPE_FW_FAILURE, DEFAULT_TIMEOUT);
    WAIT_EVENT(ctx->ipc, MDM_OOS, 0, DEFAULT_TIMEOUT);

    mdm_cli_disconnect(ctx->mdmcli);
    ctx->mdmcli = NULL;
    LOGD("=========================>  TEST SUCCEED  <=========================");
}

static void create_fake_fw(test_host_ctx_t *ctx)
{
    ASSERT(ctx);

    unlink(INPUT_FW);
    unlink(ctx->fw_out);

    errno = 0;
    int i_fd = open(MDM_FW_RANDOM, O_RDONLY);
    DASSERT(i_fd >= 0, "open of (%s) failed (%s)", MDM_FW_RANDOM, strerror(errno));

    int o_fd = open(INPUT_FW, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    DASSERT(o_fd >= 0, "open of (%s) failed (%s)", INPUT_FW, strerror(errno));

    char tmp[1024 * 1024];
    DASSERT(read(i_fd, tmp, sizeof(tmp)) == sizeof(tmp), "Failed to read (%s)", strerror(errno));

    for (int i = 0; i < 32; i++)
        DASSERT(write(o_fd, tmp, sizeof(tmp)) == sizeof(tmp), "Failed to write (%s) file (%s)",
                INPUT_FW, strerror(errno));

    close(i_fd);
    DASSERT(close(o_fd) == 0, "Failed to close file (%s)", strerror(errno));

    o_fd = open(ctx->fw_out, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    DASSERT(o_fd >= 0, "open of (%s) failed (%s)", ctx->fw_out, strerror(errno));
    DASSERT(close(o_fd) == 0, "Failed to close file (%s)", strerror(errno));
}

static void usage(cmd_options_t *opts, size_t size)
{
    for (size_t i = 0; i < size; i++)
        printf("\t--%-20s%s\n", opts[i].long_opt, opts[i].description);
    exit(-1);
}

static void get_cfg(int argc, char **argv, test_crm_cfg_t *cfg)
{
    ASSERT(argv != NULL);
    ASSERT(cfg != NULL);

    cmd_options_t opts[] = {
        { SHORT_HELP, "help", MENU_HELP, false },
        { SHORT_NO_CLIENT, "no-client", MENU_NO_CLIENT, false },
        { SHORT_LOAD_STUB, "load-stub", MENU_STUB, false },
    };

    size_t nb_opts = ARRAY_SIZE(opts);
    struct option *long_opts = calloc(sizeof(struct option), (nb_opts + 1));
    char opt_str[30] = { "" };
    int opt_index = 0;

    /* default parameters: */
    cfg->load_stub = false;
    cfg->no_client = false;

    size_t len = 0;
    for (size_t i = 0; i < nb_opts; i++) {
        long_opts[i].name = opts[i].long_opt;
        long_opts[i].has_arg = opts[i].has_arg;
        long_opts[i].flag = NULL;
        /* Return the letter of the short option if long option is provided */
        long_opts[i].val = opts[i].short_opt;
        if (len < (sizeof(opt_str) - 2)) {
            opt_str[len++] = opts[i].short_opt;
            if (opts[i].has_arg)
                opt_str[len++] = ':';
        }
    }
    opt_str[len] = '\0';

    int cmd;
    while ((cmd = getopt_long_only(argc, argv, opt_str, long_opts, &opt_index)) != -1) {
        switch (cmd) {
        case SHORT_HELP:
            usage(opts, nb_opts);
            break;
        case SHORT_LOAD_STUB:
            cfg->load_stub = true;
            break;
        case SHORT_NO_CLIENT:
            cfg->no_client = true;
            break;
        default: usage(opts, nb_opts);
        }
    }

    free(long_opts);

    LOGD("==== Configuration ===");
    LOGD("stubs: %sloaded", cfg->load_stub == true ? "" : "not ");
    LOGD("no client: %s", cfg->no_client == true ? "yes" : "no");
    LOGD("========================");
}


int main(int argc, char *argv[])
{
    test_host_ctx_t ctx;
    int inst_id = MDM_CLI_DEFAULT_INSTANCE;

    g_ctx = &ctx;
    memset(&ctx, 0, sizeof(ctx));
    get_cfg(argc, argv, &ctx.cfg);

    set_signal_handler(&ctx.sigact);

    /* Clean-up in case previous execution did not do it :) */
    unlink(CRM_PROPERTY_PIPE_NAME);
    unlink("/tmp/crm/rpcd_started");
    unlink("/tmp/crm/rpcd_stopped");

    crm_logs_init(inst_id);
    crm_property_init(inst_id);

    crm_property_set(CRM_KEY_DBG_LOAD_STUB, ctx.cfg.load_stub ? "true" : "false");
    crm_property_set(CRM_KEY_DBG_HOST, "true");
    crm_property_set(CRM_KEY_DBG_DISABLE_ESCALATION, "true");
    crm_property_set(CRM_KEY_DBG_ENABLE_SILENT_RESET, "true");
    crm_property_set(CRM_KEY_DATA_PARTITION_ENCRYPTION, "trigger_restart_framework");

    /* Create FIFO to capture RPCD handling */
    ASSERT(mkfifo(CRM_PROPERTY_PIPE_NAME, 0666) == 0);

    tcs_ctx_t *tcs = CRM_TEST_tcs_init("host_sofia", MDM_CLI_DEFAULT_INSTANCE);
    ASSERT(tcs);
    ASSERT(tcs->select_group(tcs, ".hal") == 0);
    char *vmodem_sysfs_mdm_state = tcs->get_string(tcs, "vmodem_sysfs_mdm_state");
    ASSERT(vmodem_sysfs_mdm_state);

    char *vmodem_sysfs_mdm_ctrl = tcs->get_string(tcs, "vmodem_sysfs_mdm_ctrl");
    ASSERT(vmodem_sysfs_mdm_ctrl);

    ctx.fw_out = tcs->get_string(tcs, "flash_node");
    ASSERT(ctx.fw_out);

    char group[15];
    snprintf(group, sizeof(group), "streamline%d", MDM_CLI_DEFAULT_INSTANCE);
    tcs->add_group(tcs, group, true);
    ASSERT(tcs->select_group(tcs, group) == 0);

    int nb_tlvs;
    char **tlvs = tcs->get_string_array(tcs, "tlvs", &nb_tlvs);

    /* create fake tcs tlvs */
    for (int i = 0; i < nb_tlvs; i++) {
        char path[15];
        snprintf(path, sizeof(path), "/tmp/%s", tlvs[i]);
        free(tlvs[i]);
        tlvs[i] = NULL;

        int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
        DASSERT(fd >= 0, "Failed to open file (%s)", strerror(errno));
        write(fd, "test", 4);
        ASSERT(close(fd) == 0);
    }
    free(tlvs);

    create_fake_fw(&ctx);
    unlink(vmodem_sysfs_mdm_state);
    unlink(vmodem_sysfs_mdm_ctrl);

    /* create fake blob hash file */
    int fd = open("/tmp/crm_hash", O_CREAT | O_WRONLY | O_TRUNC, 0666);
    DASSERT(fd >= 0, "Failed to open file (%s)", strerror(errno));
    write(fd, "test", 4);
    ASSERT(close(fd) == 0);

    /* Starting all processes */
    if (!ctx.cfg.load_stub) {
        // starts modem if stubs are NOT loaded
        ctx.pids[PID_MDM] = CRM_TEST_start_stub_sofia_mdm(tcs);

        ctx.stopping = false;
        ctx.ctrl_stub_mdm = crm_thread_init(mdm_ctrl, &ctx, true, false);
    } else {
        LOGD("modem not started");
    }

    ctx.pids[PID_CRM] = start_crm(ctx.cfg.load_stub, vmodem_sysfs_mdm_state, vmodem_sysfs_mdm_ctrl);

    tcs->dispose(tcs);

    ctx.ipc = crm_ipc_init(CRM_IPC_THREAD);

    /* wait for modem readiness */
    if (ctx.ctrl_stub_mdm) {
        crm_ipc_msg_t msg;
        bool wait = true;
        struct pollfd pfd = { .fd = ctx.ctrl_stub_mdm->get_poll_fd(ctx.ctrl_stub_mdm),
                              .events = POLLIN };
        while (wait) {
            poll(&pfd, 1, -1);
            while (ctx.ctrl_stub_mdm->get_msg(ctx.ctrl_stub_mdm, &msg))
                if (MREQ_READY == msg.scalar)
                    wait = false;
        }
    }

    /* let's start the test */
    if (!ctx.cfg.no_client)
        fake_client(&ctx);
    else
        while (true)
            pause();

    cleanup_test_host();
    free(vmodem_sysfs_mdm_state);
    free(vmodem_sysfs_mdm_ctrl);

    return 0;
}
