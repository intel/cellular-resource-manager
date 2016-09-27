/*
 * Copyright (C) Intel 2016
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
package com.intel.telephony.javabridge;

import android.app.Service;
import android.util.Log;
import android.content.Intent;
import android.os.IBinder;
import android.os.Bundle;
import android.content.Context;

public class BridgeService extends Service {
    static final private String LOG_TAG = "TelJBridgeS";

    private DaemonSocket mSocket;

    @Override
    public void onDestroy() {
        super.onDestroy();
        Log.d(LOG_TAG, "onDestroy");
        mSocket.serviceStopped();
    }

    @Override
    public void onCreate() {
        Log.d(LOG_TAG, "onCreate");
        DaemonCoreAndroid core = new DaemonCoreAndroid(this);
        mSocket = new DaemonSocket(core);
        mSocket.start();
    }

    @Override
    public IBinder onBind(Intent intent) {
        Log.d(LOG_TAG, "onBind");
        return null;
    }
}
