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

/* For DIR */
#include <sys/types.h>
#include <dirent.h>

/* Fore GList */
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DD_FAIL_QUIETLY_ENOENT = (1 << 0),
    DD_FAIL_QUIETLY_EACCES = (1 << 1),
    /* Open symlinks. dd_* funcs don't open symlinks by default */
    DD_OPEN_FOLLOW = (1 << 2),
    DD_OPEN_READONLY = (1 << 3),
    DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE = (1 << 4),
    DD_DONT_WAIT_FOR_LOCK = (1 << 5),
};

struct dump_dir {
    char *dd_dirname;
    DIR *next_dir;
    int locked;
    uid_t dd_uid;
    gid_t dd_gid;
    /* mode fo saved files */
    mode_t mode;
};

void dd_close(struct dump_dir *dd);

struct dump_dir *dd_opendir(const char *dir, int flags);
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
/* Deletes an item from dump directory
 * On success, zero is returned. On error, -1 is returned, and errno is set appropriately.
 * For more about errno see unlink documentation
 */
int dd_delete_item(struct dump_dir *dd, const char *name);
/* Returns 0 if directory is deleted or not found */
int dd_delete(struct dump_dir *dd);


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
report_result_t *find_in_reported_to(struct dump_dir *dd, const char *prefix);
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

#ifdef __cplusplus
}
#endif

#endif
