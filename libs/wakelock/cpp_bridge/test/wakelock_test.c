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

#include <stdlib.h>
#include <stdio.h>
#include "utils/wakelock.h"

#define CRM_MODULE_TAG "WAKET"
#include "utils/common.h"
#include "utils/logs.h"

#include <unistd.h>

int main()
{
    crm_wakelock_t *wakelock = crm_wakelock_init("test app");

    KLOG("start");

    KLOG("Checking initial state...");
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == false);
    ASSERT(wakelock->is_held(wakelock) == false);

    KLOG("first module acquires a first time");
    wakelock->acquire(wakelock, 0);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == true);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == false);
    ASSERT(wakelock->is_held(wakelock) == true);

    KLOG("first module acquires a second time");
    wakelock->acquire(wakelock, 0);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == true);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == false);
    ASSERT(wakelock->is_held(wakelock) == true);

    KLOG("first module releases a first time");
    wakelock->release(wakelock, 0);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == true);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == false);
    ASSERT(wakelock->is_held(wakelock) == true);

    KLOG("first module releases a second time");
    wakelock->release(wakelock, 0);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == false);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == false);
    ASSERT(wakelock->is_held(wakelock) == false);

    KLOG("wakelock acquired by two modules");
    wakelock->acquire(wakelock, 0);
    wakelock->acquire(wakelock, 1);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == true);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == true);
    ASSERT(wakelock->is_held(wakelock) == true);

    KLOG("wakelock released by first module");
    wakelock->release(wakelock, 0);
    ASSERT(wakelock->is_held_by_module(wakelock, 0) == false);
    ASSERT(wakelock->is_held_by_module(wakelock, 1) == true);
    ASSERT(wakelock->is_held(wakelock) == true);

    KLOG("disposing...");
    wakelock->acquire(wakelock, 0);
    wakelock->acquire(wakelock, 1);
    wakelock->acquire(wakelock, 2);
    wakelock->dispose(wakelock);

    KLOG("success");
    return 0;
}
