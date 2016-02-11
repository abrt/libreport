/*
    On-disk storage of problem data

    Copyright (C) 2009  Zdenek Prikryl (zprikryl@redhat.com)
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
#ifndef LIBREPORT_DUMP_DIR_H_
#define LIBREPORT_DUMP_DIR_H_

/* For const_string_vector_const_ptr_t */
#include "libreport_types.h"

/* For DIR */
#include <sys/types.h>
#include <dirent.h>

/* Fore GList */
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Utility function */
int create_symlink_lockfile(const char *filename, const char *pid_str);
int create_symlink_lockfile_at(int dir_fd, const char *filename, const char *pid_str);

/* Opens filename for reading relatively to a directory represented by dir_fd.
 * The function fails if the file is symbolic link, directory or hard link.
 */
int secure_openat_read(int dir_fd, const char *filename);

enum {
    DD_FAIL_QUIETLY_ENOENT = (1 << 0),
    DD_FAIL_QUIETLY_EACCES = (1 << 1),
    /* Open symlinks. dd_* funcs don't open symlinks by default */
    DD_OPEN_FOLLOW = (1 << 2),
    DD_OPEN_READONLY = (1 << 3),
    DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE = (1 << 4),
    DD_DONT_WAIT_FOR_LOCK = (1 << 5),
    /* Create the new dump directory with parent directories (mkdir -p)*/
    DD_CREATE_PARENTS = (1 << 6),
};

struct dump_dir {
    char *dd_dirname;
    DIR *next_dir;
    int locked;
    uid_t dd_uid;
    gid_t dd_gid;
    /* mode of saved files */
    mode_t mode;
    time_t dd_time;
    char *dd_type;
    int dd_fd;
};

void dd_close(struct dump_dir *dd);

/* Opens the given path and returns the resulting file descriptor.
 */
int dd_openfd(const char *dir);
struct dump_dir *dd_opendir(const char *dir, int flags);
/* Skips dd_openfd(dir) and uses the given file descriptor instead
 */
struct dump_dir *dd_fdopendir(int dir_fd, const char *dir, int flags);
struct dump_dir *dd_create_skeleton(const char *dir, uid_t uid, mode_t mode, int flags);
int dd_reset_ownership(struct dump_dir *dd);
/* Pass uid = (uid_t)-1L to disable chown'ing of newly created files
 * (IOW: if you aren't running under root):
 */
struct dump_dir *dd_create(const char *dir, uid_t uid, mode_t mode);

void dd_create_basic_files(struct dump_dir *dd, uid_t uid, const char *chroot_dir);
int dd_exist(const struct dump_dir *dd, const char *path);
void dd_sanitize_mode_and_owner(struct dump_dir *dd);

DIR *dd_init_next_file(struct dump_dir *dd);
int dd_get_next_file(struct dump_dir *dd, char **short_name, char **full_name);

char* dd_load_text_ext(const struct dump_dir *dd, const char *name, unsigned flags);
char* dd_load_text(const struct dump_dir *dd, const char *name);
void dd_save_text(struct dump_dir *dd, const char *name, const char *data);
void dd_save_binary(struct dump_dir *dd, const char *name, const char *data, unsigned size);
/* Returns value less than 0 if any error occured; otherwise returns size of an
 * item in Bytes. If an item does not exist returns 0 instead of an error
 * value.
 */
long dd_get_item_size(struct dump_dir *dd, const char *name);
/* Deletes an item from dump directory
 * On success, zero is returned. On error, -1 is returned, and errno is set appropriately.
 * For more about errno see unlink documentation
 */
int dd_delete_item(struct dump_dir *dd, const char *name);
/* Returns 0 if directory is deleted or not found */
int dd_delete(struct dump_dir *dd);
int dd_rename(struct dump_dir *dd, const char *new_path);
/* Changes owner of dump dir
 * Uses two different strategies selected at build time by
 * DUMP_DIR_OWNED_BY_USER configuration:
 *  <= 0 : owner = abrt user's uid,  group = new_uid's gid
 *   > 0 : owner = new_uid,          group = abrt group's gid
 *
 * On success, zero is returned. On error, -1 is returned.
 */
int dd_chown(struct dump_dir *dd, uid_t new_uid);


/* reported_to handling */
#define add_reported_to_data libreport_add_reported_to_data
int add_reported_to_data(char **reported_to, const char *line);
#define add_reported_to libreport_add_reported_to
void add_reported_to(struct dump_dir *dd, const char *line);
struct report_result {
    char *label;
    char *url;
    char *msg;
    char *bthash;
    /* char *whole_line; */
    /* time_t timestamp; */
    /* ^^^ if you add more fields, don't forget to update free_report_result() */
};
typedef struct report_result report_result_t;
#define free_report_result libreport_free_report_result
void free_report_result(struct report_result *result);
#define find_in_reported_to_data libreport_find_in_reported_to_data
report_result_t *find_in_reported_to_data(const char *reported_to, const char *report_label);
#define find_in_reported_to libreport_find_in_reported_to
report_result_t *find_in_reported_to(struct dump_dir *dd, const char *report_label);
#define read_entire_reported_to_data libreport_read_entire_reported_to_data
GList *read_entire_reported_to_data(const char* reported_to);
#define read_entire_reported_to libreport_read_entire_reported_to
GList *read_entire_reported_to(struct dump_dir *dd);


void delete_dump_dir(const char *dirname);
/* Checks dump dir accessibility for particular uid.
 *
 * If the directory doesn't exist the directory is not accessible and errno is
 * set to ENOTDIR.
 *
 * Returns non zero if dump dir is accessible otherwise return 0 value.
 */
int dump_dir_accessible_by_uid(const char *dirname, uid_t uid);
int fdump_dir_accessible_by_uid(int dir_fd, uid_t uid);

enum {
    DD_STAT_ACCESSIBLE_BY_UID = 1,
    DD_STAT_OWNED_BY_UID = DD_STAT_ACCESSIBLE_BY_UID << 1,
};

/* Gets information about a dump directory for particular uid.
 *
 * If the directory doesn't exist the directory is not accessible and errno is
 * set to ENOTDIR.
 *
 * Returns negative number if error occurred otherwise returns 0 or positive number.
 */
int dump_dir_stat_for_uid(const char *dirname, uid_t uid);
int fdump_dir_stat_for_uid(int dir_fd, uid_t uid);

/* creates not_reportable file in the problem directory and saves the
   reason to it, which prevents libreport from reporting the problem
   On success, zero is returned.
   On error, -1 is returned and an error message is logged.
     - this could probably happen only if the dump dir is not locked
*/
int dd_mark_as_notreportable(struct dump_dir *dd, const char *reason);

/* Creates a new archive from the dump directory contents
 *
 * The dd argument must be opened for reading.
 *
 * The archive_name must not exist. The file will be created with 0600 mode.
 *
 * The archive type is deduced from archive_name suffix. The supported archive
 * suffixes are the following:
 *   - '.tag.gz' (note: the implementation uses child gzip process)
 *
 * The archive will include only the files that are not in the exclude_elements
 * list. See get_global_always_excluded_elements().
 *
 * The argument "flags" is currently unused.
 *
 * @return 0 on success; otherwise non-0 value. -ENOSYS if archive type is not
 * supported. -EEXIST if the archive file already exists. -ECHILD if child
 * process fails. Other negative values can be converted to errno values by
 * turning them positive.
 */
int dd_create_archive(struct dump_dir *dd, const char *archive_name,
        const_string_vector_const_ptr_t exclude_elements, int flags);

#ifdef __cplusplus
}
#endif

#endif
