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
int libreport_set_echo(int enable)
{
    struct termios t;
    int chvalue = 0;
    if (tcgetattr(STDIN_FILENO, &t) < 0)
        return 0;

    /* ECHO flag change if needed */
    if ((!(t.c_lflag & ECHO)) == enable)
    {
        t.c_lflag ^= ECHO;
        chvalue = 1;
    }
    /* ECHONL flag change if needed */
    if ((!(t.c_lflag & ECHONL)) != enable)
    {
        t.c_lflag ^= ECHONL;
        chvalue = 1;
    }

    if (!chvalue)
        return 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) < 0)
        perror_msg_and_die("tcsetattr");

    return 1;
}

int libreport_ask_yes_no(const char *question)
{
    INITIALIZE_LIBREPORT();

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

int libreport_ask_yes_no_yesforever(const char *key, const char *question)
{
    INITIALIZE_LIBREPORT();

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

int libreport_ask_yes_no_save_result(const char *key, const char *question)
{
    INITIALIZE_LIBREPORT();

    const char *yes = _("y");
    const char *no = _("N");
    const char *forever = _("f");
    const char *never = _("e");

    {   /* Use response from REPORT_CLIENT_RESPONSE environment variable.
         *
         * The forever and never response is not allowed in this case.
         * There is no serious reason for that, it is just decision.
         * (It doesn't make much sense to allow the *_forever answer here.)
         */
        const char *env_response = getenv("REPORT_CLIENT_RESPONSE");
        if (env_response)
            return strncasecmp(yes, env_response, strlen(yes)) == 0;
    }

    {   /* Load an value for the key from user setting.
         * YES means forever
         * NO means never
         * 'no_value' means ask me
         */
        const char *option = get_user_setting(key);
        if (option)
            return string_to_bool(option);
    }

    if (is_slave_mode())
        printf(REPORT_PREFIX_ASK_YES_NO_SAVE_RESULT "%s %s\n", key, question);
    else
        printf("%s [%s/%s/%s/%s] ", question, yes, no, forever, never);

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
        set_user_setting(key, "yes");
        return 1;
    }
    else if ((is_slave_mode() && response[0] == 'e') || strncasecmp(never, response, strlen(never)) == 0)
    {
        set_user_setting(key, "no");
        return 0;
    }

    return ((is_slave_mode() && response[0] == 'y') || strncasecmp(yes, response, strlen(yes)) == 0);
}

char *libreport_ask(const char *question)
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

char *libreport_ask_password(const char *question)
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

    bool changed = libreport_set_echo(false);

    char *result = xmalloc_fgets(stdin);
    strtrimch(result, '\n');

    if (changed)
        libreport_set_echo(true);

    return result;
}

void libreport_alert(const char *message)
{
    if (is_slave_mode())
        printf(REPORT_PREFIX_ALERT);

    puts(message);
    fflush(stdout);
}

void libreport_client_log(const char *message)
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
