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
#include <stdbool.h>
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
#if ENABLE_NLS
# include <libintl.h>
# define _(S) gettext(S)
#else
# define _(S) (S)
#endif

#if HAVE_LOCALE_H
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


/* Pull in entire public libreport API */
#include "dump_dir.h"
#include "event_config.h"
#include "problem_data.h"
#include "report.h"
#include "run_event.h"
#include "file_obj.h"
#include "libreport_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define prefixcmp libreport_prefixcmp
int prefixcmp(const char *str, const char *prefix);
#define suffixcmp libreport_suffixcmp
int suffixcmp(const char *str, const char *suffix);
#define strtrim libreport_strtrim
char *strtrim(char *str);
#define strtrimch libreport_strtrimch
char *strtrimch(char *str, int ch);
#define append_to_malloced_string libreport_append_to_malloced_string
char *append_to_malloced_string(char *mstr, const char *append);
#define skip_whitespace libreport_skip_whitespace
char* skip_whitespace(const char *s);
#define skip_non_whitespace libreport_skip_non_whitespace
char* skip_non_whitespace(const char *s);
/* Like strcpy but can copy overlapping strings. */
#define overlapping_strcpy libreport_overlapping_strcpy
void overlapping_strcpy(char *dst, const char *src);

#define concat_path_file libreport_concat_path_file
char *concat_path_file(const char *path, const char *filename);
/*
 * Used to construct a name in a different directory with the basename
 * similar to the old name, if possible.
 */
#define concat_path_basename libreport_concat_path_basename
char *concat_path_basename(const char *path, const char *filename);

/* A-la fgets, but malloced and of unlimited size */
#define xmalloc_fgets libreport_xmalloc_fgets
char *xmalloc_fgets(FILE *file);
/* Similar, but removes trailing \n */
#define xmalloc_fgetline libreport_xmalloc_fgetline
char *xmalloc_fgetline(FILE *file);

/* On error, copyfd_XX prints error messages and returns -1 */
enum {
        COPYFD_SPARSE = 1 << 0,
};
#define copyfd_eof libreport_copyfd_eof
off_t copyfd_eof(int src_fd, int dst_fd, int flags);
#define copyfd_size libreport_copyfd_size
off_t copyfd_size(int src_fd, int dst_fd, off_t size, int flags);
#define copyfd_exact_size libreport_copyfd_exact_size
void copyfd_exact_size(int src_fd, int dst_fd, off_t size);
#define copy_file libreport_copy_file
off_t copy_file(const char *src_name, const char *dst_name, int mode);
#define copy_file_recursive libreport_copy_file_recursive
int copy_file_recursive(const char *source, const char *dest);

// NB: will return short read on error, not -1,
// if some data was read before error occurred
#define xread libreport_xread
void xread(int fd, void *buf, size_t count);
#define safe_read libreport_safe_read
ssize_t safe_read(int fd, void *buf, size_t count);
#define safe_write libreport_safe_write
ssize_t safe_write(int fd, const void *buf, size_t count);
#define full_read libreport_full_read
ssize_t full_read(int fd, void *buf, size_t count);
#define full_write libreport_full_write
ssize_t full_write(int fd, const void *buf, size_t count);
#define full_write_str libreport_full_write_str
ssize_t full_write_str(int fd, const char *buf);
#define xmalloc_read libreport_xmalloc_read
void* xmalloc_read(int fd, size_t *maxsz_p);
#define xmalloc_open_read_close libreport_xmalloc_open_read_close
void* xmalloc_open_read_close(const char *filename, size_t *maxsz_p);
#define xmalloc_xopen_read_close libreport_xmalloc_xopen_read_close
void* xmalloc_xopen_read_close(const char *filename, size_t *maxsz_p);


/* Returns malloc'ed block */
#define encode_base64 libreport_encode_base64
char *encode_base64(const void *src, int length);

/* Returns NULL if the string needs no sanitizing.
 * control_chars_to_sanitize is a bit mask.
 * If Nth bit is set, Nth control char will be sanitized (replaced by [XX]).
 */
