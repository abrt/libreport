/*
    Copyright (C) 2010  ABRT team
    Copyright (C) 2010  RedHat inc.

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

#define NEW_PD_SUFFIX ".new"

static struct dump_dir *try_dd_create(const char *base_dir_name, const char *dir_name)
{
    char *path = concat_path_file(base_dir_name, dir_name);
    struct dump_dir *dd = dd_create(path, (uid_t)-1L, 0640);
    free(path);
    return dd;
}

struct dump_dir *create_dump_dir_from_problem_data(problem_data_t *problem_data, const char *base_dir_name)
{
    char *type = problem_data_get_content_or_NULL(problem_data, FILENAME_ANALYZER);

    if (!type)
    {
        error_msg(_("Missing required item: '%s'"), FILENAME_ANALYZER);
        return NULL;
    }

    char *problem_id = xasprintf("%s-%s-%lu"NEW_PD_SUFFIX, type, iso_date_string(NULL), (long)getpid());

    VERB2 log("Saving to %s/%s", base_dir_name, problem_id);

    struct dump_dir *dd;
    if (base_dir_name)
        dd = try_dd_create(base_dir_name, problem_id);
    else
    {
        /* Try /var/run/abrt */
        dd = try_dd_create(LOCALSTATEDIR"/run/abrt", problem_id);
        /* Try $HOME/tmp */
        if (!dd)
        {
            char *home = getenv("HOME");
            if (home && home[0])
            {
                home = concat_path_file(home, "tmp");
                /*mkdir(home, 0777); - do we want this? */
                dd = try_dd_create(home, problem_id);
                free(home);
            }
        }
//TODO: try user's home dir obtained by getpwuid(getuid())?
        /* Try /tmp */
        if (!dd)
            dd = try_dd_create("/tmp", problem_id);
    }

    if (!dd) /* try_dd_create() already emitted the error message */
        goto ret;

    GHashTableIter iter;
    char *name;
    struct problem_item *value;
    g_hash_table_iter_init(&iter, problem_data);
    while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&value))
    {
        if (value->flags & CD_FLAG_BIN)
        {
            char *dest = concat_path_file(dd->dd_dirname, name);
            VERB2 log("copying '%s' to '%s'", value->content, dest);
            off_t copied = copy_file(value->content, dest, 0644);
            if (copied < 0)
                error_msg("Can't copy %s to %s", value->content, dest);
            else
                VERB2 log("copied %li bytes", (unsigned long)copied);
            free(dest);

            continue;
        }

        /* only files should contain '/' and those are handled earlier */
        if (name[0] == '.' || strchr(name, '/'))
        {
            error_msg("Problem data field name contains disallowed chars: '%s'", name);
            continue;
        }

        dd_save_text(dd, name, value->content);
    }

    /* need to create basic files AFTER we save the pd to dump_dir
     * otherwise we can't skip already created files like in case when
     * reporting from anaconda where we can't read /etc/{system,redhat}-release
     * and os_release is taken from anaconda
     */
    dd_create_basic_files(dd, (uid_t)-1L, NULL);

    problem_id[strlen(problem_id) - strlen(NEW_PD_SUFFIX)] = '\0';
    char* new_path = concat_path_file(base_dir_name, problem_id);
    VERB2 log("Renaming from '%s' to '%s'", dd->dd_dirname, new_path);
    dd_rename(dd, new_path);

 ret:
    free(problem_id);
    return dd;
}
