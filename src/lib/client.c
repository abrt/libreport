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

#include "client.h"
#include "internal_libreport.h"

static int is_slave_mode()
{
    return getenv("REPORT_CLIENT_SLAVE") != NULL;
}

/* Returns 1 if echo has been changed from another state. */
int set_echo(int enable)
{
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) < 0)
        return 0;

    /* No change needed? */
    if ((t.c_lflag & ECHO) == enable)
        return 0;

    t.c_lflag ^= ECHO;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) < 0)
        perror_msg_and_die("tcsetattr");

    return 1;
}

int ask_yes_no(const char *question)
{
#if ENABLE_NLS
    textdomain(PACKAGE);
#endif
    const char *yes = _("y");
    const char *no = _("N");

    char *env_response = getenv("REPORT_CLIENT_RESPONSE");
    if (env_response)
        return strncasecmp(yes, env_response, strlen(yes)) == 0;

    if (is_slave_mode())
        printf(REPORT_PREFIX_ASK_YES_NO "%s\n", question);
    else
        printf("%s [%s/%s] ", question, yes, no);

    fflush(stdout);

    char response[16];
    if (NULL == fgets(response, sizeof(response), stdin))
        return 0;

    return strncasecmp(yes, response, strlen(yes)) == 0;
}

char *ask(const char *question, char *response, int response_len)
{
    if (is_slave_mode())
        printf(REPORT_PREFIX_ASK "%s\n", question);
    else
        printf("%s ", question);

    fflush(stdout);

    return fgets(response, response_len, stdin);
}

char *ask_password(const char *question, char *response, int response_len)
{
    if (is_slave_mode())
        printf(REPORT_PREFIX_ASK_PASSWORD "%s\n", question);
    else
        printf("%s ", question);

    fflush(stdout);

    set_echo(false);
    char *result = fgets(response, response_len, stdin);
    set_echo(true);

    return result;
}

void alert(const char *message)
{
    if (is_slave_mode())
        printf(REPORT_PREFIX_ALERT);

    puts(message);
    fflush(stdout);
}
