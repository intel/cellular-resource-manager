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

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CRM_MODULE_TAG "DUMP"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/time.h"
#include "utils/thread.h"
#include "utils/file.h"
#include "utils/keys.h"
#include "utils/property.h"
#include "plugins/dump.h"
#include "plugins/control.h"
#include "ifwd/crm_ifwd.h"

#include "libmdmcli/mdm_cli.h"

#define DUMP_FOLDER "/data/logs/modemcrash"
#define TRACE_FILE DUMP_FOLDER "/download_lib_logs.log"
#define PROCESS_LIB "libcrm_dump_pcie_process.so"

typedef struct crm_dump_internal_ctx {
    crm_dump_ctx_t ctx; // Must be first

    crm_ctrl_ctx_t *control;
    crm_process_factory_ctx_t *factory;

    /* variables */
    int process_id;
    char *log_file;
} crm_dump_internal_ctx_t;

static char *get_next_file(char **data, size_t *size)
{
    ASSERT(*data);
    ASSERT(size && *size > 0);

    char *param = *data;
    for (size_t i = 0; i < *size; i++) {
        if (param[i] == ';') {
            param[i] = '\0';
            *size -= i + 1;
            if (*size > 0)
                *data += i + 1;
            else
                *data = NULL;
            return param;
        }
    }
    DASSERT(0, "parameter not found");
}


static void *control_dump_process(crm_thread_ctx_t *thread_ctx, void *arg)
{
    crm_dump_internal_ctx_t *i_ctx = (crm_dump_internal_ctx_t *)arg;
    char *info_file = NULL;
    char *dump_file = NULL;
    int status = -1;

    ASSERT(i_ctx);
    ASSERT(i_ctx->process_id >= 0);
    ASSERT(thread_ctx);

    struct pollfd pfd = { .fd = i_ctx->factory->get_poll_fd(i_ctx->factory, i_ctx->process_id),
                          .events = POLLIN };

    crm_ipc_msg_t msg = { -1, 0, NULL };
    struct timespec timer_end;
    crm_time_add_ms(&timer_end, 30000); // @TODO: use TCS ?
    while (true) {
        int err = poll(&pfd, 1, crm_time_get_remain_ms(&timer_end));
        if (pfd.revents & POLLIN) {
            ASSERT(i_ctx->factory->get_msg(i_ctx->factory, i_ctx->process_id, &msg));
            ASSERT(msg.scalar == 0 || msg.scalar == -1);
            status = msg.scalar;

            if (status == 0) {
                ASSERT(msg.data);
                char *tmp = msg.data;
                size_t size = strlen(tmp);
                info_file = get_next_file(&tmp, &size);
                dump_file = get_next_file(&tmp, &size);

                ASSERT(info_file && strlen(info_file) <= 512);
                ASSERT(dump_file && strlen(dump_file) <= 512);

                const char *data[] = { DUMP_STR_SUCCEED, info_file, dump_file };
                mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_DUMP_END, DBG_DEFAULT_LOG_SIZE,
                                                DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_LOG_TIME,
                                                ARRAY_SIZE(data), data };
                i_ctx->control->notify_client(i_ctx->control, MDM_DBG_INFO, sizeof(dbg_info),
                                              &dbg_info);
            }
            break;
        } else if (err == 0) {
            LOGE("timeout");
            i_ctx->factory->kill(i_ctx->factory, i_ctx->process_id);
            break;
        } else {
            ASSERT(0);
        }
    }

    thread_ctx->dispose(thread_ctx, NULL);
    i_ctx->factory->clean(i_ctx->factory, i_ctx->process_id);
    i_ctx->process_id = -1;
    free(msg.data);

    i_ctx->control->notify_dump_status(i_ctx->control, status);
    return NULL;
}

/**
 * @see dump.h
 */
static void read_dump(crm_dump_ctx_t *ctx, const char *link, const char *fw)
{
    crm_dump_internal_ctx_t *i_ctx = (crm_dump_internal_ctx_t *)ctx;

    ASSERT(i_ctx);
    ASSERT(link);
    ASSERT(fw);
    ASSERT(i_ctx->process_id == -1);

    LOGV("->%s(%s)", __FUNCTION__, link);

    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_DUMP_START, DBG_DEFAULT_NO_LOG, DBG_DEFAULT_NO_LOG,
                                    DBG_DEFAULT_NO_LOG, 0, NULL };
    i_ctx->control->notify_client(i_ctx->control, MDM_DBG_INFO, sizeof(dbg_info), &dbg_info);

    int args_size = strlen(link) + strlen(fw) + strlen(DUMP_FOLDER) + strlen(i_ctx->log_file) + 5;
    char *args = malloc(sizeof(char) * args_size);
    ASSERT(args);
    snprintf(args, args_size, "%s;%s;%s;%s;", link, fw, DUMP_FOLDER, i_ctx->log_file);

    i_ctx->process_id = i_ctx->factory->create(i_ctx->factory, PROCESS_LIB, args, args_size);
    free(args);

    if (i_ctx->process_id >= 0) {
        crm_thread_init(control_dump_process, i_ctx, false, true);
    } else {
        LOGE("failed to create process");
        i_ctx->control->notify_dump_status(i_ctx->control, -1);
    }
}

/**
 * @see dump.h
 */
static void stop(crm_dump_ctx_t *ctx)
{
    (void)ctx;
    DASSERT(0, "not implemented");
}

/**
 * @see dump.h
 */
static void dispose(crm_dump_ctx_t *ctx)
{
    crm_dump_internal_ctx_t *i_ctx = (crm_dump_internal_ctx_t *)ctx;

    ASSERT(i_ctx != NULL);

    free(i_ctx);
}

/**
 * @see dump.h
 */
crm_dump_ctx_t *crm_dump_init(tcs_ctx_t *tcs, crm_ctrl_ctx_t *control,
                              crm_process_factory_ctx_t *factory, bool host_debug)
{
    crm_dump_internal_ctx_t *i_ctx = calloc(1, sizeof(crm_dump_internal_ctx_t));

    (void)tcs;     // NOT USED
    (void)host_debug;
    ASSERT(i_ctx != NULL);
    ASSERT(control != NULL);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.read = read_dump;
    i_ctx->ctx.stop = stop;

    i_ctx->factory = factory;
    i_ctx->control = control;
    i_ctx->process_id = -1;

    char value[CRM_PROPERTY_VALUE_MAX];
    crm_property_get(CRM_KEY_DBG_ENABLE_FLASHING_LOG, value, "off");
    if (!strcmp(value, "on"))
        i_ctx->log_file = TRACE_FILE;
    else
        i_ctx->log_file = "";

    LOGV("context %p", i_ctx);
    return &i_ctx->ctx;
}
