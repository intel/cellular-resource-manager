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

#ifndef __CRM_UTILS_SOCKET_HEADER__
#define __CRM_UTILS_SOCKET_HEADER__

/**
 * Connects to Android socket with the given name.
 *
 * @param [in] socket_name  name of the socket to connect to
 *
 * @return socket file descriptor if successful, -1 otherwise
 */
int crm_socket_connect(const char *socket_name);

/**
 * Creates Android socket with the given name.
 *
 * @param [in] socket_name  name of the socket to create
 * @param [in] max_conn     maximum number of simultaneous connection requests
 *
 * @return socket file descriptor if successful, -1 otherwise
 */
int crm_socket_create(const char *socket_name, int max_conn);

/**
 * Accepts client connection on given socket
 *
 * @param [in] fd   file descriptor of the socket on which a client is pending
 *
 * @return client socket file descriptor if successful, -1 otherwise
 */
int crm_socket_accept(int fd);

/**
 * Sends data to socket
 *
 * @param [in] fd         file descriptor of the socket on which to send data
 * @param [in] timeout    timeout in milliseconds
 * @param [in] data       pointer to data to send over the socket
 * @param [in] data_size  number of bytes to send
 *
 * @return 0 in case of success, -1 in case of failure
 */
int crm_socket_write(int fd, int timeout, const void *data, size_t data_size);

/**
 * Receives data from socket
 *
 * @param [in] fd             file descriptor of the socket on which to send data
 * @param [in] timeout        timeout in milliseconds
 * @param [in] data           pointer where to store data read on the socket
 * @param [in] data_size      size of the data to read on the socket
 *
 * @return 0 in case of success, -1 in case of failure
 */
int crm_socket_read(int fd, int timeout, void *data, size_t data_size);


#endif
