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

#ifndef __CRM_UTILS_COMMON_HEADER__
#define __CRM_UTILS_COMMON_HEADER__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include "utils/logs.h"

#define xstr(s) str(s)
#define str(s) #s

#ifdef __GNUC__
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif

#define DASSERT(exp, format, ...) do { \
        if (unlikely(!(exp))) { \
            if (unlikely(format[0] != '\0')) \
                LOGE("AssertionLog " format, ## __VA_ARGS__); \
            LOGE("%s:%d Assertion '" xstr(exp) "'", __FILE__, __LINE__); \
            abort(); \
        } \
} while (0)

#define ASSERT(exp) DASSERT(exp, "")

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))

#ifdef __cplusplus
}
#endif

#endif /* __CRM_UTILS_COMMON_HEADER__ */
