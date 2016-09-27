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

#ifndef __CRM_ARCHIVE_HEADER__
#define __CRM_ARCHIVE_HEADER__

#include <sys/types.h>

/**
 *  Generates a .tar.gz file from a couple of filenames.
 *
 *  @param [in] folder       Folder where files are stored
 *  @param [in] nfiles       Total number of files.
 *  @param [in] files        Filenames to include in the .tar.gz
 *  @param [in] destination  Full path of the .tar.gz
 */
void crm_ifwd_tgz_create(const char *folder, size_t nfiles, const char **files,
                         const char *destination);

#endif /* __CRM_ARCHIVE_HEADER__ */
