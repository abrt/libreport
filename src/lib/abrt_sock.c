/*
    Copyright (C) 2010  ABRT team
    Copyright (C) 2010  RedHat Inc

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
#include <sys/un.h>
#include "internal_libreport.h"

#define SOCKET_FILE  VAR_RUN"/abrt/abrt.socket"

static int connect_to_abrtd_and_call_DeleteDebugDump(const char *dump_dir_name)
{
    int result = 1; /* error so far */

    int socketfd = xsocket(AF_UNIX, SOCK_STREAM, 0);
    /*close_on_exec_on(socketfd); - not needed, we are closing it soon */
    struct sockaddr_un local;
    memset(&local, 0, sizeof(local));
    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, SOCKET_FILE);
    int r = connect(socketfd, (struct sockaddr*)&local, sizeof(local));
    if (r == 0)
    {
        full_write(socketfd, "DELETE ", strlen("DELETE "));
        full_write(socketfd, dump_dir_name, strlen(dump_dir_name));
        full_write(socketfd, " HTTP/1.1\r\n\r\n", strlen(" HTTP/1.1\r\n\r\n"));
        shutdown(socketfd, SHUT_WR);

        char response[64];
        r = full_read(socketfd, response, sizeof(response) - 1);
        if (r >= 0)
        {
            VERB1 log("Response via socket:'%.*s'", r, response);
            /*  0123456789...  */
            /* "HTTP/1.1 200 " */
            response[5] = '1';
            response[7] = '1';
            result = strncmp(response, "HTTP/1.1 200 ", sizeof("HTTP/1.1 200 "));
        }
    }
    else
    {
        perror_msg("Can't connect to '%s'", SOCKET_FILE);
    }
    close(socketfd);

    return result;
}

int delete_dump_dir_possibly_using_abrtd(const char *dump_dir_name)
{
    /* Try to delete it ourselves */
    struct dump_dir *dd = dd_opendir(dump_dir_name, DD_OPEN_READONLY);
    if (dd)
    {
        if (dd->locked) /* it is not readonly */
            return dd_delete(dd);
        dd_close(dd);
    }

    VERB1 log("Deleting '%s' via abrtd", dump_dir_name);
    return connect_to_abrtd_and_call_DeleteDebugDump(dump_dir_name);
}
