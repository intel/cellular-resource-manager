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
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <poll.h>
#include <getopt.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <string.h>

#define CRM_MODULE_TAG "MDM"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/time.h"
#include "utils/thread.h"
#include "test/mdm_stub.h"

#define PING_BP "/tmp/crm_ping_bp"
#define SYSFS_MDM_STATE_BP "/tmp/crm_sysfs_mdm_state_bp"
#define SYSFS_MDM_CTRL_BP "/tmp/crm_sysfs_mdm_ctrl_bp"
#define DUMP_BP "/tmp/crm_dump_bp"
#define STREAMLINE_BP "/tmp/crm_streamline_bp"

enum {
    PID_PING,
    PID_SYSFS_MDM_STATE,
    PID_SYSFS_MDM_CTRL,
    PID_DUMP,
    PID_STREAMLINE,
    PID_NUM
};

enum ping_ctrl {
    PING_SUCCESS,
    PING_ERROR,
    PING_ENABLE,
    PING_DISABLE,
    PING_SELF_RESET
};

typedef enum dump_states {
    DUMP_NONE,
    DUMP_AVAILABLE,
    DUMP_INFO
} dump_states_t;

typedef struct sofia_mdm_ctx {
    /* configuration */
    char *uevent_socket;
    char *control_socket;
    char *vmodem_sysfs_mdm_state;
    char *vmodem_sysfs_mdm_ctrl;
    char *dump_node;
    char *streamline_node;
    char *uevent_vmodem;
    char *ping_ap;
    char *start_cmd;
    char *stop_cmd;

    /* variables */
    int cc_fd; // connected control socket
    int cu_fd; // connected uevent socket
    int ms_fd; // sysfs node for modem state
    int mc_fd; // sysfs node for modem control
    bool dead;
    mdm_stub_control_t ctrl;
    dump_states_t dump_state;
    pid_t pids[PID_NUM];

    /* used to manage PING error */
    bool ping_error;
    int count_req_on;
} sofia_mdm_ctx_t;

typedef struct cmd_options {
    const char short_opt;
    const char *long_opt;
    const char *description;
    bool has_arg;
} cmd_options_t;

typedef enum event_type {
    HAL_EVENT,
    CONTROL_EVENT,
} event_type_t;

// Short command line options are not used, that is why the values are not important here
enum {
    SHORT_HELP = 'a',
    SHORT_UEVENT_SOCKET,
    SHORT_CONTROL_SOCKET,
    SHORT_DEBUG_SYSFS_MDM_STATE,
    SHORT_DEBUG_SYSFS_MDM_CTRL,
    SHORT_DUMP_NODE,
    SHORT_STREAMLINE_NODE,
    SHORT_PING_AP,
    SHORT_START_CMD,
    SHORT_STOP_CMD,
    SHORT_UEVENT_CMD,
};

#define MENU_HELP "Shows this message"
#define MENU_UEVENT "<full path of uevent socket>"
#define MENU_CONTROL "<full path of control socket>"
#define MENU_SYSFS_MDM_STATE "<full path of fake sysfs notifying the modem state>"
#define MENU_SYSFS_MDM_CTRL "<full path of fake sysfs notifying the modem control>"
#define MENU_DUMP "<full path of dump node>"
#define MENU_STREAMLINE "<full path of streamline node>"
#define MENU_PING_AP "<full path of ping ap node>"
#define MENU_START "<start modem command>"
#define MENU_STOP "<stop modem command>"
#define MENU_UEVENT_CMD "<uevent message>"

static sofia_mdm_ctx_t *g_ctx = NULL;

static void usage(cmd_options_t *opts, size_t size)
{
    for (size_t i = 0; i < size; i++)
        printf("\t--%-20s%s\n", opts[i].long_opt, opts[i].description);
    exit(-1);
}

