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
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <zlib.h>

#define CRM_MODULE_TAG "DUMP"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/thread.h"
#include "plugins/dump.h"
#include "plugins/control.h"

#include "libmdmcli/mdm_cli.h"

#ifdef HOST_BUILD
#define DUMP_FOLDER "/tmp"
#else
#define DUMP_FOLDER "/data/logs/modemcrash"
#endif

typedef struct crm_dump_internal_ctx {
    crm_dump_ctx_t ctx; // Must be first

    /* config */
    bool host_debug;
    crm_ctrl_ctx_t *control;

    /* variables */
    char *vdump_path;
    bool op_ongoing;
} crm_dump_internal_ctx_t;


/**
 * Dumps core dump data into file system
 *
 * @param [in] v_fd        Dump file descriptor
 * @param [in] cmd         Command to send to the modem
 * @param [in] type        Kind of dump data (info or modem)
 * @param [in] extension   file extension
 * @param [in] local       Local time
 * @param [out] output     Full path of created file in case of success or error reason
 * @param [in] size_output Size of output buffer
 * @param [in] host_debug  HOST debug mode
 *
 * @return 0 in case of success
 */
static int dump_data(int v_fd, const char *cmd, const char *type, const char *extension,
                     const struct tm *local, char *output, size_t size_output, bool host_debug)
{
    ASSERT(cmd != NULL);
    ASSERT(type != NULL);
    ASSERT(extension != NULL);
    ASSERT(local != NULL);
    ASSERT(output != NULL);
    ASSERT(v_fd >= 0);

    snprintf(output, size_output, DUMP_FOLDER "/coredump_%s_%4d-%02d-%02d-%02d.%02d.%02d.%s.gz",
             type, local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, local->tm_hour,
             local->tm_min, local->tm_sec, extension);

    errno = 0;
    gzFile o_fd = gzopen(output, "w");
    DASSERT(o_fd != NULL, "failed to open %s. reason: %s", output, strerror(errno));

    size_t len_cmd = strlen(cmd);
    ssize_t cmd_write = write(v_fd, cmd, len_cmd);
    ASSERT(cmd_write > 0 && (size_t)cmd_write == len_cmd);

    int success = 0;
    for (;; ) {
        struct pollfd pfd = { .fd = v_fd, .events = POLLIN };

        int err = poll(&pfd, 1, 1000); //@TODO: configured value ?
        ASSERT(err != 0);

        char tmp[8192]; // @TODO: reduce this buffer size once kernel driver is fixed
        errno = 0;
        ssize_t len_read = read(v_fd, tmp, sizeof(tmp));
        if (len_read > 0) {
            int w_unconpressed = gzwrite(o_fd, tmp, len_read);
            if (w_unconpressed != len_read) {
                LOGE("[DUMP] Failed to write in %s. reason: %s", output, strerror(errno));
                success = -1;
                break;
            }
        } else if ((len_read == 0) || (host_debug && errno == EIO)) {
            /* unfortunately, host test is not able to simulate an EOF properly. To simulate
             * a transmission ending, the socket is closed, leading to an EIO error */
            break;
        } else if (len_read < 0) {
            LOGE("[VDUMP] read failure %d(%s)", errno, strerror(errno));
            success = -1;
            break;
        }
    }

    DASSERT(gzclose(o_fd) == Z_OK, "failed to close %s", output);

    if (!success) {
        LOGV("[DUMP] dump of %s stored in file %s", type, output);
    } else {
        unlink(output);
        snprintf(output, size_output, "Failed to retrieve %s. error: %s", type, strerror(errno));
    }

    return success;
}

