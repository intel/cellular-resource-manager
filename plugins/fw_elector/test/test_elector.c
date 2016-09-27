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
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define CRM_MODULE_TAG "FWELT"
#include "utils/common.h"
#include "utils/property.h"
#include "utils/keys.h"
#include "utils/file.h"
#include "plugins/fw_elector.h"
#include "libmdmcli/mdm_cli.h"
#include "test/test_utils.h"

#define FW_FOLDER  "/tmp"
#define MIU_FOLDER FW_FOLDER "/miu_@"
#define HASH_FILE  FW_FOLDER "/crm_hash"
#define FW_FILE    FW_FOLDER "/fw.fls"

static void set_instance_id(char *src, int inst_id)
{
    ASSERT(src != NULL);

    char *find = strstr(src, "@");
    ASSERT(find != NULL);

    src[find - src] = '0' + inst_id;
}

static void create_file(const char *file)
{
    ASSERT(file);

    int fd = open(file, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    DASSERT(fd >= 0, "failed to create %s", file);
    write(fd, "test", 4);
    ASSERT(close(fd) == 0);
    LOGD("file %s created", file);
}

static void check_fw_update(crm_fw_elector_ctx_t *fw_elector, const char *fw, const char **tlvs,
                            int nb_tlvs)
{
    ASSERT(fw_elector);
    ASSERT(fw);
    ASSERT(!((tlvs == NULL) ^ (nb_tlvs == 0)));

    bool first = true;
    for (int i = 0; i < 5; i++) {
        ASSERT(!strcmp(fw_elector->get_fw_path(fw_elector), fw));
        fw_elector->notify_fw_flashed(fw_elector, 0);
        ASSERT(crm_file_exists(fw));

        int nb_list;
        const char *const *list = fw_elector->get_tlv_list(fw_elector, &nb_list);
        if (first) {
            first = false;
            DASSERT(nb_list == nb_tlvs, "%d %d", nb_list, nb_tlvs);
            if (nb_list > 0) {
                ASSERT(list && tlvs);
                for (int i = 0; i < nb_list; i++) {
                    ASSERT(tlvs[i] && list[i]);
                    ASSERT(!strcmp(tlvs[i], list[i]));
                    ASSERT(crm_file_exists(list[i]));
                }
            } else {
                ASSERT(!list && nb_list == 0);
            }
        } else {
            ASSERT(!list && (nb_list == 0));
        }
        fw_elector->notify_tlv_applied(fw_elector, 0);
    }
}

static char *compute_path(const char *folder, const char *file)
{
    ASSERT(folder && file);

    int len = strlen(folder) + strlen(file) + 2;
    char *path = malloc(sizeof(char) * len);
    ASSERT(path);
    snprintf(path, len, "%s/%s", folder, file);
    return path;
}

int main(void)
{
    char *miu_folder = strdup(MIU_FOLDER);

    ASSERT(miu_folder);
    set_instance_id(miu_folder, MDM_CLI_DEFAULT_INSTANCE);

    char group[15];
    tcs_ctx_t *tcs = CRM_TEST_tcs_init("host_sofia", MDM_CLI_DEFAULT_INSTANCE);
    ASSERT(tcs);
    snprintf(group, sizeof(group), "streamline%d", MDM_CLI_DEFAULT_INSTANCE);
    tcs->add_group(tcs, group, true);
    ASSERT(tcs->select_group(tcs, group) == 0);
    int tcs_nb_tlvs;
    char **tcs_tlvs = tcs->get_string_array(tcs, "tlvs", &tcs_nb_tlvs);
    /* List of TLV can be empty */
    ASSERT(!((tcs_tlvs == NULL) ^ (tcs_nb_tlvs == 0)));

    char *miu_tlvs[] = { "miu1.tlv", "miu2.tlv" };
    int nb_tlvs = tcs_nb_tlvs + ARRAY_SIZE(miu_tlvs);
    char **tlvs = malloc(sizeof(char *) * nb_tlvs);
    ASSERT(tlvs);

    int tlvs_idx = 0;
    int miu_idx = 0;
    for (; tlvs_idx < tcs_nb_tlvs; tlvs_idx++) {
        tlvs[tlvs_idx] = compute_path(FW_FOLDER, tcs_tlvs[tlvs_idx]);
        create_file(tlvs[tlvs_idx]);
        free(tcs_tlvs[tlvs_idx]);
    }
    free(tcs_tlvs);
    for (; tlvs_idx < nb_tlvs; tlvs_idx++, miu_idx++)
        tlvs[tlvs_idx] = compute_path(miu_folder, miu_tlvs[miu_idx]);

    create_file(HASH_FILE);
    crm_property_set(CRM_KEY_DATA_PARTITION_ENCRYPTION, "trigger_restart_framework");

    create_file(FW_FILE);
    char rm_miu_folder_cmd[128];
    snprintf(rm_miu_folder_cmd, sizeof(rm_miu_folder_cmd), "rm -fr %s", miu_folder);
    system(rm_miu_folder_cmd);
    char create_miu_folder_cmd[128];
    snprintf(create_miu_folder_cmd, sizeof(create_miu_folder_cmd), "mkdir -p %s", miu_folder);

    crm_fw_elector_ctx_t *fw_elector = crm_fw_elector_init(tcs, MDM_CLI_DEFAULT_INSTANCE);
    ASSERT(fw_elector);

    char *miu_fw = compute_path(miu_folder, "miu_fw.fls");
    for (int i = 0; i < 5; i++) {
        LOGD("Standard use case: no MIU files");
        check_fw_update(fw_elector, FW_FILE, (const char **)tlvs, tcs_nb_tlvs);

        LOGD("FW provided by MIU");
        system(create_miu_folder_cmd);
        create_file(miu_fw);
        check_fw_update(fw_elector, miu_fw, (const char **)tlvs, tcs_nb_tlvs);

        LOGD("FW and TLVS provided by MIU");
        for (int i = tcs_nb_tlvs; i < nb_tlvs; i++)
            create_file(tlvs[i]);
        check_fw_update(fw_elector, miu_fw, (const char **)tlvs, nb_tlvs);

        LOGD("only TLVS provided by MIU");
        unlink(miu_fw);
        check_fw_update(fw_elector, FW_FILE, (const char **)tlvs, nb_tlvs);

        system(rm_miu_folder_cmd);
    }

    LOGD("data partition encrypted");
    crm_property_set(CRM_KEY_DATA_PARTITION_ENCRYPTION, "partition encrypted");

    fw_elector->dispose(fw_elector);
    fw_elector = crm_fw_elector_init(tcs, MDM_CLI_DEFAULT_INSTANCE);
    ASSERT(fw_elector);

    for (int i = 0; i < 5; i++) {
        check_fw_update(fw_elector, FW_FILE, NULL, 0);

        /* check if miu fw is found */
        system(create_miu_folder_cmd);
        create_file(miu_fw);
        check_fw_update(fw_elector, miu_fw, NULL, 0);

        /* check if miu tlvs are not found */
        for (int i = tcs_nb_tlvs; i < nb_tlvs; i++)
            create_file(tlvs[i]);
        check_fw_update(fw_elector, miu_fw, NULL, 0);
        system(rm_miu_folder_cmd);
    }

    for (int i = 0; i < nb_tlvs; i++) {
        unlink(tlvs[i]);
        free(tlvs[i]);
    }
    free(tlvs);
    free(miu_fw);
    free(miu_folder);
    fw_elector->dispose(fw_elector);
    tcs->dispose(tcs);

    unlink(FW_FILE);
    unlink(HASH_FILE);

    LOGD("success");
    return 0;
}
