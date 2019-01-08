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
#include "report_result.h"

#include <stdint.h>
#include <stdio.h>

/* For DIR */
#include <sys/types.h>
#include <dirent.h>

/* For 'struct stat' */
#include <sys/stat.h>

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

/******************************************************************************/
/* Global variables                                                           */
/******************************************************************************/

/* UID of super-user (default 0)
 *
 * This variable is used by the dd* functions when they access security
 * sensitive elements. The functions will ONLY TRUST the contents of those
 * elements that ARE OWNED by super-user.
 */
extern uid_t dd_g_super_user_uid;

/* GID of a dump diretory created via dd_create() with uid != -1
 *
 * The default value is -1 which means that the dd* functions must ignore this
 * variable.
 *
 * Initialize this variable only if you don't want to use the default group
 * ('abrt').
 */
extern gid_t dd_g_fs_group_gid;

/******************************************************************************/
/* Dump Directory                                                             */
/******************************************************************************/

enum dump_dir_flags {
    DD_FAIL_QUIETLY_ENOENT = (1 << 0),
    DD_FAIL_QUIETLY_EACCES = (1 << 1),
    /* Open symlinks. dd_* funcs don't open symlinks by default */
    DD_OPEN_FOLLOW = (1 << 2),
    DD_OPEN_READONLY = (1 << 3),
    DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE = (1 << 4),
    DD_DONT_WAIT_FOR_LOCK = (1 << 5),
    /* Create the new dump directory with parent directories (mkdir -p)*/
    DD_CREATE_PARENTS = (1 << 6),
    /* Initializes internal data, opens file descriptors and returns the
     * structure. This flag is useful for testing whether a directory
     * exists and to perform stat operations.
     */
    DD_OPEN_FD_ONLY = (1 << 7),
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

    /* In case of recursive locking the first caller owns the lock and is
     * responsible for unlocking. The consecutive dd_lock() callers acquire the
     * lock but are not able to unlock the dump directory.
     */
    int owns_lock;
    int dd_fd;
    /* Never use this member directly, it is intialized on demand in
     * dd_get_meta_data_dir_fd()
     */
    int dd_md_fd;
};

void dd_close(struct dump_dir *dd);

/* Opens the given path
 */
struct dump_dir *dd_opendir(const char *dir, int flags);

/* Re-opens a dump_dir opened with DD_OPEN_FD_ONLY.
 *
 * The passed dump_dir must not be used any more and the return value must be
 * used instead.
 *
 * The passed flags must not contain DD_OPEN_FD_ONLY.
 *
 * The passed dump_dir must not be already locked.
 */
struct dump_dir *dd_fdopendir(struct dump_dir *dd, int flags);

/* Creates a new directory with internal files
 *
 * The functions creates a new directory which remains owned by the user of the
 * process until dd_reset_ownership() is called.
 *
 * The function logs error messages in case of errors.
 *
 * @param dir Full file system path of the new directory
 * @param uid Desired file system owner of the new directory or -1 if the owner
 * should stay untouched even after calling dd_reset_ownership().
 * @param mode File system mode of the new directory.
 * @param flags See 'enum dump_dir_flags'
 * @return Initialized struct dump_dir of NULL
 */
struct dump_dir *dd_create_skeleton(const char *dir, uid_t uid, mode_t mode, int flags);

int dd_reset_ownership(struct dump_dir *dd);

/* Pass uid = (uid_t)-1L to disable chown'ing of newly created files
 * (IOW: if you aren't running under root):
 */
struct dump_dir *dd_create(const char *dir, uid_t uid, mode_t mode);

/* Creates the basic files except 'type' and sets the dump dir owner to passed
 * 'uid'.
 *
 * The file 'type' is required and must be added with dd_save_text().
 *
 * If you want to have owner different than the problem 'uid', than pass -1 and
 * add the file 'uid' with dd_save_text()
 *
 * List of created files:
 *   - time
 *   - last_occurrence
 *   - uid
 *   - kernel
 *   - architecture
 *   - hostname
 *   - os_info
 *   - os_release
 *
 * If any of these files has a counterpart in a chroot directory (os_info,
 * os_relase), creates an element with the prefix "root_"
 */
void dd_create_basic_files(struct dump_dir *dd, uid_t uid, const char *chroot_dir);
int dd_exist(const struct dump_dir *dd, const char *path);
void dd_sanitize_mode_and_owner(struct dump_dir *dd);

/* Initializes an iterator going through all dump directory items.
 *
 * @returns NULL if the iterator cannot be initialized; otherwise returns
 * the result of opendir(). Do not use the return value after the iteration is
 * finished or after calling dd_clear_next_file().
 */
DIR *dd_init_next_file(struct dump_dir *dd);

/* Iterates over all dump directory item names
 *
 * Initialize the iterator by calling dd_init_next_file(). When iteration is
 * finished, calls dd_clear_next_file().
 *
 * @returns 1 if the next item was read; otherwise return 0.
 */
