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

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.IOException;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.util.ArrayList;

public class DaemonSocket extends Thread {
    public enum State {
        IN_HDR, IN_MSG
    };

    static final private int CMD_WAKELOCK_ACQUIRE = 0;
    static final private int CMD_WAKELOCK_RELEASE = 1;
    static final private int CMD_START_SERVICE = 2;
    static final private int CMD_BROADCAST_INTENT = 3;

    private DaemonCore mCore;
    private DaemonCore.SocketWrapper mSocket;
    private boolean mServiceStopped;

    class WireException extends Exception {
        public WireException(String message) {
            super(message);
        }
    }

    class WireMsg {
        private static final int DATA_TYPE_INT = 0;
        private static final int DATA_TYPE_STRING = 1;

        private char mBuf[];
        private int mBufPos;
        private int mBufLen;
        private DaemonCore mCore;

        public WireMsg(DaemonCore core, int size) {
            mBuf = new char[size];
            mBufLen = size;
            mBufPos = 0;
            mCore = core;
        }

        public boolean appendData(BufferedReader reader) throws IOException {
            int len = reader.read(mBuf, mBufPos, mBufLen - mBufPos);

            if (len <= 0) {
                throw new IOException();
            }
            mBufPos += len;
            boolean ret = mBufPos == mBufLen;
            if (ret) {
                mBufPos = 0;
            }
            return ret;
        }

        public int getInt() throws WireException {
            try {
                int ret = (int)mBuf[mBufPos + 0] << 24 |
                          (int)mBuf[mBufPos + 1] << 16 |
                          (int)mBuf[mBufPos + 2] << 8 |
                          (int)mBuf[mBufPos + 3] << 0;

                mBufPos += 4;
                return ret;
            } catch (IndexOutOfBoundsException e) {
                throw new WireException("Not enough data to read a int " +
                                        mBufPos + ", " + mBufLen);
            }
        }

        public Object getSerializedType() throws IOException, WireException {
            int size = getInt();
            int type = getInt();

            if (type == DATA_TYPE_INT) {
                if (size != 4) {
                    throw new WireException("Data type is not 4 bytes");
                }
                return new Integer(getInt());
            } else if (type == DATA_TYPE_STRING) {
                try {
                    String ret = new String(mBuf, mBufPos, size);
                    mBufPos += size;
                    return ret;
                } catch (IndexOutOfBoundsException e) {
                    throw new WireException("Not enough data to read a int " +
                                            mBufPos + ", " + size + ", " + mBufLen);
                }
            } else {
                throw new WireException("Invalid data type " + type);
            }
        }

        public boolean isEof() {
            return mBufPos == mBufLen;
        }

        public void sendAck(BufferedWriter writer, int ack) throws IOException {
            mCore.log("Message read, sending reply " + ack);
            writer.write(mBuf, 0, 4);
            writer.flush();
        }

        public void reset() {
            mBufPos = 0;
        }
    }

    public void serviceStopped() {
        try {
            if (mSocket != null) {
                mSocket.close();
            }
        } catch (IOException e) {
            /* Ignore */
        }
        mServiceStopped = true;
    }

