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

int ask_yes_no(const char *question)
{
    const char *yes = _("y");
    const char *no = _("N");

    char *env_response = getenv("REPORT_CLIENT_RESPONSE");
    if (env_response)
    {
        if (strncasecmp(yes, env_response, strlen(yes)) == 0)
            return true;
        if (strncasecmp(no, env_response, strlen(no)) == 0)
            return false;
    }

    char response[16];
    if (NULL == fgets(response, sizeof(response), stdin))
        return false;

    return strncasecmp(yes, response, strlen(yes)) == 0;
}

char *ask(const char *question, char *response, int response_len)
{
    if (is_slave_mode())
        printf("ASK ");

    puts(question);

    return fgets(response, response_len, stdin);
}

void alert(const char *message)
{
    if (is_slave_mode())
        printf("ALERT ");

    puts(message);
}
