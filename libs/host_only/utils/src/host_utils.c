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
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <stdio.h>

#define CRM_MODULE_TAG "TEST"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/property.h"
#include "test/test_utils.h"
#include "test/mdm_stub.h"
#include "libtcs2/tcs.h"

/**
 * @see test_utils.h
 */
pid_t CRM_TEST_start_stub_sofia_mdm(tcs_ctx_t *tcs)
{
    const char *app_name = "crm_test_stub_modem_sofia";

    ASSERT(tcs != NULL);

    pid_t child = fork();
    ASSERT(child != -1);

    if (0 == child) {
        ASSERT(tcs->select_group(tcs, ".hal") == 0);
        char *vmodem_sysfs_mdm_state = tcs->get_string(tcs, "vmodem_sysfs_mdm_state");
        char *vmodem_sysfs_mdm_ctrl = tcs->get_string(tcs, "vmodem_sysfs_mdm_ctrl");
        char *uevent_vmodem = tcs->get_string(tcs, "uevent_vmodem_filter");
        char *ping_node = tcs->get_string(tcs, "ping_node");
        char *dump_node = tcs->get_string(tcs, "dump_node");
        char *flash_path = tcs->get_string(tcs, "flash_node");
        char *uevent_debug_socket = tcs->get_string(tcs, "uevent_host_debug_socket");
        ASSERT(vmodem_sysfs_mdm_state != NULL);
        ASSERT(vmodem_sysfs_mdm_ctrl != NULL);
        ASSERT(uevent_vmodem != NULL);
        ASSERT(ping_node != NULL);
        ASSERT(dump_node != NULL);
        ASSERT(flash_path != NULL);
        ASSERT(uevent_debug_socket != NULL);

        ASSERT(tcs->select_group(tcs, ".customization") == 0);
        char *streamline_node = tcs->get_string(tcs, "node");
        ASSERT(streamline_node);

        const char *args[] = {
            app_name,
            "--control-socket", MDM_STUB_SOFIA_CTRL,
            "--uevent-socket", uevent_debug_socket,
            "--uevent-cmd", uevent_vmodem,
            "--sysfs-mdm-state", vmodem_sysfs_mdm_state,
            "--sysfs-mdm-ctrl", vmodem_sysfs_mdm_ctrl,
            "--ping-ap", ping_node,
            "--dump", dump_node,
            "--streamline", streamline_node,
            "--on", "1",
            "--off", "0",
            NULL
        };

        errno = 0;
        int err = execvp(app_name, (char **)args);
        if (err)
            LOGE("Failed to start modem: %s", strerror(errno));

        exit(0);
    }

    return child;
}

/**
 * @see test_utils.h
 */
void CRM_TEST_wait_stub_sofia_mdm_readiness(int c_fd)
{
    ASSERT(c_fd >= 0);

    struct pollfd pfd = { .fd = c_fd, .events = POLLIN };
    poll(&pfd, 1, -1);

    int id;
    errno = 0;
    ssize_t len = recv(c_fd, &id, sizeof(id), 0);
    DASSERT(len == sizeof(id), "Failed to read data %s", strerror(errno));
    ASSERT(MREQ_READY == id);
}

/**
 * @see test_utils.h
 */
int CRM_TEST_connect_socket(const char *name)
{
    ASSERT(name != NULL);

    errno = 0;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    DASSERT(fd >= 0, "Failed to open socket %s", strerror(errno));

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", name);

    while (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
        usleep(500);

    return fd;
}

/**
 * @see test_utils.h
 */
void CRM_TEST_get_control_socket_android(const char *name)
{
    struct sockaddr_un server = { .sun_family = AF_UNIX };
    char socket_env[128];
    char socket_val[128];

    errno = 0;
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    DASSERT(sock_fd >= 0, "Failed to open socket %s", strerror(errno));

    snprintf(server.sun_path, sizeof(server.sun_path), "/tmp/%s", name);
    unlink(server.sun_path);
    DASSERT(bind(sock_fd, (struct sockaddr *)&server, sizeof(struct sockaddr_un)) == 0,
            "Failed to bind socket %s", strerror(errno));

    snprintf(socket_env, sizeof(socket_env), "ANDROID_SOCKET_%s", name);
    snprintf(socket_val, sizeof(socket_val), "%d", sock_fd);
    DASSERT(setenv(socket_env, socket_val, 1) == 0, "Failed to set environment variable (%s)",
            strerror(errno));
}

/**
 * @see test_utils.h
 */
tcs_ctx_t *CRM_TEST_tcs_init(const char *name, int inst_id)
{
    ASSERT(name);

    char *root = getenv("ANDROID_BUILD_TOP");
    DASSERT(root, "set Android environment first");
    char path[CRM_PROPERTY_VALUE_MAX];
    int size = snprintf(path, sizeof(path), "%s/out/debug/host/linux-x86/telephony/tcs", root);
    DASSERT(size < (int)sizeof(path), "path too long");

    crm_property_set("tcs.dbg.host.hw_folder", path);
    crm_property_set("ro.telephony.tcs.hw_name", name);

    char group[10];
    snprintf(group, sizeof(group), "crm%d", inst_id);
    tcs_ctx_t *tcs = tcs2_init(group);
    ASSERT(tcs);
    tcs->print(tcs);

    return tcs;
}

int socket_local_client(const char *name, int namespaceId, int type)
{
    (void)namespaceId;
    (void)type;

    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0)
        return -1;
    struct sockaddr_un server = { .sun_family = AF_UNIX };
    snprintf(server.sun_path, sizeof(server.sun_path), "/tmp/%s", name);
    if (connect(sock_fd, (struct sockaddr *)&server, sizeof(struct sockaddr_un)) < 0) {
        close(sock_fd);
        return -1;
    }

    return sock_fd;
}
