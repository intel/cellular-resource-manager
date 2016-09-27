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

#ifndef __CRM_IFWD_HEADER__
#define __CRM_IFWD_HEADER__

/**
 * Packages modem firmware with NVM data
 *
 * @param [in] fw_path     Full path of the modem firmware
 * @param [in] injected_fw Full path of the injected modem firmware
 * @param [in] nvm_folder  Path of NVM folder
 */
void crm_ifwd_package(const char *fw_path, const char *injected_fw, const char *nvm_folder);

/**
 * Writes modem firmware
 *
 * @param [in] dev_node Node used to write the firmware
 * @param [in] fw       Full path of the injected modem firmware
 * @param [in] log_file Path of the log file. Shall be '\0' to disable the log
 *
 * @return 0 if successful
 */
int crm_ifwd_write_firmware(char *dev_node, char *fw, const char *log_file);

/**
 * Reads the core dump
 *
 * @param [in] dev_node  Node used to write the firmware
 * @param [in] fw        Full path of the injected modem firmware
 * @param [in] dump_path Folder used to store the dump files
 * @param [in] log_file  Path of the log file. Shall be '\0' to disable the log
 *
 * @return list of files if successful. files are separated by a ';'. Pointer must be freed by
 * caller
 * @return NULL otherwise
 */
char *crm_ifwd_read_dump(char *dev_node, char *fw, const char *dump_path, const char *log_file);

#endif /*__CRM_IFWD_HEADER__ */
