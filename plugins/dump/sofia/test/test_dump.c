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
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define CRM_MODULE_TAG "DUMPT"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/at.h"
#include "utils/ipc.h"
#include "plugins/control.h"
#include "plugins/dump.h"
#include "test/test_utils.h"
#include "test/mdm_stub.h"

#include "libmdmcli/mdm_cli.h"

crm_ipc_ctx_t *g_ipc = NULL;

static void dump_status(crm_ctrl_ctx_t *ctx, int status)
{
    (void)ctx;

    ASSERT(g_ipc != NULL);
    crm_ipc_msg_t msg = { .scalar = status };
    g_ipc->send_msg(g_ipc, &msg);
}

static void notify_client(crm_ctrl_ctx_t *ctx, mdm_cli_event_t evt_id, size_t data_size,
                          const void *data)
{
    ASSERT(ctx != NULL);
    ASSERT(g_ipc != NULL);

    if (MDM_UP == evt_id) {
        crm_ipc_msg_t msg = { .scalar = MDM_UP };
        g_ipc->send_msg(g_ipc, &msg);
    } else if (MDM_DBG_INFO == evt_id) {
        ASSERT(data_size == sizeof(mdm_cli_dbg_info_t));
        mdm_cli_dbg_info_t *dbg_info = (mdm_cli_dbg_info_t *)data;
        ASSERT(dbg_info != NULL);
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
}

int main()
{
    int c_fd = -1;
    int u_fd = -1;
    pid_t pid = -1;

    /* Fake control context, just for testing */
    crm_ctrl_ctx_t control = {
        .notify_dump_status = dump_status,
        .notify_client = notify_client,
    };

    g_ipc = crm_ipc_init(CRM_IPC_THREAD);

    tcs_ctx_t *tcs = CRM_TEST_tcs_init("host_sofia", MDM_CLI_DEFAULT_INSTANCE);
    ASSERT(tcs);

    ASSERT(tcs->select_group(tcs, ".hal") == 0);
    char *debug_socket = tcs->get_string(tcs, "uevent_host_debug_socket");
    ASSERT(debug_socket);

    char *dump_node = tcs->get_string(tcs, "dump_node");
    ASSERT(dump_node);

    unlink(debug_socket);
    unlink(MDM_STUB_SOFIA_CTRL);

    pid = CRM_TEST_start_stub_sofia_mdm(tcs);

    /* both sockets shall be opened, otherwise stub modem will never notify its readiness */
    c_fd = CRM_TEST_connect_socket(MDM_STUB_SOFIA_CTRL);
    u_fd = CRM_TEST_connect_socket(debug_socket);
    free(debug_socket);

    CRM_TEST_wait_stub_sofia_mdm_readiness(c_fd);

    crm_dump_ctx_t *dump = crm_dump_init(NULL, &control, NULL, true);
    ASSERT(dump != NULL);

    int id = MCTRL_DUMP;
    ASSERT(send(c_fd, &id, sizeof(id), MSG_NOSIGNAL) != 0);

    dump->read(dump, dump_node, NULL);

    int i_fd = g_ipc->get_poll_fd(g_ipc);
    struct pollfd pfd = { .fd = i_fd, .events = POLLIN };
    poll(&pfd, 1, -1);

    crm_ipc_msg_t msg;
    g_ipc->get_msg(g_ipc, &msg);
    ASSERT(0 == msg.scalar);

    id = MCTRL_STOP;
    ASSERT(send(c_fd, &id, sizeof(id), MSG_NOSIGNAL) != 0);
    waitpid(pid, NULL, 0);

    close(c_fd);
    close(u_fd);

    tcs->dispose(tcs);
    free(dump_node);
    dump->dispose(dump);
    g_ipc->dispose(g_ipc, NULL);

    KLOG("success");

    return 0;
}
