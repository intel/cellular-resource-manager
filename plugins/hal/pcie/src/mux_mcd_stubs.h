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

#ifndef __CRM_MODEM_MUX_MCD_STUB_HEADER__
#define __CRM_MODEM_MUX_MCD_STUB_HEADER__

enum mcd_cmds {
    MDM_CTRL_POWER_OFF,
    MDM_CTRL_POWER_ON,
    MDM_CTRL_WARM_RESET,
    MDM_CTRL_COLD_RESET,
    MDM_CTRL_SET_POLLED_STATES,
    MDM_CTRL_SET_CFG,
    BOARD_PCIE,
    MODEM_7360,
    POWER_ON_PMIC,
};

struct mdm_ctrl_cfg {
    int a, b, c, d;
};

enum {
    N_GSM0710,
    GSMIOC_GETCONF,
    GSMIOC_SETCONF
};

struct gsm_config {
    int a;
    int encapsulation;
    int initiator;
    int mru;
    int mtu;
    int burst;
};

#endif /* __CRM_MODEM_MUX_MCD_STUB_HEADER__ */
