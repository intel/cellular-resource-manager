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

#ifndef __CRM_HAL_COMMON_RPCD_HEADER__
#define __CRM_HAL_COMMON_RPCD_HEADER__

#include "common.h"

#define TIMEOUT_RPCD_START 5000

/**
 * Initializes the RPCD handling module. Must be called at CRM boot
 *
 * @param [in] i_ctx HAL context
 */
void crm_hal_rpcd_init(crm_hal_ctx_internal_t *i_ctx);

/**
 * Starts the RPC Daemon
 *
 * @param [in] i_ctx HAL context
 */
void crm_hal_rpcd_start(crm_hal_ctx_internal_t *i_ctx);

/**
 * Stops the RPC Daemon
 *
 * @param [in] i_ctx HAL context
 */
void crm_hal_rpcd_stop(crm_hal_ctx_internal_t *i_ctx);

/**
 * Returns the file descriptor that needs to be polled (READ) to receive notification
 *
 * @param [in] i_ctx HAL context
 *
 * @return file descriptor
 * @return -1 in case of error
 */
int crm_hal_rpcd_get_fd(crm_hal_ctx_internal_t *i_ctx);

/**
 * Needs to be called when an event on the RPCD file descriptor has been detected by
 * main loop code.
 *
 * @param [in] i_ctx HAL context
 *
 * @return event to sent to HAL state machine
 * @return -1 in case event is not related to RPCD handling
 *
 */
int crm_hal_rpcd_event(crm_hal_ctx_internal_t *i_ctx);

/**
 * Disposes the RPCD handling module.
 *
 * @param [in] i_ctx HAL context
 */
void crm_hal_rpcd_dispose(crm_hal_ctx_internal_t *i_ctx);


#endif /* __CRM_HAL_COMMON_RPCD_HEADER__ */