static void get_cfg(int argc, char **argv, sofia_mdm_ctx_t *ctx)
{
    ASSERT(argv != NULL);
    ASSERT(ctx != NULL);

    cmd_options_t opts[] = {
        { SHORT_HELP, "help", MENU_HELP, false },
        { SHORT_CONTROL_SOCKET, "control-socket", MENU_CONTROL, true },
        { SHORT_UEVENT_SOCKET, "uevent-socket", MENU_UEVENT, true },
        { SHORT_UEVENT_CMD, "uevent-cmd", MENU_UEVENT_CMD, true },
        { SHORT_DEBUG_SYSFS_MDM_STATE, "sysfs-mdm-state", MENU_SYSFS_MDM_STATE, true },
        { SHORT_DEBUG_SYSFS_MDM_CTRL, "sysfs-mdm-ctrl", MENU_SYSFS_MDM_CTRL, true },
        { SHORT_DUMP_NODE, "dump", MENU_DUMP, true },
        { SHORT_STREAMLINE_NODE, "streamline", MENU_STREAMLINE, true },
        { SHORT_PING_AP, "ping-ap", MENU_PING_AP, true },
        { SHORT_START_CMD, "on", MENU_START, true },
        { SHORT_STOP_CMD, "off", MENU_STOP, true },
    };

    size_t nb_opts = ARRAY_SIZE(opts);
    struct option *long_opts = calloc(sizeof(struct option), (nb_opts + 1));
    char opt_str[30] = { "" };
    int opt_index = 0;

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
        case SHORT_UEVENT_SOCKET:
            ASSERT(ctx->uevent_socket == NULL);
            ctx->uevent_socket = strdup(optarg);
            break;
        case SHORT_UEVENT_CMD:
            ASSERT(ctx->uevent_vmodem == NULL);
            ctx->uevent_vmodem = strdup(optarg);
            break;
        case SHORT_CONTROL_SOCKET:
            ASSERT(ctx->control_socket == NULL);
            ctx->control_socket = strdup(optarg);
            break;
        case SHORT_DEBUG_SYSFS_MDM_STATE:
            ASSERT(ctx->vmodem_sysfs_mdm_state == NULL);
            ASSERT(strcmp(optarg, SYSFS_MDM_STATE_BP));
            ctx->vmodem_sysfs_mdm_state = strdup(optarg);
            break;
        case SHORT_DEBUG_SYSFS_MDM_CTRL:
            ASSERT(ctx->vmodem_sysfs_mdm_ctrl == NULL);
            ASSERT(strcmp(optarg, SYSFS_MDM_CTRL_BP));
            ctx->vmodem_sysfs_mdm_ctrl = strdup(optarg);
            break;
        case SHORT_DUMP_NODE:
            ASSERT(ctx->dump_node == NULL);
            ASSERT(strcmp(optarg, DUMP_BP));
            ctx->dump_node = strdup(optarg);
            break;
        case SHORT_STREAMLINE_NODE:
            ASSERT(ctx->streamline_node == NULL);
            ASSERT(strcmp(optarg, STREAMLINE_BP));
            ctx->streamline_node = strdup(optarg);
            break;
        case SHORT_PING_AP:
            ASSERT(ctx->ping_ap == NULL);
            ASSERT(strcmp(optarg, PING_BP));
            ctx->ping_ap = strdup(optarg);
            break;
        case SHORT_START_CMD:
            ASSERT(ctx->start_cmd == NULL);
            ctx->start_cmd = strdup(optarg);
            break;
        case SHORT_STOP_CMD:
            ASSERT(ctx->stop_cmd == NULL);
            ctx->stop_cmd = strdup(optarg);
            break;
        default: usage(opts, nb_opts);
        }
    }

    free(long_opts);

    if ((ctx->uevent_socket == NULL) || (ctx->control_socket == NULL) ||
        (ctx->vmodem_sysfs_mdm_state == NULL) || (ctx->start_cmd == NULL) ||
        (ctx->stop_cmd == NULL) || (ctx->uevent_vmodem == NULL) ||
        (ctx->ping_ap == NULL) || (ctx->dump_node == NULL) ||
        (ctx->streamline_node == NULL) || (ctx->vmodem_sysfs_mdm_ctrl == NULL)) {
        usage(opts, nb_opts);
        ASSERT(0);
    }

    LOGD("==== Configuration ===");
    LOGD("control socket: %s", ctx->control_socket);
    LOGD("uevent socket: %s", ctx->uevent_socket);
    LOGD("uevent pattern: %s", ctx->uevent_vmodem);
    LOGD("vmodem sysfs modem state: %s", ctx->vmodem_sysfs_mdm_state);
    LOGD("vmodem sysfs modem control: %s", ctx->vmodem_sysfs_mdm_ctrl);
    LOGD("ping node: %s", ctx->ping_ap);
    LOGD("dump node: %s", ctx->dump_node);
    LOGD("streamline node: %s", ctx->streamline_node);
    LOGD("start command: %s", ctx->start_cmd);
    LOGD("stop command: %s", ctx->stop_cmd);
    LOGD("========================");
}

