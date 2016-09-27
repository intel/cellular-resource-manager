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

#include "libmdmcli/mdm_cli.h"

#include "common.h"

static mdm_cli_hdle_t *hdle = NULL;

static mdm_cli_hdle_t *(*connect)(const char *client_name, int inst_id, int nb_evts,
                                  const mdm_cli_register_t evts[]);
static int (*acquire)(mdm_cli_hdle_t *hdle);
static int (*restart)(mdm_cli_hdle_t *hdle, mdm_cli_restart_cause_t cause,
                      const mdm_cli_dbg_info_t *data);

static int evt_callback(const mdm_cli_callback_data_t *cb_data)
{
    ASSERT(cb_data != NULL);
    ASSERT(cb_data->context != NULL);
    int fd = *((int *)cb_data->context);
    ASSERT(fd >= 0);
    int state = -1;
    if (cb_data->id == MDM_DOWN)
        state = MODEM_DOWN;
    else if (cb_data->id == MDM_UP)
        state = MODEM_UP;
    ASSERT(state != -1);
    send_evt(fd, state);
    return 0;
}

void init_test_CRM(int pipe_fd)
{
    /** @TODO: handle freeing this in mdm_cli_disconnect */
    int *ctx = malloc(sizeof(pipe_fd));

    ASSERT(ctx != NULL);
    *ctx = pipe_fd;

    dlerror(); // Clear previous errors if any
    void *lib = dlopen("libmdmcli.so", RTLD_LAZY);
    ASSERT(lib != NULL);
    ASSERT((connect = dlsym(lib, "mdm_cli_connect")) != NULL);
    ASSERT((acquire = dlsym(lib, "mdm_cli_acquire")) != NULL);
    ASSERT((restart = dlsym(lib, "mdm_cli_restart")) != NULL);
    const char *err = dlerror();
    if (err)
        my_printf("Error: %s", err);
    ASSERT(err == NULL);

    while (true) {
        mdm_cli_register_t evts[] = {
            { .id = MDM_DOWN, evt_callback, ctx },
            { .id = MDM_UP, evt_callback, ctx },
        };
        hdle = connect("Sanity Test", MDM_CLI_DEFAULT_INSTANCE, ARRAY_SIZE(evts), evts);
        if (hdle)
            break;
        else
            sleep(1);
    }
    acquire(hdle);
}

void restart_modem_CRM(void)
{
    restart(hdle, RESTART_MDM_ERR, NULL);
}
