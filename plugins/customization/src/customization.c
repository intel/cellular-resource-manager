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
#include <fcntl.h>
#include <sys/sendfile.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define CRM_MODULE_TAG "CUSTO"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/thread.h"
#include "utils/at.h"
#include "plugins/mdm_customization.h"
#include "plugins/control.h"

#define TLV_CHUNK 256
/* TLV file content is stored in an unsigned char buffer and split in chunks of TLV_CHUNK bytes
 * for transmission. Chunks are encoded and encapsulated in an AT command.
 * A byte of a chunk is converted in "%d," => 4 bytes long
 * AT header is 100 bytes long max */
#define TLV_ENCODED_CHUNK ((TLV_CHUNK * 4) + 100)

#define TLV_FILE_MAX_PATH 256

typedef struct crm_customization_internal_ctx {
    crm_customization_ctx_t ctx; // Must be first

    crm_ctrl_ctx_t *control;

    char **tlvs;
    int tlvs_nb;
    bool op_ongoing;
    char *tlv_node;
} crm_customization_internal_ctx_t;

static unsigned char *load_tlv_file(const char *path, size_t *len)
{
    ASSERT(path != NULL);
    ASSERT(len != NULL);

    errno = 0;
    int fd = open(path, O_RDONLY);
    DASSERT(fd >= 0, "open of (%s) failed (%s)", path, strerror(errno));

    struct stat st;
    ASSERT(fstat(fd, &st) == 0);

    /* @TODO: instead of using allocated memory, a local buffer could be used. But do we know the
     * maximum size? */
    unsigned char *data = malloc(st.st_size * sizeof(unsigned char));
    ASSERT(data != NULL);

    DASSERT(read(fd, data, st.st_size) == st.st_size, "read failed (%s)", strerror(errno));
    close(fd);

    *len = st.st_size;
    return data;
}

static void *write_tlvs(crm_thread_ctx_t *thread_ctx, void *arg)
{
    crm_customization_internal_ctx_t *i_ctx = (crm_customization_internal_ctx_t *)arg;
    int err = 0;

    ASSERT(i_ctx != NULL);
    ASSERT(thread_ctx != NULL);
    ASSERT(i_ctx->tlvs != NULL);

    ASSERT(i_ctx->op_ongoing == false);
    i_ctx->op_ongoing = true;

    LOGD("->%s()", __FUNCTION__);

    errno = 0;
    int fd = open(i_ctx->tlv_node, O_RDWR);
    DASSERT(fd >= 0, "open of (%s) failed (%s)", i_ctx->tlv_node, strerror(errno));

    for (int tlv_idx = 0; (tlv_idx < i_ctx->tlvs_nb) && (err == 0); tlv_idx++) {
        int timeout = 50000; //@TODO: fix a correct timeout
        size_t tlv_len = 0;
        unsigned char *tlv_data = load_tlv_file(i_ctx->tlvs[tlv_idx], &tlv_len);

        LOGD("[STREAMLINE] Applying %s...", i_ctx->tlvs[tlv_idx]);
        for (size_t data_idx = 0;
             (data_idx < ((tlv_len + TLV_CHUNK - 1) / TLV_CHUNK)) && (err == 0); data_idx++) {
            char gti_cmd[TLV_ENCODED_CHUNK];
            size_t gti_idx = snprintf(gti_cmd, sizeof(gti_cmd), "AT@gticom:config_script[%zu]={",
                                      data_idx * TLV_CHUNK);

            unsigned char *chunk = &tlv_data[data_idx * TLV_CHUNK];
            for (size_t i = 0; i < MIN(TLV_CHUNK, (tlv_len - data_idx * TLV_CHUNK)); i++) {
                gti_idx += snprintf(&gti_cmd[gti_idx], sizeof(gti_cmd) - gti_idx,
                                    "%d,", chunk[i]);
                ASSERT(gti_idx < sizeof(gti_cmd));
            }

            ASSERT(gti_idx > 0);
            gti_idx -= 1; // removal of last ','
            gti_idx += snprintf(&gti_cmd[gti_idx], sizeof(gti_cmd) - gti_idx, "}");
            ASSERT(gti_idx < sizeof(gti_cmd));

            /* sending TLV customization data */
            err = crm_send_at(fd, CRM_MODULE_TAG, gti_cmd, timeout, -1);
        }

        /* sending execution request */
        if (err == 0)
            err = crm_send_at(fd, CRM_MODULE_TAG, "AT@gticom:run_configuration()", timeout, -1);

        free(tlv_data);

        if (err == 0) {
            LOGV("[STREAMLINE] %s applied", i_ctx->tlvs[tlv_idx]);
        } else {
            LOGE("[STREAMLINE] Failed to apply %s", i_ctx->tlvs[tlv_idx]);
            const char *data[] = { "TFT_ERROR_TLV", "TLV failure: failed to apply TLV",
                                   i_ctx->tlvs[tlv_idx] };
            mdm_cli_dbg_info_t dbg_info = { DBG_TYPE_ERROR, DBG_DEFAULT_LOG_SIZE,
                                            DBG_DEFAULT_LOG_SIZE, DBG_DEFAULT_LOG_TIME,
                                            ARRAY_SIZE(data), data };
            i_ctx->control->notify_client(i_ctx->control, MDM_DBG_INFO, sizeof(dbg_info),
                                          &dbg_info);
        }
    }

    for (int i = 0; i < i_ctx->tlvs_nb; i++)
        free(i_ctx->tlvs[i]);
    free(i_ctx->tlvs);
    i_ctx->tlvs = NULL;
    i_ctx->tlvs_nb = 0;

    DASSERT(close(fd) == 0, "close failed (%s)", strerror(errno));

    /* @TODO clean this (configure time ? flush API ? ...) */
    sleep(2);

    i_ctx->op_ongoing = false;

    /* detached thread: Memory must be cleaned before thread termination */
    thread_ctx->dispose(thread_ctx, NULL);

    i_ctx->control->notify_customization_status(i_ctx->control, err);

    return NULL;
}

