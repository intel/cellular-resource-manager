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

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>

#define CRM_MODULE_TAG "MAIN"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/keys.h"
#include "utils/plugins.h"
#include "utils/property.h"
#include "utils/process_factory.h"
#include "plugins/control.h"

#include "libmdmcli/mdm_cli.h"
#include "libtcs2/tcs.h"

crm_process_factory_ctx_t *g_factory = NULL;

static void usage()
{
    LOGV("CRM Daemon");
    LOGV("Usage: crm [OPTION]...");
    LOGV("\t-h: Shows this message");
    LOGV("\t-v: Print CRM version");
    LOGV("\t-i: <instance number> Sets the intance number. default: %d", MDM_CLI_DEFAULT_INSTANCE);
    exit(-1);
}

static void sig_handler(int sig)
{
    (void)sig;
    if (g_factory)
        g_factory->dispose(g_factory);
    exit(-1);
}

int main(int argc, char *argv[])
{
    int inst_id = MDM_CLI_DEFAULT_INSTANCE;

    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    ASSERT(!sigaction(SIGCHLD, &sa, NULL));
    ASSERT(!sigaction(SIGABRT, &sa, NULL));
    ASSERT(!sigaction(SIGTERM, &sa, NULL));

    int cmd;
    // Start HACK
    {
        char persist_prop[CRM_PROPERTY_VALUE_MAX] = { '\0' };
        crm_property_get("persist.config.specific", persist_prop, "");
        if (persist_prop[0] != '\0')
            crm_property_set("ro.telephony.tcs.sw_folder", persist_prop);
    }

    // End HACK

    while (-1 != (cmd = getopt(argc, argv, "hvi:"))) {
        switch (cmd) {
        case 'h':
            usage();
            break;

        case 'v':
            LOGV("last commit: \"%s\"", GIT_COMMIT_ID);
            break;

        case 'i': {
            errno = 0;
            char *end_ptr = NULL;
            inst_id = strtol(optarg, &end_ptr, 10);
            ASSERT(errno == 0 && end_ptr != optarg);
            break;
        }
        default:
            usage();
        }
    }

    crm_logs_init(inst_id);
    crm_property_init(inst_id);

    LOGD("last commit: \"%s\"", GIT_COMMIT_ID);

    /* Process factory MUST be started at the earliest to reduce its memory footprint and
     * avoid file descriptor duplication, etc. */
    crm_process_factory_ctx_t *factory = crm_process_factory_init(1);
    ASSERT(factory);
    g_factory = factory;

    char name[5];
    snprintf(name, sizeof(name), "crm%d", inst_id);
    tcs_ctx_t *tcs = tcs2_init(name);
    ASSERT(tcs);
    tcs->print(tcs);

    ASSERT(tcs->select_group(tcs, ".main") == 0);

    crm_plugin_t ctrl_plugin;
    crm_plugin_load(tcs, "control", CRM_CTRL_INIT, &ctrl_plugin);

    crm_ctrl_ctx_t *control = ((crm_ctrl_init_t)ctrl_plugin.init)(inst_id, tcs, factory);

    tcs->dispose(tcs);

    control->event_loop(control);

    LOGV("An error happened. Stopping CRM");
    control->dispose(control);
    factory->dispose(factory);
    crm_plugin_unload(&ctrl_plugin);

    return 0;
}
