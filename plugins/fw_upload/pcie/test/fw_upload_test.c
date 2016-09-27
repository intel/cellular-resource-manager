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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define CRM_MODULE_TAG "FWUPT"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/ipc.h"
#include "utils/process_factory.h"
#include "plugins/control.h"
#include "plugins/fw_upload.h"
#include "test/test_utils.h"

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

    g_ipc = crm_ipc_init(CRM_IPC_THREAD);
    ASSERT(g_ipc != NULL);

    tcs_ctx_t *tcs = CRM_TEST_tcs_init("host_bxt", MDM_CLI_DEFAULT_INSTANCE);
    ASSERT(tcs);

    int timeout;
    ASSERT(tcs->select_group(tcs, ".firmware_upload") == 0);
    ASSERT(!tcs->get_int(tcs, "timeout", &timeout));

    crm_process_factory_ctx_t *factory = crm_process_factory_init(2);
    ASSERT(factory);

    crm_fw_upload_ctx_t *fw_upload = crm_fw_upload_init(MDM_CLI_DEFAULT_INSTANCE, true, tcs,
                                                        &control, factory);
    ASSERT(fw_upload != NULL);

    int i_fd = g_ipc->get_poll_fd(g_ipc);
    struct pollfd pfd = { .fd = i_fd, .events = POLLIN };

    const char *fw = "/tmp/fw.fls";
    int fd = open(fw, O_WRONLY | O_CREAT, 0666);
    ASSERT(fd >= 0);
    close(fd);

    bool error = false;
    for (int i = 0; i < 20; i++) {
        fw_upload->package(fw_upload, fw);
        /* double packaging to check that the plugin supports this error UC */
        fw_upload->package(fw_upload, fw);

        int err = poll(&pfd, 1, 500);
        if (err == 0) {
            LOGD("poll timeout");
            error = true;
            break;
        }
        crm_ipc_msg_t msg = { .scalar = -1 };
        g_ipc->get_msg(g_ipc, &msg);

        ASSERT(0 == msg.scalar);

        fw_upload->flash(fw_upload, "fake_link");

        err = poll(&pfd, 1, timeout + 300);
        if (err == 0) {
            LOGD("poll timeout");
            error = true;
            break;
        }
        msg.scalar = -1;
        g_ipc->get_msg(g_ipc, &msg);
        ASSERT(0 == msg.scalar);
    }

    tcs->dispose(tcs);
    fw_upload->dispose(fw_upload);
    g_ipc->dispose(g_ipc, NULL);

    LOGD("%s", error == false ? "success" : "failure");

    return 0;
}
