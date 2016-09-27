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
 * Original contributors for this specific feature:
 *  - Patrick Benavoli <patrick.benavoli@intel.com> (design)
 *  - Jean-Christophe Pince <jean-christophe.pince@intel.com> (development)
 *  - Piotr Diop <piotrx.diop@intel.com> (development)
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

#include <powermanager/PowerManager.h>
#include <powermanager/IPowerManager.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <thread>

#define CRM_MODULE_TAG "WAKE"
#include "utils/common.h"
#include "utils/logs.h"

#include "CrmWakelockServiceAndroid.hpp"

using namespace android;

CrmWakelockServiceAndroid::CrmWakelockServiceAndroid(const char *name)
    : _pkg(String16("CRM")),
      _tag(String16(name)),
      _toAcquire(false),
      _connecting(false),
      _deathRecipient(new PMDeathRecipient(*this))
{
    ProcessState::self()->startThreadPool();
}

CrmWakelockServiceAndroid::~CrmWakelockServiceAndroid()
{
    ASSERT(_powerManager != NULL);

    sp<IBinder> binder = IInterface::asBinder(_powerManager);
    ASSERT(binder != NULL);

    release();
    binder->unlinkToDeath(_deathRecipient);
    _powerManager.clear();
}

/* NB: this function must be called with lock hold first */
void CrmWakelockServiceAndroid::acquireWakelockWithLock()
{
    DASSERT(_lock.tryLock() != 0, "This function must be called with mutex hold");
    ASSERT(_powerManager != NULL);

    sp<IBinder> binder = new BBinder();
    ASSERT(binder != NULL);

    int64_t token = IPCThreadState::self()->clearCallingIdentity();
    status_t ret = _powerManager->acquireWakeLock(PARTIAL_WAKE_LOCK, binder, _tag, _pkg);
    IPCThreadState::self()->restoreCallingIdentity(token);

    if (ret == NO_ERROR) {
        _suspendBlockToken = binder;
        LOGV("wakelock ACQUIRED");
    } else {
        LOGE("failure to acquire wakelock, error %d", ret);
        _powerManager.clear();
        startConnectThreadWithLock();
    }
}

void CrmWakelockServiceAndroid::connectAndAcquire()
{
    ASSERT(_powerManager == NULL);

    sp<IBinder> binder = NULL;
    /* A maximum number of retries has been implemented here to avoid deadlock */
    for (int i = 0; i < 200; i++) {
        binder = defaultServiceManager()->checkService(String16("power"));
        if (binder != NULL) {
            break;
        } else {
            usleep(500000);
        }
    }
    ASSERT(binder != NULL);

    Mutex::Autolock _l(_lock);

    _powerManager = interface_cast<android::IPowerManager>(binder);
    ASSERT(_powerManager != NULL);
    binder->linkToDeath(_deathRecipient);

    LOGD("Connected to power manager service");
    _connecting = false;

    if (_toAcquire) {
        acquireWakelockWithLock();
    }
}

void CrmWakelockServiceAndroid::startConnectThreadWithLock()
{
    _connecting = true;
    std::thread connect(&CrmWakelockServiceAndroid::connectAndAcquire, this);
    connect.detach();
}

void CrmWakelockServiceAndroid::acquire()
{
    Mutex::Autolock _l(_lock);

    ASSERT((_suspendBlockToken == NULL) && (_toAcquire == false));
    _toAcquire = true;

    if (_powerManager != NULL) {
        acquireWakelockWithLock();
    } else if (_connecting == false) {
        startConnectThreadWithLock();
    }
}

void CrmWakelockServiceAndroid::releaseWakelockWithLock()
{
    ASSERT(_toAcquire == true);
    _toAcquire = false;

    if (_suspendBlockToken != NULL) {
        ASSERT(_powerManager != NULL);

        int64_t token = IPCThreadState::self()->clearCallingIdentity();
        _powerManager->releaseWakeLock(_suspendBlockToken, 0);
        IPCThreadState::self()->restoreCallingIdentity(token);

        _suspendBlockToken.clear();
        LOGV("wakelock RELEASED");
    }
}

void CrmWakelockServiceAndroid::release()
{
    Mutex::Autolock _l(_lock);
    releaseWakelockWithLock();
}

void CrmWakelockServiceAndroid::PMDeathRecipient::binderDied(const wp<IBinder> &who)
{
    LOGD("Power manager service died, %p", &who);
    _thread.clearPowerManager();
}

void CrmWakelockServiceAndroid::clearPowerManager()
{
    Mutex::Autolock _l(_lock);

    if (_toAcquire) {
        releaseWakelockWithLock();
    }
    _powerManager.clear();
}