static int create_socket(const char *name)
{
    struct sockaddr_un sa;

    ASSERT(name != NULL);

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", name);

    unlink(name);

    errno = 0;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    DASSERT(fd >= 0, "Failed to open socket (%s)", strerror(errno));

    DASSERT(bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0, "Failed to bind socket (%s)",
            strerror(errno));
    DASSERT(listen(fd, 1) == 0, "Failed to listen for connections on socket (%s)", strerror(errno));

    return fd;
}

static int read_sysfs_request(const sofia_mdm_ctx_t *ctx)
{
    ASSERT(ctx != NULL);
    int ret = -1;

    char cmd[20];

    ssize_t len = read(ctx->mc_fd, cmd, sizeof(cmd));
    cmd[len] = '\0';

    if (0 == strcmp(cmd, ctx->start_cmd))
        ret = MREQ_ON;
    else if (0 == strcmp(cmd, ctx->stop_cmd))
        ret = MREQ_OFF;

    return ret;
}

static void notify_modem_state(sofia_mdm_ctx_t *ctx, mdm_stub_control_t ctrl)
{
    ASSERT(ctx != NULL);

    time_t s_time = time(NULL);

    srand(s_time);

    if ((ctx->ctrl == MCTRL_RUN) && (ctrl == MCTRL_RUN))
        DASSERT(0, "modem UP requested while already UP");

    /* Only notify an event if the state is updated */
    if (ctx->cu_fd >= 0) {
        if (ctx->ctrl == ctrl) {
            LOGD(" modem state not updated. No UEVENT notification");
        } else {
            /* Do not notify a modem Off when previous event was already modem Off (self-reset) */
            if (ctx->ctrl == MCTRL_SELF_RESET && ctrl == MCTRL_OFF)
                return;

            ctx->ctrl = ctrl;

            const char *mdm_state = NULL;
            switch (ctrl) {
            case MCTRL_OFF:
                /* do not report shutting_down to prevent some read failures */
                mdm_state = "off";
                break;
            case MCTRL_RUN:
                /* do not report turning_on to prevent some read failures */
                mdm_state = "on";
                break;
            case MCTRL_FW_FAILURE:
                mdm_state = "verify-fail";
                break;
            case MCTRL_DUMP:
                mdm_state = "trap";
                ASSERT(ctx->dump_state == DUMP_NONE);
                ctx->dump_state = DUMP_AVAILABLE;
                break;
            case MCTRL_SELF_RESET:
                /* do not report shutting_down to prevent some read failures */
                mdm_state = "off";
                break;
            case MCTRL_UNAVAILABLE:
                ctx->dead = true;
                break;
            default: DASSERT(0, "unknown received request: %d", ctrl);
            }

            int len = strlen(mdm_state);
            DASSERT(write(ctx->ms_fd, mdm_state, len) == len, "write failed (%s)",
                    strerror(errno));

            ASSERT(send(ctx->cu_fd, ctx->uevent_vmodem, strlen(ctx->uevent_vmodem),
                        MSG_NOSIGNAL) != 0);
        }
    }
}

static bool handle_event(sofia_mdm_ctx_t *ctx, crm_thread_ctx_t *ping, event_type_t event)
{
    bool ret = true;

    ASSERT(ctx);
    ASSERT(ping);

    switch (event) {
    case CONTROL_EVENT: {
        int request;
        ssize_t len = recv(ctx->cc_fd, &request, sizeof(request), 0);
        ASSERT(len == sizeof(request));

        if (MCTRL_STOP == request) {
            ret = false;
        } else if (MCTRL_NO_PING == request) {
            LOGD("===> ERROR INJECTION: no PING");
            ctx->ping_error = true;
            crm_ipc_msg_t msg = { .scalar = PING_DISABLE };
            ping->send_msg(ping, &msg);
        } else if (MCTRL_PING_SELF_RESET == request) {
            crm_ipc_msg_t msg = { .scalar = PING_SELF_RESET };
            ping->send_msg(ping, &msg);
        } else {
            notify_modem_state(ctx, request);
        }
    }
    break;

    case HAL_EVENT: {
        int id = read_sysfs_request(ctx);
        ASSERT(ctx->cc_fd >= 0);
        switch (id) {
        case MREQ_OFF: {
            crm_ipc_msg_t msg = { .scalar = PING_DISABLE };
            ping->send_msg(ping, &msg);
        }
        break;
        case MREQ_ON: {
            bool send_enable = true;
            if (ctx->ping_error) {
                /* re-enable ping answer once HAL has requested a modem reboot */
                if ((ctx->count_req_on++ % 2))
                    ctx->ping_error = false;
                else
                    send_enable = false;
            }

            if (send_enable) {
                crm_ipc_msg_t msg = { .scalar = PING_ENABLE };
                ping->send_msg(ping, &msg);
            }
        }
        break;
        default:
            break;
        }

        if (id > 0)
            /* Ask to the controller the kind of event to generate */
            ASSERT(send(ctx->cc_fd, &id, sizeof(id), MSG_NOSIGNAL) != 0);
    }
    break;
    default: ASSERT(0);
    }

    return ret;
}

