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

#ifndef __CRM_HAL_COMMON_REQUEST_HEADER__
#define __CRM_HAL_COMMON_REQUEST_HEADER__

#include "plugins/hal.h"

/**
 * @see hal.h
 */
void hal_power_on(crm_hal_ctx_t *ctx);

/**
 * @see request.h
 */
void hal_boot(crm_hal_ctx_t *ctx);

/**
 * @see hal.h
 */
void hal_shutdown(crm_hal_ctx_t *ctx);

/**
 * @see hal.h
 */
void hal_reset(crm_hal_ctx_t *ctx, crm_hal_reset_type_t type);

#endif /* __CRM_HAL_COMMON_REQUEST_HEADER__ */
