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
#include <poll.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define CRM_MODULE_TAG "CRMT"
#include "utils/logs.h"
#include "utils/property.h"
#include "utils/string_helpers.h"
#include "utils/keys.h"
#include "utils/ipc.h"

#include "libmdmcli/mdm_cli.h"
#include "libtcs2/tcs.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

#define xstr(s) str(s)
#define str(s) #s

#define ASSERT(exp) do { \
        if (!(exp)) { \
            KLOG("**** TEST FAILURE *** (assertion at %s:%d '" \
                 xstr(exp) "')", __FILE__, __LINE__); \
            abort(); \
        } \
} while (0)

// Private API, only accessible in libcrm_mdmcli. That is why the function is declared as external
extern mdm_cli_hdle_t *mdm_cli_connect_dbg(const char *client_name, int inst_id, int nb_evts,
                                           const mdm_cli_register_t evts[]);

static const char *g_data[] = { "sanity test", "of CRM component",
                                "with an array", "of max", "data" };
static const mdm_cli_dbg_info_t g_dbg_info = { DBG_TYPE_STATS, DBG_DEFAULT_LOG_SIZE,
                                               DBG_DEFAULT_NO_LOG, DBG_DEFAULT_NO_LOG,
                                               ARRAY_SIZE(g_data), g_data };

static void drive_crm(bool start)
{
    pid_t child = fork();

    ASSERT(child != -1);

    if (0 == child) {
        const char *cmd[] = { "stop", "start" };
        const char *args[] = { cmd[start], "crm", NULL };

        execvp(cmd[start], (char **)args);
        ASSERT(0);
    } else {
        while (waitpid(-1, NULL, 0))
            if (errno == ECHILD)
                break;
    }
}

static void restart_crm(bool debug_mode)
{
    // Set property first. Otherwise, MDMCLI will immediately try to reconnect when CRM is stopped
    const char *enable[] = { "false", "true" };

    crm_property_set(CRM_KEY_DBG_SANITY_TEST_MODE, enable[debug_mode]);

    KLOG("Starting CRM in %s mode...", debug_mode ? "debug" : "standard");
    drive_crm(false);

    drive_crm(true);
    /* CRM needs some time to start. This sleep is here to avoid error logs during the
     * next reconnection loop */
    usleep(500 * 1000);
}

static int mdm_evt(const mdm_cli_callback_data_t *ev)
{
    int ret = 0;

    ASSERT(ev != NULL);
    crm_ipc_ctx_t *ipc = (crm_ipc_ctx_t *)ev->context;
    ASSERT(ipc != NULL);

    crm_ipc_msg_t msg;
    switch (ev->id) {
    case MDM_SHUTDOWN:
        ret = -1;
    case MDM_UP:
    case MDM_DOWN:
    case MDM_OOS:
    case MDM_COLD_RESET:
        msg.scalar = ev->id;
        break;
    case MDM_DBG_INFO:
        msg.scalar = ev->id;
        msg.data_size = ev->data_size;
        mdm_cli_dbg_info_t *dbg_info = (mdm_cli_dbg_info_t *)ev->data;

        mdm_cli_dbg_info_t *data = malloc(sizeof(mdm_cli_dbg_info_t));
        ASSERT(data != NULL);

        *data = *dbg_info;

        data->data = malloc(data->nb_data * sizeof(char *));
        ASSERT(data->data != NULL);
        for (size_t i = 0; i < dbg_info->nb_data; i++) {
            data->data[i] = strdup(dbg_info->data[i]);
            ASSERT(data->data[i] != NULL);
        }
        msg.data = data;
        break;
    default: ASSERT(0);
    }

    ASSERT(ipc->send_msg(ipc, &msg));
    return ret;
}

