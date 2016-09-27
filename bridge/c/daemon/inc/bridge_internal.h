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

#ifndef __TEL_JAVA_BRIDGE_INTERNAL_HEADER__
#define __TEL_JAVA_BRIDGE_INTERNAL_HEADER__

/*
 * This file is common to the bridge and client library implementation and defines the wire
 * protocol used to communicate through the various sockets.
 *
 * Note that the format of the message between the C clients and the bridge and the bridge
 * and the Java application is the same.
 *
 * Those constants need to be aligned with the constants defined in DaemonSocket.java file.
 *
 * Message format:
 *   = uint32_t message id: for bridge <=> daemon handshake. Only present in C => Java
 *   = uint32_t message_size: size of the message excluding header
 *   = uint32_t message_type: type from tel_bridge_commands_t enumerate
 *   = message content in TLV format (actually LTV :) ):
 *        = uint32_t data_size
 *        = uint32_t data_type from tel_bridge_data_type_t enumerate
 *        = data
 */
typedef enum tel_bridge_commands {
    TEL_BRIDGE_COMMAND_WAKELOCK_ACQUIRE,
    TEL_BRIDGE_COMMAND_WAKELOCK_RELEASE,
    TEL_BRIDGE_COMMAND_START_SERVICE,
    TEL_BRIDGE_COMMAND_BROADCAST_INTENT,
    TEL_BRIDGE_COMMAND_NUM
} tel_bridge_commands_t;

typedef enum tel_bridge_data_types {
    TEL_BRIDGE_DATA_TYPE_INT,
    TEL_BRIDGE_DATA_TYPE_STRING,
    TEL_BRIDGE_DATA_TYPE_NUM
} tel_bridge_data_type_t;

#define BRIDGE_SOCKET_C     "tel_jvb_c"      // Socket used by users of the library
#define BRIDGE_SOCKET_JAVA  "tel_jvb_java"   // Socket used by the Java application

#endif /* __TEL_JAVA_BRIDGE_INTERNAL_HEADER__ */
