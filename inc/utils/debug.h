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

#ifndef __CRM_UTILS_DEBUG_HEADER__
#define __CRM_UTILS_DEBUG_HEADER__

#include <stdbool.h>

/**
 * Checks if sanity test mode is enabled. This mode is enabled if property
 * CRM_KEY_DBG_SANITY_TEST_MODE is set to true and CRM runs in an eng or userdebug build.
 *
 * @return true if sanity test mode is enabled
 */
bool crm_is_in_sanity_test_mode(void);

#endif /* __CRM_UTILS_DEBUG_HEADER__ */
