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

#define CRM_MODULE_TAG "UTILS"
#include "utils/common.h"
#include "utils/time.h"

/**
 * @see time.h
 */
void crm_time_add_ms(struct timespec *timer_end, int ms)
{
    ASSERT(clock_gettime(CLOCK_BOOTTIME, timer_end) == 0);
    timer_end->tv_sec += ms / 1000;
    timer_end->tv_nsec += (ms % 1000) * 1000000;

    if (timer_end->tv_nsec > 1000000000) {
        timer_end->tv_sec++;
        timer_end->tv_nsec -= 1000000000;
    }
}

/**
 * @see time.h
 */
int crm_time_get_remain_ms(const struct timespec *timer_end)
{
    struct timespec current;

    ASSERT(clock_gettime(CLOCK_BOOTTIME, &current) == 0);

    if (current.tv_sec > timer_end->tv_sec ||
        (current.tv_sec == timer_end->tv_sec && current.tv_nsec > timer_end->tv_nsec))
        return 0;
    else
        return ((timer_end->tv_sec - current.tv_sec) * 1000) +
               ((timer_end->tv_nsec - current.tv_nsec) / 1000000);
}
