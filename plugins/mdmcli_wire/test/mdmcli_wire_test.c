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

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#define CRM_MODULE_TAG "CLIWT"
#include "utils/common.h"
#include "utils/logs.h"
#include "plugins/mdmcli_wire.h"

int main(void)
{
    int p_fd[2];
    crm_mdmcli_wire_ctx_t *ctx[2];
    mdm_cli_dbg_info_t dbg_info;

    ASSERT(pipe(p_fd) == 0);
    ctx[0] = crm_mdmcli_wire_init(CRM_SERVER_TO_CLIENT, 0);
    ctx[1] = crm_mdmcli_wire_init(CRM_CLIENT_TO_SERVER, 5);

    /* Test socket name */
    LOGD("Testing socket name");
    ASSERT(strcmp(ctx[0]->get_socket_name(ctx[0]), "crm0") == 0);
    ASSERT(strcmp(ctx[1]->get_socket_name(ctx[1]), "crm5") == 0);

    crm_mdmcli_wire_msg_t s_msg;
    crm_mdmcli_wire_msg_t *r_msg;

    /* Test REGISTER message */
    int ids[] = { CRM_REQ_REGISTER, CRM_REQ_REGISTER_DBG };
    for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
        LOGD("Testing REGISTER %d message", ids[i]); // @TODO: use string converter here
        s_msg.id = ids[i];
        s_msg.msg.register_client.events_bitmap = 0x12345678;
        s_msg.msg.register_client.name = "TEST ME !!!";

        ctx[1]->send_msg(ctx[1], &s_msg, p_fd[1]);
        r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);

        ASSERT(r_msg->id == s_msg.id);
        ASSERT(r_msg->msg.register_client.events_bitmap == s_msg.msg.register_client.events_bitmap);
        ASSERT(strcmp(r_msg->msg.register_client.name, s_msg.msg.register_client.name) == 0);
    }

    /* Test 'single id' message */
    LOGD("Testing 'single id' message");
    s_msg.id = CRM_REQ_RELEASE;

    ctx[1]->send_msg(ctx[1], &s_msg, p_fd[1]);
    r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);

    ASSERT(r_msg->id == s_msg.id);

    /* Test 'RESTART' message */
    LOGD("Testing RESTART messages (no debug info)");
    s_msg.id = CRM_REQ_RESTART;
    s_msg.msg.restart.cause = 0xca11b17e;
    s_msg.msg.restart.debug = NULL;
    ctx[1]->send_msg(ctx[1], &s_msg, p_fd[1]);
    r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);
    ASSERT(r_msg->id == s_msg.id);
    ASSERT(r_msg->msg.restart.cause == s_msg.msg.restart.cause);
    ASSERT(r_msg->msg.restart.debug == NULL);

    LOGD("Testing RESTART messages (0 to MAX_NB_DATA strings)");
    for (int i = 0; i < MDM_CLI_MAX_NB_DATA; i++) {
        s_msg.id = CRM_REQ_RESTART;
        s_msg.msg.restart.cause = 0xdeadcafe;
        s_msg.msg.restart.debug = &dbg_info;
        dbg_info.type = 1234;
        dbg_info.ap_logs_size = 5678;
        dbg_info.bp_logs_size = 8901;
        dbg_info.bp_logs_time = 2345;
        dbg_info.nb_data = i;

        if (i > 0) {
            const char **data = malloc(i * sizeof(char *));
            ASSERT(data != NULL);
            for (int j = 0; j < i; j++) {
                data[j] = malloc(50);
                ASSERT(data[j] != NULL);
                snprintf((char *)data[j], 50, "test !!! %d %d", i, j);
            }
            dbg_info.data = data;
        }

        ctx[1]->send_msg(ctx[1], &s_msg, p_fd[1]);
        r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);

        ASSERT(r_msg->id == s_msg.id);
        ASSERT(r_msg->msg.restart.cause == s_msg.msg.restart.cause);
        ASSERT(r_msg->msg.restart.debug->type == s_msg.msg.restart.debug->type);
        ASSERT(r_msg->msg.restart.debug->ap_logs_size == s_msg.msg.debug->ap_logs_size);
        ASSERT(r_msg->msg.restart.debug->bp_logs_size == s_msg.msg.debug->bp_logs_size);
        ASSERT(r_msg->msg.restart.debug->bp_logs_time == s_msg.msg.debug->bp_logs_time);
        ASSERT(r_msg->msg.restart.debug->nb_data == s_msg.msg.restart.debug->nb_data);
        for (size_t j = 0; j < s_msg.msg.restart.debug->nb_data; j++)
            ASSERT(strcmp(r_msg->msg.restart.debug->data[j],
                          s_msg.msg.restart.debug->data[j]) == 0);

        if (i > 0) {
            for (int j = 0; j < i; j++)
                free((char *)s_msg.msg.restart.debug->data[j]);
            free(s_msg.msg.restart.debug->data);
        }
    }

    /* Test 'NOTIFY_DBG' message */
    LOGD("Testing NOTIFY_DBG messages (no debug info, not a real use case :) )");
    s_msg.id = CRM_REQ_NOTIFY_DBG;
    s_msg.msg.debug = NULL;
    ctx[1]->send_msg(ctx[1], &s_msg, p_fd[1]);
    r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);
    ASSERT(r_msg->id == s_msg.id);
    ASSERT(r_msg->msg.debug == NULL);

    LOGD("Testing NOTIFY_DBG messages (0 to MAX_NB_DATA strings)");
    for (int i = 0; i < MDM_CLI_MAX_NB_DATA; i++) {
        s_msg.id = CRM_REQ_NOTIFY_DBG;
        s_msg.msg.debug = &dbg_info;
        dbg_info.type = 1234;
        dbg_info.ap_logs_size = 5678;
        dbg_info.bp_logs_size = 8901;
        dbg_info.bp_logs_time = 2345;
        dbg_info.nb_data = i;

        if (i > 0) {
            const char **data = malloc(i * sizeof(char *));
            ASSERT(data != NULL);
            for (int j = 0; j < i; j++) {
                data[j] = malloc(50);
                ASSERT(data[j] != NULL);
                snprintf((char *)data[j], 50, "test !!! %d %d", i, j);
            }
            dbg_info.data = data;
        }

        ctx[1]->send_msg(ctx[1], &s_msg, p_fd[1]);
        r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);

        ASSERT(r_msg->id == s_msg.id);
        ASSERT(r_msg->msg.debug->type == s_msg.msg.debug->type);
        ASSERT(r_msg->msg.debug->ap_logs_size == s_msg.msg.debug->ap_logs_size);
        ASSERT(r_msg->msg.debug->bp_logs_size == s_msg.msg.debug->bp_logs_size);
        ASSERT(r_msg->msg.debug->bp_logs_time == s_msg.msg.debug->bp_logs_time);
        ASSERT(r_msg->msg.debug->nb_data == s_msg.msg.debug->nb_data);
        for (size_t j = 0; j < s_msg.msg.debug->nb_data; j++)
            ASSERT(strcmp(r_msg->msg.debug->data[j],
                          s_msg.msg.debug->data[j]) == 0);

        if (i > 0) {
            for (int j = 0; j < i; j++)
                free((char *)s_msg.msg.debug->data[j]);
            free(s_msg.msg.debug->data);
        }
    }

    /* Testing 'single id' message in other direction */
    LOGD("Testing 'single id' message (other direction)");
    s_msg.id = MDM_ON;

    ctx[0]->send_msg(ctx[0], &s_msg, p_fd[1]);
    r_msg = ctx[1]->recv_msg(ctx[1], p_fd[0]);

    ASSERT(r_msg->id == s_msg.id);

    /* Testing 'DBG_INFO' message */
    LOGD("Testing DBG_INFO messages (no debug info, not a real use case :) )");
    s_msg.id = MDM_DBG_INFO;
    s_msg.msg.debug = NULL;
    ctx[0]->send_msg(ctx[0], &s_msg, p_fd[1]);
    r_msg = ctx[1]->recv_msg(ctx[1], p_fd[0]);

    ASSERT(r_msg->id == s_msg.id);
    ASSERT(r_msg->msg.debug == NULL);

    LOGD("Testing DBG_INFO messages (0 to MAX_NB_DATA strings)");
    for (int i = 0; i < MDM_CLI_MAX_NB_DATA; i++) {
        s_msg.id = MDM_DBG_INFO;
        s_msg.msg.debug = &dbg_info;
        dbg_info.type = 1234;
        dbg_info.ap_logs_size = 5678;
        dbg_info.bp_logs_size = -8901;
        dbg_info.bp_logs_time = 2345;
        dbg_info.nb_data = i;

        if (i > 0) {
            const char **data = malloc(i * sizeof(char *));
            for (int j = 0; j < i; j++) {
                data[j] = malloc(50);
                snprintf((char *)data[j], 50, "test !!! %d %d", i, j);
            }
            dbg_info.data = data;
        }

        ctx[0]->send_msg(ctx[0], &s_msg, p_fd[1]);
        r_msg = ctx[1]->recv_msg(ctx[1], p_fd[0]);

        ASSERT(r_msg->id == s_msg.id);
        ASSERT(r_msg->msg.debug->type == s_msg.msg.debug->type);
        ASSERT(r_msg->msg.debug->ap_logs_size == s_msg.msg.debug->ap_logs_size);
        ASSERT(r_msg->msg.debug->bp_logs_size == s_msg.msg.debug->bp_logs_size);
        ASSERT(r_msg->msg.debug->bp_logs_time == s_msg.msg.debug->bp_logs_time);
        ASSERT(r_msg->msg.debug->nb_data == s_msg.msg.debug->nb_data);
        for (size_t j = 0; j < s_msg.msg.debug->nb_data; j++)
            ASSERT(strcmp(r_msg->msg.debug->data[j], s_msg.msg.debug->data[j]) == 0);

        if (i > 0) {
            for (int j = 0; j < i; j++)
                free((char *)s_msg.msg.debug->data[j]);
            free(s_msg.msg.debug->data);
        }
    }

    /* Test REGISTER message (serialized APIs) */
    LOGD("Testing REGISTER message (serialized APIs)");
    s_msg.id = CRM_REQ_REGISTER;
    s_msg.msg.register_client.events_bitmap = 0x12345678;
    s_msg.msg.register_client.name = "TEST ME !!!";

    const void *serialized_msg = ctx[1]->serialize_msg(ctx[1], &s_msg, false);
    ctx[1]->send_serialized_msg(ctx[1], serialized_msg, p_fd[1]);
    r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);

    ASSERT(r_msg != NULL);
    ASSERT(r_msg->id == s_msg.id);
    ASSERT(r_msg->msg.register_client.events_bitmap == s_msg.msg.register_client.events_bitmap);
    ASSERT(strcmp(r_msg->msg.register_client.name, s_msg.msg.register_client.name) == 0);

    serialized_msg = ctx[1]->serialize_msg(ctx[1], &s_msg, true);
    ctx[1]->send_serialized_msg(ctx[1], serialized_msg, p_fd[1]);
    r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);

    ASSERT(r_msg != NULL);
    ASSERT(r_msg->id == s_msg.id);
    ASSERT(r_msg->msg.register_client.events_bitmap == s_msg.msg.register_client.events_bitmap);
    ASSERT(strcmp(r_msg->msg.register_client.name, s_msg.msg.register_client.name) == 0);

    free((void *)serialized_msg);

    /* Testing error scenarios */
    LOGD("Testing invalid message lengths");
    int foo = 0;
    write(p_fd[1], &foo, sizeof(foo));
    foo = 0x01000000;
    write(p_fd[1], &foo, sizeof(foo));
    r_msg = ctx[1]->recv_msg(ctx[1], p_fd[0]);
    ASSERT(r_msg == NULL);

    foo = 0;
    write(p_fd[1], &foo, sizeof(foo));
    foo = 0x00000001;
    write(p_fd[1], &foo, sizeof(foo));
    r_msg = ctx[1]->recv_msg(ctx[1], p_fd[0]);
    ASSERT(r_msg == NULL);

    s_msg.id = CRM_REQ_REGISTER;
    s_msg.msg.register_client.events_bitmap = 0x12345678;
    s_msg.msg.register_client.name =
        "this is a very long client name ... yes, yes, very VERY VERY long";
    ctx[1]->send_msg(ctx[1], &s_msg, p_fd[1]);
    r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);
    ASSERT(r_msg == NULL);

    foo = htonl(CRM_REQ_RELEASE);
    write(p_fd[1], &foo, sizeof(foo));
    foo = htonl(12);
    write(p_fd[1], &foo, sizeof(foo));
    write(p_fd[1], &foo, sizeof(foo));
    r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);
    ASSERT(r_msg == NULL);

    s_msg.id = CRM_REQ_RESTART;
    s_msg.msg.restart.cause = 0xdeadcafe;
    s_msg.msg.restart.debug = &dbg_info;
    dbg_info.type = 1234;
    dbg_info.ap_logs_size = 5678;
    dbg_info.bp_logs_size = 8901;
    dbg_info.bp_logs_time = 2345;
    dbg_info.nb_data = MDM_CLI_MAX_NB_DATA + 1;
    dbg_info.data = malloc(dbg_info.nb_data * sizeof(char *));
    for (size_t j = 0; j < dbg_info.nb_data; j++)
        dbg_info.data[j] = ".";
    ctx[1]->send_msg(ctx[1], &s_msg, p_fd[1]);
    r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);
    ASSERT(r_msg == NULL);
    free(s_msg.msg.restart.debug->data);

    s_msg.id = CRM_REQ_RESTART;
    s_msg.msg.restart.cause = 0xdeadcafe;
    s_msg.msg.restart.debug = &dbg_info;
    dbg_info.type = 1234;
    dbg_info.ap_logs_size = 5678;
    dbg_info.bp_logs_size = 8901;
    dbg_info.bp_logs_time = 2345;
    dbg_info.nb_data = 1;
    dbg_info.data = malloc(dbg_info.nb_data * sizeof(char *));
    dbg_info.data[0] =
        "................................................................................"
        "................................................................................"
        "................................................................................"
        "................................................................................"
        "................................................................................"
        "................................................................................"
        "................................................................................"
        "................................................................................"
        "................................................................................"
        "................................................................................";
    ctx[1]->send_msg(ctx[1], &s_msg, p_fd[1]);
    r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);
    ASSERT(r_msg == NULL);
    free(s_msg.msg.restart.debug->data);

    /* Testing time-out scenarios */
    LOGD("Testing time-out scenarios");
    foo = htonl(CRM_REQ_RELEASE);
    write(p_fd[1], &foo, sizeof(foo));
    r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);
    ASSERT(r_msg == NULL);

    foo = htonl(CRM_REQ_RELEASE);
    write(p_fd[1], &foo, sizeof(foo));
    foo = htonl(16);
    write(p_fd[1], &foo, sizeof(foo));
    write(p_fd[1], &foo, sizeof(foo));
    r_msg = ctx[0]->recv_msg(ctx[0], p_fd[0]);
    ASSERT(r_msg == NULL);

    while (true) {
        /* Trying to fill the pipe to do a write time-out */
        s_msg.id = CRM_REQ_REGISTER;
        s_msg.msg.register_client.events_bitmap = 0x12345678;
        s_msg.msg.register_client.name =
            "this is a very long client name ... yes, yes, very VERY VERY long";
        if (ctx[1]->send_msg(ctx[1], &s_msg, p_fd[1]) == -1)
            break;
    }

    close(p_fd[0]);
    close(p_fd[1]);

    LOGD("Test successful !!!");

    ctx[0]->dispose(ctx[0]);
    ctx[1]->dispose(ctx[1]);

    return 0;
}
