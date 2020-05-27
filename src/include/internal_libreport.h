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

#ifndef LIBREPORT_INTERNAL_H_
#define LIBREPORT_INTERNAL_H_

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <syslog.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h> /* sockaddr_in, sockaddr_in6 etc */
#include <termios.h>
#include <time.h>
#include <unistd.h>
/* Try to pull in PATH_MAX */
#include <limits.h>
#include <sys/param.h>
#ifndef PATH_MAX
# define PATH_MAX 256
#endif
#include <pwd.h>
#include <grp.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* Must be after #include "config.h" */
#ifdef ENABLE_NLS
# include <libintl.h>
# define _(S) dgettext(PACKAGE, S)
#else
# define _(S) (S)
#endif

#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif /* HAVE_LOCALE_H */

/* Some libc's forget to declare these, do it ourself */
extern char **environ;
#if defined(__GLIBC__) && __GLIBC__ < 2
int vdprintf(int d, const char *format, va_list ap);
#endif

#undef NORETURN
#define NORETURN __attribute__ ((noreturn))

#undef ERR_PTR
#define ERR_PTR ((void*)(uintptr_t)1)

#undef ARRAY_SIZE
#define ARRAY_SIZE(x) ((unsigned)(sizeof(x) / sizeof((x)[0])))

/* consts used across whole libreport */
#define CREATE_PRIVATE_TICKET "ABRT_CREATE_PRIVATE_TICKET"
#define STOP_ON_NOT_REPORTABLE "ABRT_STOP_ON_NOT_REPORTABLE"

/* path of user's local config, path is relative to user's home */
#define USER_HOME_CONFIG_PATH "/.config/libreport"

/* Pull in entire public libreport API */
#include "global_configuration.h"
#include "dump_dir.h"
#include "event_config.h"
#include "problem_data.h"
#include "report.h"
#include "run_event.h"
#include "workflow.h"
#include "file_obj.h"
#include "libreport_types.h"
#include "reporters.h"