static void wait_evt(crm_ipc_ctx_t *ipc, mdm_cli_event_t evt, int dbg_type)
{
    ASSERT(ipc != NULL);
    struct pollfd pfd = { .fd = ipc->get_poll_fd(ipc), .events = POLLIN };

    KLOG("waiting for modem event: %s", crm_mdmcli_wire_req_to_string(evt));
    int err = poll(&pfd, 1, 60000);           //@TODO set timer
    if (0 == err) {
        KLOG("ERROR: event not received");
        ASSERT(0);
    }

    /* Only one message is popped from the stack to make sure that all received events
     * are the expected ones */
    crm_ipc_msg_t msg;
    ASSERT(ipc->get_msg(ipc, &msg));

    KLOG("event received: %s", crm_mdmcli_wire_req_to_string(msg.scalar));
    ASSERT(msg.scalar == evt);
    if (msg.scalar == MDM_DBG_INFO) {
        if (dbg_type < DBG_TYPE_STATS)
            dbg_type = g_dbg_info.type;

        ASSERT(msg.data_size == sizeof(g_dbg_info));
        mdm_cli_dbg_info_t *dbg_info = (mdm_cli_dbg_info_t *)msg.data;
        ASSERT(dbg_info->ap_logs_size == g_dbg_info.ap_logs_size);
        ASSERT(dbg_info->bp_logs_size == g_dbg_info.bp_logs_size);
        ASSERT(dbg_info->bp_logs_time == g_dbg_info.bp_logs_time);

        /* When debug info is not sent by client, CRM fixes it by sending debug info, debug
         * info type is APIMR. debug data will be NULL. */
        if (dbg_type == DBG_TYPE_APIMR) {
            ASSERT(dbg_info->nb_data == 0);
            ASSERT(dbg_info->data == NULL);
        } else {
            ASSERT(dbg_info->nb_data == g_dbg_info.nb_data);
            ASSERT(dbg_info->type == (mdm_cli_dbg_type_t)dbg_type);
            for (size_t i = 0; i < dbg_info->nb_data; i++) {
                ASSERT(strncmp(dbg_info->data[i], g_dbg_info.data[i],
                               MDM_CLI_MAX_LEN_DATA) == 0);
                free((char *)dbg_info->data[i]);
            }
            free(dbg_info->data);
            free(dbg_info);
        }
    }
}

static void check_cold_reset(mdm_cli_hdle_t *mdm, crm_ipc_ctx_t *ipc, int count)
{
    ASSERT(mdm != NULL);
    ASSERT(ipc != NULL);
    ASSERT(count);

    for (int i = 0; i < count; i++) {
        ASSERT(mdm_cli_restart(mdm, RESTART_MDM_ERR, &g_dbg_info) == 0);
        wait_evt(ipc, MDM_DOWN, -1);
        wait_evt(ipc, MDM_DBG_INFO, DBG_TYPE_STATS);
        wait_evt(ipc, MDM_UP, -1);
    }
}

static void check_reset_counter(mdm_cli_hdle_t *mdm, crm_ipc_ctx_t *ipc, int reboot, int timeout)
{
    ASSERT(mdm != NULL);
    ASSERT(ipc != NULL);

    char value[CRM_PROPERTY_VALUE_MAX];
    snprintf(value, sizeof(value), "%d", reboot + 1);
    crm_property_set(CRM_KEY_REBOOT_COUNTER, value);

    timeout += 10;
    KLOG("Waiting for reset delay of: %d milliseconds", timeout);
    usleep(timeout * 1000);

    ASSERT(mdm_cli_restart(mdm, RESTART_MDM_ERR, &g_dbg_info) == 0);
    wait_evt(ipc, MDM_DOWN, -1);
    wait_evt(ipc, MDM_DBG_INFO, DBG_TYPE_STATS);
    wait_evt(ipc, MDM_UP, -1);

    /* System reboot property is reset to 0, so providing default value as 1. */
    crm_property_get(CRM_KEY_REBOOT_COUNTER, value, "1");
    errno = 0;
    long reboot_counter = strtol(value, NULL, 0);
    ASSERT(errno == 0);
    ASSERT(reboot_counter == 0);
}

