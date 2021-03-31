/*
    Copyright (C) ABRT Team
    Copyright (C) RedHat inc.

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

#include <gio/gdesktopappinfo.h>
#include <string.h>
#include <stdlib.h>
#include "problem_utils.h"

/* This function allocates resources, so the caller must
 * free the returned value using g_free */
char *
problem_get_argv0 (const char *cmdline)
{
    char *ret;
    char **items;

    items = g_strsplit (cmdline, " ", -1);
    ret = g_strdup (items[0]);
    g_strfreev (items);
    return ret;
}

static void
remove_quotes (char **args)
{
    guint i;

    for (i = 0; args[i] != NULL; i++)
    {
        char **items;
        char *str;

        items = g_strsplit (args[i], "\"", -1);
        str = g_strjoinv (NULL, items);
        g_strfreev (items);

        g_free (args[i]);
        args[i] = str;
    }
}

static gboolean
_is_it_file_arg (const char *s)
{
    if (s == NULL)
        return FALSE;
    if (*s == '-')
        return FALSE;
    return TRUE;
}

static gboolean
_is_it_url (const char *s)
{
    if (s == NULL)
        return FALSE;
    if (strstr (s, "://"))
        return TRUE;
    return FALSE;
}

static gboolean
compare_args (char **cmdargs,
          char **dcmdargs)
{
    guint cargi, dargi;
    gboolean ret;

    /* Start at 1, as we already compared the binaries */
    cargi = dargi = 1;
    while (dargi < g_strv_length(dcmdargs))
    {
        if (g_str_equal (dcmdargs[dargi], "%f"))
        {
            if (cargi >= g_strv_length(cmdargs) || _is_it_file_arg(cmdargs[cargi]))
            {
                return FALSE;
            } else {
                dargi++;
                cargi++;
            }
        }
        else if (g_str_equal (dcmdargs[dargi], "%F"))
        {
            if (cargi >= g_strv_length(cmdargs) || _is_it_file_arg(cmdargs[cargi]))
                dargi++;
            else
                cargi++;
        }
        else if (g_str_equal (dcmdargs[dargi], "%u"))
        {
            if (cargi >= g_strv_length(cmdargs) ||
                (!_is_it_url(cmdargs[cargi]) && !_is_it_file_arg(cmdargs[cargi])))
            {
                return FALSE;
            }
            else
            {
                cargi++;
                dargi++;
            }
        }
        else if (g_str_equal (dcmdargs[dargi], "%U"))
        {
            if (cargi >= g_strv_length(cmdargs) ||
                (!_is_it_url(cmdargs[cargi]) && !_is_it_file_arg(cmdargs[cargi])))
            {
                dargi++;
            }
            else
            {
                cargi++;
            }
        }
        else if (g_str_equal (dcmdargs[dargi], "%i"))
        {
            //logging.debug("Unsupported Exec key %i");
            dargi++;
            cargi += 2;
        }
        else if (g_str_equal (dcmdargs[dargi], "%c") ||
                 g_str_equal (dcmdargs[dargi], "%k"))
        {
            //logging.debug("Unsupported Exec key %s", dcmdargs[dargi]);
            dargi++;
            cargi++;
        }
        else
        {
            if (cargi >= g_strv_length(cmdargs) || !g_str_equal (dcmdargs[dargi], cmdargs[cargi]))
                return FALSE;
            dargi++;
            cargi++;
        }
    }

    ret = (cargi == g_strv_length(cmdargs) && dargi == g_strv_length(dcmdargs));

    return ret;
}

static gboolean
compare_binaries (char *cmd,
          const char *dcmd)
{
    g_autofree char *lhs_basename = NULL;
    g_autofree char *rhs_basename = NULL;

    if (g_strcmp0 (cmd, dcmd) == 0)
        return TRUE;

    lhs_basename = g_path_get_basename (cmd);
    rhs_basename = g_path_get_basename (dcmd);

    return g_strcmp0 (lhs_basename, rhs_basename) == 0;
}

GAppInfo *
problem_create_app_from_cmdline (const char *cmdline)
{
    GAppInfo *app;
    GList *apps, *l;
    GList *shortlist;
    char *binary;
    char **cmdargs;

    binary = problem_get_argv0(cmdline);

    apps = g_app_info_get_all ();
    shortlist = NULL;
    app = NULL;
    for (l = apps; l != NULL; l = l->next)
    {
        GAppInfo *a = l->data;

        if (!g_app_info_should_show(a))
            continue;

        if (!compare_binaries (binary, g_app_info_get_executable (a)))
            continue;

        shortlist = g_list_prepend (shortlist, a);
    }

    if (shortlist == NULL)
    {
        g_list_free_full (apps, g_object_unref);
        return NULL;
    }

    cmdargs = g_strsplit (cmdline, " ", -1);
    remove_quotes (cmdargs);

    for (l = shortlist; l != NULL; l = l->next)
    {
        GAppInfo *a = l->data;
        char **dcmdargs;

        const char *commandline = g_app_info_get_commandline (a);
        if (commandline == NULL)
            continue;

        dcmdargs = g_strsplit (commandline, " ", -1);
        remove_quotes (dcmdargs);

        if (compare_args (cmdargs, dcmdargs))
            app = g_object_ref (a);

        g_strfreev (dcmdargs);
        if (app != NULL)
            break;
    }

    g_strfreev(cmdargs);
    g_list_free (shortlist);
    g_list_free_full (apps, g_object_unref);
    g_free(binary);
    return app;
}

#define GIO_LAUNCHED_DESKTOP_FILE_PREFIX     "GIO_LAUNCHED_DESKTOP_FILE="
#define GIO_LAUNCHED_DESKTOP_FILE_PID_PREFIX "GIO_LAUNCHED_DESKTOP_FILE_PID="

GAppInfo *
problem_create_app_from_env (const char **envp,
			     pid_t        pid)
{
    GDesktopAppInfo *app;
    guint i;
    const char *desktop, *epid;

    if (envp == NULL)
        return NULL;
    if (pid < 0)
        return NULL;

    desktop = epid = NULL;
    for (i = 0; envp[i] != NULL; i++)
    {
        if (g_str_has_prefix (envp[i], GIO_LAUNCHED_DESKTOP_FILE_PREFIX))
            desktop = envp[i] + strlen (GIO_LAUNCHED_DESKTOP_FILE_PREFIX);
        else if (g_str_has_prefix (envp[i], GIO_LAUNCHED_DESKTOP_FILE_PID_PREFIX))
            epid = envp[i] + strlen (GIO_LAUNCHED_DESKTOP_FILE_PID_PREFIX);

        if (desktop && epid)
            break;
    }

    if (!desktop || !epid)
        return NULL;

    /* Verify PID */
    if (atoi (epid) != pid)
        return NULL;

    if (*desktop == '/')
        app = g_desktop_app_info_new_from_filename (desktop);
    else
        app = g_desktop_app_info_new (desktop);

    return (GAppInfo *) app;
}