#define sanitize_utf8 libreport_sanitize_utf8
char *sanitize_utf8(const char *src, uint32_t control_chars_to_sanitize);
enum {
    SANITIZE_ALL = 0xffffffff,
    SANITIZE_TAB = (1 << 9),
    SANITIZE_LF  = (1 << 10),
    SANITIZE_CR  = (1 << 13),
};

#define SHA1_RESULT_LEN (5 * 4)
typedef struct sha1_ctx_t {
        uint8_t wbuffer[64]; /* always correctly aligned for uint64_t */
        /* for sha256: void (*process_block)(struct md5_ctx_t*); */
        uint64_t total64;    /* must be directly before hash[] */
        uint32_t hash[8];    /* 4 elements for md5, 5 for sha1, 8 for sha256 */
} sha1_ctx_t;
#define sha1_begin libreport_sha1_begin
void sha1_begin(sha1_ctx_t *ctx);
#define sha1_hash libreport_sha1_hash
void sha1_hash(sha1_ctx_t *ctx, const void *buffer, size_t len);
#define sha1_end libreport_sha1_end
void sha1_end(sha1_ctx_t *ctx, void *resbuf);

/* Helpers to hash a string: */
#define str_to_sha1 libreport_str_to_sha1
const uint8_t *str_to_sha1(uint8_t result[SHA1_RESULT_LEN], const char *str);
#define str_to_sha1str libreport_str_to_sha1str
const char    *str_to_sha1str(char result[SHA1_RESULT_LEN*2 + 1], const char *str);

#define xatou libreport_xatou
unsigned xatou(const char *numstr);
#define xatoi libreport_xatoi
int xatoi(const char *numstr);
/* Using xatoi() instead of naive atoi() is not always convenient -
 * in many places people want *non-negative* values, but store them
 * in signed int. Therefore we need this one:
 * dies if input is not in [0, INT_MAX] range. Also will reject '-0' etc.
 * It should really be named xatoi_nonnegative (since it allows 0),
 * but that would be too long.
 */
#define xatoi_positive libreport_xatoi_positive
int xatoi_positive(const char *numstr);

//unused for now
//unsigned long long monotonic_ns(void);
//unsigned long long monotonic_us(void);
//unsigned monotonic_sec(void);

#define safe_waitpid libreport_safe_waitpid
pid_t safe_waitpid(pid_t pid, int *wstat, int options);

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
#define fork_execv_on_steroids libreport_fork_execv_on_steroids
pid_t fork_execv_on_steroids(int flags,
                char **argv,
                int *pipefds,
                char **env_vec,
                const char *dir,
                uid_t uid);
/* Returns malloc'ed string. NULs are retained, and extra one is appended
 * after the last byte (this NUL is not accounted for in *size_p) */
#define run_in_shell_and_save_output libreport_run_in_shell_and_save_output
char *run_in_shell_and_save_output(int flags,
                const char *cmd,
                const char *dir,
                size_t *size_p);

/* Random utility functions */

#define is_in_string_list libreport_is_in_string_list
bool is_in_string_list(const char *name, char **v);

#define is_in_comma_separated_list libreport_is_in_comma_separated_list
bool is_in_comma_separated_list(const char *value, const char *list);
#define is_in_comma_separated_list_of_glob_patterns libreport_is_in_comma_separated_list_of_glob_patterns
bool is_in_comma_separated_list_of_glob_patterns(const char *value, const char *list);

/* Frees every element'd data using free(),
 * then frees list itself using g_list_free(list):
 */
#define list_free_with_free libreport_list_free_with_free
void list_free_with_free(GList *list);

#define get_dirsize libreport_get_dirsize
double get_dirsize(const char *pPath);
#define get_dirsize_find_largest_dir libreport_get_dirsize_find_largest_dir
double get_dirsize_find_largest_dir(
                const char *pPath,
                char **worst_dir, /* can be NULL */
                const char *excluded /* can be NULL */
);

#define ndelay_on libreport_ndelay_on
int ndelay_on(int fd);
#define ndelay_off libreport_ndelay_off
int ndelay_off(int fd);
#define close_on_exec_on libreport_close_on_exec_on
int close_on_exec_on(int fd);

