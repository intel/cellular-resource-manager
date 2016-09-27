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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

#define CRM_MODULE_TAG "CUSTOT"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/ipc.h"
#include "utils/property.h"
#include "utils/keys.h"
#include "plugins/control.h"
#include "plugins/mdm_customization.h"
#include "test/test_utils.h"
#include "test/mdm_stub.h"

#define FW_FOLDER "/tmp/crm/"

crm_ipc_ctx_t *g_ipc = NULL;

static void notify_customization_status(crm_ctrl_ctx_t *ctx, int status)
{
    (void)ctx;   // UNUSED

    crm_ipc_msg_t msg = { .scalar = status };
    g_ipc->send_msg(g_ipc, &msg);
}

int main()
{
    /* Fake control context, just for testing */
    crm_ctrl_ctx_t control = {
        .notify_customization_status = notify_customization_status,
    };

    g_ipc = crm_ipc_init(CRM_IPC_THREAD);
    ASSERT(g_ipc != NULL);

    tcs_ctx_t *tcs = CRM_TEST_tcs_init("host_sofia", MDM_CLI_DEFAULT_INSTANCE);
    ASSERT(tcs);

    ASSERT(tcs->select_group(tcs, ".hal") == 0);
    char *debug_socket = tcs->get_string(tcs, "uevent_host_debug_socket");
    ASSERT(debug_socket);

    unlink(debug_socket);
    unlink(MDM_STUB_SOFIA_CTRL);

    char group[15];
    snprintf(group, sizeof(group), "streamline%d", MDM_CLI_DEFAULT_INSTANCE);
    tcs->add_group(tcs, group, true);
    ASSERT(tcs->select_group(tcs, group) == 0);

    errno = 0;
    ASSERT(mkdir(FW_FOLDER, 0777) == 0 || errno == EEXIST);

    /* create fake tcs tlvs */
    int nb_tlvs;
    char **tcs_tlvs = tcs->get_string_array(tcs, "tlvs", &nb_tlvs);

    /* List of TLV's can be empty */
    ASSERT(!((tcs_tlvs == NULL) ^ (nb_tlvs == 0)));

    char **tlvs = malloc(sizeof(char *) * nb_tlvs);
    ASSERT(tlvs != NULL);

    for (int i = 0; i < nb_tlvs; i++) {
        int len = strlen(FW_FOLDER) + strlen(tcs_tlvs[i]) + 1;
        tlvs[i] = malloc(sizeof(char) * len);
        ASSERT(tlvs[i] != NULL);
        snprintf(tlvs[i], len, "%s%s", FW_FOLDER, tcs_tlvs[i]);
        int fd = open(tlvs[i], O_CREAT | O_WRONLY | O_TRUNC, 0666);
        DASSERT(fd >= 0, "Failed to open file (%s)", strerror(errno));
        write(fd, "test", 4);
        ASSERT(close(fd) == 0);
        free(tcs_tlvs[i]);
    }
    free(tcs_tlvs);

    pid_t pid = CRM_TEST_start_stub_sofia_mdm(tcs);

    int ctl_fd = CRM_TEST_connect_socket(MDM_STUB_SOFIA_CTRL);
    int dbg_fd = CRM_TEST_connect_socket(debug_socket);

    free(debug_socket);

    CRM_TEST_wait_stub_sofia_mdm_readiness(ctl_fd);

    crm_property_set(CRM_KEY_DBG_HOST, "true");

    crm_customization_ctx_t *customization = crm_customization_init(tcs, &control);
    ASSERT(customization);
    tcs->dispose(tcs);

    customization->send(customization, (const char **)tlvs, nb_tlvs);

    struct pollfd pfd = { .fd = g_ipc->get_poll_fd(g_ipc), .events = POLLIN };
    poll(&pfd, 1, -1);
    crm_ipc_msg_t msg = { .scalar = -1 };
    g_ipc->get_msg(g_ipc, &msg);
    ASSERT(0 == msg.scalar);

    for (int i = 0; i < nb_tlvs; i++)
        free(tlvs[i]);
    free(tlvs);

    g_ipc->dispose(g_ipc, NULL);
    customization->dispose(customization);

    kill(pid, SIGKILL);
    close(ctl_fd);
    close(dbg_fd);

    LOGD("success");
    return 0;
}