static void *ping_answer(crm_thread_ctx_t *thread_ctx, void *arg)
{
    time_t s_time = time(NULL);

    srand(s_time);

    int p_fd = *((int *)arg);

    ASSERT(thread_ctx != NULL);
    ASSERT(p_fd >= 0);

    struct pollfd pfd = { .fd = thread_ctx->get_poll_fd(thread_ctx), .events = POLLIN };

    struct timespec timer_end;
    const char *answer[2] = { NULL, NULL };

    bool self_reset = false;
    bool enabled = false;
    bool armed = false;
    while (true) {
        int err = poll(&pfd, 1, armed && enabled ? crm_time_get_remain_ms(&timer_end) : -1);
        if (0 == err) {
            armed = false;
            enabled = false;
            if (self_reset) {
                self_reset = false;
                LOGD("===> ERROR INJECTION: modem down during PING");
                crm_ipc_msg_t msg = { .scalar = MCTRL_OFF };
                thread_ctx->send_msg(thread_ctx, &msg);
            } else {
                ASSERT(answer[0] != NULL);
                write(p_fd, answer[0], strlen(answer[0]));
                if (answer[1]) {
                    usleep(50000);
                    write(p_fd, answer[1], strlen(answer[1]));
                }
                answer[0] = NULL;
                answer[1] = NULL;
            }
        } else if (pfd.revents & POLLIN) {
            crm_ipc_msg_t msg;
            if (!thread_ctx->get_msg(thread_ctx, &msg))
                break;

            if (MCTRL_STOP == msg.scalar) {
                break;
            } else if (PING_ENABLE == msg.scalar) {
                //ASSERT(enabled == false);
                enabled = true;
            } else if (PING_DISABLE == msg.scalar) {
                enabled = false;
            } else if (PING_SELF_RESET == msg.scalar) {
                self_reset = true;
            } else if (enabled) {
                if (!armed) {
                    armed = true;
                    crm_time_add_ms(&timer_end, rand() % 3000);
                }

                if (PING_SUCCESS == msg.scalar) {
                    answer[0] = "RETRY\r\nO";
                    answer[1] = "K\r\n";
                } else if (PING_ERROR == msg.scalar) {
                    answer[0] = "ERROR\r\n";
                } else {
                    ASSERT(0);
                }
            }
        } else if (pfd.revents & POLLNVAL) {
            break;
        } else {
            DASSERT(0, "pfd event: %x", pfd.revents);
        }
    }

    return NULL;
}

static void write_random_data(int fd, int max_size)
{
    ASSERT(fd >= 0);

    time_t s_time = time(NULL);
    srand(s_time);
    int loop = (rand() % max_size) / 1024;

    int wrote = 0;
    for (int i = 0; i < loop; i++) {
        char tmp[1204];
        size_t size = rand() % sizeof(tmp);
        for (size_t i = 0; i < size; i++)
            tmp[i] = rand();
        wrote += write(fd, tmp, size);
        usleep(rand() % 50);
    }
    LOGD("wrote: %d", wrote);
}

static void free_cfg(sofia_mdm_ctx_t *ctx)
{
    ASSERT(ctx != NULL);
    free(ctx->uevent_socket);
    free(ctx->control_socket);
    free(ctx->vmodem_sysfs_mdm_state);
    free(ctx->vmodem_sysfs_mdm_ctrl);
    free(ctx->uevent_vmodem);
    free(ctx->dump_node);
    free(ctx->streamline_node);
    free(ctx->ping_ap);
    free(ctx->start_cmd);
    free(ctx->stop_cmd);
}

