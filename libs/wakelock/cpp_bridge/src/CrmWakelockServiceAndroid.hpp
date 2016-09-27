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

#ifndef __CRM_WAKELOCK_ANDROID_HEADER__
#define __CRM_WAKELOCK_ANDROID_HEADER__

#include "CrmWakelockServiceBase.hpp"

#include <binder/IBinder.h>
#include <utils/Mutex.h>

namespace android
{
class IPowerManager;
}

class CrmWakelockServiceAndroid : public android::RefBase, public CrmWakelockServiceBase
{
public:
    CrmWakelockServiceAndroid(const char *name);
    ~CrmWakelockServiceAndroid();

    void acquire();
    void release();

    /**
     * Interface for receiving a callback when the process hosting an IBinder has gone away.
     */
    class PMDeathRecipient : public android::IBinder::DeathRecipient
    {
    public:
        PMDeathRecipient(CrmWakelockServiceAndroid &thread) : _thread(thread) {}
        virtual ~PMDeathRecipient() {}

        // IBinder::DeathRecipient
        virtual void binderDied(const android::wp<android::IBinder> &who);

    private:
        PMDeathRecipient(const PMDeathRecipient &);
        PMDeathRecipient &operator=(const PMDeathRecipient &);

        CrmWakelockServiceAndroid &_thread;
    };

private:
    /** Prevents concurrent access to acquire() and release() */
    android::Mutex _lock;

    /* mapped with PowerManager.java */
    const int PARTIAL_WAKE_LOCK = 0x00000001;

    /** Name of wakelock's package */
    const android::String16 _pkg;

    /** Tag of wakelock's owner */
    const android::String16 _tag;

    bool _toAcquire;
    bool _connecting;

    /** Callback object if the binder goes away */
    const android::sp<PMDeathRecipient> _deathRecipient;

    /** Reference to power manager */
    android::sp<android::IPowerManager> _powerManager;
    /** This token uniquely identifies the wakelock */
    android::sp<android::IBinder> _suspendBlockToken;

    /**
     * Resets wakelocks when PowerManagerservice has died
     */
    void clearPowerManager();

    void acquireWakelockWithLock();
    void releaseWakelockWithLock();
    void connectAndAcquire();
    void startConnectThreadWithLock();
};
#endif /* __CRM_WAKELOCK_ANDROID_HEADER__ */
