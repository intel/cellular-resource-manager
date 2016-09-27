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

#ifndef __CRM_UTILS_FILES_HEADER__
#define __CRM_UTILS_FILES_HEADER__

#include <stdbool.h>

/**
 *  Writes a string to a file
 *
 * @param [in] path complete file path
 * @param [in] value string to write
 *
 * @return 0 if successful
 * @return -1 if open fails
 */
int crm_file_write(const char *path, const char *value);


/**
 * Reads a string from a file
 *
 * @param [in]  path  complete file path
 * @param [out] value buffer where the string read is stored. The return string is NULL terminated
 * @param [in]  size  Size of value
 *
 * @return 0 if successful
 * @return -1 if open fails
 */
int crm_file_read(const char *path, char *value, size_t size);

/**
 * Checks for file existence.
 *
 * @param [in]  path  complete file path
 *
 * @return true if file exists and is a regular file, false otherwise.
 */
bool crm_file_exists(const char *path);

/**
 * Copies a file from specified source to destination.
 *
 * @param [in] src      Source file to copy.
 * @param [in] dst      Destination file to copy to.
 * @param [in] in_raw   Set to true if source file is in RAW format (GPP partition)
 * @param [in] out_raw  Set to true if destination file is in RAW format (GPP partition)
 * @param [in] dst_mode Mode to give to destination file.
 *
 * @return 0 in case of success, -1 otherwise
 */
int crm_file_copy(const char *src, const char *dst, bool in_raw, bool out_raw, mode_t dst_mode);

#endif /* __CRM_UTILS_FILES_HEADER__ */
