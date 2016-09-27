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

#ifndef __TEL_JAVA_BRIDGE_HEADER__
#define __TEL_JAVA_BRIDGE_HEADER__

#include <stdbool.h>

typedef struct tel_java_bridge_ctx tel_java_bridge_ctx_t;

/**
 * Initializes the java bridge connection library
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
tel_java_bridge_ctx_t *tel_java_bridge_init(void);

struct tel_java_bridge_ctx {
    /**
     * Disposes the module
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(tel_java_bridge_ctx_t *ctx);

    /**
     * Attempts to connect to the java bridge daemon
     *
     * @param [in] ctx Module context
     *
     * @return 0 in case of success, -1 otherwise
     */
    int (*connect)(tel_java_bridge_ctx_t *ctx);

    /**
     * Disconnects to the java bridge daemon
     *
     * @param [in] ctx Module context
     */
    void (*disconnect)(tel_java_bridge_ctx_t *ctx);

    /**
     * Returns the file descriptor to poll (with POLLIN) to handle connection errors
     *
     * @param [in] ctx Module context
     *
     * @return file descriptor to add to poll list
     */
    int (*get_poll_fd)(tel_java_bridge_ctx_t *ctx);

    /**
     * Function to be called in case of an event on the file descriptor returned by a
     * call to 'get_poll_fd' function.
     *
     * If this function returns -1 (i.e. disconnected from the java bridge daemon),
     * the client shall consider all its wakelock released (i.e. as if wakelock count
     * is 0).
     *
     * @param [in] ctx Module context
     * @param [in] revent The 'revent' field of the pollfd structure
     *
     * @return 0 if still connected, -1 otherwise
     */
    int (*handle_poll_event)(tel_java_bridge_ctx_t *ctx, int revent);

    /**
     * Acquire or release wakelock
     *
     * @param [in] ctx Module context
     * @param [in] acquire true to acquire the wakelock, false otherwise
     *
     * @return 0 in case of success, -1 otherwise
     */
    int (*wakelock)(tel_java_bridge_ctx_t *ctx, bool acquire);

    /**
     * Start service
     *
     * @param [in] ctx Module context
     * @param [in] s_package Service package
     * @param [in] s_class Service class name in package
     *
     * @return 0 in case of success, -1 otherwise
     */
    int (*start_service)(tel_java_bridge_ctx_t *ctx, const char *s_package, const char *s_class);

    /**
     * Broadcast intent.
     *
     * Usage: format string contains the intent parameters and currently supports "%d" (Integer)
     *        and "%s" (String) parameters. Each "%d" / "%s" parameter needs to be prefixed by
     *        the name of the parameter.
     *
     * Examples:
     *   = broadcast_intent("foo", "instId%d", 2);
     *   = broadcast_intent("bar", "instId%ddata_key%s", 1, "this is a string");
     *
     * Note: this format has been used so as to reuse the format string capabilities of
     *       modern compilers to check the code statically.
     *
     * @param [in] ctx Module context
     * @param [in] name Intent name
     * @param [in] format Format string of the intent parameters. Only %d and %s is supported.
     *
     * @return 0 in case of success, -1 otherwise
     */
    int (*broadcast_intent)(tel_java_bridge_ctx_t *ctx, const char *name, const char *format, ...);
};

/**
 * Contextless API
 *
 * @see broadcast_intent for details
 * This function connects to java bridge, broadcasts the intent and disposes the context
 */
int tel_java_brige_broadcast_intent(const char *name, const char *format, ...)
#if defined(__GNUC__)
__attribute__ ((format(printf, 2, 3)))          // Used to have compiler check arguments
#endif
;

/**
 * Contextless API
 *
 * @see start_service for details
 * This function connects to java bridge, starts the service and disposes the context
 */
int tel_java_brige_start_service(const char *s_package, const char *s_class);

#endif /* __TEL_JAVA_BRIDGE_HEADER__ */
