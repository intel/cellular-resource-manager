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

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

#define CRM_MODULE_TAG "UTILS"
#include "utils/common.h"
#include "utils/file.h"
#include "utils/logs.h"

/**
 * @see file.h
 */
int crm_file_write(const char *path, const char *value)
{
    int ret = -1;

    ASSERT(path != NULL);
    ASSERT(value != NULL);

    errno = 0;
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        LOGE("open of (%s) failed (%s)", path, strerror(errno));
    } else {
        size_t size = strlen(value);
        ssize_t write_size = 0;
        if (size > 0)
            write_size = write(fd, value, size);
        if ((close(fd) == 0) && ((size_t)write_size == size))
            ret = 0;
        else
            LOGE("Failed to write (%s) file. (%s)", path, strerror(errno));
    }

    return ret;
}

/**
 * @see file.h
 */
int crm_file_read(const char *path, char *value, size_t size)
{
    int ret = -1;

    ASSERT(path != NULL);
    ASSERT(value != NULL);

    errno = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        LOGE("open of (%s) failed (%s)", path, strerror(errno));
    } else {
        ssize_t read_size = 0;
        if (size > 0) {
            read_size = read(fd, value, size);
            if (read_size == (ssize_t)size) {
                value[size - 1] = '\0';
                DASSERT(0, "value truncated. buffer too small. read: %s", value);
            }
        }

        if (read_size > 0) {
            value[MIN(read_size, (ssize_t)size - 1)] = '\0';
            ret = 0;
        }

        if (close(fd)) {
            LOGE("Failed to read (%s) file. (%s)", path, strerror(errno));
            ret = -1;
        }
    }

    return ret;
}

/**
 * @see file.h
 */
bool crm_file_exists(const char *path)
{
    ASSERT(path != NULL);

    struct stat st;
    return !stat(path, &st) && S_ISREG(st.st_mode);
}

/**
 * @see file.h
 */
int crm_file_copy(const char *src, const char *dst, bool in_raw, bool out_raw, mode_t dst_mode)
{
    int in_fd;
    int out_fd = -1;
    mode_t old_umask = 0;
    uint32_t size;
    int ret = -1;

    ASSERT(src != NULL);
    ASSERT(dst != NULL);

    old_umask = umask(~dst_mode & 0777);

    in_fd = open(src, O_RDONLY);
    if (in_fd < 0) {
        LOGE("Cannot open source file (%s), errno = %d [%s]", src, errno, strerror(errno));
        goto out;
    }

    out_fd = open(dst, O_CREAT | O_WRONLY | O_TRUNC, dst_mode);
    if (out_fd < 0) {
        LOGE("Cannot create destination file: (%s), errno = %d [%s]", dst, errno, strerror(errno));
        goto out;
    }

    if (in_raw) {
        if (read(in_fd, &size, sizeof(size)) != sizeof(size)) {
            LOGE("Reading the file failed, errno = %d [%s]", errno, strerror(errno));
            goto out;
        }
    } else {
        struct stat sb;
        if (fstat(in_fd, &sb) == -1) {
            LOGE("Failed obtaining file status");
            goto out;
        }
        size = sb.st_size;
    }

    if (size) {
        if (out_raw && write(out_fd, &size, sizeof(size)) != sizeof(size)) {
            LOGE("Failed to write the size, errno = %d [%s]", errno, strerror(errno));
            goto out;
        }
        ssize_t w_size;
        if ((w_size = sendfile(out_fd, in_fd, NULL, size)) == -1) {
            LOGE("Copying file failed, errno = %d [%s]", errno, strerror(errno));
            goto out;
        } else if ((size_t)w_size != size) {
            LOGE("Did not copy the whole data (sent %zd bytes of %u)", w_size, size);
            goto out;
        }
    }

    ret = 0;

out:
    if (in_fd >= 0)
        close(in_fd);
    if (out_fd >= 0) {
        if (close(out_fd) < 0) {
            LOGE("Error while closing %s: %d [%s]", dst, errno, strerror(errno));
            ret = -1;
        }
    }
    umask(old_umask & 0777);

    return ret;
}
