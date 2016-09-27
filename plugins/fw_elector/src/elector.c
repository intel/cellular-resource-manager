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
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <openssl/md5.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>
#include <regex.h>

#define CRM_MODULE_TAG "FWEL"
#include "utils/common.h"
#include "utils/keys.h"
#include "utils/property.h"
#include "plugins/fw_elector.h"
#include "utils/file.h"

#define MD5_HASH_SIZE 16
#define HASH_SIZE (2 * MD5_HASH_SIZE + 1)

#ifdef HOST_BUILD
#define MIU_FOLDER "/tmp/miu_@/"
#define BLOB_HASH_PATH "/tmp/crm_hash"
#define FW_FOLDER "/tmp/"
#else
#define MIU_FOLDER "/system/vendor/firmware/telephony/miu_@/"
#define BLOB_HASH_PATH "/system/vendor/firmware/telephony/hash"
#define FW_FOLDER "/system/vendor/firmware/telephony/"
#endif

typedef struct crm_fw_elector_ctx_internal {
    crm_fw_elector_ctx_t ctx; // Needs to be first

    char *fw_filter;
    char *fw_path;
    char **tlvs;
    int nb_tlvs;
    int nb_found_tlvs;
    char blob_hash_value[HASH_SIZE];
    char config_hash_value[HASH_SIZE];
    char *miu_folder;
    bool are_hashes_readable;
} crm_fw_elector_ctx_internal_t;

static bool is_hash_equal(char *hash_key, const char *hash_value)
{
    ASSERT(hash_key != NULL);
    ASSERT(hash_value != NULL);

    char value[CRM_PROPERTY_VALUE_MAX];
    crm_property_get(hash_key, value, "");
    return strcmp(value, hash_value) == 0;
}

static int filter_tlv(const struct dirent *entry)
{
    return strstr(entry->d_name, ".tlv") != NULL;
}

static void update_miu_tlvs(crm_fw_elector_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);
    ASSERT(i_ctx->miu_folder != NULL);

    for (int i = 0; i < i_ctx->nb_found_tlvs; i++) {
        free(i_ctx->tlvs[i + i_ctx->nb_tlvs]);
        i_ctx->tlvs[i + i_ctx->nb_tlvs] = NULL;
    }

    i_ctx->nb_found_tlvs = 0;

    struct stat st;
    if (!stat(i_ctx->miu_folder, &st) && S_ISDIR(st.st_mode)) {
        struct dirent **list = NULL;
        int nb = scandir(i_ctx->miu_folder, &list, filter_tlv, alphasort);
        DASSERT(nb >= 0, "error: %d/%s", errno, strerror(errno));
        if (nb > 0) {
            i_ctx->nb_found_tlvs = nb;
            int nb_all_tlvs = i_ctx->nb_tlvs + i_ctx->nb_found_tlvs;
            i_ctx->tlvs = realloc(i_ctx->tlvs, sizeof(char *) * nb_all_tlvs);
            ASSERT(i_ctx->tlvs != NULL);

            for (int idx_list = 0; idx_list < i_ctx->nb_found_tlvs; idx_list++) {
                int len = strlen(i_ctx->miu_folder) + strlen(list[idx_list]->d_name) + 1;
                int idx_tlvs = idx_list + i_ctx->nb_tlvs;
                i_ctx->tlvs[idx_tlvs] = malloc(sizeof(char) * len);
                ASSERT(i_ctx->tlvs[idx_tlvs] != NULL);

                snprintf(i_ctx->tlvs[idx_tlvs], len, "%s%s", i_ctx->miu_folder,
                         list[idx_list]->d_name);
                free(list[idx_list]);
            }

            free(list);
        }
    }
}

static void compute_config_hash(crm_fw_elector_ctx_internal_t *i_ctx)
{
    ASSERT(i_ctx != NULL);

    unsigned char md5[MD5_HASH_SIZE];

    MD5_CTX ctx;
    MD5_Init(&ctx);

    for (int i = 0; i < i_ctx->nb_tlvs + i_ctx->nb_found_tlvs; i++)
        MD5_Update(&ctx, i_ctx->tlvs[i], strlen(i_ctx->tlvs[i]));

    ASSERT(i_ctx->fw_path != NULL);

    MD5_Update(&ctx, i_ctx->fw_path, strlen(i_ctx->fw_path));
    MD5_Final(md5, &ctx);

    for (size_t i = 0; i < sizeof(md5); i++)
        sprintf(&i_ctx->config_hash_value[i * 2], "%02x", md5[i]);

    LOGD("config hash value: (%s)", i_ctx->config_hash_value);
}

