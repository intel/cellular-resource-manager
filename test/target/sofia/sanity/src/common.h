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

#define xstr(s) str(s)
#define str(s) #s

#ifdef __GNUC__
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif

#define ASSERT(exp) do { \
        if (unlikely(!(exp))) { \
            fprintf(stderr, "%s:%d Assertion '" xstr(exp) "'\n", __FILE__, __LINE__); \
            abort(); \
        } \
} while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

#define MODEM_UP 0
#define MODEM_DOWN 1
#define AT_OK 2

#define AT_PIPE "/dev/mvpipe-atc"

extern void send_evt(int fd, int evt);
extern char *send_at(int fd, char *at, int timeout);

extern void init_test_CRM(int pipe_fd);
extern void restart_modem_CRM(void);

extern void init_test_SYSFS(int pipe_fd);
extern void restart_modem_SYSFS(void);

extern int my_printf(const char *format, ...);