#define xmalloc libreport_xmalloc
void* xmalloc(size_t size);
#define xrealloc libreport_xrealloc
void* xrealloc(void *ptr, size_t size);
#define xzalloc libreport_xzalloc
void* xzalloc(size_t size);
#define xstrdup libreport_xstrdup
char* xstrdup(const char *s);
#define xstrndup libreport_xstrndup
char* xstrndup(const char *s, int n);

#define xpipe libreport_xpipe
void xpipe(int filedes[2]);
#define xdup libreport_xdup
int xdup(int from);
#define xdup2 libreport_xdup2
void xdup2(int from, int to);
#define xmove_fd libreport_xmove_fd
void xmove_fd(int from, int to);

#define xwrite libreport_xwrite
void xwrite(int fd, const void *buf, size_t count);
#define xwrite_str libreport_xwrite_str
void xwrite_str(int fd, const char *str);

#define xlseek libreport_xlseek
off_t xlseek(int fd, off_t offset, int whence);

#define xchdir libreport_xchdir
void xchdir(const char *path);

#define xvasprintf libreport_xvasprintf
char* xvasprintf(const char *format, va_list p);
#define xasprintf libreport_xasprintf
char* xasprintf(const char *format, ...);

#define xsetenv libreport_xsetenv
void xsetenv(const char *key, const char *value);
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
#define safe_unsetenv libreport_safe_unsetenv
void safe_unsetenv(const char *var_val);

#define xsocket libreport_xsocket
int xsocket(int domain, int type, int protocol);
#define xbind libreport_xbind
void xbind(int sockfd, struct sockaddr *my_addr, socklen_t addrlen);
#define xlisten libreport_xlisten
void xlisten(int s, int backlog);
#define xsendto libreport_xsendto
ssize_t xsendto(int s, const void *buf, size_t len,
                const struct sockaddr *to, socklen_t tolen);

#define xstat libreport_xstat
void xstat(const char *name, struct stat *stat_buf);
#define fstat_st_size_or_die libreport_fstat_st_size_or_die
off_t fstat_st_size_or_die(int fd);
#define stat_st_size_or_die libreport_stat_st_size_or_die
off_t stat_st_size_or_die(const char *filename);

#define xopen3 libreport_xopen3
int xopen3(const char *pathname, int flags, int mode);
#define xopen libreport_xopen
int xopen(const char *pathname, int flags);
#define xunlink libreport_xunlink
void xunlink(const char *pathname);

/* Just testing dent->d_type == DT_REG is wrong: some filesystems
 * do not report the type, they report DT_UNKNOWN for every dirent
 * (and this is not a bug in filesystem, this is allowed by standards).
 * This function handles this case. Note: it returns 0 on symlinks
 * even if they point to regular files.
 */
#define is_regular_file libreport_is_regular_file
int is_regular_file(struct dirent *dent, const char *dirname);

#define dot_or_dotdot libreport_dot_or_dotdot
bool dot_or_dotdot(const char *filename);
#define last_char_is libreport_last_char_is
char *last_char_is(const char *s, int c);

#define string_to_bool libreport_string_to_bool
bool string_to_bool(const char *s);

#define xseteuid libreport_xseteuid
void xseteuid(uid_t euid);
#define xsetegid libreport_xsetegid
void xsetegid(gid_t egid);
#define xsetreuid libreport_xsetreuid
void xsetreuid(uid_t ruid, uid_t euid);
#define xsetregid libreport_xsetregid
void xsetregid(gid_t rgid, gid_t egid);


/* Emit a string of hex representation of bytes */
#define bin2hex libreport_bin2hex
char* bin2hex(char *dst, const char *str, int count);
/* Convert "xxxxxxxx" hex string to binary, no more than COUNT bytes */
#define hex2bin libreport_hex2bin
char* hex2bin(char *dst, const char *str, int count);


enum {
    LOGMODE_NONE = 0,
    LOGMODE_STDIO = (1 << 0),
    LOGMODE_SYSLOG = (1 << 1),
    LOGMODE_BOTH = LOGMODE_SYSLOG + LOGMODE_STDIO,
    LOGMODE_CUSTOM = (1 << 2),
};

#define g_custom_logger libreport_g_custom_logger
extern void (*g_custom_logger)(const char*);
#define msg_prefix libreport_msg_prefix
extern const char *msg_prefix;
#define msg_eol libreport_msg_eol
extern const char *msg_eol;
#define logmode libreport_logmode
extern int logmode;
#define xfunc_error_retval libreport_xfunc_error_retval
extern int xfunc_error_retval;

