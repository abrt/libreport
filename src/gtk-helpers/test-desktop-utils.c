/*
    Copyright (C) 2009  Jiri Moskovcak (jmoskovc@redhat.com)
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

#include <locale.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include "problem_utils.h"

int main(int argc, char** argv)
{
    GAppInfo *app;

    setlocale(LC_ALL, "");

    if (argc != 2)
    {
        g_print ("Usage: %s CMDLINE\n", argv[0]);
        return 1;
    }

    app = problem_create_app_from_cmdline (argv[1]);
    if (!app)
    {
        g_print ("Could not find an app for cmdline '%s'\n", argv[1]);
        return 1;
    }
    g_print ("Found desktop file: %s\n", g_desktop_app_info_get_filename (G_DESKTOP_APP_INFO (app)));

    return 0;
}
