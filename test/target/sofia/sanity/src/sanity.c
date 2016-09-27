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
#include <getopt.h>

#include "libmdmcli/mdm_cli.h"
#include "utils/property.h"
#include "utils/keys.h"
#include "common.h"

typedef struct cmd_options {
    const char short_opt;
    const char *long_opt;
    const char *description;
    bool has_arg;
} cmd_options_t;

enum {
    SHORT_HELP = 'a',
    SHORT_TYPE,
    SHORT_SCENARIO,
};

#define MENU_HELP "Shows this message"
#define MENU_TYPE "mdmcli or sysfs"
#define MENU_SCENARIO "reset, apimr, dump or random"

typedef enum {
    SCENARIO_NONE,
    SCENARIO_SELF_RESET,
    SCENARIO_MODEM_RESTART,
    SCENARIO_DUMP,
    SCENARIO_RANDOM // Must be last !!!
} sanity_scenario_t;

int wait_event(int fd, int timeout)
{
    if (timeout < 0)
        timeout = 15000;
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int poll_ret = poll(&pfd, 1, timeout);
    ASSERT((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0);
    ASSERT(poll_ret == 1);
    int evt;
    ASSERT(read(fd, &evt, sizeof(evt)) == sizeof(evt));
    return evt;
}

int wait_modem_state_change(int fd, int timeout)
{
    const char *m_state[] = { "UP", "DOWN" };
    int state = wait_event(fd, timeout);

    ASSERT(state <= MODEM_DOWN);
    my_printf("Modem state: %s", m_state[state]);
    return state;
}

void *at_thread(void *ctx)
{
    ASSERT(ctx != NULL);
    int pipe_fd = *((int *)ctx);
    free(ctx);

    int fd = open(AT_PIPE, O_RDWR);
    ASSERT(fd >= 0);

    char *reply = send_at(fd, "ATE0", -1);
    ASSERT(reply != NULL);
    free(reply);

    reply = send_at(fd, "AT+CGSN", -1);
    ASSERT(reply != NULL);
    ASSERT((strstr(reply, "\r\n0044") != NULL) || (strstr(reply, "\r\n0049") != NULL));
    free(reply);

    reply = send_at(fd, "AT+XGENDATA", -1);
    ASSERT(reply != NULL);
    ASSERT(strstr(reply, "SF_LTE") != NULL);
    free(reply);

    reply = send_at(fd, "AT+CFUN=1", -1);
    ASSERT(reply != NULL);
    ASSERT(strstr(reply, "OK") != NULL);
    free(reply);

    reply = send_at(fd, "AT+CFUN?", -1);
    ASSERT(reply != NULL);
    ASSERT(strstr(reply, "+CFUN: 1,0") != NULL);
    ASSERT(strstr(reply, "OK") != NULL);
    free(reply);

    close(fd);

    send_evt(pipe_fd, AT_OK);

    return NULL;
}

void do_at_sanity(int r_fd, int w_fd)
{
    pthread_t thr;
    int *ctx = malloc(sizeof(*ctx));

    ASSERT(ctx != NULL);
    *ctx = w_fd;
    ASSERT(pthread_create(&thr, NULL, at_thread, ctx) == 0);

    int evt = wait_event(r_fd, -1);
    ASSERT(evt == AT_OK);

    pthread_join(thr, NULL);
}

static void do_cleanup()
{
    crm_property_set(CRM_KEY_FAKE_EVENT, "");
}

static void usage(cmd_options_t *opts, size_t size)
{
    for (size_t i = 0; i < size; i++)
        printf("\t--%-20s%s\n", opts[i].long_opt, opts[i].description);
    exit(-1);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    void (*ptr_init)(int pipe_fd) = NULL;
    void (*ptr_restart)(void) = NULL;

    sanity_scenario_t scenario = SCENARIO_NONE;

    if (atexit(do_cleanup) != 0) {
        my_printf("Error: Exit configuration failed. Exit");
        return -1;
    }

    crm_property_set(CRM_KEY_FAKE_EVENT, "modem");

    cmd_options_t opts[] = {
        { SHORT_HELP, "help", MENU_HELP, false },
        { SHORT_TYPE, "type", MENU_TYPE, true },
        { SHORT_SCENARIO, "scenario", MENU_SCENARIO, true },
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
        case SHORT_TYPE:
            if (!strcmp(optarg, "mdmcli")) {
                ptr_init = init_test_CRM;
                ptr_restart = restart_modem_CRM;
            } else if (!strcmp(optarg, "sysfs")) {
                ptr_init = init_test_SYSFS;
                ptr_restart = restart_modem_SYSFS;
            }
            break;
        case SHORT_SCENARIO:
            if (!strcmp(optarg, "reset"))
                scenario = SCENARIO_SELF_RESET;
            else if (!strcmp(optarg, "apimr"))
                scenario = SCENARIO_MODEM_RESTART;
            else if (!strcmp(optarg, "dump"))
                scenario = SCENARIO_DUMP;
            else if (!strcmp(optarg, "random"))
                scenario = SCENARIO_RANDOM;
            break;
        default: usage(opts, nb_opts);
        }
    }
    free(long_opts);

    if ((scenario == SCENARIO_NONE) ||
        (ptr_init == NULL) ||
        (ptr_restart == NULL))
        usage(opts, nb_opts);

    int pipefd[2];
    ASSERT(pipe(pipefd) == 0);
    ptr_init(pipefd[1]);
    int modem_state = wait_modem_state_change(pipefd[0], -1);
    if (modem_state == MODEM_DOWN) {
        my_printf("Initial state is down ... waiting for up !");
        modem_state = wait_modem_state_change(pipefd[0], -1);
        ASSERT(modem_state == MODEM_UP);
    }

    int counter = 1;
    while (1) {
        int mdm_up_timeout = -1;

        /* Do AT command sanity checks */
        do_at_sanity(pipefd[0], pipefd[1]);

        sanity_scenario_t sc = scenario;
        if (sc == SCENARIO_RANDOM)
            sc = rand() % SCENARIO_RANDOM;

        switch (sc) {
        case SCENARIO_SELF_RESET: {
            int fd = open(AT_PIPE, O_RDWR);
            ASSERT(fd >= 0);
            send_at(fd, "AT+CFUN=15", 500);
            close(fd);
        } break;
        case SCENARIO_DUMP: {
            int fd = open(AT_PIPE, O_RDWR);
            ASSERT(fd >= 0);
            send_at(fd, "AT+XLOG=4", 500);
            close(fd);

            /* core dump retrieval takes at least 30s */
            mdm_up_timeout = 45000;
        } break;
        case SCENARIO_MODEM_RESTART:
            ptr_restart();
            break;
        default:
            ASSERT(0);
        }

        modem_state = wait_modem_state_change(pipefd[0], -1);
        ASSERT(modem_state == MODEM_DOWN);
        modem_state = wait_modem_state_change(pipefd[0], mdm_up_timeout);
        ASSERT(modem_state == MODEM_UP);

        my_printf("Number of successful retry: %d", counter++);
    }
}
