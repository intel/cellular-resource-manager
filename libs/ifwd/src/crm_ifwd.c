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

#include <grp.h>
#include <pwd.h>
#include <stdbool.h>

#define CRM_MODULE_TAG "IFWD"
#include "utils/logs.h"
#include "utils/common.h"
#include "utils/file.h"
#include "archive.h"

#include "downloadLib_interfaces/DownloadTool.h"
#include "downloadLib_interfaces/fls_access.h"

#define SET_CFG(param, value, error_buffer) do { \
        DASSERT(IFWD_DL_OK == IFWD_DL_set_dll_parameter(param, value, error_buffer), \
                "Failed to set " xstr(param)); } while (0)


/* Logical channel used by the download library. Default: 1 */
#define FLASHING_CHANNEL 1
#define ERR_BUFFER_SIZE 500

#ifndef HOST_BUILD

static inline void delete_file(const char *folder, const char *file)
{
    char path[128];

    snprintf(path, sizeof(path), "%s/%s", folder, file);
    if (crm_file_exists(path))
        ASSERT(!unlink(path));
}

static inline void delete_dump_files(const char *folder, const char *info_file,
                                     const char **dump_files,
                                     size_t nb_dump_files)
{
    delete_file(folder, info_file);
    for (size_t i = 0; i < nb_dump_files; i++)
        delete_file(folder, dump_files[i]);
}

static inline void set_name(const struct tm *local, const char *folder, const char *type,
                            const char *extension, char *output, size_t size_output)
{
    snprintf(output, size_output, "%s/coredump_%s_%4d-%02d-%02d-%02d.%02d.%02d.%s", folder,
             type, local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, local->tm_hour,
             local->tm_min, local->tm_sec, extension);
}

static void print_ifwd_logs(uint8_t channel, IFWD_DL_status_enum state, char *text)
{
    (void)channel;

    ASSERT(text != NULL);

    switch (state) {
    case IFWD_DL_ProgressUpdated:
    {
        errno = 0;
        char *end_ptr = NULL;
        int percent = strtol(text, NULL, 10);
        ASSERT(errno == 0 && end_ptr != text);
        LOGD("[IFWD] Progress: %d%%", percent);
    }
    break;
    case IFWD_DL_ProcessOutlineUpdated:
    case IFWD_DL_ProcessDetailUpdated:
        LOGD("[IFWD] %s", text);
        break;
    case IFWD_DL_OK:
        break;
    case IFWD_DL_Error:
        LOGD("[IFWD] Failed to upload: %s", text);
        break;
    case IFWD_DL_AT_Command: break;
    default: ASSERT(0);
    }
}

static void package(const char *fw_path, const char *injected_fw, const char *nvm_folder)
{
    struct stat st;

    ASSERT(stat(fw_path, &st) == 0);
    ASSERT(S_ISREG(st.st_mode) == true);

    unlink(injected_fw);
    ASSERT(fls_access_flashless_inject_nvm(0, fw_path, injected_fw, nvm_folder, 0) != 0);
}

