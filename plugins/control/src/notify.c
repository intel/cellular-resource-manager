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

#define CRM_MODULE_TAG "CTRL"
#include "utils/common.h"
#include "utils/logs.h"

#include "common.h"
#include "utils.h"

static inline void notify_simple_event(crm_ipc_ctx_t *ipc, ctrl_events_t id, int status)
{
    crm_ipc_msg_t msg = { id, 0, NULL };

    if (status)
        msg.scalar = EV_FAILURE;

    ipc->send_msg(ipc, &msg);
}

static inline long long get_hal_id(crm_hal_evt_type_t type)
{
    switch (type) {
    case HAL_MDM_OFF:          return EV_HAL_MDM_OFF;
    case HAL_MDM_RUN:          return EV_HAL_MDM_RUN;
    case HAL_MDM_BUSY:         return EV_HAL_MDM_BUSY;
    case HAL_MDM_NEED_RESET:   return EV_HAL_MDM_NEED_RESET;
    case HAL_MDM_FLASH:        return EV_HAL_MDM_FLASH;
    case HAL_MDM_DUMP:         return EV_HAL_MDM_DUMP;
    case HAL_MDM_UNRESPONSIVE: return EV_HAL_MDM_UNRESPONSIVE;
    default: ASSERT(0);
    }
}

static inline const char *get_hal_evt_txt(crm_hal_evt_type_t evt)
{
    switch (evt) {
    case HAL_MDM_OFF:          return "HAL_MDM_OFF";
    case HAL_MDM_RUN:          return "HAL_MDM_RUN";
    case HAL_MDM_BUSY:         return "HAL_MDM_BUSY";
    case HAL_MDM_NEED_RESET:   return "HAL_MDM_NEED_RESET";
    case HAL_MDM_FLASH:        return "HAL_MDM_FLASH";
    case HAL_MDM_DUMP:         return "HAL_MDM_DUMP";
    case HAL_MDM_UNRESPONSIVE: return "HAL_MDM_UNRESPONSIVE";
    default: ASSERT(0);
    }
}

/**
 * @see control.h
 */
void notify_hal_event(crm_ctrl_ctx_t *ctx, const crm_hal_evt_t *event)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);
    ASSERT(event != NULL);

    /**
     * @TODO: add logging of link content (type of link, pathes, ...)
     */
    LOGD("->%s(%s)", __FUNCTION__, get_hal_evt_txt(event->type));

    switch (event->type) {
    case HAL_MDM_OFF:
    case HAL_MDM_RUN:
    case HAL_MDM_BUSY:
    {
        crm_ipc_msg_t msg = { get_hal_id(event->type), 0, NULL };
        ASSERT(i_ctx->ipc->send_msg(i_ctx->ipc, &msg));
    }
    break;
    case HAL_MDM_NEED_RESET:
    case HAL_MDM_UNRESPONSIVE:
    {
        crm_ipc_msg_t msg = { .scalar = get_hal_id(event->type) };
        msg.data = copy_dbg_info(event->dbg_info);
        msg.data_size = sizeof(event->dbg_info);
        ASSERT(i_ctx->ipc->send_msg(i_ctx->ipc, &msg));
    }
    break;
    case HAL_MDM_FLASH:
    case HAL_MDM_DUMP:
    {
        crm_hal_evt_t *event_copy = malloc(sizeof(crm_hal_evt_t));
        ASSERT(event_copy);
        *event_copy = *event;

        crm_ipc_msg_t msg = { get_hal_id(event->type), sizeof(*event_copy), event_copy };
        ASSERT(i_ctx->ipc->send_msg(i_ctx->ipc, &msg));
    }
    break;

    default: ASSERT(0);
    }
}

/**
 * @see control.h
 */
void notify_nvm_status(crm_ctrl_ctx_t *ctx, int status)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s(%s)", __FUNCTION__, 0 == status ? "success" : "failure");
    notify_simple_event(i_ctx->ipc, EV_NVM_SUCCESS, status);
}

/**
 * @see control.h
 */
void notify_fw_upload_status(crm_ctrl_ctx_t *ctx, int status)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s(%s)", __FUNCTION__, 0 == status ? "success" : "failure");
    notify_simple_event(i_ctx->ipc, EV_FW_SUCCESS, status);
}

/**
 * @see control.h
 */
void notify_customization_status(crm_ctrl_ctx_t *ctx, int status)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s(%s)", __FUNCTION__, 0 == status ? "success" : "failure");
    notify_simple_event(i_ctx->ipc, EV_FW_SUCCESS, status);
}

/**
 * @see control.h
 */
void notify_dump_status(crm_ctrl_ctx_t *ctx, int status)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s(%s)", __FUNCTION__, 0 == status ? "success" : "failure");
    notify_simple_event(i_ctx->ipc, EV_DUMP_SUCCESS, status);
}

/**
 * @see control.h
 */
void notify_client(crm_ctrl_ctx_t *ctx, mdm_cli_event_t evt_id, size_t data_size,
                   const void *data)
{
    crm_control_ctx_internal_t *i_ctx = (crm_control_ctx_internal_t *)ctx;

    ASSERT(i_ctx != NULL);

    LOGD("->%s(evt: %d)", __FUNCTION__, evt_id); //@TODO: use utils to print event name
    i_ctx->clients->notify_client(i_ctx->clients, evt_id, data_size, data);
}