/* A few magic exit codes */
#define EXIT_CANCEL_BY_USER 69
#define EXIT_STOP_EVENT_RUN 70

/* Verbosity level */
#define g_verbose libreport_g_verbose
extern int g_verbose;
/* VERB1 log("what you sometimes want to see, even on a production box") */
#define VERB1 if (g_verbose >= 1)
/* VERB2 log("debug message, not going into insanely small details") */
#define VERB2 if (g_verbose >= 2)
/* VERB3 log("lots and lots of details") */
#define VERB3 if (g_verbose >= 3)
/* there is no level > 3 */

#define  libreport_
#define xfunc_die libreport_xfunc_die
void xfunc_die(void) NORETURN;
#define log_msg libreport_log_msg
void log_msg(const char *s, ...) __attribute__ ((format (printf, 1, 2)));
/* It's a macro, not function, since it collides with log() from math.h */
#undef log
#define log(...) log_msg(__VA_ARGS__)
/* error_msg family will use g_custom_logger. log_msg does not. */
#define error_msg libreport_error_msg
void error_msg(const char *s, ...) __attribute__ ((format (printf, 1, 2)));
#define error_msg_and_die libreport_error_msg_and_die
void error_msg_and_die(const char *s, ...) __attribute__ ((noreturn, format (printf, 1, 2)));
/* Reports error message with libc's errno error description attached. */
#define perror_msg libreport_perror_msg
void perror_msg(const char *s, ...) __attribute__ ((format (printf, 1, 2)));
#define perror_msg_and_die libreport_perror_msg_and_die
void perror_msg_and_die(const char *s, ...) __attribute__ ((noreturn, format (printf, 1, 2)));
#define die_out_of_memory libreport_die_out_of_memory
void die_out_of_memory(void) NORETURN;


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
 * calling the function strbuf_free().
 */
#define strbuf_new libreport_strbuf_new
struct strbuf *strbuf_new(void);

/**
 * Releases the memory held by the string buffer.
 * @param strbuf
 * If the strbuf is NULL, no operation is performed.
 */
#define strbuf_free libreport_strbuf_free
void strbuf_free(struct strbuf *strbuf);

/**
 * Releases the strbuf, but not the internal buffer.  The internal
 * string buffer is returned.  Caller is responsible to release the
 * returned memory using free().
 */
#define strbuf_free_nobuf libreport_strbuf_free_nobuf
char* strbuf_free_nobuf(struct strbuf *strbuf);

/**
 * The string content is set to an empty string, erasing any previous
 * content and leaving its length at 0 characters.
 */
#define strbuf_clear libreport_strbuf_clear
void strbuf_clear(struct strbuf *strbuf);

/**
 * The current content of the string buffer is extended by adding a
 * character c at its end.
 */
#define strbuf_append_char libreport_strbuf_append_char
struct strbuf *strbuf_append_char(struct strbuf *strbuf, char c);

/**
 * The current content of the string buffer is extended by adding a
 * string str at its end.
 */
#define strbuf_append_str libreport_strbuf_append_str
struct strbuf *strbuf_append_str(struct strbuf *strbuf,
                                 const char *str);

/**
 * The current content of the string buffer is extended by inserting a
 * string str at its beginning.
 */
#define strbuf_prepend_str libreport_strbuf_prepend_str
struct strbuf *strbuf_prepend_str(struct strbuf *strbuf,
                                  const char *str);

/**
 * The current content of the string buffer is extended by adding a
 * sequence of data formatted as the format argument specifies.
 */
#define strbuf_append_strf libreport_strbuf_append_strf
struct strbuf *strbuf_append_strf(struct strbuf *strbuf,
                                  const char *format, ...);

/**
 * Same as strbuf_append_strf except that va_list is passed instead of
 * variable number of arguments.
 */
#define strbuf_append_strfv libreport_strbuf_append_strfv
struct strbuf *strbuf_append_strfv(struct strbuf *strbuf,
                                   const char *format, va_list p);

