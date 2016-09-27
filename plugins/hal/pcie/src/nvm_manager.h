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

#ifndef __CRM_HEADER_HAL_NVM_MANAGER__
#define __CRM_HEADER_HAL_NVM_MANAGER__

#include "common.h"
#include "daemons.h"

/**
 * @see daemons.h
 */
int crm_hal_nvm_daemon_cb(int id, void *ctx, crm_hal_daemon_evt_t evt, int msg_id, size_t msg_len);

/**
 * This function should be called when the modem is UP and ready to do AT commands
 *
 * @param [in] i_ctx   PCIe HAL context
 */
void crm_hal_nvm_on_modem_up(crm_hal_ctx_internal_t *i_ctx);

/**
 * This function should be called before shutting down the modem
 *
 * @param [in] i_ctx   PCIe HAL context
 */
void crm_hal_nvm_on_modem_down(crm_hal_ctx_internal_t *i_ctx);

/**
 * This function should be called on NVM server crash (i.e. when it does not answer to CRM commands)
 *
 * @param [in] i_ctx   PCIe HAL context
 */
void crm_hal_nvm_on_manager_crash(crm_hal_ctx_internal_t *i_ctx);

#endif /* __CRM_HEADER_HAL_NVM_MANAGER__ */
