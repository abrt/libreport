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

static uid_t parse_uid(const char *uid_str)
{
    assert(sizeof(uid_t) == sizeof(unsigned));

    uid_t uid = (uid_t)-1;

    if (try_atou(uid_str, &uid) != 0)
        error_msg(_("uid value is not valid: '%s'"), uid_str);

    return uid;
}

static struct dump_dir *try_dd_create(const char *base_dir_name, const char *dir_name, uid_t uid)
{
    char *path = concat_path_file(base_dir_name, dir_name);
    struct dump_dir *dd = dd_create(path, uid, DEFAULT_DUMP_DIR_MODE);
    free(path);
    return dd;
}

struct dump_dir *create_dump_dir_ext(const char *base_dir_name, const char *type, pid_t pid, uid_t uid, save_data_call_back save_data, void *args)
{
    INITIALIZE_LIBREPORT();

    if (!str_is_correct_filename(type))
    {
        error_msg(_("'%s' is not correct file name"), FILENAME_TYPE);
        return NULL;
    }

    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0)
    {
        perror_msg("gettimeofday()");
        return NULL;
    }

    char *problem_id = xasprintf("%s-%s.%ld-%lu"NEW_PD_SUFFIX, type, iso_date_string(&(tv.tv_sec)), (long)tv.tv_usec, (long)pid);

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
    char *uid_str = dd_load_text_ext(dd, FILENAME_UID, DD_FAIL_QUIETLY_ENOENT | DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE);
    const uid_t crashed_uid = uid_str != NULL ? /*uid already saved*/-1 : uid;
    dd_create_basic_files(dd, crashed_uid, NULL);

    /* If crashed uid is (uid_t)-1, then dd_create_basic_files() didn't set the
     * dd owner and the dd owner remained on fs owner (the default owner used
     * when creating a new dump directory).
     *
     * Our callers expect, that the dd owner is set to value of UID (it used to
     * be the case before the dd owner was introduced), so we have to try to
     * get UID from the dump directory, parse it and use the parse value.
     * Errors are not critical, because the dump directory is already owned by
     * the fs owner.
     */
    if (crashed_uid == (uid_t)-1 && uid_str != NULL)
    {
        uid_t owner_uid = parse_uid(uid_str);
        if (owner_uid != (uid_t)-1)
        {
            log_notice("Changing owner of the new problem to: %s", uid_str);
            /* Ignore errors, the old value is preseverd or fs uid will be used
             * instead. The function prints out good error messges.*/
            dd_set_owner(dd, owner_uid);
        }
        else
            log_notice("Failed to parse UID, keeping the default owner.");
    }
    else
        log_notice("No UID provided, keeping the default owner.");

    free(uid_str);

    char *type_str = dd_load_text_ext(dd, FILENAME_TYPE, DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE);
    if (type_str == NULL)
        dd_save_text(dd, FILENAME_TYPE, type);
    free(type_str);

    problem_id[strlen(problem_id) - strlen(NEW_PD_SUFFIX)] = '\0';
    char* new_path = concat_path_file(base_dir_name, problem_id);
    log_info("Renaming from '%s' to '%s'", dd->dd_dirname, new_path);
    dd_rename(dd, new_path);
    free(new_path);

 ret:
    free(problem_id);
    return dd;
}

struct dump_dir *create_dump_dir(const char *base_dir_name, const char *type, uid_t uid, save_data_call_back save_data, void *args)
{
    return create_dump_dir_ext(base_dir_name, type, getpid(), uid, save_data, args);
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
        if (!str_is_correct_filename(name))
        {
            error_msg("Problem data field name contains disallowed chars: '%s'", name);
            continue;
        }

        if (value->flags & CD_FLAG_BIN)
        {
            dd_copy_file(dd, name, value->content);
            continue;
        }

        dd_save_text(dd, name, value->content);
    }

    return 0;
}

struct dump_dir *create_dump_dir_from_problem_data_ext(problem_data_t *problem_data, const char *base_dir_name, uid_t uid)
{
    INITIALIZE_LIBREPORT();

    char *type = problem_data_get_content_or_NULL(problem_data, FILENAME_TYPE);

    if (!type)
    {
        error_msg(_("Missing required item: '%s'"), FILENAME_TYPE);
        return NULL;
    }

    return create_dump_dir(base_dir_name, type, uid, (save_data_call_back)save_problem_data_in_dump_dir, problem_data);
}

struct dump_dir *create_dump_dir_from_problem_data(problem_data_t *problem_data, const char *base_dir_name)
{
    INITIALIZE_LIBREPORT();

    uid_t uid = (uid_t)-1L;
    char *uid_str = problem_data_get_content_or_NULL(problem_data, FILENAME_UID);

    if (uid_str)
    {
        uid = parse_uid(uid_str);
        if (uid == (uid_t)-1)
            return NULL;
    }

    return create_dump_dir_from_problem_data_ext(problem_data, base_dir_name, uid);
}
