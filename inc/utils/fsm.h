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

#ifndef __CRM_UTILS_FSM_HEADER__
#define __CRM_UTILS_FSM_HEADER__

/**
 * Structure used to define the FSM
 *
 * @func operation      Operation to be performed according to the state and the event.
 *
 *                      @param [in]  fsm_param   FSM operation parameter. Argument provided with
 *                                               init function
 *                      @param [in]  event_param Event parameter. Optional data
 *
 *                      @return >=0  New state   Can be overwritten by force_new_state
 *                      @return  -1  No update   Keep previous state
 *                                               Can be overwritten by force_new_state
 *                      @return <=-2 Error case  Failsafe operation is called.
 *                                               Cannot be overwritten by force_new_state
 * @var force_new_state If different than -1, it overwrites the state returned by the 'operation'
 *                      function call
 */
typedef struct crm_fsm_ops {
    int force_new_state;
    int (*operation)(void *fsm_param, void *event_param);
} crm_fsm_ops_t;

typedef struct crm_fsm_ctx crm_fsm_ctx_t;

/**
 * Initializes the FSM module and starts it.
 *
 * @param [in] fsm                Array of (num_states * num_events) elements defining the FSM.
 *                                To reduce memory fragmentation the content is not copied,
 *                                must be a static const.
 * @param [in] num_events         Number of events defined in fsm
 * @param [in] num_states         Number of states defined in fsm
 * @param [in] initial_state      Initial state
 * @param [in] pre_op             Optional. If not NULL, this function is called before calling the
 *                                operation function
 * @param [in] state_trans        Optional. If not NULL, this function is called when the operation
 *                                has trigged a new state. Both previous and (future) current state
 *                                are provided
 * @param [in] failsafe_operation Failsafe operation. When an error is detected, this operation is
 *                                performed. Failsafe operation must return the new state.
 * @param [in] logging_tag        Logging tag
 * @param [in] fsm_param          FSM operation parameter
 * @param [in] get_state_txt      Function returning the name of the state
 * @param [in] get_event_txt      Function returning the event
 *
 * @return a valid handle. Must be freed by calling the dispose function
 */
/* *INDENT-OFF* */
crm_fsm_ctx_t *crm_fsm_init(const crm_fsm_ops_t *fsm, int num_events, int num_states, int initial_state,
                        void (*pre_op)(int event, void *fsm_param),
                        void (*state_trans)(int prev_state, int new_state, int event, void *fsm_param, void *evt_param),
                        int (*failsafe_op)(void *fsm_param, void *evt_param),
                        void *fsm_param, const char *logging_tag,
                        const char *(*get_state_txt)(int), const char *(*get_event_txt)(int));
/* *INDENT-ON* */

struct crm_fsm_ctx {
    /**
     * Disposes the module
     * NB: Synchronous API
     *
     * @param [in] ctx Module context
     */
    void (*dispose)(crm_fsm_ctx_t *ctx);

    /**
     * Notify an event to the fsm. This function asserts in case of error
     *
     * @param [in] ctx        Module context
     * @param [in] evt        Event ID
     * @param [in] evt_param  Event optional data
     */
    void (*notify_event)(crm_fsm_ctx_t *ctx, int evt, void *evt_param);
};

#endif /* __CRM_UTILS_FSM_HEADER__ */