static void *dump_thread(crm_thread_ctx_t *thread_ctx, void *arg)
{
    crm_dump_internal_ctx_t *i_ctx = (crm_dump_internal_ctx_t *)arg;

    ASSERT(i_ctx != NULL);
    ASSERT(thread_ctx != NULL);

    ASSERT(i_ctx->op_ongoing == false);
    i_ctx->op_ongoing = true;

    char dump_path_or_error[512] = { "" }; // size mapped with dbg_info
    char info_path_or_error[512] = { "" };

    struct tm tmp;
    time_t now = time(NULL);
    struct tm *local = localtime_r(&now, &tmp);
    ASSERT(local != NULL);

    errno = 0;
    int v_fd = open(i_ctx->vdump_path, O_RDWR);
    DASSERT(v_fd >= 0, "[VDUMP] failed to open %s. reason: %s", i_ctx->vdump_path, strerror(errno));
    LOGD("[VDUMP] opened: %s", i_ctx->vdump_path);

    int status = dump_data(v_fd, "get_coredump_info", "info", "txt", local, info_path_or_error,
                           sizeof(info_path_or_error), i_ctx->host_debug);
    if (!status) {
        if (i_ctx->host_debug) {
            /* this is need for HOST test purpose */
            close(v_fd);
            v_fd = -1;
            for (int i = 0; i < 2000; i++) {
                v_fd = open(i_ctx->vdump_path, O_RDWR);
                if (v_fd >= 0)
                    break;
                else
                    usleep(5000);
            }
            ASSERT(v_fd >= 0);
        }

        status = dump_data(v_fd, "get_coredump", "modem", "istp", local, dump_path_or_error,
                           sizeof(dump_path_or_error), i_ctx->host_debug);
    }

    errno = 0;
    DASSERT(close(v_fd) == 0, "[VDUMP] failed to close %s.reason: %s", i_ctx->vdump_path,
            strerror(errno));

    LOGD("[VDUMP] closed: %s", i_ctx->vdump_path);

    const char *data[] = { DUMP_STR_SUCCEED, dump_path_or_error, info_path_or_error };
    if (status)
        data[0] = DUMP_STR_LINK_ERR;

    mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_DUMP_END, DBG_DEFAULT_LOG_SIZE,
                                    DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_LOG_TIME,
                                    ARRAY_SIZE(data), data };
    i_ctx->control->notify_client(i_ctx->control, MDM_DBG_INFO, sizeof(dbg_info), &dbg_info);

    free(i_ctx->vdump_path);
    i_ctx->vdump_path = NULL;
    i_ctx->op_ongoing = false;

    i_ctx->control->notify_dump_status(i_ctx->control, status);

    thread_ctx->dispose(thread_ctx, NULL);

    return NULL;
}

/**
 * @see dump.h
 */
static void read_dump(crm_dump_ctx_t *ctx, const char *link, const char *fw)
{
    crm_dump_internal_ctx_t *i_ctx = (crm_dump_internal_ctx_t *)ctx;

    (void)fw;  // UNUSED
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->op_ongoing == false);
    ASSERT(i_ctx->vdump_path == NULL);
    ASSERT(link);

    LOGV("->%s(%s)", __FUNCTION__, link);

    i_ctx->vdump_path = strdup(link);
    ASSERT(i_ctx->vdump_path != NULL);

    crm_thread_init(dump_thread, i_ctx, false, true);
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
    ASSERT(i_ctx->op_ongoing == false);

    free(i_ctx);
}

/**
 * @see dump.h
 */
crm_dump_ctx_t *crm_dump_init(tcs_ctx_t *tcs, crm_ctrl_ctx_t *control,
                              crm_process_factory_ctx_t *factory, bool host_debug)
{
    crm_dump_internal_ctx_t *i_ctx = calloc(1, sizeof(crm_dump_internal_ctx_t));

    (void)factory; // UNUSED
    (void)tcs;     // Scalability not used by this plugin
    ASSERT(i_ctx != NULL);
    ASSERT(control != NULL);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.read = read_dump;
    i_ctx->ctx.stop = stop;

    i_ctx->host_debug = host_debug;
    i_ctx->control = control;

    LOGV("context %p", i_ctx);
    return &i_ctx->ctx;
}