/**
 * @see mdm_customization.h
 */
static void dispose(crm_customization_ctx_t *ctx)
{
    crm_customization_internal_ctx_t *i_ctx = (crm_customization_internal_ctx_t *)ctx;

    ASSERT(i_ctx != NULL);

    free(i_ctx->tlv_node);

    free(i_ctx);
}

/**
 * @see mdm_customization.h
 */
static void send(crm_customization_ctx_t *ctx, const char *const *tlv_list, int nb)
{
    crm_customization_internal_ctx_t *i_ctx = (crm_customization_internal_ctx_t *)ctx;

    LOGD("->%s()", __FUNCTION__);

    ASSERT(i_ctx != NULL);
    ASSERT(tlv_list != NULL);
    ASSERT(nb > 0);
    ASSERT(i_ctx->op_ongoing == false);
    ASSERT(i_ctx->tlvs == NULL);

    i_ctx->tlvs_nb = nb;
    i_ctx->tlvs = malloc(nb * sizeof(char *));
    ASSERT(i_ctx->tlvs != NULL);
    for (int i = 0; i < nb; i++) {
        i_ctx->tlvs[i] = strdup(tlv_list[i]);
        ASSERT(i_ctx->tlvs[i]);
    }

    crm_thread_init(write_tlvs, i_ctx, false, true);
}

/**
 * @see mdm_customization.h
 */
crm_customization_ctx_t *crm_customization_init(tcs_ctx_t *tcs, crm_ctrl_ctx_t *control)
{
    crm_customization_internal_ctx_t *i_ctx = calloc(1, sizeof(crm_customization_internal_ctx_t));

    ASSERT(i_ctx != NULL);
    ASSERT(tcs != NULL);
    ASSERT(control != NULL);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.send = send;

    ASSERT(tcs->select_group(tcs, ".customization") == 0);
    i_ctx->tlv_node = tcs->get_string(tcs, "node");
    ASSERT(i_ctx->tlv_node != NULL);

    i_ctx->control = control;

    LOGV("context %p", i_ctx);
    return &i_ctx->ctx;
}
