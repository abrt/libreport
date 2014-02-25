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

static int is_noninteractive_mode()
{
    return getenv("REPORT_CLIENT_NONINTERACTIVE") != NULL;
}

/* Returns 1 if echo has been changed from another state. */
int set_echo(int enable)
{
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) < 0)
        return 0;

    /* No change needed? */
    if (!(t.c_lflag & ECHO) == !enable)
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

    if (!is_slave_mode() && is_noninteractive_mode())
    {
        putchar('\n');
        fflush(stdout);
        return 0;
    }

    char response[16];
    if (NULL == fgets(response, sizeof(response), stdin))
        return 0;

    return ((is_slave_mode() && response[0] == 'y') || strncasecmp(yes, response, strlen(yes)) == 0);
}

int ask_yes_no_yesforever(const char *key, const char *question)
{
#if ENABLE_NLS
    textdomain(PACKAGE);
#endif
    const char *yes = _("y");
    const char *no = _("N");
    const char *forever = _("f");

    {   /* Use response from REPORT_CLIENT_RESPONSE environment variable.
         *
         * The forever response is not allowed in this case.
         * There is no serious reason for that, it is just decision.
         * (It doesn't make much sense to allow the forever answer here.)
         */
        const char *env_response = getenv("REPORT_CLIENT_RESPONSE");
        if (env_response)
            return strncasecmp(yes, env_response, strlen(yes)) == 0;
    }

    {   /* Load an value for the key from user setting.
         * NO means 'Don't ask me again, I said yes forever'.
         */
        const char *option = get_user_setting(key);
        if (option && string_to_bool(option) == false)
            return 1;
    }

    if (is_slave_mode())
        printf(REPORT_PREFIX_ASK_YES_NO_YESFOREVER "%s %s\n", key, question);
    else
        printf("%s [%s/%s/%s] ", question, yes, no, forever);

    fflush(stdout);

    if (!is_slave_mode() && is_noninteractive_mode())
    {
        putchar('\n');
        fflush(stdout);
        return 0;
    }

    char response[16];
    if (NULL == fgets(response, sizeof(response), stdin))
        return 0;

    if ((is_slave_mode() && response[0] == 'f') || strncasecmp(forever, response, strlen(forever)) == 0)
    {
        /* NO means 'Don't ask me again, I said yes forever'. */
        set_user_setting(key, "no");
        return 1;
    }
    else
        set_user_setting(key, "yes");

    return ((is_slave_mode() && response[0] == 'y') || strncasecmp(yes, response, strlen(yes)) == 0);
}

char *ask(const char *question)
{
    if (is_slave_mode())
        printf(REPORT_PREFIX_ASK "%s\n", question);
    else
        printf("%s ", question);

    fflush(stdout);

    if (!is_slave_mode() && is_noninteractive_mode())
    {
        putchar('\n');
        fflush(stdout);
        return xstrdup("");
    }

    char *result = xmalloc_fgets(stdin);
    strtrimch(result, '\n');

    return result;
}

char *ask_password(const char *question)
{
    if (is_slave_mode())
        printf(REPORT_PREFIX_ASK_PASSWORD "%s\n", question);
    else
        printf("%s ", question);

    fflush(stdout);

    if (!is_slave_mode() && is_noninteractive_mode())
    {
        putchar('\n');
        fflush(stdout);
        return xstrdup("");
    }

    bool changed = set_echo(false);

    char *result = xmalloc_fgets(stdin);
    strtrimch(result, '\n');

    if (changed)
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

void client_log(const char *message)
{
    if (message != NULL
        && (message[0] == '.' && message[1] == '\0')
        && !is_slave_mode()
       )
        putchar('.');
    else
        printf("%s\n", message);

    fflush(stdout);
}
