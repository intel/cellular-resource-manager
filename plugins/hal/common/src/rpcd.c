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

#include <sys/stat.h>
#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <errno.h>

#define CRM_MODULE_TAG "HAL"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/keys.h"
#include "utils/property.h"

#include "common.h"
#include "rpcd.h"

#ifdef HOST_BUILD
#define CRM_HAL_RPCD_STATE_FOLDER "/tmp/crm"
#else
#define CRM_HAL_RPCD_STATE_FOLDER "/data/telephony/crm"
#endif
#define CRM_HAL_RPCD_STATE_FILE_STARTED "rpcd_started"
#define CRM_HAL_RPCD_STATE_FILE_STOPPED "rpcd_stopped"

#define MAX_WATCHED_PATH 256

/*
 * @see rpcd.h
 */
void crm_hal_rpcd_init(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    crm_property_set(CRM_KEY_SERVICE_STOP, CRM_KEY_CONTENT_SERVICE_RPCD);
    i_ctx->rpcd_running = false;

#ifdef HOST_BUILD
    mkdir(CRM_HAL_RPCD_STATE_FOLDER, 0777);
#endif

    unlink(CRM_HAL_RPCD_STATE_FOLDER "/" CRM_HAL_RPCD_STATE_FILE_STARTED);
    unlink(CRM_HAL_RPCD_STATE_FOLDER "/" CRM_HAL_RPCD_STATE_FILE_STOPPED);

    i_ctx->rpcd_inotify_socket = inotify_init();
    ASSERT(i_ctx->rpcd_inotify_socket >= 0);
    i_ctx->rpcd_folder_watch = inotify_add_watch(i_ctx->rpcd_inotify_socket,
                                                 CRM_HAL_RPCD_STATE_FOLDER, IN_CREATE);
    ASSERT(i_ctx->rpcd_folder_watch >= 0);
}

/*
 * @see rpcd.h
 */
void crm_hal_rpcd_start(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->rpcd_running == false);
    i_ctx->rpcd_running = true;
    crm_property_set(CRM_KEY_SERVICE_START, CRM_KEY_CONTENT_SERVICE_RPCD);
}

/*
 * @see rpcd.h
 */
void crm_hal_rpcd_stop(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->rpcd_running == true);
    i_ctx->rpcd_running = false;
    crm_property_set(CRM_KEY_SERVICE_STOP, CRM_KEY_CONTENT_SERVICE_RPCD);
}

/*
 * @see rpcd.h
 */
int crm_hal_rpcd_get_fd(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);
    return i_ctx->rpcd_inotify_socket;
}

/*
 * @see rpcd.h
 */
int crm_hal_rpcd_event(crm_hal_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);
    int fsm_evt = -1;

    /* This is needed to ensure that 'buf' is properly aligned. */
    int buf[(sizeof(struct inotify_event) + MAX_WATCHED_PATH +
             sizeof(int) - 1) / sizeof(int)];
    ssize_t ret = read(i_ctx->rpcd_inotify_socket, buf, sizeof(buf));
    DASSERT(ret >= (ssize_t)sizeof(struct inotify_event), "error: %zd / %s", ret, strerror(errno));

    char *cur_buf = (char *)buf;
    while (ret > 0) {
        struct inotify_event *evt = (struct inotify_event *)cur_buf;

        if (evt->len > 0) {
            LOGV("file event: '%s'", evt->name);
            char *name = NULL;
            if (!strcmp(evt->name, CRM_HAL_RPCD_STATE_FILE_STOPPED)) {
                ASSERT(fsm_evt == -1);
                name = evt->name;
                fsm_evt = EV_RPC_DEAD;
            } else if (!strcmp(evt->name, CRM_HAL_RPCD_STATE_FILE_STARTED)) {
                ASSERT(fsm_evt == -1);
                name = evt->name;
                fsm_evt = EV_RPC_RUN;
            }
            if (name != NULL) {
                char name_buf[MAX_WATCHED_PATH];
                int len = snprintf(name_buf, sizeof(name_buf), "%s/%s",
                                   CRM_HAL_RPCD_STATE_FOLDER, name);
                ASSERT(len >= 0 && len < (ssize_t)sizeof(name_buf));
                /* No ASSERT on unlink failure as in some rare cases, the file might already
                 * have been deleted.
                 */
                unlink(name_buf);
            }
        }

        /* Move to next event (if more than one are reported) */
        cur_buf += evt->len + sizeof(struct inotify_event);
        ret -= evt->len + sizeof(struct inotify_event);
    }
    ASSERT(ret == 0);
    return fsm_evt;
}

/*
 * @see rpcd.h
 */
void crm_hal_rpcd_dispose(crm_hal_ctx_internal_t *i_ctx)
{
    (void)i_ctx;
    ASSERT(inotify_rm_watch(i_ctx->rpcd_inotify_socket, i_ctx->rpcd_folder_watch) >= 0);
    close(i_ctx->rpcd_inotify_socket);
}
