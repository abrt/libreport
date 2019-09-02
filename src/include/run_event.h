/*
    Copyright (C) 2009  ABRT team.
    Copyright (C) 2009  RedHat inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#ifndef LIBREPORT_RUN_EVENT_H_
#define LIBREPORT_RUN_EVENT_H_

#include "problem_data.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dump_dir;

struct run_event_state {
    int children_count;

    /* Used only for post-create dup detection. TODO: document its API */
    int (*post_run_callback)(const char *dump_dir_name, void *param);
    void *post_run_param;

    /* Can take ownership of log_line, which is malloced. In this case, return NULL.
     * Otherwise should return log_line (it will be freed by caller)
     *
     * The default value prints log_line with trailing newline to stdout.
     */
    char* (*logging_callback)(char *log_line, void *param);
    void *logging_param;

    /*
     * Called if any error occures during communication with child's command.
     *
     * The default value prints error_line with trailing newline to stderr and
     * exits with an error code.
     *
     * @param error_line An error message
     * @param param a custom param
     */
    void (*error_callback)(const char *error_line, void *param);
    void *error_param;

    /*
     * An optional argument for the following callbacks
     */
    void *interaction_param;

    /*
     * Called when child command produced an alert.
     *
     * The default value is run_event_stdio_alert()
     *
     * @param msg An alert message produced byt child command
     * @param args An interaction param
     */
    void (*alert_callback)(const char *msg, void *interaction_param);

    /*
     * Called when child command ask for some input. A callee
     * should return a text whithout any new line character.
     *
     * The default value is run_event_stdio_ask()
     *
     * @param msg An ask message produced by child command
     * @param args An interaction param
     * @return Must allways return string without new lines, an empty string
     *         if response was not get.
     */
    char *(*ask_callback)(const char *msg, void *interaction_param);

    /*
     * Called when child command wants to know 'yes/no' decision.
     *
     * The default value is run_event_stdio_ask_yes_no()
     *
     * @param msg An ask message produced by child command
     * @param args An implementor args
     * @return Return 0 if an answer is NO, otherwise return nonzero value.
     */
    int (*ask_yes_no_callback)(const char *msg, void *interaction_param);

    /*
     * Called when child command wants to know 'yes/no/yesforever' decision.
     * The yes forever means that in next call the yes answer is returned
     * immediately without asking. The yes forever answer is stored in
     * configuration under a passed key.
     *
     * The default value is run_event_stdio_ask_yes_no_yesforever()
     *
     * @param key An option name used as a key in configuration
     * @param msg An ask message produced by child command
     * @param args An implementor args
     * @return Return 0 if an answer is NO, otherwise return nonzero value.
     */
    int (*ask_yes_no_yesforever_callback)(const char *key, const char *msg, void *interaction_param);

    /*
     * Called when child command wants to know 'yes/no/forever/never' decision.
     * The forever means that in next call the yes answer is returned
     * immediately without asking. The never means that in next call the
     * no answer is returned immediately without asking. The answer is stored
     * in configuration under a passed key.
     *
     * The default value is run_event_stdio_ask_yes_no_save_result()
     *
     * @param key An option name used as a key in configuration
     * @param msg An ask message produced by child command
     * @param args An implementor args
     * @return Return 0 if an answer is NO, otherwise return nonzero value.
     */
    int (*ask_yes_no_save_result_callback)(const char *key, const char *msg, void *interaction_param);

    /*
     * Called when child wants to know a password.
     *
     * The default value is run_event_stdio_ask_password()
     *
     * @param msg An ask message produced by child command
     * @param args An interaction param
     * @return Must allways return string without new lines, an empty string
     *         if password was not get.
     */
    char *(*ask_password_callback)(const char *msg, void *interaction_param);

    GPtrArray *extra_environment;

    /* Internal data for async command execution */
    GList *rule_list;
    pid_t command_pid;
    int command_out_fd;
    int command_in_fd;
    int process_status;
    struct strbuf *command_output;
};
struct run_event_state *new_run_event_state(void);
void free_run_event_state(struct run_event_state *state);

/*
 * Configure callbacks to forward requests
 *
 * @param state A valid run event state pointer
 */
void make_run_event_state_forwarding(struct run_event_state *state);


/* Asynchronous command execution */
struct rule {
    GList *conditions;
    char *command; /* never NULL */
};

/* Returns 0 if no commands at all are defined for event processing, otherwise
 * returns 1. */
int prepare_commands(struct run_event_state *state);
/*
 * Returns -1 if no more commands needs to be executed,
 * else sets state->command_pid and state->command_out_fd and returns >=0.
 * execflags can be e.g. EXECFLG_SETPGID to put the event handling process
 * into a new process group, EXECFLG_SETSID to put it in a new session, etc.
 */
int spawn_next_command(struct run_event_state *state,
                const char *dump_dir_name,
                const char *event,
                unsigned execflags);
