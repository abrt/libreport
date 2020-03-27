/*
    Copyright (C) 2011  ABRT team.
    Copyright (C) 2011  RedHat inc.

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


    @brief API for interaction with users

    These functions should be used by all libreport plugins to make it possible
    to use the plugins in EVENT scripts.

    ## Communication Protocol

    These functions work in two modes:
     - MASTER : the called function will interact with user
     - SLAVE  : the called function will interact with the master process

    If the function is called in the slave mode, the function will prefix its
    output with the relevant "REPORT_PREFIX_*" macro defined below. These
    prefixes are detected by the master process so the master can react
    appropriately to the slave's output.

    In the master mode, the functions does not taint its output because
    the output will be directly shown to users.

    The mode is driven by the environment variable **REPORT_CLIENT_SLAVE**
    If the variable is set to any value, the mode is SLAVE. In all other cases
    the mode is MASTER.


    ## Default answers

    Another environment variable that controls behaviour of these functions is
    **REPORT_CLIENT_NONINTERACTIVE**. If this variable is set to some value and
    the mode is not SLAVE, the function call does not wait for response from
    user but returns immediately with a default return value. The default
    values must not cause any harm to users, so the boolean functions returns
    false (the default answer is "No") and the string function returns ""
    (the default answer is no answer).
*/

#ifndef LIBREPORT_CLIENT_H_
#define LIBREPORT_CLIENT_H_


#define REPORT_PREFIX_ASK_YES_NO "ASK_YES_NO "
/**
 * The REPORT_PREFIX_ASK_YES_NO_YESFOREVER prefix must be followed by a single
 * word used as key. If the prefix is followed only by the key the
 * REPORT_PREFIX_ASK_YES_NO implementation is used instead.
 *
 * Example:
 *   ASK_YES_NO_YESFOREVER ask_before_delete Do you want to delete selected files?
 *
 * Example of message handled as REPORT_PREFIX_ASK_YES_NO:
 *   ASK_YES_NO_YESFOREVER Continue?
 *
 * The receiver of the message is responsible for remembering of
 * the 'YESFOREVER' reply.
 */
#define REPORT_PREFIX_ASK_YES_NO_YESFOREVER "ASK_YES_NO_YESFOREVER "
/**
 * The REPORT_PREFIX_ASK_YES_NO_SAVE_RESULT is similar to
 * the REPORT_PREFIX_ASK_YES_NO_YESFOREVER except it allows to remember both
 * replies.
 */
#define REPORT_PREFIX_ASK_YES_NO_SAVE_RESULT "ASK_YES_NO_SAVE_RESULT "
#define REPORT_PREFIX_ASK "ASK "
#define REPORT_PREFIX_ASK_PASSWORD "ASK_PASSWORD "
#define REPORT_PREFIX_ALERT "ALERT "

#ifdef __cplusplus
extern "C" {
#endif

int libreport_set_echo(int enable);

int libreport_ask_yes_no(const char *question);

/**
 * Prints out the question and if the reply is 'yesforever' temporarily stores
 * the reply, so the next time this function is called with the key, no user input
 * is required and the function immediately returns 'Yes'.
 *
 * Persistence:
 * The slaves (the libreport event scripts) should rely on persistence provided
 * by applications, but are not required to do so.
 *
 * The function uses the global libreport application configuration. If the
 * libreport_ask_yes_no_yesforever() function is called from a library function, the
 * configuration key will be stored in the configuration of the application
 * which called that library function.
 *
 * In the case you are developing a front-end application, you have to call the
 * load_user_setting() function before calling the libreport_ask_yes_no_yesforever()
 * function and call the libreport_save_user_settings() function after calling the
 * libreport_ask_yes_no_yesforever() function to make the 'yesforever' reply persistent.
 *
 * @param key The key under which the yes forever answer is stored
 * @param question The asked question
 * @return 0 if user's answer is 'no', otherwise non 0 value
 */
int libreport_ask_yes_no_yesforever(const char *key, const char *question);

/**
 * The function behaves exactly like the libreport_ask_yes_no_yesforever() function, but
 * allows to remember both "Yes" and "No" replies.
 */
int libreport_ask_yes_no_save_result(const char *key, const char *question);

char *libreport_ask(const char *question);

char *libreport_ask_password(const char *question);

void libreport_alert(const char *message);

void libreport_client_log(const char *message);

#ifdef __cplusplus
}
#endif

#endif