    @Override
    public void run() {
        int retryCount = 0;
        int retryDelay = 10;

        while (!mServiceStopped) {
            /* First, code trying to open the socket */
            try {
                mSocket = mCore.getSocket();

                /* Reset retry counters on connection success */
                retryDelay = 10;
                retryCount = 0;
            } catch (IOException e) {
                if ((retryCount % 10) == 0) {
                    mCore.log("Cannot open socket, retrying...");
                }
                retryCount += 1;
            }

            /* Then handling the socket messages until an error */
            while (mSocket != null && mSocket.isConnected() && !mServiceStopped) {
                try {
                    InputStream inputStream = mSocket.getInputStream();
                    BufferedReader bufferedReader =
                        new BufferedReader(new InputStreamReader(inputStream));

                    OutputStream outputStream = mSocket.getOutputStream();
                    BufferedWriter bufferedWriter =
                        new BufferedWriter(new OutputStreamWriter(outputStream));

                    State state = State.IN_HDR;
                    WireMsg msgHdr = new WireMsg(mCore, 12);
                    WireMsg msgContent = new WireMsg(mCore, 0);
                    int msgType = 0;
                    int msgSize = 0;
                    int msgAck = 0;
                    while (true) {
                        if (state == State.IN_HDR) {
                            if (msgHdr.appendData(bufferedReader)) {
                                msgAck = msgHdr.getInt();
                                msgSize = msgHdr.getInt();
                                msgType = msgHdr.getInt();

                                if (msgSize > 0) {
                                    state = State.IN_MSG;
                                    msgContent = new WireMsg(mCore, msgSize);
                                } else {
                                    if (msgType == CMD_WAKELOCK_ACQUIRE) {
                                        mCore.log("Msg received: WAKELOCK_ACQUIRE");
                                        mCore.wakelockAcquire();
                                    } else if (msgType == CMD_WAKELOCK_RELEASE) {
                                        mCore.log("Msg received: WAKELOCK_RELEASE");
                                        mCore.wakelockRelease();
                                    } else {
                                        throw new WireException("Bad message type for a 0-size message " + msgType);
                                    }
                                    msgHdr.sendAck(bufferedWriter, msgAck);
                                    msgHdr.reset();
                                }
                            }
                        } else {
                            if (msgContent.appendData(bufferedReader)) {
                                if (msgType == CMD_START_SERVICE) {
                                    String servicePkg = (String)msgContent.getSerializedType();
                                    String serviceClass = (String)msgContent.getSerializedType();
                                    mCore.log("Msg received: START_SERVICE(" + servicePkg + "," + serviceClass + ")");
                                    mCore.startService(servicePkg, serviceClass);
                                } else if (msgType == CMD_BROADCAST_INTENT) {
                                    String intentName = (String)msgContent.getSerializedType();
                                    String debug = "Msg received: BROADCAST_INTENT(" + intentName + ":";
                                    ArrayList<Object> params = new ArrayList<Object>(0);
                                    while (!msgContent.isEof()) {
                                        String paramName = (String)msgContent.getSerializedType();
                                        params.add(paramName);
                                        debug += paramName + "=";
                                        Object p = msgContent.getSerializedType();
                                        params.add(p);
                                        if (p instanceof java.lang.String) {
                                            String s = (String)p;
                                            debug += "\"";
                                            debug += s;
                                            debug += "\"";
                                        } else {
                                            Integer i = (Integer)p;
                                            debug += i;
                                        }
                                        if (!msgContent.isEof()) {
                                            debug += ",";
                                        }
                                    }
                                    debug = debug + ")";
                                    mCore.log(debug);
                                    mCore.broadcastIntent(intentName, params);
                                } else {
                                    throw new WireException("Bad message type for a non-0-size message " + msgType);
                                }
                                msgHdr.sendAck(bufferedWriter, msgAck);
                                msgHdr.reset();
                                state = State.IN_HDR;
                            }
                        }
                    }
                } catch (WireException e) {
                    mCore.log("Error on wire interface " + e);
                    break;
                } catch (IOException e) {
                    mCore.log("IO error on wire interface " + e);
                    break;
                } catch (Throwable e) {
                    mCore.log("Unknown exception " + e);
                    break;
                }
            }

            /* Then close the socket */
            try {
                if (mSocket != null) {
                    mSocket.close();
                }
            } catch (IOException e) {
                /* Ignore */
            }
            mCore.disconnected();

            /* And do some temporization before trying to reconnect */
            try {
                Thread.sleep(retryDelay);
            } catch (InterruptedException er) {
            }
            retryDelay *= 2;
            if (retryDelay > 500) {
                retryDelay = 500;
            }
        }
    }

    DaemonSocket(DaemonCore core) {
        super();
        mCore = core;
        mServiceStopped = false;
        mSocket = null;
    }
}