#ifdef __cplusplus
extern "C" {
#endif

char *libreport_trim_all_whitespace(const char *str);
char *libreport_shorten_string_to_length(const char *str, unsigned length);
char *libreport_strtrimch(char *str, int ch);
char *libreport_strremovech(char *str, int ch);
char *libreport_append_to_malloced_string(char *mstr, const char *append);
char *libreport_skip_blank(const char *s);
char *libreport_skip_whitespace(const char *s);
char *libreport_skip_non_whitespace(const char *s);
/* Like strcpy but can copy overlapping strings. */
void libreport_overlapping_strcpy(char *dst, const char *src);

char *libreport_concat_path_file(const char *path, const char *filename);
/*
 * Used to construct a name in a different directory with the basename
 * similar to the old name, if possible.
 */
char *libreport_concat_path_basename(const char *path, const char *filename);

/* Allows all printable characters except '/',
 * the string must not exceed 64 characters of length
 * and must not equal neither "." nor ".." (these strings may appear in the string) */
bool libreport_str_is_correct_filename(const char *str);

/* A-la fgets, but malloced and of unlimited size */
char *libreport_xmalloc_fgets(FILE *file);
/* Similar, but removes trailing \n */
char *libreport_xmalloc_fgetline(FILE *file);
/* Useful for easy reading of various /proc files */
char *libreport_xmalloc_fopen_fgetline_fclose(const char *filename);


typedef enum {
        COPYFD_SPARSE = 1 << 0,
} libreport_copyfd_flags;

/* Writes up to 'size' Bytes from a file descriptor to a file in a directory
 *
 * If you need to write all Bytes of the file descriptor, pass 0 as the size.
 *
 * @param src The source file descriptor
 * @param dir_fd A file descriptor for the parent directory of the destination file
 * @param name The destination file name
 * @param mode The destination file open mode
 * @param uid The destination file's uid
 * @param gid The destination file's gid
 * @param open_flags The destination file open flags
 * @param copy_flags libreport_copyfd_flags
 * @param size The upper limit for written bytes (0 for no limit).
 * @return Number of read Bytes on success. On errors, return -1 and prints out
 * reasonable good error messages.
 */
off_t libreport_copyfd_ext_at(int src, int dir_fd, const char *name, int mode,
        uid_t uid, gid_t gid, int open_flags, int copy_flags, off_t size);

/* On error, copyfd_XX prints error messages and returns -1 */
off_t libreport_copyfd_eof(int src_fd, int dst_fd, int flags);
off_t libreport_copyfd_size(int src_fd, int dst_fd, off_t size, int flags);
void libreport_copyfd_exact_size(int src_fd, int dst_fd, off_t size);
off_t libreport_copy_file_ext_2at(int src_dir_fd, const char *src_name, int dir_fd, const char *name, int mode, uid_t uid, gid_t gid, int src_flags, int dst_flags);
off_t libreport_copy_file_ext_at(const char *src_name, int dir_fd, const char *name, int mode, uid_t uid, gid_t gid, int src_flags, int dst_flags);
#define libreport_copy_file_ext(src_name, dst_name, mode, uid, gid, src_flags, dst_flags) \
    libreport_copy_file_ext_at(src_name, AT_FDCWD, dst_name, mode, uid, gid, src_flags, dst_flags)
off_t libreport_copy_file(const char *src_name, const char *dst_name, int mode);
off_t libreport_copy_file_at(const char *src_name, int dir_fd, const char *name, int mode);
int libreport_copy_file_recursive(const char *source, const char *dest);

int libreport_decompress_fd(int fdi, int fdo);
int libreport_decompress_file(const char *path_in, const char *path_out, mode_t mode_out);
int libreport_decompress_file_ext_at(const char *path_in, int dir_fd, const char *path_out,
        mode_t mode_out, uid_t uid, gid_t gid, int src_flags, int dst_flags);

// NB: will return short read on error, not -1,
// if some data was read before error occurred
void libreport_xread(int fd, void *buf, size_t count);
ssize_t libreport_safe_read(int fd, void *buf, size_t count);
ssize_t libreport_safe_write(int fd, const void *buf, size_t count);
ssize_t libreport_full_read(int fd, void *buf, size_t count);
ssize_t libreport_full_write(int fd, const void *buf, size_t count);
ssize_t libreport_full_write_str(int fd, const char *buf);
void *libreport_xmalloc_read(int fd, size_t *maxsz_p);
void *libreport_xmalloc_open_read_close(const char *filename, size_t *maxsz_p);
void *libreport_xmalloc_xopen_read_close(const char *filename, size_t *maxsz_p);
char *libreport_malloc_readlink(const char *linkname);
char *libreport_malloc_readlinkat(int dir_fd, const char *linkname);


/* Returns malloc'ed block */
char *libreport_encode_base64(const void *src, int length);

/* Returns NULL if the string needs no sanitizing.
 * control_chars_to_sanitize is a bit mask.
 * If Nth bit is set, Nth control char will be sanitized (replaced by [XX]).
 */
char *libreport_sanitize_utf8(const char *src, uint32_t control_chars_to_sanitize);
enum {
    SANITIZE_ALL = 0xffffffff,
    SANITIZE_TAB = (1 << 9),
    SANITIZE_LF  = (1 << 10),
    SANITIZE_CR  = (1 << 13),
};

int libreport_try_atou(const char *numstr, unsigned *value);
int libreport_try_atoi(const char *numstr, int *value);
/* Using libreport_xatoi() instead of naive atoi() is not always convenient -
 * in many places people want *non-negative* values, but store them
 * in signed int. Therefore we need this one:
 * dies if input is not in [0, INT_MAX] range. Also will reject '-0' etc.
 * It should really be named xatoi_nonnegative (since it allows 0),
 * but that would be too long.
 */
int libreport_try_atoi_positive(const char *numstr, int *value);

//unused for now
//unsigned long long monotonic_ns(void);
//unsigned long long monotonic_us(void);
//unsigned monotonic_sec(void);

pid_t libreport_safe_waitpid(pid_t pid, int *wstat, int options);

enum {
        /* on return, pipefds[1] is fd to which parent may write
         * and deliver data to child's stdin: */
        EXECFLG_INPUT      = 1 << 0,
        /* on return, pipefds[0] is fd from which parent may read
         * child's stdout: */
        EXECFLG_OUTPUT     = 1 << 1,
        /* open child's stdin to /dev/null: */
        EXECFLG_INPUT_NUL  = 1 << 2,
        /* open child's stdout to /dev/null: */
        EXECFLG_OUTPUT_NUL = 1 << 3,
        /* redirect child's stderr to stdout: */
        EXECFLG_ERR2OUT    = 1 << 4,
        /* open child's stderr to /dev/null: */
        EXECFLG_ERR_NUL    = 1 << 5,
        /* suppress perror_msg("Can't execute 'foo'") if exec fails */
        EXECFLG_QUIET      = 1 << 6,
        EXECFLG_SETGUID    = 1 << 7,
        EXECFLG_SETSID     = 1 << 8,
        EXECFLG_SETPGID    = 1 << 9,
};
/*
 * env_vec: list of variables to set in environment (if string has
 * "VAR=VAL" form) or unset in environment (if string has no '=' char).
 *
 * Returns pid.
 */
pid_t libreport_fork_execv_on_steroids(int flags,
                char **argv,
                int *pipefds,
                char **env_vec,
                const char *dir,
                uid_t uid);
/* Returns malloc'ed string. NULs are retained, and extra one is appended
 * after the last byte (this NUL is not accounted for in *size_p) */
char *libreport_run_in_shell_and_save_output(int flags,
                const char *cmd,
                const char *dir,
                size_t *size_p);

/* Random utility functions */

bool libreport_is_in_string_list(const char *name, const char *const *v);

int libreport_index_of_string_in_list(const char *name, const char *const *v);

bool libreport_is_in_comma_separated_list(const char *value, const char *list);
bool libreport_is_in_comma_separated_list_of_glob_patterns(const char *value, const char *list);

/* Calls GLib version appropriate initialization function.
 */
void libreport_glib_init(void);

/* Frees every element'd data using free(),
 * then frees list itself using g_list_free(list):
 */
void libreport_list_free_with_free(GList *list);

double libreport_get_dirsize(const char *pPath);
double libreport_get_dirsize_find_largest_dir(
                const char *pPath,
                char **worst_dir, /* can be NULL */
                const char *excluded, /* can be NULL */
                const char *proc_dir /* can be NULL */
);

int libreport_ndelay_on(int fd);
int libreport_ndelay_off(int fd);
int libreport_close_on_exec_on(int fd);

char *libreport_xstrdup_between(const char *s, const char *open, const char *close);

void libreport_xpipe(int filedes[2]);
int libreport_xdup(int from);
void libreport_xdup2(int from, int to);
void libreport_xmove_fd(int from, int to);

void libreport_xwrite(int fd, const void *buf, size_t count);
void libreport_xwrite_str(int fd, const char *str);

off_t libreport_xlseek(int fd, off_t offset, int whence);

void libreport_xchdir(const char *path);

char *libreport_xvasprintf(const char *format, va_list p);

void libreport_xsetenv(const char *key, const char *value);
/*
 * Utility function to unsetenv a string which was possibly putenv'ed.
 * The problem here is that "natural" optimization:
 * strchrnul(var_val, '=')[0] = '\0';
 * unsetenv(var_val);
 * is BUGGY: if string was put into environment via putenv,
 * its modification (s/=/NUL/) is illegal, and unsetenv will fail to unset it.
 * Of course, saving/restoring the char wouldn't work either.
 * This helper creates a copy up to '=', unsetenv's it, and frees:
 */
void libreport_safe_unsetenv(const char *var_val);

int libreport_xsocket(int domain, int type, int protocol);
void libreport_xbind(int sockfd, struct sockaddr *my_addr, socklen_t addrlen);
void libreport_xlisten(int s, int backlog);
ssize_t libreport_xsendto(int s, const void *buf, size_t len,
                const struct sockaddr *to, socklen_t tolen);

void libreport_xstat(const char *name, struct stat *stat_buf);
off_t libreport_fstat_st_size_or_die(int fd);
off_t libreport_stat_st_size_or_die(const char *filename);

int libreport_xopen3(const char *pathname, int flags, int mode);
void libreport_xunlinkat(int dir_fd, const char *pathname, int flags);

/* Just testing dent->d_type == DT_REG is wrong: some filesystems
 * do not report the type, they report DT_UNKNOWN for every dirent
 * (and this is not a bug in filesystem, this is allowed by standards).
 * This function handles this case. Note: it returns 0 on symlinks
 * even if they point to regular files.
 */
int libreport_is_regular_file(struct dirent *dent, const char *dirname);
int libreport_is_regular_file_at(struct dirent *dent, int dir_fd);

bool libreport_dot_or_dotdot(const char *filename);
char *libreport_last_char_is(const char *s, int c);

bool libreport_string_to_bool(const char *s);

void libreport_xseteuid(uid_t euid);
void libreport_xsetegid(gid_t egid);
void libreport_xsetreuid(uid_t ruid, uid_t euid);
void libreport_xsetregid(gid_t rgid, gid_t egid);

FILE *libreport_xfdopen(int fd, const char *mode);

/* Emit a string of hex representation of bytes */
char *libreport_bin2hex(char *dst, const char *str, int count);
/* Convert "xxxxxxxx" hex string to binary, no more than COUNT bytes */
char* libreport_hex2bin(char *dst, const char *str, int count);


enum {
    LOGMODE_NONE = 0,
    LOGMODE_STDIO = (1 << 0),
    LOGMODE_SYSLOG = (1 << 1),
    LOGMODE_BOTH = LOGMODE_SYSLOG + LOGMODE_STDIO,
    LOGMODE_CUSTOM = (1 << 2),
    LOGMODE_JOURNAL = (1 << 3),
};

enum libreport_diemode {
    DIEMODE_EXIT = 0,
    DIEMODE_ABORT = 1,
};

extern void (*libreport_g_custom_logger)(const char*);
extern const char *libreport_msg_prefix;
extern const char *libreport_msg_eol;
extern int libreport_logmode;
extern int libreport_xfunc_error_retval;

/* A few magic exit codes */
#define EXIT_CANCEL_BY_USER 69
#define EXIT_STOP_EVENT_RUN 70

void libreport_set_xfunc_error_retval(int retval);

void libreport_set_xfunc_diemode(enum libreport_diemode mode);

/* Verbosity level */
extern int libreport_g_verbose;
/* VERB1 log_warning("what you sometimes want to see, even on a production box") */
#define VERB1 if (libreport_g_verbose >= 1)
/* VERB2 log_warning("debug message, not going into insanely small details") */
#define VERB2 if (libreport_g_verbose >= 2)
/* VERB3 log_warning("lots and lots of details") */
#define VERB3 if (libreport_g_verbose >= 3)
/* there is no level > 3 */

void libreport_xfunc_die(void) NORETURN;

void libreport_die_out_of_memory(void) NORETURN;

/* It's a macro, not function, since it collides with log_warning() from math.h */
#undef log
#define log_warning(...)         log_standard(LOG_WARNING, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_debug(...)   log_standard(LOG_DEBUG,   __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_info(...)    log_standard(LOG_INFO,    __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_notice(...)  log_standard(LOG_NOTICE,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_warning(...) log_standard(LOG_WARNING, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_error(...)   log_standard(LOG_ERR,     __FILE__, __LINE__, __func__, __VA_ARGS__)

// specific subsystem debugging
#define log_parser(...)  if(0) log_debug(__VA_ARGS__)

#define log_standard(level, file, line, func, ...) log_wrapper(level, __FILE__, __LINE__, __func__, false, false, __VA_ARGS__)

//                                            level,     file,     line,     func, perror, custom logger, format & args
#define log_error_and_die(...)  log_wrapper(LOG_ERR, __FILE__, __LINE__, __func__, false, false,__VA_ARGS__)
#define log_perror(...)         log_wrapper(LOG_ERR, __FILE__, __LINE__, __func__, true, false, __VA_ARGS__)
#define log_perror_and_die(...) log_wrapper(LOG_ERR, __FILE__, __LINE__, __func__, true, false, __VA_ARGS__)

#define error_msg(...)          log_wrapper(LOG_ERR, __FILE__, __LINE__, __func__, false, true, __VA_ARGS__)
#define perror_msg(...)         log_wrapper(LOG_ERR, __FILE__, __LINE__, __func__, true, true, __VA_ARGS__)
#define warn_msg(...)           log_wrapper(LOG_WARNING, __FILE__, __LINE__, __func__, false, true, __VA_ARGS__)
#define pwarn_msg(...)          log_wrapper(LOG_WARNING, __FILE__, __LINE__, __func__, true, true, __VA_ARGS__)
#define notice_msg(...)         log_wrapper(LOG_NOTICE, __FILE__, __LINE__, __func__, false, true, __VA_ARGS__)
#define pnotice_msg(...)        log_wrapper(LOG_NOTICE, __FILE__, __LINE__, __func__, true, true, __VA_ARGS__)
#define error_msg_and_die(...)  log_and_die_wrapper(LOG_ERR, __FILE__, __LINE__, __func__, false, true, __VA_ARGS__)
#define perror_msg_and_die(...) log_and_die_wrapper(LOG_ERR, __FILE__, __LINE__, __func__, true, true, __VA_ARGS__)


void log_wrapper(int level,
                 const char *file,
                 int line,
                 const char *func,
                 bool process_perror,
                 bool use_custom_logger,
                 const char *format, ...) __attribute__ ((format (printf, 7,8)));

void log_and_die_wrapper(int level,
                 const char *file,
                 int line,
                 const char *func,
                 bool process_perror,
                 bool use_custom_logger,
                 const char *format, ...) __attribute__ ((noreturn, format (printf, 7,8)));

struct strbuf
{
    /* Size of the allocated buffer. Always > 0. */
    int alloc;
    /* Length of the string, without the ending \0. */
    int len;
    char *buf;
};

/**
 * Creates and initializes a new string buffer.
 * @returns
 * It never returns NULL. The returned pointer must be released by
 * calling the function libreport_strbuf_free().
 */
struct strbuf *libreport_strbuf_new(void);

/**
 * Releases the memory held by the string buffer.
 * @param strbuf
 * If the strbuf is NULL, no operation is performed.
 */
void libreport_strbuf_free(struct strbuf *strbuf);

/**
 * Releases the strbuf, but not the internal buffer.  The internal
 * string buffer is returned.  Caller is responsible to release the
 * returned memory using free().
 */
char *libreport_strbuf_free_nobuf(struct strbuf *strbuf);

/**
 * The string content is set to an empty string, erasing any previous
 * content and leaving its length at 0 characters.
 */
void libreport_strbuf_clear(struct strbuf *strbuf);

/**
 * The current content of the string buffer is extended by adding a
 * character c at its end.
 */
struct strbuf *libreport_strbuf_append_char(struct strbuf *strbuf, char c);

/**
 * The current content of the string buffer is extended by adding a
 * string str at its end.
 */
struct strbuf *libreport_strbuf_append_str(struct strbuf *strbuf,
                                 const char *str);

/**
 * The current content of the string buffer is extended by inserting a
 * string str at its beginning.
 */
struct strbuf *libreport_strbuf_prepend_str(struct strbuf *strbuf,
                                  const char *str);

/**
 * The current content of the string buffer is extended by adding a
 * sequence of data formatted as the format argument specifies.
 */
struct strbuf *libreport_strbuf_append_strf(struct strbuf *strbuf,
                                  const char *format, ...);

/**
 * Same as libreport_strbuf_append_strf except that va_list is passed instead of
 * variable number of arguments.
 */
struct strbuf *libreport_strbuf_append_strfv(struct strbuf *strbuf,
                                   const char *format, va_list p);

/**
 * The current content of the string buffer is extended by inserting a
 * sequence of data formatted as the format argument specifies at the
 * buffer beginning.
 */
struct strbuf *libreport_strbuf_prepend_strf(struct strbuf *strbuf,
                                   const char *format, ...);

/**
 * Same as libreport_strbuf_prepend_strf except that va_list is passed instead of
 * variable number of arguments.
 */
struct strbuf *libreport_strbuf_prepend_strfv(struct strbuf *strbuf,
                                    const char *format, va_list p);

/* Returns command line of running program.
 * Caller is responsible to free() the returned value.
 * If the pid is not valid or command line can not be obtained,
 * empty string is returned.
 */
int libreport_open_proc_pid_dir(pid_t pid);
char *libreport_get_cmdline_at(pid_t pid);
char *libreport_get_cmdline(pid_t pid);
char *libreport_get_environ_at(pid_t pid);
char *libreport_get_environ(pid_t pid);
char *libreport_get_executable_at(pid_t pid);
char *libreport_get_executable(pid_t pid);
char *libreport_get_cwd_at(pid_t pid);
char *libreport_get_cwd(pid_t pid);
char *libreport_get_rootdir_at(pid_t pid);
char *libreport_get_rootdir(pid_t pid);

int libreport_get_fsuid(const char *proc_pid_status);
int libreport_get_fsgid(const char *proc_pid_status);
int libreport_dump_fd_info_at(int pid_proc_fd, FILE *dest);
int libreport_dump_fd_info_ext(const char *dest_filename, const char *proc_pid_fd_path, uid_t uid, gid_t gid);
int libreport_dump_fd_info(const char *dest_filename, const char *proc_pid_fd_path);
int libreport_get_env_variable_ext(int fd, char delim, const char *name, char **value);
int libreport_get_env_variable(pid_t pid, const char *name, char **value);

#define PROC_NS_UNSUPPORTED ((ino_t)-1)
#define PROC_NS_ID_CGROUP 0
#define PROC_NS_ID_IPC 1
#define PROC_NS_ID_MNT 2
#define PROC_NS_ID_NET 3
#define PROC_NS_ID_PID 4
#define PROC_NS_ID_TIME 6
#define PROC_NS_ID_USER 8
#define PROC_NS_ID_UTS 9
static const char * libreport_proc_namespaces[] = {
    "cgroup",
    "ipc",
    "mnt",
    "net",
    "pid",
    "pid_for_children",
    "time",
    "time_for_children",
    "user",
    "uts",
};

struct ns_ids {
    ino_t nsi_ids[ARRAY_SIZE(libreport_proc_namespaces)];
};

int libreport_get_ns_ids_at(int pid_proc_fd, struct ns_ids *ids);
int libreport_get_ns_ids(pid_t pid, struct ns_ids *ids);

/* These functions require a privileged user and does not work correctly in
 * processes running in own PID namespace
 */
int libreport_process_has_own_root_at(int proc_pid_fd);
int libreport_process_has_own_root(pid_t pid);

int libreport_get_pid_of_container_at(int pid_proc_fd, pid_t *init_pid);
int libreport_get_pid_of_container(pid_t pid, pid_t *init_pid);
int libreport_dump_namespace_diff_at(int base_pid_proc_fd, int tested_pid_proc_fd, FILE *dest);
int libreport_dump_namespace_diff_ext(const char *dest_filename, pid_t base_pid, pid_t tested_pid, uid_t uid, gid_t gid);
int libreport_dump_namespace_diff(const char *dest_filename, pid_t base_pid, pid_t tested_pid);

enum
{
    MOUNTINFO_INDEX_MOUNT_ID,
    MOUNTINFO_INDEX_PARENT_ID,
    MOUNTINFO_INDEX_MAJOR_MINOR,
    MOUNTINFO_INDEX_ROOT,
    MOUNTINFO_INDEX_MOUNT_POINT,
    MOUNTINFO_INDEX_MOUNT_OPTIONS,
    MOUNTINFO_INDEX_OPTIONAL_FIELDS,
    MOUNTINFO_INDEX_FS_TYPE,
    MOUNTINFO_INDEX_MOUNT_SOURCE,
    MOUNTINFO_INDEX_SUPER_OPITONS,
    _MOUNTINFO_INDEX_MAX,
};

#define MOUNTINFO_ROOT(val) (val.mntnf_items[MOUNTINFO_INDEX_ROOT])
#define MOUNTINFO_MOUNT_POINT(val) (val.mntnf_items[MOUNTINFO_INDEX_MOUNT_POINT])
#define MOUNTINFO_MOUNT_SOURCE(val) (val.mntnf_items[MOUNTINFO_INDEX_MOUNT_SOURCE])

struct mountinfo
{
    /*  4 : root of the mount within the filesystem */
    /*  5 : mount point relative to the process's root */
    /* 10 : mount source: filesystem specific information or "none" */
    /*      but it mount source is preceded by 0 or more optional fields */
    /*      so the effective value is 9 */
    char *mntnf_items[_MOUNTINFO_INDEX_MAX];
};
void libreport_mountinfo_destroy(struct mountinfo *mntnf);
int libreport_get_mountinfo_for_mount_point(FILE *fin, struct mountinfo *mntnf, const char *mnt_point);

/* Takes ptr to time_t, or NULL if you want to use current time.
 * Returns "YYYY-MM-DD-hh:mm:ss" string.
 */
char *libreport_iso_date_string(const time_t *pt);
#define LIBREPORT_ISO_DATE_STRING_SAMPLE "YYYY-MM-DD-hh:mm:ss"
#define LIBREPORT_ISO_DATE_STRING_FORMAT "%Y-%m-%d-%H:%M:%S"

/* Parses date into integer UNIX time stamp
 *
 * @param date The parsed date string
 * @param pt Return value
 * @return 0 on success; otherwise non-0 number. -EINVAL if the parameter date
 * does not match LIBREPORT_ISO_DATE_STRING_FORMAT
 */
int libreport_iso_date_string_parse(const char *date, time_t *pt);

enum {
    MAKEDESC_SHOW_FILES     = (1 << 0),
    MAKEDESC_SHOW_MULTILINE = (1 << 1),
    MAKEDESC_SHOW_ONLY_LIST = (1 << 2),
    MAKEDESC_WHITELIST      = (1 << 3),
    /* Include all URLs from FILENAME_REPORTED_TO element in the description text */
    MAKEDESC_SHOW_URLS      = (1 << 4),
};
char *libreport_make_description(problem_data_t *problem_data, char **names_to_skip, unsigned max_text_size, unsigned desc_flags);
char *libreport_make_description_logger(problem_data_t *problem_data, unsigned max_text_size);

/* See man os-release(5) for details */
#define OSINFO_ID "ID"
#define OSINFO_NAME "NAME"
#define OSINFO_VERSION_ID "VERSION_ID"
#define OSINFO_PRETTY_NAME "PRETTY_NAME"

/* @brief Loads a text in format of os-release(5) in to a map
 *
 * Function doesn't check for format errors much. It just tries to avoid
 * program errors. In case of error the function prints out a log message and
 * continues in parsing.
 *
 * @param osinfo_bytes Non-NULL pointer to osinfo bytes.
 * @param osinfo The map where result is stored
 */
void libreport_parse_osinfo(const char *osinfo_bytes, map_string_t *osinfo);

/* @brief Builds product string and product's version string for Bugzilla
 *
 * At first tries to get strings from the os specific variables
 * (REDHAT_BUGZILLA_PRODUCT, REDHAT_BUGZILLA_PRODUCT_VERSION) if no such
 * variables are found, uses NAME key for the product and VERSION_ID key for
 * the product's version. If neither NAME nor VERSION_ID are provided fallbacks
 * to parsing of os_release which should be stored under PRETTY_NAME key.
 *
 * https://bugzilla.redhat.com/show_bug.cgi?id=950373
 *
 * @param osinfo Input data from which the values are built
 * @param produc Non-NULL pointer where pointer to malloced string will be stored. Memory must be released by free()
 * @param version Non-NULL pointer where pointer to malloced string will be stored. Memory must be released by free()
 */
void libreport_parse_osinfo_for_bz(map_string_t *osinfo, char **product, char **version);

/* @brief Extract BUG_REPORT_URL from os-release
 *
 * A default location for bug reports can be stored in os-release.
 * This extracts the value if present and stores it in url.
 * If unset, url will become NULL
 *
 * https://github.com/abrt/libreport/issues/459
 *
 * @param osinfo Input data from which the values are built
 * @param url Non-NULL pointer where pointer to malloced string will be stored. Memory must be released by free()
 */
void libreport_parse_osinfo_for_bug_url(map_string_t *osinfo, char** url);

/* @brief Builds product string and product's version string for Red Hat Support
 *
 * At first tries to get strings from the os specific variables
 * (REDHAT_SUPPORT_PRODUCT, REDHAT_SUPPORT_PRODUCT_VERSION) if no such
 * variables are found, uses NAME key for the product and VERSION_ID key for
 * the product's version. If no NAME nor VERSION_ID are provided fallbacks to
 * parsing of os_release which should be stored under PRETTY_NAME key.
 *
 * https://bugzilla.redhat.com/show_bug.cgi?id=950373
 *
 * @param osinfo Input data from which the values are built
 * @param produc Non-NULL pointer where pointer to malloced string will be stored. Memory must be released by free()
 * @param version Non-NULL pointer where pointer to malloced string will be stored. Memory must be released by free()
 */
void libreport_parse_osinfo_for_rhts(map_string_t *osinfo, char **product, char **version);

void libreport_parse_release_for_bz(const char *pRelease, char **product, char **version);
void libreport_parse_release_for_rhts(const char *pRelease, char **product, char **version);

/**
 * Loads settings and stores it in second parameter. On success it
 * returns true, otherwise returns false.
 *
 * @param path A path of config file.
 *  Config file consists of "key=value" lines.
 * @param settings A read plugin's settings.
 * @param skipKeysWithoutValue
 *  If true, lines in format "key=" (without value) are skipped.
 *  Otherwise empty value "" is inserted into pSettings.
 *  TODO: all callers pass "false" here, drop this parameter
 *  in mid-2013 if no user for it is identified.
 * @return if it success it returns true, otherwise it returns false.
 */
bool libreport_load_conf_file(const char *pPath, map_string_t *settings, bool skipKeysWithoutValue);
bool libreport_load_plugin_conf_file(const char *name, map_string_t *settings, bool skipKeysWithoutValue);

const char *libreport_get_user_conf_base_dir(void);

bool libreport_load_conf_file_from_dirs(const char *base_name, const char *const *directories, map_string_t *settings, bool skipKeysWithoutValue);

enum {
    CONF_DIR_FLAG_NONE = 0,
    CONF_DIR_FLAG_OPTIONAL = 1,
};

bool libreport_load_conf_file_from_dirs_ext(const char *base_name, const char *const *directories,
                                  const int * dir_flags, map_string_t *settings,
                                  bool skipKeysWithoutValue);

bool libreport_save_conf_file(const char *path, map_string_t *settings);
bool libreport_save_plugin_conf_file(const char *name, map_string_t *settings);

bool libreport_save_app_conf_file(const char* application_name, map_string_t *settings);
bool libreport_load_app_conf_file(const char *application_name, map_string_t *settings);
void libreport_set_app_user_setting(map_string_t *settings, const char *name, const char *value);
const char *libreport_get_app_user_setting(map_string_t *settings, const char *name);

bool libreport_save_user_settings(void);
bool libreport_load_user_settings(const char *application_name);
void libreport_set_user_setting(const char *name, const char *value);
const char *libreport_get_user_setting(const char *name);

/* filename is expected to exist in CONF_DIR
 * usually /etc/libreport
 */
GList *libreport_load_words_from_file(const char *filename);
GList *libreport_get_file_list(const char *path, const char *ext);
void libreport_free_file_list(GList *filelist);
file_obj_t *libreport_new_file_obj(const char* fullpath, const char* filename);
void libreport_free_file_obj(file_obj_t *f);
GList *libreport_parse_delimited_list(const char *string, const char *delimiter);

/* Connect to abrtd over unix domain socket, issue DELETE command */
int delete_dump_dir_possibly_using_abrtd(const char *dump_dir_name);

/* Tries to create a copy of dump_dir_name in base_dir, with same or similar basename.
 * Returns NULL if copying failed. In this case, logs a message before returning. */
struct dump_dir *libreport_steal_directory(const char *base_dir, const char *dump_dir_name);

/* Resolves if the given user is in given group
 *
 * @param uid user ID
 * @param gid group ID
 * @returns TRUE in case the user is in the group otherwise returns FALSE
 */
bool libreport_uid_in_group(uid_t uid, gid_t gid);

/* Tries to open dump_dir_name with writing access. If function needs to steal
 * directory calls ask_continue(new base dir, dump dir) callback to ask user
 * for permission. If ask_continue param is NULL the function thinks that an
 * answer is positive and steals directory.
 * Returns NULL if opening failed or if stealing was dismissed. In this case,
 * logs a message before returning. */
struct dump_dir *libreport_open_directory_for_writing(
                         const char *dump_dir_name,
                         bool (*ask_continue)(const char *, const char *));

// Files bigger than this are never considered to be text.
//
// Started at 64k limit. But _some_ limit is necessary:
// fields declared "text" may end up in editing fields and such.
// We don't want to accidentally end up with 100meg text in a textbox!
// So, don't remove this. If you really need to, raise the limit.
//
// Bumped up to 200k: saw 124740 byte /proc/PID/smaps file
// Bumped up to 500k: saw 375252 byte anaconda traceback file
// Bumped up to 1M: bugzilla.redhat.com/show_bug.cgi?id=746727
// mentions 853646 byte anaconda-tb-* file.
// Bumped up to 8M: bugzilla.redhat.com/show_bug.cgi?id=887570
// (anaconda-tb file of 1.38 MBytes)
//
#define CD_MAX_TEXT_SIZE (8*1024*1024)

// Text bigger than this usually is attached, not added inline
// was 2k, 20kb is too much, let's try 4kb
//
// For bug databases
#define CD_TEXT_ATT_SIZE_BZ     (4*1024)
// For dumping problem data into a text file, email, etc
#define CD_TEXT_ATT_SIZE_LOGGER (CD_MAX_TEXT_SIZE)

// Filenames in problem directory:
// filled by a hook:
#define FILENAME_TIME         "time"        /* mandatory */
#define FILENAME_LAST_OCCURRENCE  "last_occurrence" /* optional */
#define FILENAME_REASON       "reason"      /* mandatory? */
#define FILENAME_UID          "uid"         /* mandatory? */

/*
 * "analyzer" is to be gradually changed to "type":
 * For now, we fetch and look at "analyzer" element,
 * but we always save both "analyzer" and "type" (with same contents).
 * By 2013, we switch to looking at "type". Then we will stop generating
 * "analyzer" element.
 * ----
 * Update 2015: based on the recent changes where we have introduced several
 * tools generating one problem type, we have decided to retain 'analyzer'
 * file, but it shall contain string identifier of a tool that created the
 * problem.
 */
#define FILENAME_ANALYZER     "analyzer"
#define FILENAME_TYPE         "type"
#define FILENAME_EXECUTABLE   "executable"
#define FILENAME_PID          "pid"
#define FILENAME_TID          "tid"
#define FILENAME_GLOBAL_PID   "global_pid"
#define FILENAME_PWD          "pwd"
#define FILENAME_ROOTDIR      "rootdir"
#define FILENAME_BINARY       "binary"
#define FILENAME_CMDLINE      "cmdline"
#define FILENAME_COREDUMP     "coredump"
#define FILENAME_CGROUP       "cgroup"
#define FILENAME_BACKTRACE    "backtrace"
#define FILENAME_MAPS         "maps"
#define FILENAME_SMAPS        "smaps"
#define FILENAME_PROC_PID_STATUS "proc_pid_status"
#define FILENAME_ENVIRON      "environ"
#define FILENAME_LIMITS       "limits"
#define FILENAME_OPEN_FDS     "open_fds"
#define FILENAME_MOUNTINFO    "mountinfo"
#define FILENAME_NAMESPACES   "namespaces"
#define FILENAME_CPUINFO      "cpuinfo"

/* Global problem identifier which is usually generated by some "analyze_*"
 * event because it may take a lot of time to obtain strong problem
 * identification */
#define FILENAME_DUPHASH      "duphash"

// Name of the function where the application crashed.
// Optional.
#define FILENAME_CRASH_FUNCTION "crash_function"
#define FILENAME_ARCHITECTURE "architecture"
#define FILENAME_KERNEL       "kernel"
/*
 * From /etc/os-release
 * os_release filename name is alredy occupied by /etc/redhat-release (see
 * below) in sake of backward compatibility /etc/os-release is stored in
 * os_info file
 */
#define FILENAME_OS_INFO      "os_info"
#define FILENAME_OS_INFO_IN_ROOTDIR "os_info_in_rootdir"
// From /etc/system-release or /etc/redhat-release
#define FILENAME_OS_RELEASE   "os_release"
#define FILENAME_OS_RELEASE_IN_ROOTDIR "os_release_in_rootdir"
// Filled by <what?>
#define FILENAME_PACKAGE      "package"
#define FILENAME_COMPONENT    "component"
#define FILENAME_COMMENT      "comment"
#define FILENAME_RATING       "backtrace_rating"
#define FILENAME_HOSTNAME     "hostname"
// Optional. Set to "1" by abrt-handle-upload for every unpacked dump
#define FILENAME_REMOTE       "remote"
#define FILENAME_TAINTED      "kernel_tainted"
#define FILENAME_TAINTED_SHORT "kernel_tainted_short"
#define FILENAME_TAINTED_LONG  "kernel_tainted_long"
#define FILENAME_VMCORE       "vmcore"
#define FILENAME_KERNEL_LOG   "kernel_log"
// File created by createAlertSignature() from libreport's python module
// The file should contain a description of an alert
#define FILENAME_DESCRIPTION  "description"

/* Local problem identifier (weaker than global identifier) designed for fast
 * local for fast local duplicate identification. This file is usually provided
 * by crashed application (problem creator).
 */
#define FILENAME_UUID         "uuid"

#define FILENAME_COUNT        "count"
/* Multi-line list of places problem was reported.
 * Recommended line format:
 * "Reporter: VAR=VAL VAR=VAL"
 * Use libreport_add_reported_to(dd, "line_without_newline"): it adds line
 * only if it is not already there.
 */
#define FILENAME_REPORTED_TO  "reported_to"
#define FILENAME_EVENT_LOG    "event_log"
/*
 * If exists, should contain a full sentence (with trailing period)
 * which describes why this problem should not be reported.
 * Example: "Your laptop firmware 1.9a is buggy, version 1.10 contains the fix."
 */
#define FILENAME_NOT_REPORTABLE "not-reportable"
#define FILENAME_CORE_BACKTRACE "core_backtrace"
#define FILENAME_REMOTE_RESULT "remote_result"
#define FILENAME_PKG_EPOCH     "pkg_epoch"
#define FILENAME_PKG_NAME      "pkg_name"
#define FILENAME_PKG_VERSION   "pkg_version"
#define FILENAME_PKG_RELEASE   "pkg_release"
#define FILENAME_PKG_ARCH      "pkg_arch"

/* RHEL packages - Red Hat, Inc. */
#define FILENAME_PKG_VENDOR    "pkg_vendor"
/* RHEL keys - https://access.redhat.com/security/team/key */
#define FILENAME_PKG_FINGERPRINT "pkg_fingerprint"

#define FILENAME_USERNAME      "username"
#define FILENAME_ABRT_VERSION  "abrt_version"
#define FILENAME_EXPLOITABLE   "exploitable"

/* reproducible element is used by functions from problem_data.h */
#define FILENAME_REPRODUCIBLE  "reproducible"
#define FILENAME_REPRODUCER    "reproducer"

/* File names related to Anaconda problems
 */
#define FILENAME_KICKSTART_CFG "ks.cfg"
#define FILENAME_ANACONDA_TB   "anaconda-tb"

/* Containers
 */
#define FILENAME_CONTAINER         "container"
#define FILENAME_CONTAINER_ID      "container_id"
#define FILENAME_CONTAINER_UUID    "container_uuid"
#define FILENAME_CONTAINER_IMAGE   "container_image"
#define FILENAME_CONTAINER_CMDLINE "container_cmdline"
/* Container root file-system directory as seen from the host. */
#define FILENAME_CONTAINER_ROOTFS  "container_rootfs"
#define FILENAME_DOCKER_INSPECT    "docker_inspect"

/* Type of catched exception
 * Optional.
 */
#define FILENAME_EXCEPTION_TYPE    "exception_type"

// Not stored as files, added "on the fly":
#define CD_DUMPDIR            "Directory"

gint libreport_cmp_problem_data(gconstpointer a, gconstpointer b, gpointer filename);

//UNUSED:
//// "Which events are possible (make sense) on this dump dir?"
//// (a string with "\n" terminated event names)
//#define CD_EVENTS             "Events"

/* FILENAME_EVENT_LOG is trimmed to below LOW_WATERMARK
 * when it reaches HIGH_WATERMARK size
 */
enum {
    EVENT_LOG_HIGH_WATERMARK = 30 * 1024,
    EVENT_LOG_LOW_WATERMARK  = 20 * 1024,
};

void libreport_log_problem_data(problem_data_t *problem_data, const char *pfx);

extern int g_libreport_inited;
void libreport_init(void);

#define INITIALIZE_LIBREPORT() \
    do \
    { \
        if (!g_libreport_inited) \
        { \
            g_libreport_inited = 1; \
            libreport_init(); \
        } \
    } \
    while (0)

const char *abrt_init(char **argv);
void libreport_export_abrt_envvars(int pfx);
extern const char *libreport_g_progname;

enum parse_opt_type {
    OPTION_BOOL,
    OPTION_GROUP,
    OPTION_STRING,
    OPTION_INTEGER,
    OPTION_OPTSTRING,
    OPTION_LIST,
    OPTION_END,
};

struct options {
    enum parse_opt_type type;
    int short_name;
    const char *long_name;
    void *value;
    const char *argh;
    const char *help;
};

/*
 * s - short_name
 * l - long_name
 * v - value
 * a - option parameter name (for help text)
 * h - help
 */
#define OPT_END()                    { OPTION_END, 0, NULL, NULL, NULL, NULL }
#define OPT_GROUP(h)                 { OPTION_GROUP, 0, NULL, NULL, NULL, (h) }
#define OPT_BOOL(     s, l, v,    h) { OPTION_BOOL     , (s), (l), (v), NULL , (h) }
#define OPT_INTEGER(  s, l, v,    h) { OPTION_INTEGER  , (s), (l), (v), "NUM", (h) }
#define OPT_STRING(   s, l, v, a, h) { OPTION_STRING   , (s), (l), (v), (a)  , (h) }
#define OPT_OPTSTRING(s, l, v, a, h) { OPTION_OPTSTRING, (s), (l), (v), (a)  , (h) }
#define OPT_LIST(     s, l, v, a, h) { OPTION_LIST     , (s), (l), (v), (a)  , (h) }

#define OPT__VERBOSE(v)     OPT_BOOL('v', "verbose", (v), _("Be verbose"))
#define OPT__DUMP_DIR(v)    OPT_STRING('d', "problem-dir", (v), "DIR", _("Problem directory"))

unsigned libreport_parse_opts(int argc, char **argv, const struct options *opt,
                const char *usage);

void libreport_show_usage_and_die(const char *usage, const struct options *opt) NORETURN;

/* Can't include "abrt_curl.h", it's not a public API.
 * Resorting to just forward-declaring the struct we need.
 */
struct abrt_post_state;

/* Decomposes uri to its base elements, removes userinfo out of the hostname and
 * composes a new uri without userinfo.
 *
 * The function does not validate the url.
 *
 * @param uri The uri that might contain userinfo
 * @param result The userinfo free uri will be store here. Cannot be null. Must
 * be de-allocated by free.
 * @param scheme Scheme of the uri. Can be NULL. Result can be NULL. Result
 * must be de-allocated by free.
 * @param hostname Hostname of the uri. Can be NULL. Result can be NULL. Result
 * must be de-allocated by free.
 * @param username Username of the uri. Can be NULL. Result can be NULL. Result
 * must be de-allocated by free.
 * @param password Password of the uri. Can be NULL. Result can be NULL. Result
 * must be de-allocated by free.
 * @param location Location of the uri. Can be NULL. Result is never NULL. Result
 * must be de-allocated by free.
 */
int libreport_uri_userinfo_remove(const char *uri, char **result, char **scheme, char **hostname, char **username, char **password, char **location);

#ifdef __cplusplus
}
#endif

#endif