static void cleanup_mdm()
{
    if (g_ctx) {
        // @TODO: maybe add a mutex here
        sofia_mdm_ctx_t *ctx = g_ctx;
        g_ctx = NULL;

        for (size_t i = 0; i < PID_NUM; i++) {
            if (ctx->pids[i] != -1) {
                LOGD("killing PID %zu %d...", i, ctx->pids[i]);
                kill(ctx->pids[i], SIGKILL);
            }
        }

        LOGD("Waiting children...");
        for (int i = 0; i < PID_NUM; i++) {
            errno = 0;
            DASSERT(waitpid(ctx->pids[i], NULL, 0) == ctx->pids[i], "Failed to kill process %d. %s",
                    ctx->pids[i], strerror(errno));
        }

        unlink(ctx->ping_ap);
        unlink(ctx->dump_node);
        unlink(ctx->vmodem_sysfs_mdm_state);
        unlink(ctx->vmodem_sysfs_mdm_ctrl);
        unlink(PING_BP);
        unlink(DUMP_BP);
        unlink(SYSFS_MDM_STATE_BP);
        unlink(SYSFS_MDM_CTRL_BP);

        free_cfg(ctx);

        LOGV("*** modem stopped ***");
    }
}

static void start_node_daemon(sofia_mdm_ctx_t *ctx, const char *in, const char *out)
{
    ASSERT(ctx != NULL);
    ASSERT(in != NULL);
    ASSERT(out != NULL);

    static const char *const socat_cfg = "pty,raw,echo=0,link=";
    static const char *app_name = "socat";
    char ap[1024];
    char bp[1024];

    snprintf(ap, sizeof(ap), "%s%s", socat_cfg, in);
    snprintf(bp, sizeof(bp), "%s%s", socat_cfg, out);

    unlink(in);
    unlink(out);

    const char *args[] = { app_name, ap, bp, NULL };

    free_cfg(ctx);
    g_ctx = NULL;

    int err = execvp(app_name, (char **)args);
    if (err)
        LOGE("socat is not installed in your host. Please, install it");
    exit(0);
}

static pid_t start_ping_daemon(sofia_mdm_ctx_t *ctx)
{
    ASSERT(ctx != NULL);

    pid_t child = fork();
    ASSERT(child != -1);

    if (0 == child) {
        start_node_daemon(ctx, ctx->ping_ap, PING_BP);
        ASSERT(0);
    }

    return child;
}

static pid_t start_sysfs_mdm_state_daemon(sofia_mdm_ctx_t *ctx)
{
    ASSERT(ctx != NULL);

    pid_t child = fork();
    ASSERT(child != -1);

    if (0 == child) {
        start_node_daemon(ctx, ctx->vmodem_sysfs_mdm_state, SYSFS_MDM_STATE_BP);
        ASSERT(0);
    }

    return child;
}

static pid_t start_sysfs_mdm_ctrl_daemon(sofia_mdm_ctx_t *ctx)
{
    ASSERT(ctx != NULL);

    pid_t child = fork();
    ASSERT(child != -1);

    if (0 == child) {
        start_node_daemon(ctx, ctx->vmodem_sysfs_mdm_ctrl, SYSFS_MDM_CTRL_BP);
        ASSERT(0);
    }

    return child;
}

static pid_t start_dump_daemon(sofia_mdm_ctx_t *ctx)
{
    ASSERT(ctx != NULL);

    pid_t child = fork();
    ASSERT(child != -1);

    if (0 == child) {
        start_node_daemon(ctx, ctx->dump_node, DUMP_BP);
        ASSERT(0);
    }

    return child;
}

static pid_t start_streamline_daemon(sofia_mdm_ctx_t *ctx)
{
    ASSERT(ctx != NULL);

    pid_t child = fork();
    ASSERT(child != -1);

    if (0 == child) {
        start_node_daemon(ctx, ctx->streamline_node, STREAMLINE_BP);
        ASSERT(0);
    }

    return child;
}

