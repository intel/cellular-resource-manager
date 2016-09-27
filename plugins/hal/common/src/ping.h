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

#ifndef __CRM_HAL_COMMON_PING_HEADER__
#define __CRM_HAL_COMMON_PING_HEADER__

#include <stdbool.h>

/**
 * Sends PING request to modem every 500ms until modem responds or timeout
 * Function can be aborted by writing in fd_abort file descriptor
 *
 * @param [in] ping_node    Node to be used
 * @param [in] ping_timeout in milliseconds
 * @param [in] fd_abort     File descriptor used to abort this function
 * @param [in] is_tty       Set to true if ping_node points to a TTY instead of a char device
 *
 * @return >=0 in case of success. Opened file descriptor is returned
 * @return -1 in case of error
 * @return -2 if aborted
 */
int crm_hal_ping_modem(const char *ping_node, int ping_timeout, int fd_abort, bool is_tty);

#endif /* __CRM_HAL_COMMON_PING_HEADER__ */
