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

#ifndef __CRM_WAKELOCK_CLASS_HEADER__
#define __CRM_WAKELOCK_CLASS_HEADER__

#include "CrmWakelockServiceBase.hpp"

#include <map>
#include <mutex>

typedef std::map<int, int> CounterMap;

class CrmWakelockMux
{
public:

    CrmWakelockMux(const char *name);
    virtual ~CrmWakelockMux();

    /**
     * Acquires the wakelock
     *
     * @param [in] moduleId module identifier (>= 0)
     */
    void acquire(int moduleId);

    /**
     * Releases the wakelock if all modules have released the wakelock
     *
     * @param [in] moduleId module identifier (>= 0)
     */
    void release(int moduleId);

    /**
     * Checks if wakelock is hold by the module
     *
     * @param [in] moduleId module identifier (>= 0)
     *
     * @return true if wakelock is hold
     */
    bool isHeld(int moduleId) const;

    /**
     * Checks if at least one module holds a wakelock
     *
     * @return true if wakelock is hold
     */
    bool isHeld() const;

private:
    CounterMap  _counterMap;
    CrmWakelockServiceBase *_wakelock;

    std::mutex _mutex;

    void increaseCounter(int moduleId);
    void decreaseCounter(int moduleId);
};
#endif /** __CRM_WAKELOCK_CLASS_HEADER__ */
