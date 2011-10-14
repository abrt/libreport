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

#define ask libreport_ask
char *ask(const char *question);

#define ask_password libreport_ask_password
char *ask_password(const char *question)

#define alert libreport_alert
void alert(const char *message);

#ifdef __cplusplus
}
#endif

#endif
