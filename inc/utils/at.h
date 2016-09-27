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

#ifndef __CRM_UTILS_AT_CMD_HEADER__
#define __CRM_UTILS_AT_CMD_HEADER__

/**
 * Sends an AT command and waits for its answer or timeout
 *
 * @param [in] fd       Valid file descriptor
 * @param [in] tag      Log tag
 * @param [in] at_cmd   At command to send. Must \0 terminated (without \r\n)
 * @param [in] timeout  in milliseconds
 * @param [in] fd_abort File descriptor used to abort this function
 *
 * @return 0 if AT command is sent successfully
 * @return -1 in case of error
 * @return -2 if aborted
 */
int crm_send_at(int fd, const char *tag, const char *at_cmd, int timeout, int fd_abort);

#endif