static int boot_modem_flashing_mode(char *error_buffer, char *dev_node, char *fw,
                                    const char *log_file, const char *dump_folder,
                                    bool dump_enabled)
{
    unsigned int hw_platform;

    ASSERT(error_buffer);
    ASSERT(dev_node);
    ASSERT(fw);
    ASSERT(log_file);

    if (*log_file != '\0') {
        unlink(log_file);
        SET_CFG(IFWD_DL_dll_parameter_set_trace_filename, (uintptr_t)log_file, error_buffer);
        SET_CFG(IFWD_DL_dll_parameter_set_trace, true, error_buffer);
    }

    if (dump_enabled) {
        ASSERT(dump_folder);
        SET_CFG(IFWD_DL_dll_parameter_allow_hw_channel_switch, 0, error_buffer);
        SET_CFG(IFWD_DL_dll_parameter_set_coredump_mode, IFWD_DL_coredump_mode_auto, error_buffer);
        SET_CFG(IFWD_DL_dll_parameter_set_coredump_path, (uintptr_t)dump_folder, error_buffer);
    } else {
        SET_CFG(IFWD_DL_dll_parameter_set_coredump_mode, IFWD_DL_coredump_mode_disable,
                error_buffer);
    }

    SET_CFG(IFWD_DL_dll_parameter_allow_hw_channel_switch, 0, error_buffer);

    // @TODO: configure the baudrate if needed
    DASSERT(IFWD_DL_open_comm_port(FLASHING_CHANNEL, dev_node, dev_node, 3000000, error_buffer)
            == IFWD_DL_OK, "Failed to open comm port (%s)", error_buffer);

    DASSERT(IFWD_DL_init_callback(print_ifwd_logs, error_buffer) == IFWD_DL_OK,
            "Failed to initialize callback (%s)", error_buffer);

    SET_CFG(IFWD_DL_dll_parameter_stay_in_function, 1, error_buffer);

    DASSERT(IFWD_DL_TOC_get_hw_platform(fw, &hw_platform) == IFWD_DL_OK,
            "Failed to receive code section");

    if (IFWD_DL_OK != IFWD_DL_pre_boot_target(FLASHING_CHANNEL, hw_platform, error_buffer)) {
        LOGE("Failed to pre-boot the target (%s)", error_buffer);
        return -1;
    }

    IFWD_DL_modem_control_signals_type mcs;
    memset(&mcs, 0, sizeof(mcs));
    mcs.RTS = IFWD_DL_mco_set_to_1;

    if (IFWD_DL_OK != IFWD_DL_boot_target(FLASHING_CHANNEL, fw, &mcs, error_buffer)) {
        LOGE("Failed to boot the target (%s)", error_buffer);
        return -1;
    }

    return 0;
}

static int boot_modem_normal_mode(char *error_buffer, const char *log_file)
{
    int ret = 0;

    /* Default values are provided to this function: (FLASHING_CHANNEL, 1, 16, 17, 0) */
    if (IFWD_DL_OK != IFWD_DL_force_target_reset(FLASHING_CHANNEL, 1, 16, 17, 0, error_buffer)) {
        LOGE("Failed to reset the modem. reason: %s", error_buffer);
        ret = -1;
    }

    return ret;
}

static void cleanup(char *error_buffer, const char *log_file)
{
    /* NB: do not delete injected firmware to allocate the space in the file system */
    IFWD_DL_close_comm_port(FLASHING_CHANNEL, error_buffer);
    if (*log_file != '\0')
        SET_CFG(IFWD_DL_dll_parameter_set_trace, false, error_buffer);
}

static int write_fw(char *dev_node, char *fw, const char *log_file)
{
    char error_buffer[ERR_BUFFER_SIZE] = { '\0' };
    int ret = -1;

    ASSERT(dev_node);
    ASSERT(fw);
    ASSERT(log_file);

    LOGD("<link: (%s)> <fw: (%s)> <log file: (%s)>", dev_node, fw, log_file);

    if (!boot_modem_flashing_mode(error_buffer, dev_node, fw, log_file, NULL, false)) {
        uint32_t nb_parts;
        uint32_t parts_flashed = 0;
        IFWD_DL_TOC_get_nof_items(fw, &nb_parts);
        ASSERT(nb_parts > 0);

        /* Flash MEMORY_CLASS_CODE sections first, MEMORY_CLASS_CUST in second */
        static const uint32_t classes[] = { MEMORY_CLASS_CODE, MEMORY_CLASS_CUST };
        for (size_t class_idx = 0; class_idx < ARRAY_SIZE(classes); class_idx++) {
            for (uint32_t part_idx = 0; part_idx < nb_parts; part_idx++) {
                uint32_t memory_class;
                IFWD_DL_TOC_get_memory_class(fw, part_idx, &memory_class);
                if (memory_class == classes[class_idx]) {
                    char fls[512];
                    snprintf(fls, sizeof(fls), "|%d|%s", part_idx, fw);

                    IFWD_DL_status_enum err;
                    if (classes[class_idx] == MEMORY_CLASS_CODE) {
                        err = IFWD_DL_download_fls_file(FLASHING_CHANNEL, fls, error_buffer);
                        parts_flashed++;
                    } else if (classes[class_idx] == MEMORY_CLASS_CUST) {
                        err = IFWD_DL_download_cust_file(FLASHING_CHANNEL, fls, error_buffer);
                        parts_flashed++;
                    } else {
                        ASSERT(0);
                    }

                    DASSERT(err == IFWD_DL_OK, "failed to flash %s. reason: %s", fls, error_buffer);
                }
            }
        }

        ASSERT(parts_flashed == nb_parts);
        ret = boot_modem_normal_mode(error_buffer, log_file);
        if (!ret)
            LOGD("Firmware flashed successfully");
    }

    cleanup(error_buffer, log_file);

    return ret;
}

