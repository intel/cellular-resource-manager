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

#include <stdarg.h>

#define CRM_MODULE_TAG "CRM"

#include "utils/logs.h"
#include "utils/common.h"

#define TAG_LEN 10
#define LOG_LEN 1024

static int g_inst_id = 0;

/* OS specific macros: */

#ifndef HOST_BUILD

#include <utils/Log.h>

#define VERBOSE ANDROID_LOG_VERBOSE
#define DEBUG   ANDROID_LOG_DEBUG
#define INFO    ANDROID_LOG_INFO
#define ERROR   ANDROID_LOG_ERROR

#define CRM_LOG(level, tag, log) \
    do { __android_log_buf_write(LOG_ID_RADIO, level, tag, log); } while (0)

#else /* HOST_BUILD */

#include <sys/syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/timeb.h>

#define VERBOSE 'V'
#define DEBUG 'D'
#define INFO 'I'
#define ERROR 'E'

#define CRM_LOG(level, tag, log) do { host_crm_log(level, tag, log); } while (0)

static inline int gettid()
{
    return (int)syscall(SYS_gettid);
}

static void host_crm_log(char level, const char *tag, const char *log)
{
    struct timeb tp;
    struct tm tmp;

    ftime(&tp);
    struct tm *local = localtime_r(&tp.time, &tmp);
    printf("%02d:%02d:%02d.%03d %c %-5d %-5d %" xstr(TAG_LEN) "s %s\n",
           local->tm_hour, local->tm_min, local->tm_sec, tp.millitm,
           level, getpid(), gettid(), tag, log);
}

#endif /* HOST_BUILD */

static void crm_log(int level, const char *tag, const char *format, va_list args)
{
    char plugin_tag[TAG_LEN];
    char log[LOG_LEN];

    // Spaces at the end of the format string is to add padding at the end of log tag to force
    // a size of TAG_LEN bytes
    snprintf(plugin_tag, sizeof(plugin_tag), "%s%s%d     ", CRM_MODULE_TAG, tag, g_inst_id);

    vsnprintf(log, sizeof(log), format, args);

    CRM_LOG(level, plugin_tag, log);
}

void crm_console(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void crm_logs_debug(const char *tag, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    crm_log(DEBUG, tag, format, args);
    va_end(args);
}

void crm_logs_verbose(const char *tag, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    crm_log(VERBOSE, tag, format, args);
    va_end(args);
}

void crm_logs_info(const char *tag, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    crm_log(INFO, tag, format, args);
    va_end(args);
}

void crm_logs_error(const char *tag, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    crm_log(ERROR, tag, format, args);
    va_end(args);
}

void crm_logs_init(int id)
{
    g_inst_id = id;
}
