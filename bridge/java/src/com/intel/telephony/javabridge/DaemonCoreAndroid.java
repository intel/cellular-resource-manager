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

import java.net.Socket;
import java.util.ArrayList;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import android.util.Log;
import android.os.PowerManager;
import android.os.UserHandle;
import android.content.Context;
import android.content.Intent;
import android.content.ComponentName;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;

public class DaemonCoreAndroid implements DaemonCore {
    static final private String LOG_TAG = "TelJBridgeT";
    static final private String C_DAEMON_SOCKET_NAME = "tel_jvb_java";

    private PowerManager.WakeLock mWakelock;
    private Context mContext;

    public class SocketAndroid implements SocketWrapper {
        private LocalSocket mSocket;
        private boolean mConnected;

        SocketAndroid() throws IOException {
            LocalSocket s = null;

            try {
                s = new LocalSocket();
                LocalSocketAddress l = new LocalSocketAddress(C_DAEMON_SOCKET_NAME,
                                                              LocalSocketAddress.Namespace.RESERVED);
                s.connect(l);
                mSocket = s;
                mConnected = true;
            } catch (IOException e) {
                if (s != null) {
                    try {
                        s.close();
                    } catch (IOException e2) {
                        /* Ignore */
                    }
                }
                throw e;
            }
        }

        public boolean isConnected() {
            return mConnected;
        }

        public void close() throws IOException {
            mConnected = false;
            mSocket.close();
        }

        public InputStream getInputStream() throws IOException {
            return mSocket.getInputStream();
        }

        public OutputStream getOutputStream() throws IOException {
            return mSocket.getOutputStream();
        }
    }

    public SocketWrapper getSocket() throws IOException {
        return new SocketAndroid();
    }

    public void log(String l) {
        Log.d(LOG_TAG, l);
    }

    public void disconnected() {
        mWakelock.release();
    }

    public void wakelockAcquire() {
        mWakelock.acquire(300000); // Put a 5 minutes time limit on the wakelock
    }

    public void wakelockRelease() {
        mWakelock.release();
    }

    public void broadcastIntent(String intentName, ArrayList<Object> params) {
        Intent intent = new Intent(intentName);
        int idx = 0;

        while ((idx + 1) < params.size()) {
            String name = (String)params.get(idx);
            Object param = params.get(idx + 1);
            if (param instanceof java.lang.String) {
                intent.putExtra(name, (String)param);
            } else {
                intent.putExtra(name, (Integer)param);
            }
            idx += 2;
        }
        try {
            mContext.sendBroadcastAsUser(intent, new UserHandle(UserHandle.USER_ALL));
        } catch (Throwable e) {
            Log.d(LOG_TAG, "Failed to send broadcast intent, exception " + e);
        }
    }

    public void startService(String service_pkg, String service_class) {
        Intent intent = new Intent();

        intent.setComponent(new ComponentName(service_pkg, service_class));
        try {
            ComponentName name = mContext.startServiceAsUser(intent,
                                                             new UserHandle(UserHandle.USER_OWNER));
            if (name == null) {
                Log.d(LOG_TAG, "Failed to start component " + service_pkg + "/" + service_class);
            }
        } catch (Throwable e) {
            Log.d(LOG_TAG, "Failed to start component " + service_pkg + "/" + service_class +
                  ", exception " + e);
        }
    }

    DaemonCoreAndroid(Context context) {
        mContext = context;
        PowerManager pm = (PowerManager)context.getSystemService(Context.POWER_SERVICE);
        mWakelock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "TelephonyBridge");
        mWakelock.setReferenceCounted(false);
    }
}