static void set_instance_id(char *src, int inst_id)
{
    ASSERT(src != NULL);

    char *find = strchr(src, '@');
    ASSERT(find != NULL);

    src[find - src] = '0' + inst_id;
}

static char *search_fw(const crm_fw_elector_ctx_internal_t *i_ctx, char *fw_folder, char *fw_filter)
{
    ASSERT(i_ctx != NULL);
    ASSERT(fw_folder != NULL);
    ASSERT(fw_filter != NULL);

    regex_t reg;
    DASSERT(regcomp(&reg, fw_filter, REG_ICASE | REG_EXTENDED) == 0, "wrong filter provided");

    struct dirent **list = NULL;
    int nb_files = scandir(fw_folder, &list, NULL, NULL);
    char *fw_file = NULL;

    for (int i = 0; i < nb_files; i++) {
        if (!regexec(&reg, list[i]->d_name, 0, NULL, 0)) {
            DASSERT(!fw_file, "at least two firmware files have been found");
            int len = strlen(fw_folder) + strlen(list[i]->d_name) + 1;
            fw_file = malloc(len * sizeof(char));
            ASSERT(fw_file != NULL);
            snprintf(fw_file, len, "%s%s", fw_folder, list[i]->d_name);
        }
        free(list[i]);
    }

    free(list);
    regfree(&reg);
    return fw_file;
}

/**
 * @see fw_elector.h
 */
static const char *get_fw_path(const crm_fw_elector_ctx_t *ctx)
{
    crm_fw_elector_ctx_internal_t *i_ctx = (crm_fw_elector_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);
    free(i_ctx->fw_path);
    i_ctx->fw_path = NULL;

    struct stat st;
    if (!stat(i_ctx->miu_folder, &st) && S_ISDIR(st.st_mode))
        i_ctx->fw_path = search_fw(i_ctx, i_ctx->miu_folder, "\\.fls$");

    if (i_ctx->fw_path == NULL)
        i_ctx->fw_path = search_fw(i_ctx, FW_FOLDER, i_ctx->fw_filter);

    ASSERT(i_ctx->fw_path != NULL);

    LOGD("->%s(%s)", __FUNCTION__, i_ctx->fw_path);

    return i_ctx->fw_path;
}

/**
 * @see fw_elector.h
 */
static const char *get_rnd_path(const crm_fw_elector_ctx_t *ctx)
{
    LOGD("->%s()", __FUNCTION__);
    ASSERT(ctx != NULL);
    // crm_fw_elector_ctx_internal_t *i_ctx = (crm_fw_elector_ctx_internal_t *)ctx;
    DASSERT(0, "not implemented");
}

/**
 * @see fw_elector.h
 */
static const char *const *get_tlv_list(const crm_fw_elector_ctx_t *ctx, int *nb)
{
    LOGD("->%s()", __FUNCTION__);

    crm_fw_elector_ctx_internal_t *i_ctx = (crm_fw_elector_ctx_internal_t *)ctx;
    ASSERT(i_ctx != NULL);
    ASSERT(nb != NULL);

    *nb = 0;

    if (i_ctx->are_hashes_readable) {
        update_miu_tlvs(i_ctx);
        compute_config_hash(i_ctx);

        if (!is_hash_equal(CRM_KEY_BLOB_HASH, i_ctx->blob_hash_value) ||
            !is_hash_equal(CRM_KEY_CONFIG_HASH, i_ctx->config_hash_value))
            *nb = i_ctx->nb_tlvs + i_ctx->nb_found_tlvs;
    }

    return 0 == *nb ? NULL : (const char **)i_ctx->tlvs;
}

/**
 * @see fw_elector.h
 */
static void notify_fw_flashed(const crm_fw_elector_ctx_t *ctx, int status)
{
    LOGD("->%s()", __FUNCTION__);
    (void)ctx;    // UNUSED
    (void)status; // UNUSED
}

/**
 * @see fw_elector.h
 */