/**
 * The current content of the string buffer is extended by inserting a
 * sequence of data formatted as the format argument specifies at the
 * buffer beginning.
 */
#define strbuf_prepend_strf libreport_strbuf_prepend_strf
struct strbuf *strbuf_prepend_strf(struct strbuf *strbuf,
                                   const char *format, ...);

/**
 * Same as strbuf_prepend_strf except that va_list is passed instead of
 * variable number of arguments.
 */
#define strbuf_prepend_strfv libreport_strbuf_prepend_strfv
struct strbuf *strbuf_prepend_strfv(struct strbuf *strbuf,
                                    const char *format, va_list p);

/* Returns command line of running program.
 * Caller is responsible to free() the returned value.
 * If the pid is not valid or command line can not be obtained,
 * empty string is returned.
 */
#define get_cmdline libreport_get_cmdline
char* get_cmdline(pid_t pid);
#define get_environ libreport_get_environ
char* get_environ(pid_t pid);

/* Takes ptr to time_t, or NULL if you want to use current time.
 * Returns "YYYY-MM-DD-hh:mm:ss" string.
 */
#define iso_date_string libreport_iso_date_string
char *iso_date_string(const time_t *pt);
#define LIBREPORT_ISO_DATE_STRING_SAMPLE "YYYY-MM-DD-hh:mm:ss"

enum {
    MAKEDESC_SHOW_FILES     = (1 << 0),
    MAKEDESC_SHOW_MULTILINE = (1 << 1),
    MAKEDESC_SHOW_ONLY_LIST = (1 << 2),
    MAKEDESC_WHITELIST      = (1 << 3),
    /* Include all URLs from FILENAME_REPORTED_TO element in the description text */
    MAKEDESC_SHOW_URLS      = (1 << 4),
};
#define make_description libreport_make_description
char *make_description(problem_data_t *problem_data, char **names_to_skip, unsigned max_text_size, unsigned desc_flags);
#define make_description_item_multiline libreport_make_description_item_multiline
char *make_description_item_multiline(const char *name, const char* content);
#define make_description_bz libreport_make_description_bz
char* make_description_bz(problem_data_t *problem_data, unsigned max_text_size);
#define make_description_logger libreport_make_description_logger
char* make_description_logger(problem_data_t *problem_data, unsigned max_text_size);
#define make_description_mailx libreport_make_description_mailx
char* make_description_mailx(problem_data_t *problem_data, unsigned max_text_size);

#define parse_release_for_bz libreport_parse_release_for_bz
void parse_release_for_bz(const char *pRelease, char **product, char **version);
#define parse_release_for_rhts libreport_parse_release_for_rhts
void parse_release_for_rhts(const char *pRelease, char **product, char **version);

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
 * @return if it success it returns true, otherwise it returns false.
 */
#define load_conf_file libreport_load_conf_file
bool load_conf_file(const char *pPath, map_string_t *settings, bool skipKeysWithoutValue);

#define load_conf_file_from_dirs libreport_load_conf_file_from_dirs
bool load_conf_file_from_dirs(const char *base_name, const char *const *directories, map_string_t *settings, bool skipKeysWithoutValue);


#define save_conf_file libreport_save_conf_file
bool save_conf_file(const char *path, map_string_t *settings);
#define save_user_settings libreport_save_user_settings
bool save_user_settings();
#define load_user_settings libreport_load_user_settings
bool load_user_settings(const char *application_name);
#define set_user_setting libreport_set_user_setting
void set_user_setting(const char *name, const char *value);
#define get_user_setting libreport_get_user_setting
const char *get_user_setting(const char *name);
#define load_forbidden_words libreport_load_forbidden_words
GList *load_forbidden_words();
#define  get_file_list libreport_get_file_list
GList *get_file_list(const char *path, const char *ext);
#define free_file_list libreport_free_file_list
void free_file_list(GList *filelist);
#define new_file_obj libreport_new_file_obj
file_obj_t *new_file_obj(const char* fullpath, const char* filename);
#define free_file_obj libreport_free_file_obj
void free_file_obj(file_obj_t *f);
#define parse_list libreport_parse_list
GList *parse_list(const char* list);

/* Connect to abrtd over unix domain socket, issue DELETE command */
int delete_dump_dir_possibly_using_abrtd(const char *dump_dir_name);