static void loop_event(sofia_mdm_ctx_t *ctx)
{
    int u_fd = create_socket(ctx->uevent_socket);

    ASSERT(u_fd >= 0);

    int c_fd = create_socket(ctx->control_socket);
    ASSERT(c_fd >= 0);

    // @TODO: better sync between socat and this process
    ctx->ms_fd = -1;
    while ((ctx->ms_fd = open(SYSFS_MDM_STATE_BP, O_RDWR)) < 0)
        usleep(500);

    ctx->mc_fd = -1;
    while ((ctx->mc_fd = open(SYSFS_MDM_CTRL_BP, O_RDWR)) < 0)
        usleep(500);

    int p_fd = -1;
    while ((p_fd = open(PING_BP, O_RDWR)) < 0)
        usleep(500);

    int d_fd = -1;
    while ((d_fd = open(DUMP_BP, O_RDWR)) < 0)
        usleep(500);

    int s_fd = -1;
    while ((s_fd = open(STREAMLINE_BP, O_RDWR)) < 0)
        usleep(500);

    crm_thread_ctx_t *ping = crm_thread_init(ping_answer, &p_fd, true, false);
    int pt_fd = ping->get_poll_fd(ping);
    ASSERT(pt_fd >= 0);

    ctx->cc_fd = -1;
    ctx->cu_fd = -1;
    ctx->dead = false;

    LOGV("*** modem ready ***");

    bool running = true;
    bool ready = false;
    while (running) {
        struct pollfd pfd[] = {
            { .fd = ctx->mc_fd, .events = POLLIN }, // fake sysfs for modem control
            { .fd = c_fd, .events = POLLIN },       // control socket
            { .fd = u_fd, .events = POLLIN },       // uevent socket
            { .fd = ctx->cc_fd, .events = POLLIN }, // connected control socket
            { .fd = p_fd, .events = POLLIN },       // ping node
            { .fd = pt_fd, .events = POLLIN },      // ping thread
            { .fd = d_fd, .events = POLLIN },       // dump node
            { .fd = s_fd, .events = POLLIN },       // streamline node
        };

        if (!ready) {
            if ((ctx->cu_fd != -1) && (ctx->cc_fd != -1)) {
                ready = true;
                int id = MREQ_READY;
                ASSERT(send(ctx->cc_fd, &id, sizeof(id), MSG_NOSIGNAL) != 0);
            } else {
                pfd[0].events = 0;
            }
        }

        if (poll(pfd, ARRAY_SIZE(pfd), (d_fd < 0) ? 1 : -1) == 0) {
            ASSERT(d_fd < 0);
            d_fd = open(DUMP_BP, O_RDWR);
            if (d_fd > 0)
                LOGD("dump daemon ready");
            continue;
        }

        if (pfd[0].revents & POLLIN) {        // sysfs event: request from HAL
            running = handle_event(ctx, ping, HAL_EVENT);
        } else if (pfd[1].revents & POLLIN) { // control socket event
            ASSERT(pfd[1].fd == c_fd);
            ASSERT(ctx->cc_fd == -1);         // multiple connections are not supported

            ctx->cc_fd = accept(c_fd, NULL, 0);
            ASSERT(ctx->cc_fd >= 0);
            LOGD("connected to control");
        } else if (pfd[2].revents & POLLIN) { // uevent socket event
            ASSERT(pfd[2].fd == u_fd);
            ASSERT(ctx->cu_fd == -1);         // multiple connections are not supported

            ctx->cu_fd = accept(u_fd, NULL, 0);
            ASSERT(ctx->cu_fd >= 0);
            LOGD("connected to uevent");
        } else if (pfd[3].revents & POLLIN) { // connected control socket
            running = handle_event(ctx, ping, CONTROL_EVENT);
        } else if (pfd[4].revents & POLLIN) { // ping node
            char tmp[1024];
            const char *ping_cmd = "ATE0";
            read(p_fd, tmp, sizeof(tmp));

            crm_ipc_msg_t msg;
            msg.scalar = strncmp(tmp, ping_cmd, strlen(ping_cmd)) == 0 ? PING_SUCCESS : PING_ERROR;
            ping->send_msg(ping, &msg);
        } else if (pfd[5].revents & POLLIN) { // ping thread
            crm_ipc_msg_t msg;
            ping->get_msg(ping, &msg);
            notify_modem_state(ctx, msg.scalar);
        } else if (pfd[6].revents & POLLIN) { // dump node
            char tmp[1024];
            const char *info_cmd = "get_coredump_info";
            const char *dump_cmd = "get_coredump";

            ssize_t len = read(d_fd, tmp, sizeof(tmp));

            if (len < 0)
                len = 0;

            ASSERT((size_t)len < sizeof(tmp));
            tmp[len] = '\0';

            if (strcmp(tmp, info_cmd) == 0) {
                ASSERT(ctx->dump_state == DUMP_AVAILABLE);
                write_random_data(d_fd, 2 * 1024 * 1024);
                ctx->dump_state = DUMP_INFO;
            } else if (strcmp(tmp, dump_cmd) == 0) {
                ASSERT(ctx->dump_state == DUMP_INFO);
                write_random_data(d_fd, 32 * 1024 * 1024);
                ctx->dump_state = DUMP_NONE;
            } else if (!strcmp(tmp, "set_coredump_config silent_reset=1")) {
                LOGD("silent reset enabled");
                // @TODO: handle this configuration case
            } else {
                ASSERT(0);
            }

            close(d_fd);
            d_fd = -1;
            pfd[6].revents = 0;

            LOGD("restarting dump daemon");
            kill(ctx->pids[PID_DUMP], SIGKILL);
            waitpid(ctx->pids[PID_DUMP], NULL, 0);

            ctx->pids[PID_DUMP] = start_dump_daemon(ctx);
        } else if (pfd[7].revents & POLLIN) { // streamline node
            char tmp[1124];                   //look at fw_upload.c

            ssize_t len = read(s_fd, tmp, sizeof(tmp));

            if (len < 0)
                len = 0;

            ASSERT((size_t)len < sizeof(tmp));
            tmp[len] = '\0';

            /* @TODO: simulate streamline errors here */
            /* @TODO: current implementation can be easily enhanced. We could check here:
             * - index script value
             * - commands received (config_script vs run_configuration)
             */
            if (strstr(tmp, "\r\n"))
                write(s_fd, "OK\r\n", 4);
        }

        if (!running)
            break;

        for (size_t i = 0; i < ARRAY_SIZE(pfd); i++) {
            if (pfd[i].revents & POLLHUP) {
                if (i != 0) {
                    LOGD("disconnection");
                    running = false;
                    break;
                } else {
                    DASSERT(0, "error in disconnection");
                }
            }
            if (pfd[i].revents & (POLLERR | POLLNVAL))
                DASSERT(0, "error on fd: %zu", i);
        }
    }

    crm_ipc_msg_t msg = { .scalar = MCTRL_STOP };
    ping->send_msg(ping, &msg);

    ping->dispose(ping, NULL);

    close(u_fd);
    close(c_fd);
    close(p_fd);
    close(d_fd);
    close(s_fd);
    close(ctx->cc_fd);
    close(ctx->cu_fd);
    close(ctx->ms_fd);
    close(ctx->mc_fd);
}

