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

#define CRM_MODULE_TAG "WAKE"
#include "utils/common.h"

#include "CrmWakelockMux.hpp"

#ifndef STUB_BUILD
#include "CrmWakelockServiceAndroid.hpp"
#endif

CrmWakelockMux::CrmWakelockMux(const char  *name)
{
#ifdef STUB_BUILD
    (void)name;
    _wakelock = new CrmWakelockServiceBase();
#else
    _wakelock = new CrmWakelockServiceAndroid(name);
#endif
}

CrmWakelockMux::~CrmWakelockMux()
{
    _counterMap.clear();
    delete _wakelock;
}

bool CrmWakelockMux::isHeld(int moduleId) const
{
    bool hold = false;
    ASSERT(moduleId >= 0);

    CounterMap::const_iterator it = _counterMap.find(moduleId);
    if (it != _counterMap.end()) {
        hold = (it->second > 0);
    }

    return hold;
}

bool CrmWakelockMux::isHeld() const
{
    bool hold = false;

    for (CounterMap::const_iterator it = _counterMap.begin(); it != _counterMap.end(); it++) {
        if (it->second > 0) {
            hold = true;
            break;
        }
    }

    return hold;
}

void CrmWakelockMux::increaseCounter(int moduleId)
{
    CounterMap::iterator it = _counterMap.find(moduleId);

    if (it != _counterMap.end()) {
        it->second++;
    } else {
        _counterMap.insert(std::pair<int, int>(moduleId, 1));
    }
}

void CrmWakelockMux::decreaseCounter(int moduleId)
{
    CounterMap::iterator it = _counterMap.find(moduleId);

    if (it != _counterMap.end()) {
        it->second--;
        ASSERT(it->second >= 0);
    } else {
        ASSERT(0);
    }
}

void CrmWakelockMux::acquire(int moduleId)
{
    ASSERT(moduleId >= 0);

    _mutex.lock();

    if (!isHeld()) {
        _wakelock->acquire();
    }

    increaseCounter(moduleId);

    _mutex.unlock();
}

void CrmWakelockMux::release(int moduleId)
{
    _mutex.lock();

    ASSERT(moduleId >= 0);
    decreaseCounter(moduleId);

    if (!isHeld()) {
        _wakelock->release();
    }

    _mutex.unlock();
}
