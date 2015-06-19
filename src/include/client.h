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

#define set_echo libreport_set_echo
int set_echo(int enable);

#define ask_yes_no libreport_ask_yes_no
int ask_yes_no(const char *question);

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
 * ask_yes_no_yesforever() function is called from a library function, the
 * configuration key will be stored in the configuration of the application
 * which called that library function.
 *
 * In the case you are developing a front-end application, you have to call the
 * load_user_setting() function before calling the ask_yes_no_yesforever()
 * function and call the save_user_settings() function after calling the
 * ask_yes_no_yesforever() function to make the 'yesforever' reply persistent.
 *
 * @param key The key under which the yes forever answer is stored
 * @param question The asked question
 * @return 0 if user's answer is 'no', otherwise non 0 value
 */
#define ask_yes_no_yesforever libreport_ask_yes_no_yesforever
int ask_yes_no_yesforever(const char *key, const char *question);

/**
 * The function behaves exactly like the ask_yes_no_yesforever() function, but
 * allows to remember both "Yes" and "No" replies.
 */
#define ask_yes_no_save_resutl libreport_ask_yes_no_save_result
int ask_yes_no_save_result(const char *key, const char *question);

#define ask libreport_ask
char *ask(const char *question);

#define ask_password libreport_ask_password
char *ask_password(const char *question);

#define alert libreport_alert
void alert(const char *message);

#define client_log libreport_client_log
void client_log(const char *message);

#ifdef __cplusplus
}
#endif

#endif
