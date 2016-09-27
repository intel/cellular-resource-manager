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

#ifndef __CRM_HAL_SOFIA_MODEM_HEADER__
#define __CRM_HAL_SOFIA_MODEM_HEADER__

#include "common.h"

#ifdef HOST_BUILD
#define TIMEOUT_MDM_OFF 1000
#define TIMEOUT_MDM_ON  2000
#else
#define TIMEOUT_MDM_OFF  100
#define TIMEOUT_MDM_ON  5000 // Note: could be reduced once system issues have been fixed
#endif

/**
 * Returns the file descriptor that needs to be polled (READ) to receive a message notification
 * This file descriptor can only be used to receive events. It cannot be used to read, write.
 *
 * @param [in] host_socket_name Socket path. Used in HOST debug mode only
 *
 * @return file descriptor
 * @return -1 in case of error
 */
int crm_hal_get_poll_mdm_fd(const char *host_socket_name);

/**
 * Returns the modem state. Must be called when an event is notified by the polled FD
 *
 * @return -1 if the event is not modem related
 */
int crm_hal_get_mdm_state(crm_hal_ctx_internal_t *i_ctx);

/**
 * Sends a PING AT command to check if modem is running. This function notifies EV_MDM_RUN or
 * EV_TIMEOUT
 *
 * @param [in] thread_ctx Context of the thread
 * @param [in] param      Thread parameters
 */
void *crm_hal_ping_modem_thread(crm_thread_ctx_t *thread_ctx, void *param);

/**
 * Initializes the modem. This function stops the modem and flush the VMODEM event.
 * Must be called at CRM boot
 *
 * @param [in] i_ctx HAL context
 * @param [in] force_stop true to force modem to be stopped whatever the TCS configuration
 */
void crm_hal_init_modem(crm_hal_ctx_internal_t *i_ctx, bool force_stop);

/**
 * Starts the modem
 *
 * @param [in] i_ctx HAL context
 *
 * @return 0 if successful, -1 otherwise
 */
int crm_hal_start_modem(crm_hal_ctx_internal_t *i_ctx);

/**
 * Stops the modem
 *
 * @param [in] i_ctx HAL context
 *
 * @return 0 if successful, -1 otherwise
 */
int crm_hal_stop_modem(crm_hal_ctx_internal_t *i_ctx);

#endif /* __CRM_HAL_SOFIA_MODEM_HEADER__ */
