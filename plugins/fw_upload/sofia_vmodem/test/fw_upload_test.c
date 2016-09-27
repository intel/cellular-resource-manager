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
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#define CRM_MODULE_TAG "FWUPT"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/ipc.h"
#include "plugins/control.h"
#include "plugins/fw_upload.h"
#include "test/test_utils.h"

#include "libmdmcli/mdm_cli.h"

#define FW_PATH "/tmp/fake_fw.fls"
#define DEVICE_PATH "/tmp/modem_flash"

crm_ipc_ctx_t *g_ipc = NULL;

static void notify_fw_upload_status(crm_ctrl_ctx_t *ctx, int status)
{
    (void)ctx;   // UNUSED

    ASSERT(g_ipc != NULL);
    crm_ipc_msg_t msg = { .scalar = status };
    g_ipc->send_msg(g_ipc, &msg);
}

int main()
{
    /* Fake control context, just for testing */
    crm_ctrl_ctx_t control = {
        .notify_fw_upload_status = notify_fw_upload_status,
    };

    tcs_ctx_t *tcs = CRM_TEST_tcs_init("host_sofia", MDM_CLI_DEFAULT_INSTANCE);

    ASSERT(tcs);

    crm_fw_upload_ctx_t *fw = crm_fw_upload_init(MDM_CLI_DEFAULT_INSTANCE, true, tcs, &control,
                                                 NULL);
    ASSERT(fw);

    tcs->dispose(tcs);

    g_ipc = crm_ipc_init(CRM_IPC_THREAD);
    int i_fd = g_ipc->get_poll_fd(g_ipc);
    struct pollfd pfd = { .fd = i_fd, .events = POLLIN };

    fw->package(fw, FW_PATH);

    poll(&pfd, 1, 500);
    crm_ipc_msg_t msg = { .scalar = -1 };
    g_ipc->get_msg(g_ipc, &msg);
    ASSERT(0 == msg.scalar);

    /* create fake fw */
    int fw_fd = open(FW_PATH, O_WRONLY | O_CREAT, 0666);
    ASSERT(fw_fd >= 0);
    write(fw_fd, "1234", 4);
    close(fw_fd);

    /* create empty file */
    int dev_fd = open(DEVICE_PATH, O_WRONLY | O_CREAT, 0666);
    ASSERT(dev_fd >= 0);
    close(dev_fd);

    fw->flash(fw, DEVICE_PATH);

    poll(&pfd, 1, 500);
    msg.scalar = -1;
    g_ipc->get_msg(g_ipc, &msg);
    ASSERT(0 == msg.scalar);

    fw->dispose(fw);
    g_ipc->dispose(g_ipc, NULL);

    LOGD("success");
    return 0;
}