static mdm_cli_hdle_t *connect_to_crm(bool debug, int inst_id, size_t nb_evts,
                                      mdm_cli_register_t *evts)
{
    mdm_cli_hdle_t *mdm = NULL;

    for (int i = 0; i < 500 && !mdm; i++) {
        if (debug)
            mdm = mdm_cli_connect_dbg("sanity", inst_id, nb_evts, evts);
        else
            mdm = mdm_cli_connect("sanity", inst_id, nb_evts, evts);

        if (!mdm)
            usleep(5000);
    }
    ASSERT(mdm != NULL);

    return mdm;
}

int main(int argc, char *argv[])
{
    int inst_id = MDM_CLI_DEFAULT_INSTANCE; /* @TODO: handle instance ID */

    (void)argc;
    (void)argv;

    crm_logs_init(inst_id);
    crm_property_init(inst_id);

    crm_ipc_ctx_t *ipc = crm_ipc_init(CRM_IPC_THREAD);
    ASSERT(ipc != NULL);

    mdm_cli_hdle_t *mdm = NULL;
    mdm_cli_register_t evts[] = {
        { MDM_UP, mdm_evt, ipc },
        { MDM_DOWN, mdm_evt, ipc },
        { MDM_OOS, mdm_evt, ipc },
        { MDM_DBG_INFO, mdm_evt, ipc },
        { MDM_COLD_RESET, mdm_evt, ipc }, // Must be the two lasts
        { MDM_SHUTDOWN, mdm_evt, ipc },
    };
    size_t nb_evts = ARRAY_SIZE(evts);

    restart_crm(true);

    /* Basic operations: acquire, release, restart, shutdown and notify debug */
    for (int i = 0; i < 2; i++) {
        bool ack_handling = true;
        if (i == 1) {
            KLOG("test without acknowledge...");
            ack_handling = false;
            nb_evts -= 2; //removal of MDM_COLD_RESET and MDM_SHUTDOWN registration
        } else {
            KLOG("test with acknowledge...");
        }

        for (int j = 0; j < 3; j++) {
            mdm = connect_to_crm(true, inst_id, nb_evts, evts);

            /* No client holding the resource. Modem should be down */

            wait_evt(ipc, MDM_DOWN, -1);

            ASSERT(mdm_cli_acquire(mdm) == 0);
            wait_evt(ipc, MDM_UP, -1);

            /* @TODO: check modem behavior here */

            ASSERT(mdm_cli_release(mdm) == 0);
            wait_evt(ipc, MDM_DOWN, -1);
            if (ack_handling) {
                wait_evt(ipc, MDM_SHUTDOWN, -1);
                ASSERT(mdm_cli_ack_shutdown(mdm) == 0);
            }

            ASSERT(mdm_cli_acquire(mdm) == 0);
            wait_evt(ipc, MDM_UP, -1);

            /* @TODO: check modem behavior here */

            /* debug_info type is provided here. CRM should send same debug info */
            ASSERT(mdm_cli_restart(mdm, RESTART_MDM_ERR, &g_dbg_info) == 0);
            wait_evt(ipc, MDM_DOWN, -1);
            if (ack_handling)
                wait_evt(ipc, MDM_COLD_RESET, -1);
            wait_evt(ipc, MDM_DBG_INFO, DBG_TYPE_STATS);
            wait_evt(ipc, MDM_UP, -1);

            /* @TODO: check modem behavior here */

            ASSERT(mdm_cli_notify_dbg(mdm, &g_dbg_info) == 0);
            wait_evt(ipc, MDM_DBG_INFO, -1);

            ASSERT(mdm_cli_release(mdm) == 0);
            wait_evt(ipc, MDM_DOWN, -1);
            if (ack_handling) {
                wait_evt(ipc, MDM_SHUTDOWN, -1);
                ASSERT(mdm_cli_ack_shutdown(mdm) == 0);
            }

            ASSERT(mdm_cli_disconnect(mdm) == 0);
        }
    }

    nb_evts = ARRAY_SIZE(evts) - 2;

    /* CRM shall turn off the modem if the only client holding the resource disconnects.
     * let's test it... */
    KLOG("testing resource handling during client disconnection...");
    {
        mdm = connect_to_crm(true, inst_id, nb_evts, evts);
        wait_evt(ipc, MDM_DOWN, -1);
        ASSERT(mdm_cli_acquire(mdm) == 0);
        wait_evt(ipc, MDM_UP, -1);
        ASSERT(mdm_cli_disconnect(mdm) == 0);

        mdm = connect_to_crm(true, inst_id, nb_evts, evts);
        wait_evt(ipc, MDM_DOWN, -1);
        ASSERT(mdm_cli_disconnect(mdm) == 0);
    }

    /* let's acquire the resource, restart CRM and check that modem is restarted */
    KLOG("testing resource handling after CRM crash...");
    {
        mdm = connect_to_crm(true, inst_id, nb_evts, evts);
        wait_evt(ipc, MDM_DOWN, -1);
        ASSERT(mdm_cli_acquire(mdm) == 0);
        wait_evt(ipc, MDM_UP, -1);

        restart_crm(true);

        /* one MDM_DOWN is received when CRM is stopped. Second one when CRM is started */
        wait_evt(ipc, MDM_DOWN, -1);
        wait_evt(ipc, MDM_DOWN, -1);
        wait_evt(ipc, MDM_UP, -1);
        ASSERT(mdm_cli_disconnect(mdm) == 0);
    }

    /**
     * @TODO: tests to add (non-exhaustive list)
     * - core dump
     * - modem self-reset
     */

    restart_crm(true);

    /* full escalation recovery */
    {
        char group[10];
        snprintf(group, sizeof(group), "crm%d", inst_id);
        tcs_ctx_t *tcs = tcs2_init(group);
        ASSERT(tcs);

        int cold_reset, reboot, timeout;
        ASSERT(!tcs->select_group(tcs, ".escalation"));
        ASSERT(!tcs->get_int(tcs, "cold_reset", &cold_reset));
        ASSERT(!tcs->get_int(tcs, "reboot", &reboot));
        ASSERT(!tcs->get_int(tcs, "timeout_sanity_mode", &timeout));

        mdm = connect_to_crm(true, inst_id, nb_evts, evts);
        wait_evt(ipc, MDM_DOWN, -1);

        ASSERT(mdm_cli_acquire(mdm) == 0);
        wait_evt(ipc, MDM_UP, -1);

        KLOG("escalation recovery");
        check_cold_reset(mdm, ipc, cold_reset);

        KLOG("escalation check reset counter");
        check_reset_counter(mdm, ipc, reboot, timeout);

        /* cold reset done once in stability timeout, decrement cold reset by 1  */
        check_cold_reset(mdm, ipc, cold_reset - 1);

        /* Set system reboot property to max reboot value to move to OOS state directly */
        KLOG("escalation OOS");
        char value[CRM_PROPERTY_VALUE_MAX];
        snprintf(value, sizeof(value), "%d", reboot + 1);
        crm_property_set(CRM_KEY_REBOOT_COUNTER, value);

        ASSERT(mdm_cli_restart(mdm, RESTART_MDM_ERR, &g_dbg_info) == 0);
        wait_evt(ipc, MDM_DOWN, -1);
        wait_evt(ipc, MDM_DBG_INFO, -1);
        wait_evt(ipc, MDM_OOS, -1);

        /* Reset system reboot property value to 0 */
        crm_property_set(CRM_KEY_REBOOT_COUNTER, "0");

        ASSERT(mdm_cli_disconnect(mdm) == 0);
    }

    /* End of the test. Let's restart CRM and check if everything is OK */
    restart_crm(false);

    {
        mdm = connect_to_crm(false, inst_id, nb_evts, evts);
        ASSERT(mdm_cli_disconnect(mdm) == 0);
    }

    ipc->dispose(ipc, NULL);

    KLOG("*** sanity test succeed ***");
    return 0;
}