/**
 * Core dump files description
 *
 * - report.json
 *   Information file
 *
 * - coredump.fcd
 *   Core dump itself
 *
 * - bootcore_prev_trace.bin
 *   Previous bootloader trace. Useful to debug a bootloader issue.
 *
 * - bootcore_trace.bin
 *   Current bootloader trace created while uploading the dump. Useful to debug dump related issues
 *   up until the point where this file itself is uploaded
 */
static char *read_dump(char *dev_node, char *fw, const char *dump_folder, const char *log_file)
{
    const char *info_file = "report.json";
    const char *dump_files[] = { "coredump.fcd", "bootcore_prev_trace.bin", "bootcore_trace.bin" };
    char error_buffer[ERR_BUFFER_SIZE] = { '\0' };
    char *files = NULL;

    ASSERT(dev_node);
    ASSERT(fw);
    ASSERT(dump_folder);
    ASSERT(log_file);

    delete_dump_files(dump_folder, info_file, dump_files, ARRAY_SIZE(dump_files));

    if (!boot_modem_flashing_mode(error_buffer, dev_node, fw, log_file, dump_folder, true)) {
        struct tm tmp;
        time_t now = time(NULL);
        struct tm *local = localtime_r(&now, &tmp);
        ASSERT(local != NULL);
        char info_path[128];
        char dump_path[128];
        set_name(local, dump_folder, "info", "json", info_path, sizeof(info_path));
        set_name(local, dump_folder, "modem", "tgz", dump_path, sizeof(dump_path));

        crm_ifwd_tgz_create(dump_folder, ARRAY_SIZE(dump_files), dump_files, dump_path);

        char file[128];
        snprintf(file, sizeof(file), "%s/%s", dump_folder, info_file);
        ASSERT(!rename(file, info_path)); // file is very small, no need to compress it

        // @TODO: remove this once CRM runs as system
        struct passwd *pwd = getpwnam("system");
        struct group *gp = getgrnam("radio");
        ASSERT(pwd && gp);
        ASSERT(!chown(info_path, pwd->pw_uid, gp->gr_gid));
        ASSERT(!chown(dump_path, pwd->pw_uid, gp->gr_gid));

        size_t len = strlen(info_path) + strlen(dump_path) + 3;
        files = malloc(sizeof(char) * len);
        ASSERT(files);
        snprintf(files, len, "%s;%s;", info_path, dump_path);
    }

    cleanup(error_buffer, log_file);
    delete_dump_files(dump_folder, info_file, dump_files, ARRAY_SIZE(dump_files));

    return files;
}
#endif

/**
 * @see crm_ifwd.h
 */
int crm_ifwd_write_firmware(char *dev_node, char *fw, const char *log_file)
{
#ifdef HOST_BUILD
    (void)dev_node;
    (void)fw;
    (void)log_file;
    return 0;
#else
    return write_fw(dev_node, fw, log_file);
#endif
}

/**
 * @see crm_ifwd.h
 */
char *crm_ifwd_read_dump(char *dev_node, char *fw, const char *dump_path, const char *log_file)
{
#ifdef HOST_BUILD
    (void)dev_node;
    (void)fw;
    (void)dump_path;
    (void)log_file;
    return NULL;
#else
    return read_dump(dev_node, fw, dump_path, log_file);
#endif
}

/**
 * @see crm_ifwd.h
 */
void crm_ifwd_package(const char *fw_path, const char *injected_fw, const char *nvm_folder)
{
#ifdef HOST_BUILD
    (void)fw_path;
    (void)injected_fw;
    (void)nvm_folder;
#else
    package(fw_path, injected_fw, nvm_folder);
#endif
}
