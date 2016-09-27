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

#ifndef __CRM_UTILS_LOGS_HEADER__
#define __CRM_UTILS_LOGS_HEADER__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRM_MODULE_TAG
#error
#endif

#define FCT_FORMAT "%25s:%-4d "

#define KLOG(format, ...) do { \
        crm_console(format "\n", ## __VA_ARGS__); \
        LOGD(format, ## __VA_ARGS__); \
} while (0)

#define LOGD(format, ...) crm_logs_debug(CRM_MODULE_TAG, FCT_FORMAT format, __FUNCTION__, __LINE__, \
                                         ## __VA_ARGS__)

#define LOGV(format, ...) crm_logs_verbose(CRM_MODULE_TAG, FCT_FORMAT format, __FUNCTION__, \
                                           __LINE__, ## __VA_ARGS__)

#define LOGI(format, ...) crm_logs_info(CRM_MODULE_TAG, FCT_FORMAT format, __FUNCTION__, \
                                        __LINE__, ## __VA_ARGS__)

#define LOGE(format, ...) crm_logs_error(CRM_MODULE_TAG, FCT_FORMAT format, __FUNCTION__, \
                                         __LINE__, ## __VA_ARGS__)

void crm_logs_init(int inst_id);

void crm_console(const char *format, ...);

void crm_logs_verbose(const char *tag, const char *format, ...)
#if defined(__GNUC__)
__attribute__ ((format(printf, 2, 3)))              // Used to have compiler check arguments
#endif
;

void crm_logs_info(const char *tag, const char *format, ...)
#if defined(__GNUC__)
__attribute__ ((format(printf, 2, 3)))          // Used to have compiler check arguments
#endif
;

void crm_logs_debug(const char *tag, const char *format, ...)
#if defined(__GNUC__)
__attribute__ ((format(printf, 2, 3)))          // Used to have compiler check arguments
#endif
;

void crm_logs_error(const char *tag, const char *format, ...)
#if defined(__GNUC__)
__attribute__ ((format(printf, 2, 3)))          // Used to have compiler check arguments
#endif
;

#ifdef __cplusplus
}
#endif

#endif /* __CRM_UTILS_LOGS_HEADER__ */