/* Tries to create a copy of dump_dir_name in base_dir, with same or similar basename.
 * Returns NULL if copying failed. In this case, logs a message before returning. */
#define steal_directory libreport_steal_directory
struct dump_dir *steal_directory(const char *base_dir, const char *dump_dir_name);

#define make_dir_recursive libreport_make_dir_recursive
bool make_dir_recursive(char *dir, mode_t dir_mode);

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
//
#define CD_MAX_TEXT_SIZE (1024*1024)

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
 */
#define FILENAME_ANALYZER     "analyzer"
#define FILENAME_TYPE         "type"
#define FILENAME_EXECUTABLE   "executable"
#define FILENAME_PID          "pid"
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
#define FILENAME_DUPHASH      "duphash"
// Name of the function where the application crashed.
// Optional.
#define FILENAME_CRASH_FUNCTION "crash_function"
#define FILENAME_ARCHITECTURE "architecture"
#define FILENAME_KERNEL       "kernel"
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

#define FILENAME_UUID         "uuid"
#define FILENAME_COUNT        "count"
/* Multi-line list of places problem was reported.
 * Recommended line format:
 * "Reporter: VAR=VAL VAR=VAL"
 * Use add_reported_to(dd, "line_without_newline"): it adds line
 * only if it is not already there.
 */
#define FILENAME_REPORTED_TO  "reported_to"
#define FILENAME_EVENT_LOG    "event_log"
#define FILENAME_NOT_REPORTABLE "not-reportable"
#define FILENAME_CORE_BACKTRACE "core_backtrace"
#define FILENAME_REMOTE_RESULT "remote_result"
#define FILENAME_PKG_EPOCH     "pkg_epoch"
#define FILENAME_PKG_NAME      "pkg_name"
#define FILENAME_PKG_VERSION   "pkg_version"
#define FILENAME_PKG_RELEASE   "pkg_release"
#define FILENAME_PKG_ARCH      "pkg_arch"
#define FILENAME_USERNAME      "username"
#define FILENAME_ABRT_VERSION  "abrt_version"

// Not stored as files, added "on the fly":
#define CD_DUMPDIR            "Directory"

#define cmp_problem_data libreport_cmp_problem_data
gint cmp_problem_data(gconstpointer a, gconstpointer b, gpointer filename);

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

#define log_problem_data libreport_log_problem_data
void log_problem_data(problem_data_t *problem_data, const char *pfx);


const char *abrt_init(char **argv);
#define export_abrt_envvars libreport_export_abrt_envvars
void export_abrt_envvars(int pfx);
#define g_progname libreport_g_progname
extern const char *g_progname;

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
#define OPT_END()                    { OPTION_END }
#define OPT_GROUP(h)                 { OPTION_GROUP, 0, NULL, NULL, NULL, (h) }
#define OPT_BOOL(     s, l, v,    h) { OPTION_BOOL     , (s), (l), (v), NULL , (h) }
#define OPT_INTEGER(  s, l, v,    h) { OPTION_INTEGER  , (s), (l), (v), "NUM", (h) }
#define OPT_STRING(   s, l, v, a, h) { OPTION_STRING   , (s), (l), (v), (a)  , (h) }
#define OPT_OPTSTRING(s, l, v, a, h) { OPTION_OPTSTRING, (s), (l), (v), (a)  , (h) }
#define OPT_LIST(     s, l, v, a, h) { OPTION_LIST     , (s), (l), (v), (a)  , (h) }

#define OPT__VERBOSE(v)     OPT_BOOL('v', "verbose", (v), _("Be verbose"))
#define OPT__DUMP_DIR(v)    OPT_STRING('d', "problem-dir", (v), "DIR", _("Problem directory"))

#define parse_opts libreport_parse_opts
unsigned parse_opts(int argc, char **argv, const struct options *opt,
                const char *usage);

#define show_usage_and_die libreport_show_usage_and_die
void show_usage_and_die(const char *usage, const struct options *opt) NORETURN;

/* Can't include "abrt_curl.h", it's not a public API.
 * Resorting to just forward-declaring the struct we need.
 */
struct abrt_post_state;

#ifdef __cplusplus
}
#endif

#endif
