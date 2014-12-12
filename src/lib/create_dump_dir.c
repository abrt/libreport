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
#include <errno.h>

#define NEW_PD_SUFFIX ".new"

static struct dump_dir *try_dd_create(const char *base_dir_name, const char *dir_name, uid_t uid)
{
    char *path = concat_path_file(base_dir_name, dir_name);
    struct dump_dir *dd = dd_create(path, uid, DEFAULT_DUMP_DIR_MODE);
    free(path);
    return dd;
}

struct dump_dir *create_dump_dir(const char *base_dir_name, const char *type, uid_t uid, save_data_call_back save_data, void *args)
{
    INITIALIZE_LIBREPORT();

    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0)
    {
        perror_msg("gettimeofday()");
        return NULL;
    }

    char *problem_id = xasprintf("%s-%s.%ld-%lu"NEW_PD_SUFFIX, type, iso_date_string(&(tv.tv_sec)), (long)tv.tv_usec, (long)getpid());

    log_info("Saving to %s/%s with uid %d", base_dir_name, problem_id, uid);

    struct dump_dir *dd;
    if (base_dir_name)
        dd = try_dd_create(base_dir_name, problem_id, uid);
    else
    {
        /* Try /var/run/abrt */
        dd = try_dd_create(LOCALSTATEDIR"/run/abrt", problem_id, uid);
        /* Try $HOME/tmp */
        if (!dd)
        {
            char *home = getenv("HOME");
            if (home && home[0])
            {
                home = concat_path_file(home, "tmp");
                /*mkdir(home, 0777); - do we want this? */
                dd = try_dd_create(home, problem_id, uid);
                free(home);
            }
        }
//TODO: try user's home dir obtained by getpwuid(getuid())?
        /* Try system temporary directory */
        if (!dd)
            dd = try_dd_create(LARGE_DATA_TMP_DIR, problem_id, uid);
    }

    if (!dd) /* try_dd_create() already emitted the error message */
        goto ret;

    if (save_data(dd, args))
    {
        dd_delete(dd);
        dd = NULL;
        goto ret;
    }

    /* need to create basic files AFTER we save the pd to dump_dir
     * otherwise we can't skip already created files like in case when
     * reporting from anaconda where we can't read /etc/{system,redhat}-release
     * and os_release is taken from anaconda
     */
    dd_create_basic_files(dd, uid, NULL);

    problem_id[strlen(problem_id) - strlen(NEW_PD_SUFFIX)] = '\0';
    char* new_path = concat_path_file(base_dir_name, problem_id);
    log_info("Renaming from '%s' to '%s'", dd->dd_dirname, new_path);
    dd_rename(dd, new_path);

 ret:
    free(problem_id);
    return dd;
}

int save_problem_data_in_dump_dir(struct dump_dir *dd, problem_data_t *problem_data)
{
    INITIALIZE_LIBREPORT();

    GHashTableIter iter;
    char *name;
    struct problem_item *value;
    g_hash_table_iter_init(&iter, problem_data);
    while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&value))
    {
        if (value->flags & CD_FLAG_BIN)
        {
            dd_copy_file(dd, name, value->content);
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

    return 0;
}

struct dump_dir *create_dump_dir_from_problem_data(problem_data_t *problem_data, const char *base_dir_name)
{
    INITIALIZE_LIBREPORT();

    char *type = problem_data_get_content_or_NULL(problem_data, FILENAME_ANALYZER);

    if (!type)
    {
        error_msg(_("Missing required item: '%s'"), FILENAME_ANALYZER);
        return NULL;
    }

    uid_t uid = (uid_t)-1L;
    char *uid_str = problem_data_get_content_or_NULL(problem_data, FILENAME_UID);

    if (uid_str)
    {
        char *endptr;
        errno = 0;
        long val = strtol(uid_str, &endptr, 10);

        if (errno != 0 || endptr == uid_str || *endptr != '\0' || INT_MAX < val)
        {
            error_msg(_("uid value is not valid: '%s'"), uid_str);
            return NULL;
        }

        uid = (uid_t)val;
    }

    return create_dump_dir(base_dir_name, type, uid, (save_data_call_back)save_problem_data_in_dump_dir, problem_data);
}
