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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <libgen.h>
#include <zlib.h>

#define CRM_MODULE_TAG "IFWD"
#include "utils/logs.h"
#include "utils/common.h"

typedef struct crm_tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag[1];
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char pad[167];
} crm_tar_header_t;

static void compute_octal(char *dest, size_t len, uint64_t value)
{
    char format[9];

    ASSERT(dest);
    snprintf(format, sizeof(format), "%%0%zdllo", len - 1);
    snprintf(dest, len, format, value);
}

static size_t compute_header(const char *path, const char *filename, crm_tar_header_t *header)
{
    ASSERT(path);
    ASSERT(filename);
    ASSERT(header);

    struct stat st;
    ASSERT(!stat(path, &st) && S_ISREG(st.st_mode));

    memset(header, 0, sizeof(*header));

    ASSERT(strlen(filename) < sizeof(header->name));
    snprintf(header->name, sizeof(header->name), "%s", filename);
    header->typeflag[0] = '0';
    memcpy(header->magic, "ustar ", sizeof(header->magic));
    memcpy(header->version, " \0", sizeof(header->version));
    compute_octal(header->mtime, sizeof(header->mtime), st.st_mtime);

    /* set system as default user and group name with 666 as default mode */
    memcpy(header->uname, "system", 6);
    memcpy(header->gname, "system", 6);
    memcpy(header->mode, "0000666", 8);

    compute_octal(header->uid, sizeof(header->uid), st.st_uid);
    compute_octal(header->gid, sizeof(header->gid), st.st_gid);
    compute_octal(header->size, sizeof(header->size), st.st_size);

    /* compute checksum */
    uint32_t checksum = 0;
    memset(header->checksum, ' ', sizeof(header->checksum));
    const unsigned char *hdr = (unsigned char *)header;
    for (size_t i = 0; i < sizeof(crm_tar_header_t); i++)
        checksum += hdr[i];
    compute_octal(header->checksum, 6, checksum);

    return st.st_size;
}

/**
 * @see archive.h
 */
void crm_ifwd_tgz_create(const char *folder, size_t nfiles, const char **files,
                         const char *destination)
{
    ASSERT(files);
    ASSERT(destination);

    char zeroes[1024];
    memset(zeroes, 0, sizeof(zeroes));

    gzFile fd_out = gzopen(destination, "wb");
    DASSERT(fd_out != NULL, "failed to open %s. error: %s", destination, strerror(errno));

    for (size_t i = 0; i < nfiles; i++) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", folder, files[i]);

        crm_tar_header_t header;
        int file_size = compute_header(path, files[i], &header);
        ASSERT(file_size > 0);

        int fd_in = open(path, O_RDONLY);
        ASSERT(fd_in >= 0);
        char *map_in = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd_in, 0);
        ASSERT(map_in);
        ASSERT(gzwrite(fd_out, &header, sizeof(header)) == sizeof(header));
        ASSERT(gzwrite(fd_out, map_in, file_size) == file_size);
        int len_zeroes = sizeof(header) - (file_size % sizeof(header));
        ASSERT(gzwrite(fd_out, zeroes, len_zeroes) == len_zeroes);
        ASSERT(!munmap(map_in, file_size));
        ASSERT(!close(fd_in));
    }

    /* Empty records to mark end of archive */
    for (unsigned int i = 0; i < 8; i++)
        ASSERT(gzwrite(fd_out, zeroes, sizeof(zeroes)) == sizeof(zeroes));
    DASSERT(gzclose(fd_out) == Z_OK, "failed to close %s", destination);
}
