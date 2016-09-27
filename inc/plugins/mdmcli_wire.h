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

#ifndef __CRM_MDMCLI_WIRE_HEADER__
#define __CRM_MDMCLI_WIRE_HEADER__

#include <stdbool.h>

#include "libmdmcli/mdm_cli.h"

/**
 * Direction of the marshalling
 */
typedef enum crm_mdmcli_wire_direction {
    CRM_CLIENT_TO_SERVER,
    CRM_SERVER_TO_CLIENT,
} crm_mdmcli_wire_direction_t;

/**
 * IDs to serialize client requests.
 * Note1: there is no id for the 'disconnect' call as it will simply close the communication socket.
 * Note2: CRM => client messages are directly using mdm_cli_event_t type as an id.
 */
typedef enum crm_mdmcli_wire_req_ids {
    CRM_REQ_REGISTER = MDM_NUM_EVENTS,
    CRM_REQ_REGISTER_DBG, // this request _MUST_ be used only by the test app
    CRM_REQ_ACQUIRE,
    CRM_REQ_RELEASE,
    CRM_REQ_RESTART,
    CRM_REQ_SHUTDOWN,
    CRM_REQ_NVM_BACKUP,
    CRM_REQ_ACK_COLD_RESET,
    CRM_REQ_ACK_SHUTDOWN,
    CRM_REQ_NOTIFY_DBG,
} crm_mdmcli_wire_req_ids_t;

/**
 * Structure used as interface to serializer / deserializer code
 */
typedef struct crm_mdmcli_wire_msg {
    int id;                              /* Can be either a crm_mdmcli_ids_t or a mdm_cli_event_t */
    union {
        const mdm_cli_dbg_info_t *debug; /* If id is MDM_DBG_INFO or CRM_REQ_NOTIFY_DBG */
        struct {
            mdm_cli_restart_cause_t cause;
            const mdm_cli_dbg_info_t *debug;
        } restart; /* If id is MDMCLI_REQ_RESTART */
        struct {
            int events_bitmap;
            const char *name;
        } register_client; /* If id is MDMCLI_REQ_REGISTER */
    } msg;
} crm_mdmcli_wire_msg_t;

typedef struct crm_mdmcli_wire_ctx crm_mdmcli_wire_ctx_t;

/**
 * Create the context for the MDMCLI <=> CRM wire interface protocol.
 *
 * @param [in] direction Direction of the communication (used to define what messages needs to be
 *                       marshalled).
 * @param [in] instance_id Instance of the CRM server
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_mdmcli_wire_ctx_t *crm_mdmcli_wire_init(crm_mdmcli_wire_direction_t direction, int instance_id);

struct crm_mdmcli_wire_ctx {
    /**
     * Disposes the module.
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_mdmcli_wire_ctx_t *ctx);

    /**
     * Gets the socket name associated to this context.
     * Important note: should not be freed.
     *
     * @param [in] ctx Module context
     *
     * @return name of the socket to use.
     * @return NULL in case of error
     */
    const char *(*get_socket_name)(crm_mdmcli_wire_ctx_t *ctx);

    /**
     * Sends given message on the wire interface socket.
     *
     * @param [in] ctx Module context
     * @param [in] msg Message to send
     * @param [in] socket File descriptor of the communication socket
     *
     * @return 0 in case of success
     * @return -1 in case of error
     */
    int (*send_msg)(crm_mdmcli_wire_ctx_t *ctx, const crm_mdmcli_wire_msg_t *msg, int socket);

    /**
     * Serialize given message.
     *
     * @param [in] ctx Module context
     * @param [in] msg Message to serialize
     * @param [in] allocate If set to true, returned pointer needs to be freed
     *
     * @return pointer to serialized message in case of success
     * @return NULL in case of error
     */
    void *(*serialize_msg)(crm_mdmcli_wire_ctx_t *ctx, const crm_mdmcli_wire_msg_t *msg,
                           bool allocate);

    /**
     * Sends given serialized message on the wire interface socket.
     *
     * @param [in] ctx Module context
     * @param [in] msg Serialized message to send
     * @param [in] socket File descriptor of the communication socket
     *
     * @return 0 in case of success
     * @return -1 in case of error
     */
    int (*send_serialized_msg)(crm_mdmcli_wire_ctx_t *ctx, const void *msg, int socket);

    /**
     * Receives a message on the wire interface socket.
     * Important note: the returned message may be modified by any subsequent call to this function.
     *
     * @param [in] ctx Module context
     * @param [in] socket File descriptor of the communication socket
     *
     * @return message pointer in case of success.
     * @return NULL in case of error
     */
    crm_mdmcli_wire_msg_t *(*recv_msg)(crm_mdmcli_wire_ctx_t *ctx, int socket);
};

#endif /* __CRM_MDMCLI_WIRE_HEADER__ */
