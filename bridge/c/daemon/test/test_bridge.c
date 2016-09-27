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

#include <unistd.h>
#include <stdio.h>

/* NOTE: CRM dependency is only on logging and makefiles, could easily be removed in the future */
#define CRM_MODULE_TAG "JVBT"
#include "utils/debug.h"
#include "utils/common.h"
#include "utils/logs.h"
#include "utils/time.h"

#include "teljavabridge/tel_java_bridge.h"

int main(void)
{
    tel_java_bridge_ctx_t *ctx = tel_java_bridge_init();

    ASSERT(ctx);

    ASSERT(ctx->connect(ctx) == 0);
#if 0
    ASSERT(ctx->start_service(ctx, "Test", "BAR") == 0);
    int ret = ctx->broadcast_intent(ctx, "Test", "Param1%dParam2%sParam3%d", 5, "Toto", 18);
    ASSERT(ret == 0);
    ASSERT(ctx->broadcast_intent(ctx, "Test", NULL) == 0);
    ASSERT(ctx->broadcast_intent(ctx, "TestTest", "") == 0);
    ASSERT(ctx->wakelock(ctx, true) == 0);
    ASSERT(ctx->wakelock(ctx, true) == 0);
    usleep(250000);
    ASSERT(ctx->wakelock(ctx, false) == 0);
    ASSERT(ctx->wakelock(ctx, false) == 0);
    usleep(250000);
    ASSERT(ctx->wakelock(ctx, true) == 0);
#else
    printf("Wakelock acquire\n");
    ASSERT(ctx->wakelock(ctx, true) == 0);
    sleep(10);
    printf("Wakelock release\n");
    ASSERT(ctx->wakelock(ctx, false) == 0);
    sleep(10);
    printf("Core dump\n");
    int ret = ctx->broadcast_intent(ctx, "com.intel.action.CORE_DUMP_WARNING", "instId%d", 1);
    ASSERT(ret == 0);
    sleep(10);
    printf("Service\n");
    ASSERT(ctx->start_service(ctx, "com.intel.internal.telephony.OemTelephonyApp",
                              "com.intel.internal.telephony.OemTelephonyApp.OemTelephonyService") ==
           0);
    sleep(10);
    printf("Reboot\n");
    ret = ctx->broadcast_intent(ctx, "android.intent.action.REBOOT", "nowait%d", 1);
    ASSERT(ret == 0);
#endif
    ctx->dispose(ctx);

    ASSERT(!tel_java_brige_broadcast_intent("Test", NULL));
    ASSERT(!tel_java_brige_start_service("Test", "BAR"));

    return 0;
}
