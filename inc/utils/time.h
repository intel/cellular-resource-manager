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

#ifndef __CRM_UTILS_TIME_HEADER__
#define __CRM_UTILS_TIME_HEADER__

#include <time.h>

/**
 * Gets current time and add ms to it
 *
 * @param [out] timer_end returned value equal to current time + ms
 * @param [in] ms         time in ms
 */
void crm_time_add_ms(struct timespec *timer_end, int ms);

/**
 * Returns remaining time between current time and timer_end
 *
 * @param [in] timer_end
 *
 * @return remaining time in ms. If remaining time is less than 0, returned value is 0
 */
int crm_time_get_remain_ms(const struct timespec *timer_end);

#endif /* __CRM_UTILS_TIME_HEADER__ */
