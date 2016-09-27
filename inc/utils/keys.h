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

#ifndef __CRM_UTILS_KEYS_HEADER__
#define __CRM_UTILS_KEYS_HEADER__

/**
 * Key rules:
 *  - all keys used by CRM _MUST_ be declared in this file
 *  - debug keys must be CRM_KEY_DBG_ prefixed
 *  - for dynamic keys, add @ in the define. It will be replaced by the instance id
 *
 */

/* DEBUG KEYS */
#define CRM_KEY_DBG_LOAD_STUB "persist.sys.crm@.load_stub"
#define CRM_KEY_DBG_SANITY_TEST_MODE "sys.crm@.sanity"
#define CRM_KEY_DBG_ENABLE_SILENT_RESET "persist.sys.crm@.silent_reset"
#define CRM_KEY_DBG_DISABLE_ESCALATION "persist.sys.crm@.escalation_off"
#define CRM_KEY_DBG_ENABLE_FLASHING_LOG "persist.sys.crm@.flashing_log"
#define CRM_KEY_DBG_DISABLE_DUMP "persist.sys.crm@.dump_off"

/* DEBUG KEYS: HOST ONLY */
#define CRM_KEY_DBG_HOST "crm@.host_test"

/* OS native keys */
#define CRM_KEY_BUILD_TYPE "ro.build.type"
#define CRM_KEY_SERVICE_START "ctl.start"
#define CRM_KEY_SERVICE_STOP "ctl.stop"
#define CRM_KEY_DATA_PARTITION_ENCRYPTION "vold.decrypt"

/* Service names (to be started / stopped with CRM_KEY_SERVICE_xxx keys) */
#define CRM_KEY_CONTENT_SERVICE_RPCD "rpc-daemon"

/* CRM specific keys */
#define CRM_KEY_REBOOT_COUNTER "persist.sys.crm@.reboot"
#define CRM_KEY_BLOB_HASH "persist.sys.crm@.blob_hash"
#define CRM_KEY_CONFIG_HASH "persist.sys.crm@.config_hash"
#define CRM_KEY_FAKE_EVENT "crashreport.events.fake"
#define CRM_KEY_FIRST_START "sys.crm@.first_start"

/* device specific keys */
#define CRM_KEY_SERVICE_WWAN "sys.wwan0.state"
#define CRM_KEY_NET_DEVICE_STATE "system.net_device.state"

#endif /* __CRM_UTILS_KEYS_HEADER__ */