/* Cleans up internal state created in prepare_commands */
void free_commands(struct run_event_state *state);

/* Load rule list from config file.
 * Returns a list of all defined rules (struct rule)
 * @param rule_list variable to append to or NONE to create new list
 * @param conf_file_name path to configuration file
 * @param recursion_depth internal recursion protection, should be 0
 */
GList *load_rule_list(GList *rule_list, const char *conf_file_name, unsigned recursion_depth);

/* Cleans up rule list created by load_rule_list */
void free_rule_list(GList *rule_list);

/* Synchronous command execution */

/* The function believes that a state param value is fully initialized and
 * action is started.
 *
 * Returns exit code of action, or nonzero return value of post_run_callback
 * If action is successful, returns 0.
 *
 * If return value is lower than 0 and you set O_NONBLOCK to command's out fd
 * examine errno to detect EAGAIN case. Incomplete child lines are buffered
 * in the state param.
 */
int consume_event_command_output(struct run_event_state *state, const char *dump_dir_name);

/* Returns exit code of first failed action, or first nonzero return value
 * of post_run_callback. If all actions are successful, returns 0.
 */
int run_event_on_dir_name(struct run_event_state *state, const char *dump_dir_name, const char *event);
int run_event_on_problem_data(struct run_event_state *state, problem_data_t *data, const char *event);


/* Querying for possible events */

/* Scans event.conf for events starting with pfx which are applicable
 * to dd, or (if dd is NULL), to dump_dir.
 * Returns a malloced string with '\n'-terminated event names.
 */
char *list_possible_events(struct dump_dir *dd, const char *dump_dir_name, const char *pfx);

/* Like list_possible_events but accepts problem_data_t */
char *list_possible_events_problem_data(problem_data_t *pd, const char *dump_dir_name, const char *pfx);

/*
 * Returns a list of possible events for given problem directory
 *
 * @param problem_dir_name the name of the problem directory
 * @param pfx the prefix of the events "report", "workflow"
 */
GList *list_possible_events_glist(const char *problem_dir_name,
                                  const char *pfx);

/* Like list_possible_events_glist but accepts problem_data_t */
GList *list_possible_events_problem_data_glist(problem_data_t *pd,
                                  const char *problem_dir_name,
                                  const char *pfx);

/* Command line run event callback implemenetation */

/*
 * Prints the msg param on stdout
 *
 * @param msg a printed message
 * @param param ONLY NULL IS ALLOWED; other values are intended for internal use only
 */
void run_event_stdio_alert(const char *msg, void *param);

/*
 * Prints the msg param on stdout and reads a response from stdin
 *
 * @param msg a printed message
 * @param param ONLY NULL IS ALLOWED; other values are intended for internal use only
 * @return a malloced string with response, an empty string on error or no response
 */
char *run_event_stdio_ask(const char *msg, void *param);

/*
 * Prints the msg param on stdout and reads a response from stdin
 *
 * @param msg a printed message
 * @param param ONLY NULL IS ALLOWED; other values are intended for internal use only
 * @return 0 if user's answer is 'no', otherwise non 0 value
 */
int run_event_stdio_ask_yes_no(const char *msg, void *param);

/*
 * Prints the msg param on stdout and reads a response from stdin. To store the
 * yes forever answer uses libreport's user settings API. Therefore if you want
 * to get the yes forever stuff working you have to call load_user_setting()
 * function before this function call and call save_user_settings() function
 * after this function call.
 *
 * @param msg a printed message
 * @param key a key under which the yes forever answer is stored
 * @param param ONLY NULL IS ALLOWED; other values are intended for internal use only
 * @return 0 if user's answer is 'no', otherwise non 0 value
 */
int run_event_stdio_ask_yes_no_yesforever(const char *msg, const char *key, void *param);

/*
 * Prints the msg param on stdout and reads a response from stdin. To store the
 * forever or never answer uses libreport's user settings API.
 * Therefore if you want to get the forever or never stuff working you
 * have to call load_user_setting() function before this function call and call
 * save_user_settings() function after this function call.
 *
 * @param msg a printed message
 * @param key a key under which the forever (yes) or never (no) answer is stored
 * @param param ONLY NULL IS ALLOWED; other values are intended for internal use only
 * @return 0 if user's answer is 'no', otherwise non 0 value
 */
int run_event_stdio_ask_yes_no_save_result(const char *msg, const char *key, void *param);

/*
 * Prints the msg param on stdout and reads a response from stdin
 *
 * @param msg a printed message
 * @param param ONLY NULL IS ALLOWED; other values are intended for internal use only
 * @return a malloced string with response, an empty string on error or no response
 */
char *run_event_stdio_ask_password(const char *msg, void *param);


/* A simple helper */
#define exit_status_as_string libreport_exit_status_as_string
char *exit_status_as_string(const char *progname, int status);


#ifdef __cplusplus
}
#endif

#endif
