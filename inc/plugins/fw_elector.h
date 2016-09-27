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

#ifndef __CRM_FW_ELECTOR_HEADER__
#define __CRM_FW_ELECTOR_HEADER__

#include "libtcs2/tcs.h"

typedef struct crm_fw_elector_ctx crm_fw_elector_ctx_t;

/* Used by crm_plugin_load API */
#define CRM_FW_ELECTOR_INIT "crm_fw_elector_init"
typedef crm_fw_elector_ctx_t * (*crm_fw_elector_init_t)(tcs_ctx_t *, int);

/**
 * Initializes the module
 *
 * @param [in] tcs     TCS context
 * @param [in] inst_id CRM instance ID
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
crm_fw_elector_ctx_t *crm_fw_elector_init(tcs_ctx_t *tcs, int inst_id);

struct crm_fw_elector_ctx {
    /**
     * Disposes the module
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_fw_elector_ctx_t *ctx);

    /**
     * Gets the modem firmware file path to apply
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     *
     * @return the path of the modem firmware
     * @return NULL if there is no modem firmware
     */
    const char * (*get_fw_path)(const crm_fw_elector_ctx_t *ctx);

    /**
     * Gets the modem RnD file path to apply
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     *
     * @return the path of the modem firmware
     * @return NULL if there is no modem firmware
     */
    const char * (*get_rnd_path)(const crm_fw_elector_ctx_t *ctx);

    /**
     * Gets the list of customization files (TLV) to apply
     * NB: Synchronous API
     *
     * @param [in]  ctx Module context
     * @param [out] nb Number of TLVs
     *
     * @return List of filenames (relative path) of modem customization files
     * @return NULL if there is no customization to apply
     */
    const char *const * (*get_tlv_list)(const crm_fw_elector_ctx_t *ctx, int *nb);

    /**
     * Notifies if the modem firmware has been successfully flashed
     * NB: Synchronous API
     *
     * @param [in] ctx    Module context
     * @param [in] status Status 0 if successful
     */
    void (*notify_fw_flashed)(const crm_fw_elector_ctx_t *ctx, int status);

    /**
     * Notifies if streamline files have been successfully applied
     * NB: Synchronous API
     *
     * @param [in] ctx    Module context
     * @param [in] status Status 0 if successful
     */
    void (*notify_tlv_applied)(const crm_fw_elector_ctx_t *ctx, int status);
};

#endif /* __CRM_FW_ELECTOR_HEADER__ */
