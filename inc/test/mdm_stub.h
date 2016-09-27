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

#ifndef __CRM_MODEM_STUB_HEADER__
#define __CRM_MODEM_STUB_HEADER__

/* stub modem requests received from HAL */
typedef enum mdm_stub_hal_request {
    MREQ_READY = 1, // stub modem app is ready
    MREQ_OFF,
    MREQ_ON,
    /* @TODO: add specifics for premium platforms */
    MREQ_NUM
} mdm_stub_hal_request_t;

/* requests sent by controller (test app) to stub modem */
typedef enum mdm_stub_control {
    MCTRL_STOP = 100,      // stop stub modem app

    MCTRL_OFF,             // stop modem
    MCTRL_RUN,             // start modem
    MCTRL_FW_FAILURE,      // modem binary verification failed
    MCTRL_DUMP,            // generate dump event
    MCTRL_SELF_RESET,      // generate self-reset event
    MCTRL_NO_PING,         // modem does not answer to PING command
    MCTRL_PING_SELF_RESET, // modem self-reset during PING
    MCTRL_UNAVAILABLE,     // modem is unresponsive
    /* @TODO: add specifics for premium platforms */
    MCTRL_NUM
} mdm_stub_control_t;

#endif /* __CRM_MODEM_STUB_HEADER__ */
