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

#ifndef __CRM_TEST_UTILS_HEADER__
#define __CRM_TEST_UTILS_HEADER__

#include <unistd.h>
#include "libtcs2/tcs.h"

#define MDM_STUB_SOFIA_CTRL "/tmp/crm_sofia_modem_control"

/**
 * Starts stub modem sofia as a new process
 *
 * @param [in] tcs TCS context
 *
 * @return pid
 */
pid_t CRM_TEST_start_stub_sofia_mdm(tcs_ctx_t *tcs);

/**
 * Waits for stub modem sofia readiness
 *
 * @param [in] c_fd Control socket file descriptor
 */
void CRM_TEST_wait_stub_sofia_mdm_readiness(int c_fd);

/**
 * Opens a socket
 *
 * @param [in] name Name of the socket
 *
 * @return valid file descriptor
 */
int CRM_TEST_connect_socket(const char *name);

/**
 * Creates a socket which can be opened with Android helpers
 *
 * @param [in] name Name of the socket
 */
void CRM_TEST_get_control_socket_android(const char *name);

/**
 * Configure TCS to select the CRM HOST configuration files
 *
 * @param [in] name    Name of TCS configuration
 * @param [in] inst_id Instance ID
 *
 * @return a valid TCS context
 */
tcs_ctx_t *CRM_TEST_tcs_init(const char *name, int inst_id);

#endif /* __CRM_TEST_UTILS_HEADER__ */
