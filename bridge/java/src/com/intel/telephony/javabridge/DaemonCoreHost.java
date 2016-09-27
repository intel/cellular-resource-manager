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

public class DaemonCoreHost implements DaemonCore {
    public class SocketHost implements SocketWrapper {
        Socket mSocket;
        boolean mConnected;

        SocketHost() throws IOException {
            mSocket = new Socket("localhost", 1703);
            mConnected = true;
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
        return new SocketHost();
    }

    public void log(String l) {
        System.out.println(l);
    }

    public void disconnected() {
    }

    public void wakelockAcquire() {
    }

    public void wakelockRelease() {
    }

    public void broadcastIntent(String intentName, ArrayList<Object> params) {
    }

    public void startService(String service_pkg, String service_class) {
    }
}