int dd_get_next_file(struct dump_dir *dd, char **short_name, char **full_name);

/* Destroys the next file iterator and cleans dump directory internal structures
 *
 * Calling dd_get_next_file() after this function returns will return 0. This
 * function also invalidates the return value of dd_init_next_file().
 */
void dd_clear_next_file(struct dump_dir *dd);

char *load_text_file(const char *path, unsigned flags);

char* dd_load_text_ext(const struct dump_dir *dd, const char *name, unsigned flags);
char* dd_load_text(const struct dump_dir *dd, const char *name);
int dd_load_int32(const struct dump_dir *dd, const char *name, int32_t *value);
int dd_load_uint32(const struct dump_dir *dd, const char *name, uint32_t *value);
int dd_load_int64(const struct dump_dir *dd, const char *name, int64_t *value);
int dd_load_uint64(const struct dump_dir *dd, const char *name, uint64_t *value);

/* Returns value of environment variable with given name.
 *
 * @param dd Dump directory
 * @param name Variables's name
 * @param value Return value.
 * @return 0 no success, or negative value if an error occurred (-ENOENT if the
 * given dd does not support environment variables).
 */
int dd_get_env_variable(struct dump_dir *dd, const char *name, char **value);

void dd_save_text(struct dump_dir *dd, const char *name, const char *data);
void dd_save_binary(struct dump_dir *dd, const char *name, const char *data, unsigned size);
int dd_copy_file(struct dump_dir *dd, const char *name, const char *source_path);
int dd_copy_file_unpack(struct dump_dir *dd, const char *name, const char *source_path);

/* Create an item of the given name with contents of the given file (see man openat)
 *
 * @param dd Dump directory
 * @param name Item's name
 * @param src_dir_fd Source directory's file descriptor
 * @param src_name Source file name
 * @return 0 no success, or negative value if an error occurred
 */
int dd_copy_file_at(struct dump_dir *dd, const char *name, int src_dir_fd, const char *src_name);

/* Creates/overwrites an element with data read from a file descriptor
 *
 * @param dd Dump directory
 * @param name The name of the element
 * @param fd The file descriptor
 * @param flags libreport_copyfd_flags
 * @param maxsize Limit for number of written Bytes. (0 for unlimited).
 * @return Number of read Bytes. If the return value is greater than the maxsize
 * the file descriptor content was truncated to the maxsize. The return value
 * is not size of the file descriptor.
 */
off_t dd_copy_fd(struct dump_dir *dd, const char *name, int fd, int copy_flags, off_t maxsize);

/* Stats dump dir elements
 *
 * @param dd Dump Directory
 * @param name The name of the element
 * @param statbuf See 'man 2 stat'
 * @return -EINVAL if name is invalid element name, -EMEDIUMTYPE if name is not
 *  regular file, -errno on errors and 0 on success.
 */
int dd_item_stat(struct dump_dir *dd, const char *name, struct stat *statbuf);

/* Returns value less than 0 if any error occured; otherwise returns size of an
 * item in Bytes. If an item does not exist returns 0 instead of an error
 * value.
 */
long dd_get_item_size(struct dump_dir *dd, const char *name);

/* Returns the number of items in the dump directory (does not count meta-data).
 *
 * @return Negative number on errors (-errno). Otherwise number of dump
 * directory items.
 */
int dd_get_items_count(struct dump_dir *dd);

/* Deletes an item from dump directory
 * On success, zero is returned. On error, -1 is returned, and errno is set appropriately.
 * For more about errno see unlink documentation
 */
int dd_delete_item(struct dump_dir *dd, const char *name);

/* Returns a file descriptor for the given name. The function is limited to open
 * an element read only, write only or create new.
 *
 * O_RDONLY - opens an existing item for reading
 * O_RDWR - removes an item, creates its file and opens the file for reading and writing
 *
 * @param dd Dump directory
 * @param name The name of the item
 * @param flags One of these : O_RDONLY, O_RDWR
 * @return Negative number on error
 */
int dd_open_item(struct dump_dir *dd, const char *name, int flags);

/* Returns a FILE for the given name. The function is limited to open
 * an element read only, write only or create new.
 *
 * O_RDONLY - opens an existing file for reading
 * O_RDWR - removes an item, creates its file and opens the file for reading and writing
 *
 * @param dd Dump directory
 * @param name The name of the item
 * @param flags One of these : O_RDONLY, O_RDWR
 * @return NULL on error
 */
FILE *dd_open_item_file(struct dump_dir *dd, const char *name, int flags);

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

/* Returns the number of Bytes consumed by the dump directory.
 *
 * @param flags For the future needs (count also meta-data, ...).
 * @return Negative number on errors (-errno). Otherwise size in Bytes.
 */
off_t dd_compute_size(struct dump_dir *dd, int flags);

/* Sets a new owner (does NOT chown the directory)
 *
 * Does not validate the passed uid.
 * The given dump_dir must be opened for writing.
 */
