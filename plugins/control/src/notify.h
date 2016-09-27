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

#ifndef __CRM_CONTROL_NOTIFY_HEADER__
#define __CRM_CONTROL_NOTIFY_HEADER__

#include "plugins/control.h"

/**
 * @see control.h
 */
void notify_hal_event(crm_ctrl_ctx_t *ctx, const crm_hal_evt_t *event);

/**
 * @see control.h
 */
void notify_nvm_status(crm_ctrl_ctx_t *ctx, int status);

/**
 * @see control.h
 */
void notify_fw_upload_status(crm_ctrl_ctx_t *ctx, int status);

/**
 * @see control.h
 */
void notify_customization_status(crm_ctrl_ctx_t *ctx, int status);

/**
 * @see control.h
 */
void notify_dump_status(crm_ctrl_ctx_t *ctx, int status);

/**
 * @see control.h
 */
void notify_client(crm_ctrl_ctx_t *ctx, mdm_cli_event_t evt_id, size_t data_size, const void *data);

#endif /* __CRM_CONTROL_NOTIFY_HEADER__ */