static void notify_tlv_applied(const crm_fw_elector_ctx_t *ctx, int status)
{
    LOGD("->%s()", __FUNCTION__);
    crm_fw_elector_ctx_internal_t *i_ctx = (crm_fw_elector_ctx_internal_t *)ctx;
    ASSERT(i_ctx != NULL);

    if (0 == status && i_ctx->are_hashes_readable) {
        crm_property_set(CRM_KEY_BLOB_HASH, i_ctx->blob_hash_value);
        crm_property_set(CRM_KEY_CONFIG_HASH, i_ctx->config_hash_value);
    }
}

/**
 * @see fw_elector.h
 */
static void dispose(crm_fw_elector_ctx_t *ctx)
{
    LOGD("->%s()", __FUNCTION__);
    ASSERT(ctx != NULL);
    crm_fw_elector_ctx_internal_t *i_ctx = (crm_fw_elector_ctx_internal_t *)ctx;

    for (int i = 0; i < i_ctx->nb_tlvs + i_ctx->nb_found_tlvs; i++)
        free(i_ctx->tlvs[i]);
    free(i_ctx->tlvs);

    free(i_ctx->fw_filter);
    free(i_ctx->fw_path);
    free(i_ctx->miu_folder);
    free(i_ctx);
}

/**
 * @see fw_elector.h
 */
crm_fw_elector_ctx_t *crm_fw_elector_init(tcs_ctx_t *tcs, int inst_id)
{
    ASSERT(tcs != NULL);

    crm_fw_elector_ctx_internal_t *i_ctx = calloc(1, sizeof(*i_ctx));
    ASSERT(i_ctx != NULL);

    i_ctx->ctx.dispose = dispose;
    i_ctx->ctx.get_fw_path = get_fw_path;
    i_ctx->ctx.get_rnd_path = get_rnd_path;
    i_ctx->ctx.get_tlv_list = get_tlv_list;
    i_ctx->ctx.notify_fw_flashed = notify_fw_flashed;
    i_ctx->ctx.notify_tlv_applied = notify_tlv_applied;

    ASSERT(tcs->select_group(tcs, ".firmware_elector") == 0);
    i_ctx->fw_filter = tcs->get_string(tcs, "firmware_filter");
    ASSERT(i_ctx->fw_filter);

    char group[20];
    snprintf(group, sizeof(group), "streamline%d", inst_id);
    tcs->add_group(tcs, group, true);

    ASSERT(tcs->select_group(tcs, group) == 0);
    char **tlvs = tcs->get_string_array(tcs, "tlvs", &i_ctx->nb_tlvs);

    /* List of TLV can be empty */
    ASSERT(!((tlvs == NULL) ^ (i_ctx->nb_tlvs == 0)));

    i_ctx->tlvs = malloc(sizeof(char *) * i_ctx->nb_tlvs);
    ASSERT(i_ctx->tlvs != NULL);

    for (int i = 0; i < i_ctx->nb_tlvs; i++) {
        int len = strlen(FW_FOLDER) + strlen(tlvs[i]) + 1;
        i_ctx->tlvs[i] = malloc(sizeof(char) * len);

        ASSERT(i_ctx->tlvs[i] != NULL);
        snprintf(i_ctx->tlvs[i], len, "%s%s", FW_FOLDER, tlvs[i]);
        free(tlvs[i]);
    }
    free(tlvs);

    i_ctx->miu_folder = strdup(MIU_FOLDER);
    ASSERT(i_ctx->miu_folder != NULL);
    set_instance_id(i_ctx->miu_folder, inst_id);

    char value[CRM_PROPERTY_VALUE_MAX] = { '\0' };
    crm_property_get(CRM_KEY_DATA_PARTITION_ENCRYPTION, value, "");

    if (!strcmp(value, "trigger_restart_framework")) {
        i_ctx->are_hashes_readable = true;

        char blob_hash[HASH_SIZE + 1];
        DASSERT(crm_file_read(BLOB_HASH_PATH, blob_hash, sizeof(blob_hash)) == 0,
                "Failed to read blob hash from file");
        snprintf(i_ctx->blob_hash_value, sizeof(i_ctx->blob_hash_value), "%s", blob_hash);

        LOGD("blob hash value: (%s)", i_ctx->blob_hash_value);
    }

    LOGV("context %p", i_ctx);

    return &i_ctx->ctx;
}
