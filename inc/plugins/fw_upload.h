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

#ifndef __CRM_FW_UPLOAD_HEADER__
#define __CRM_FW_UPLOAD_HEADER__

#include "plugins/dependent_types.h"
#include "utils/process_factory.h"
#include "libtcs2/tcs.h"

typedef struct crm_fw_upload_ctx crm_fw_upload_ctx_t;

/* Used by crm_plugin_load API */
#define CRM_FW_UPLOAD_INIT "crm_fw_upload_init"
typedef crm_fw_upload_ctx_t * (*crm_fw_upload_init_t)(int, bool, tcs_ctx_t *, crm_ctrl_ctx_t *,
                                                      crm_process_factory_ctx_t *);

/**
 * Initializes the module
 *
 * @param [in] inst_id   CRM Instance ID
 * @param [in] flashless Boolean indicating if the modem is flashless
 * @param [in] tcs       TCS context
 * @param [in] ctrl      Control context
 * @param [in] factory   Process factory context
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_fw_upload_ctx_t *crm_fw_upload_init(int inst_id, bool flashless, tcs_ctx_t *tcs,
                                        crm_ctrl_ctx_t *ctrl, crm_process_factory_ctx_t *factory);
struct crm_fw_upload_ctx {
    /**
     * Disposes the module
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_fw_upload_ctx_t *ctx);

    /**
     * Packages the modem firmware
     * NB: Asynchronous API. The result of the operation is provided by the callback
     *
     * @param [in] ctx Module context
     * @param [in] fw_path full path of the modem firmware
     */
    void (*package)(crm_fw_upload_ctx_t *ctx, const char *fw_path);

    /**
     * Uploads modem firmware to modem.
     * This function will fail if the firmware was not packaged before.
     * NB: Asynchronous API. The result of the operation is provided by the callback
     *
     * @param [in] ctx   Module context
     * @param [in] nodes Nodes to be used to flash the modem. Nodes are separated by a ';'
     */
    void (*flash)(crm_fw_upload_ctx_t *ctx, const char *nodes);
};

#endif /* __CRM_MDM_FW_UPLOAD_HEADER__ */
