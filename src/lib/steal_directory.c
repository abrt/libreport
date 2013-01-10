/*
    Copyright (C) 2011  ABRT Team
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
#include "internal_libreport.h"

struct dump_dir *steal_directory(const char *base_dir, const char *dump_dir_name)
{
    const char *base_name = strrchr(dump_dir_name, '/');
    if (base_name)
    {
        if (base_name[1] == '\0')
        {
            /* Drats. It has trailing slash(es) */
            /* Skip all trailing slashes */
            while (base_name > dump_dir_name && *--base_name == '/')
                continue;
            /* Find previous one */
            for (;;)
            {
                if (*base_name == '/')
                    break;
                base_name--;
                if (base_name < dump_dir_name)
                    /* It has ONLY trailing slash(es) */
                    break;
            }
        }
        base_name++;
    }
    else
        base_name = dump_dir_name;

    struct dump_dir *dd_dst;
    unsigned count = 100;
    char *dst_dir_name = concat_path_file(base_dir, base_name);
    while (1)
    {
        dd_dst = dd_create(dst_dir_name, (uid_t)-1, DEFAULT_DUMP_DIR_MODE);
        free(dst_dir_name);
        if (dd_dst)
            break;
        if (--count == 0)
        {
            error_msg("Can't create new dump dir in '%s'", base_dir);
            return NULL;
        }
        struct timeval tv;
        gettimeofday(&tv, NULL);
        dst_dir_name = xasprintf("%s/%s.%u", base_dir, base_name, (int)tv.tv_usec);
    }

    VERB1 log("Creating copy in '%s'", dd_dst->dd_dirname);
    if (copy_file_recursive(dump_dir_name, dd_dst->dd_dirname) < 0)
    {
        /* error. copy_file_recursive already emitted error message */
        /* Don't leave half-copied dir lying around */
        dd_delete(dd_dst);
        return NULL;
    }

    return dd_dst;
}

struct dump_dir *open_directory_for_writing(
                            const char *dump_dir_name,
                            bool (*ask)(const char *, const char *))
{
    struct dump_dir *dd = dd_opendir(dump_dir_name, DD_OPEN_READONLY);

    if (!dd)
        xfunc_die(); /* error msg was already logged */

    if (dd->locked)
        return dd;

    log("'%s' is not writable", dump_dir_name);
    dd_close(dd);

    char *spooldir = concat_path_file(g_get_user_cache_dir(), "abrt/spool");

    if (ask && !ask(spooldir, dump_dir_name))
        return NULL;

    dd = steal_directory(spooldir, dump_dir_name);
    free(spooldir);

    if (!dd)
        return NULL;

    bool dd_was_cwd = false;
    {
        char old_cwd[PATH_MAX + 1];
        /* must get CWD before deleting */
        if (getcwd(old_cwd, sizeof(old_cwd)))
            dd_was_cwd = strcmp(old_cwd, dump_dir_name) == 0;
        else
            perror_msg("getcwd()");
    }

    /* Delete old dir and switch to new one.
     * Don't want to keep new dd open across deletion,
     * therefore it's a bit more complicated.
     */
    delete_dump_dir_possibly_using_abrtd(dump_dir_name);
    char *new_name = xstrdup(dd->dd_dirname);
    dd_close(dd);
    dd = dd_opendir(new_name, 0);
    free(new_name);

    if (!dd)
        xfunc_die(); /* error msg was already logged */

    /* Update CWD to the new dump dir path if CWD is the stolen dump dir */
    /* Nonexisting CWD breaks lot of things (i.e. gnome-open can't open URL)*/
    if (dd_was_cwd && chdir(dd->dd_dirname))
        perror_msg("chdir()");

    return dd;
}