int dd_set_owner(struct dump_dir *dd, uid_t owner);

/* Makes the dump directory owned by nobody.
 *
 * The directory will be accessible for all users.
 * The given dump_dir must be opened for writing.
 */
int dd_set_no_owner(struct dump_dir *dd);

/* Gets the owner
 *
 * If meta-data misses owner, returns fs owner.
 * Can be used with DD_OPEN_FD_ONLY.
 */
uid_t dd_get_owner(struct dump_dir *dd);

/* Returns UNIX time stamp of the first occurrence of the problem.
 *
 * @param dd Examined dump directory
 * @returns On success, the value of time of the first occurrence in seconds
 * since the Epoch is returned. On error, ((time_t) -1) is returned, and errno
 * is set appropriately (ENODATA).
 */
time_t dd_get_first_occurrence(struct dump_dir *dd);

/* Returns UNIX time stamp of the last occurrence of the problem.
 *
 * @param dd Examined dump directory
 * @returns The returned value is never lower than the value returned by
 * dd_get_first_occurrence(). On success, the value of time of the first
 * occurrence in seconds since the Epoch is returned.On error, ((time_t) -1) is
 * returned, and errno is set appropriately (ENODATA).
 */
time_t dd_get_last_occurrence(struct dump_dir *dd);

/* Appends a new unique line to the list of report results
 *
 * If the reported_to data already contains the given line, the line will not
 * be added again.
 *
 * @param reported_to The data
 * @param line The appended line
 * @return 1 if the line was added at the end of the reported_to; otherwise 0.
 */
#define add_reported_to_data libreport_add_reported_to_data
int add_reported_to_data(char **reported_to, const char *line);

/* Appends a new unique entry to the list of report results
 *
 * result->label must be non-empty string which does not contain ':' character.
 *
 * The function converts the result to a valid reported_to line and calls
 * add_reported_to_data().
 *
 * @param reported_to The data
 * @param result The appended entry
 * @return -EINVAL if result->label is invalid; otherwise return value of
 * add_reported_to_data
 */
#define add_reported_to_entry_data libreport_add_reported_to_entry_data
int add_reported_to_entry_data(char **reported_to, struct report_result *result);

/* This is a wrapper of add_reported_to_data which accepts 'struct dump_dir *'
 * in the first argument instead of 'char **'. The added line is stored in
 * 'reported_to' dump directory file.
 */
#define add_reported_to libreport_add_reported_to
void add_reported_to(struct dump_dir *dd, const char *line);

/* This is a wrapper of add_reported_to_entry_data which accepts 'struct
 * dump_dir *' in the first argument instead of 'char **'. The added entry is
 * stored in 'reported_to' dump directory file.
 */
#define add_reported_to_entry libreport_add_reported_to_entry
void add_reported_to_entry(struct dump_dir *dd, struct report_result *result);

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
/* Returns the same information as dump_dir_accessible_by_uid
 *
 * The passed dump_dir can be opened with DD_OPEN_FD_ONLY
 */
int dd_accessible_by_uid(struct dump_dir *dd, uid_t uid);

enum {
    DD_STAT_ACCESSIBLE_BY_UID = 1,
    DD_STAT_OWNED_BY_UID = DD_STAT_ACCESSIBLE_BY_UID << 1,
    DD_STAT_NO_OWNER = DD_STAT_OWNED_BY_UID << 1,
};

/* Gets information about a dump directory for particular uid.
 *
 * If the directory doesn't exist the directory is not accessible and errno is
 * set to ENOTDIR.
 *
 * Returns negative number if error occurred otherwise returns 0 or positive number.
 */
int dump_dir_stat_for_uid(const char *dirname, uid_t uid);
/* Returns the same information as dump_dir_stat_for_uid
 *
 * The passed dump_dir can be opened with DD_OPEN_FD_ONLY
 */
int dd_stat_for_uid(struct dump_dir *dd, uid_t uid);

/* creates not_reportable file in the problem directory and saves the
   reason to it, which prevents libreport from reporting the problem
   On success, zero is returned.
   On error, -1 is returned and an error message is logged.
     - this could probably happen only if the dump dir is not locked
*/
int dd_mark_as_notreportable(struct dump_dir *dd, const char *reason);

typedef int (*save_data_call_back)(struct dump_dir *, void *args);

/* Saves data in a new dump directory
 *
 * Creates a new dump directory in "problem dump location", adds the basic
 * information to the new directory, calls given callback to allow callees to
 * customize the dump dir contents (save problem data) and commits the dump
 * directory (makes the directory visible for a problem daemon).
 */
struct dump_dir *create_dump_dir(const char *base_dir_name, const char *type,
        uid_t uid, save_data_call_back save_data, void *args);

struct dump_dir *create_dump_dir_ext(const char *base_dir_name, const char *type,
        pid_t pid, uid_t uid, save_data_call_back save_data, void *args);

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