static void sig_handler(int sig)
{
    LOGD("signal %d caught", sig);
    cleanup_mdm();
    exit(-1);
}

static void set_signal_handler()
{
    struct sigaction sigact;

    memset(&sigact, 0, sizeof(struct sigaction));
    ASSERT(sigemptyset(&sigact.sa_mask) == 0);
    sigact.sa_flags = 0;
    sigact.sa_handler = sig_handler;

    ASSERT(sigaction(SIGTERM, &sigact, NULL) == 0);
    ASSERT(sigaction(SIGABRT, &sigact, NULL) == 0);
    ASSERT(sigaction(SIGUSR1, &sigact, NULL) == 0);
    ASSERT(sigaction(SIGHUP, &sigact, NULL) == 0);
    ASSERT(sigaction(SIGINT, &sigact, NULL) == 0);
}

int main(int argc, char **argv)
{
    sofia_mdm_ctx_t ctx;

    g_ctx = &ctx;

    memset(&ctx, 0, sizeof(ctx));
    memset(ctx.pids, -1, sizeof(ctx.pids));
    get_cfg(argc, argv, &ctx);
    ctx.ctrl = MCTRL_OFF;

    set_signal_handler();

    ctx.pids[PID_PING] = start_ping_daemon(&ctx);
    ctx.pids[PID_SYSFS_MDM_STATE] = start_sysfs_mdm_state_daemon(&ctx);
    ctx.pids[PID_SYSFS_MDM_CTRL] = start_sysfs_mdm_ctrl_daemon(&ctx);
    ctx.pids[PID_DUMP] = start_dump_daemon(&ctx);
    ctx.pids[PID_STREAMLINE] = start_streamline_daemon(&ctx);

    loop_event(&ctx);

    cleanup_mdm();
    return 0;
}
