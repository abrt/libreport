/*
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
#include "internal_libreport.h"

/* If s is a string with only printable ASCII chars
 * and has no spaces, ", ', and \, copy it verbatim.
 * Else, encapsulate it in single quotes, and
 * encode ', " and \ with \c escapes.
 * Control chars are encoded as \r, \n, \t, or \xNN.
 * In all cases, terminating NUL is added
 * and the pointer to it is returned.
 */
static char *append_escaped(char *start, const char *s)
{
    char *dst = start;
    const unsigned char *p = (unsigned char *)s;

    while (1)
    {
        const unsigned char *old_p = p;
        while (*p > ' ' && *p <= 0x7e && *p != '\"' && *p != '\'' && *p != '\\')
            p++;
        if (dst == start)
        {
            if (p != (unsigned char *)s && *p == '\0')
            {
                /* entire word does not need escaping and quoting */
                strcpy(dst, s);
                dst += strlen(s);
                return dst;
            }
            *dst++ = '\'';
        }

        strncpy(dst, (char *)old_p, (p - old_p));
        dst += (p - old_p);

        if (*p == '\0')
        {
            *dst++ = '\'';
            *dst = '\0';
            return dst;
        }

        char hex_char_buf[5];
        const char *a;
        switch (*p)
        {
        case '\r': a = "\\r"; break;
        case '\n': a = "\\n"; break;
        case '\t': a = "\\t"; break;
        case '\'': a = "\\\'"; break;
        case '\"': a = "\\\""; break;
        case '\\': a = "\\\\"; break;
        case ' ': a = " "; break;
        default:
            /* Build \xNN string */
            hex_char_buf[0] = '\\';
            hex_char_buf[1] = 'x';
            hex_char_buf[2] = "0123456789abcdef"[*p >> 4];
            hex_char_buf[3] = "0123456789abcdef"[*p & 0xf];
            hex_char_buf[4] = '\0';
            a = hex_char_buf;
        }
        strcpy(dst, a);
        dst += strlen(a);
        p++;
    }
}

static char* get_escaped_at(int dir_fd, const char *name, char separator)
{
    char *escaped = NULL;

    int fd = openat(dir_fd, name, O_RDONLY);
    if (fd >= 0)
    {
        char *dst = NULL;
        unsigned total_esc_len = 0;
        while (total_esc_len < 1024 * 1024) /* paranoia check */
        {
            /* read and escape one block */
            char buffer[4 * 1024 + 1];
            int len = read(fd, buffer, sizeof(buffer) - 1);
            if (len <= 0)
                break;
            buffer[len] = '\0';

            /* string CC can expand into '\xNN\xNN' and thus needs len*4 + 3 bytes,
             * including terminating NUL.
             * We add +1 for possible \n added at the very end.
             */
            escaped = xrealloc(escaped, total_esc_len + len*4 + 4);
            char *src = buffer;
            dst = escaped + total_esc_len;
            while (1)
            {
                /* escape till next NUL char */
                char *d = append_escaped(dst, src);
                total_esc_len += (d - dst);
                dst = d;
                src += strlen(src) + 1;
                if ((src - buffer) >= len)
                    break;
                *dst++ = separator;
            }

        }

        if (dst)
        {
            if (separator == '\n')
                *dst++ = separator;
            *dst = '\0';
        }

        close(fd);
    }

    return escaped;
}

#define DEFINE_GET_PROC_FILE_WRAPPER_AT(FUNCTION_NAME) \
char *FUNCTION_NAME(pid_t pid) \
{ \
    const int pid_proc_fd = open_proc_pid_dir(pid); \
    if (pid_proc_fd < 0) \
        return NULL; \
    char *const r = FUNCTION_NAME##_at(pid_proc_fd); \
    close(pid_proc_fd); \
    return r; \
}

DEFINE_GET_PROC_FILE_WRAPPER_AT(get_cmdline)

char* get_cmdline_at(int pid_proc_fd)
{
    return get_escaped_at(pid_proc_fd, "cmdline", ' ');
}

DEFINE_GET_PROC_FILE_WRAPPER_AT(get_environ)

char* get_environ_at(int pid_proc_fd)
{
    return get_escaped_at(pid_proc_fd, "environ", '\n');
}

DEFINE_GET_PROC_FILE_WRAPPER_AT(get_executable)

char* get_executable_at(int pid_proc_fd)
{
    char *executable = malloc_readlinkat(pid_proc_fd, "exe");
    if (!executable)
        return NULL;
    /* find and cut off " (deleted)" from the path */
    char *deleted = executable + strlen(executable) - strlen(" (deleted)");
    if (deleted > executable && strcmp(deleted, " (deleted)") == 0)
    {
        *deleted = '\0';
        log_info("File '%s' seems to be deleted", executable);
    }
    /* find and cut off prelink suffixes from the path */
    char *prelink = executable + strlen(executable) - strlen(".#prelink#.XXXXXX");
    if (prelink > executable && strncmp(prelink, ".#prelink#.", strlen(".#prelink#.")) == 0)
    {
        log_info("File '%s' seems to be a prelink temporary file", executable);
        *prelink = '\0';
    }
    return executable;
}

DEFINE_GET_PROC_FILE_WRAPPER_AT(get_cwd)

char* get_cwd_at(int proc_pid_fd)
{
    return malloc_readlinkat(proc_pid_fd, "cwd");
}

DEFINE_GET_PROC_FILE_WRAPPER_AT(get_rootdir)

char* get_rootdir_at(int pid_proc_dir_fd)
{
    return malloc_readlinkat(pid_proc_dir_fd, "root");
}

static int get_proc_fs_id(const char *proc_pid_status, char type)
{
    char id_type[] = "_id";
    id_type[0] = type;

    int real, e_id, saved;
    int fs_id = 0;

    const char *line = proc_pid_status; /* never NULL */
    for (;;)
    {
        if (strncmp(line, id_type, 3) == 0)
        {
            int n = sscanf(line, "%*cid:\t%d\t%d\t%d\t%d\n", &real, &e_id, &saved, &fs_id);
            if (n != 4)
            {
                error_msg("Failed to parser /proc/[pid]/status: invalid format of '%cui:' line", type);
                return -1;
            }
            return fs_id;
        }
        line = strchr(line, '\n');
        if (!line)
            break;
        line++;
    }

    error_msg("Failed to parser /proc/[pid]/status: not found '%cui:' line", type);
    return -2;
}

int get_fsuid(const char *proc_pid_status)
{
    return get_proc_fs_id(proc_pid_status, /*UID*/'U');
}

int get_fsgid(const char *proc_pid_status)
{
    return get_proc_fs_id(proc_pid_status, /*GID*/'G');
}

int dump_fd_info_at(int pid_proc_fd, FILE *dest)
{
    DIR *proc_fd_dir = NULL;
    int proc_fdinfo_fd = -1;
    const char *fddelim = "";
    struct dirent *dent = NULL;
    int r = 0;

    int proc_fd_dir_fd = openat(pid_proc_fd, "fd", O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (proc_fd_dir_fd < 0)
    {
        r = -errno;
        goto dumpfd_cleanup;
    }

    proc_fd_dir = fdopendir(proc_fd_dir_fd);
    if (!proc_fd_dir)
    {
        r = -errno;
        goto dumpfd_cleanup;
    }

    proc_fdinfo_fd = openat(pid_proc_fd, "fdinfo", O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_PATH);
    if (proc_fdinfo_fd < 0)
    {
        r = -errno;
        goto dumpfd_cleanup;
    }

    while (1)
    {
        errno = 0;
        dent = readdir(proc_fd_dir);
        if (dent == NULL)
        {
            if (errno > 0)
            {
                r = -errno;
                goto dumpfd_cleanup;
            }
            break;
        }
        else if (dot_or_dotdot(dent->d_name))
            continue;

        FILE *fdinfo = NULL;
        char *fdname = NULL;
        char line[LINE_MAX];
        int fd;

        fdname = malloc_readlinkat(dirfd(proc_fd_dir), dent->d_name);

        fprintf(dest, "%s%s:%s\n", fddelim, dent->d_name, fdname);
        fddelim = "\n";

        /* Use the directory entry from /proc/[pid]/fd with /proc/[pid]/fdinfo */
        fd = openat(proc_fdinfo_fd, dent->d_name, O_NOFOLLOW|O_CLOEXEC|O_RDONLY);
        if (fd < 0)
            goto dumpfd_next_fd;

        fdinfo = fdopen(fd, "re");
        if (fdinfo == NULL)
            goto dumpfd_next_fd;

        while (fgets(line, sizeof(line)-1, fdinfo))
        {
            /* in case the line is not terminated, terminate it */
            char *eol = strchrnul(line, '\n');
            eol[0] = '\n';
            eol[1] = '\0';
            fputs(line, dest);
        }
        fclose(fdinfo);

dumpfd_next_fd:
        free(fdname);
    }

dumpfd_cleanup:
    closedir(proc_fd_dir);
    close(proc_fdinfo_fd);

    return r;
}

int dump_fd_info_ext(const char *dest_filename, const char *proc_pid_fd_path, uid_t uid, gid_t gid)
{
    int proc_fd_dir_fd = -1;
    int pid_proc_fd = -1;
    int dest_fd = -1;
    FILE *dest = NULL;
    int r = -1;

    proc_fd_dir_fd = open(proc_pid_fd_path, O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_PATH);
    if (proc_fd_dir_fd < 0)
    {
        r = -errno;
        goto dumpfd_cleanup;
    }

    pid_proc_fd = openat(proc_fd_dir_fd, "../", O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_PATH);
    if (pid_proc_fd < 0)
    {
        r = -errno;
        goto dumpfd_cleanup;
    }

    dest_fd = open(dest_filename, O_CREAT | O_WRONLY | O_EXCL | O_CLOEXEC, 0600);
    if (dest_fd < 0)
    {
        r = -errno;
        goto dumpfd_cleanup;
    }

    dest = xfdopen(dest_fd, "w");
    r = dump_fd_info_at(pid_proc_fd, dest);

dumpfd_cleanup:
    errno = 0;

    if ((dest_fd >= 0) && (uid != (uid_t)-1L))
    {
        r = fchown(dest_fd, uid, gid);
        if (r < 0)
        {
            perror_msg("Can't change '%s' ownership to %lu:%lu", dest_filename, (long)uid, (long)gid);
            close(dest_fd);
            unlink(dest_filename);
            dest_fd = -1;
        }
    }

    if (dest != NULL)
    {
        fclose(dest);
        dest_fd = -1;
    }

    if (dest_fd >= 0)
        close(dest_fd);

    if (r == 0 && errno != 0)
        r = -errno;

    close(proc_fd_dir_fd);
    close(pid_proc_fd);

    return r;
}

int dump_fd_info(const char *dest_filename, const char *proc_pid_fd_path)
{
    return dump_fd_info_ext(dest_filename, proc_pid_fd_path, /*UID*/-1, /*GID*/-1);
}

int get_env_variable_ext(int fd, char delim, const char *name, char **value)
{
    int workfd = dup(fd);
    if (workfd < 0)
    {
        perror_msg("dup()");
        return -errno;
    }

    FILE *fenv = fdopen(workfd, "re");
    if (fenv == NULL)
    {
        close(workfd);
        perror_msg("fdopen()");
        return -errno;
    }

    size_t len = strlen(name);
    int c = 0;
    while (c != EOF)
    {
        long i = 0;
        /* Check variable name */
        while ((c = fgetc(fenv)) != EOF && (i < len && c == name[i++]))
            ;

        if (c == EOF)
            break;

        const int skip = (c != '=' || name[i] != '\0');
        i = 0;

        /* Read to the end of variable entry */
        while ((c = fgetc(fenv)) != EOF && c != delim)
            ++i;

        /* Go to the next entry if the read entry isn't the searched variable */
        if (skip)
            continue;

        const int eof = c != EOF;
        *value = xmalloc(i+1);

        /* i+1 because we didn't count '\0'  */
        if (fseek(fenv, -(i+eof), SEEK_CUR) < 0)
            error_msg_and_die("Failed to seek");

        if (fread(*value, 1, i, fenv) != i)
            error_msg_and_die("Failed to read value");

        (*value)[i] = '\0';

        break;
    }

    fclose(fenv);
    return 0;
}

int get_env_variable(pid_t pid, const char *name, char **value)
{
    char path[sizeof("/proc/%lu/environ") + sizeof(long)*3];
    snprintf(path, sizeof(path), "/proc/%lu/environ", (long)pid);

    const int envfd = open(path, O_RDONLY);
    if (envfd < 0)
    {
        pwarn_msg("Failed to open environ file");
        return -errno;
    }

    const int r = get_env_variable_ext(envfd, '\0', name, value);
    close(envfd);

    return r;
}

int get_ns_ids(pid_t pid, struct ns_ids *ids)
{
    const int proc_pid_fd = open_proc_pid_dir(pid);
    const int r = get_ns_ids_at(proc_pid_fd, ids);
    close(proc_pid_fd);
    return r;
}

int get_ns_ids_at(int pid_proc_fd, struct ns_ids *ids)
{
    const int nsfd = openat(pid_proc_fd, "ns", O_DIRECTORY | O_PATH);
    if (nsfd < 0)
    {
        pwarn_msg("Failed to open /proc/[pid]/ns directory");
        return -errno;
    }

    int r = 0;
    for (size_t i = 0; i < ARRAY_SIZE(libreport_proc_namespaces); ++i)
    {
        const char *const ns_name = libreport_proc_namespaces[i];
        struct stat stbuf;
        if (fstatat(nsfd, ns_name, &stbuf, /* flags */0) < 0)
        {
            if (errno != ENOENT)
            {
                r = i + 1;
                break;
            }

            ids->nsi_ids[i] = PROC_NS_UNSUPPORTED;
        }
        else
        {
            ids->nsi_ids[i] = stbuf.st_ino;
        }
    }

    close(nsfd);
    return r;
}

int dump_namespace_diff_ext(const char *dest_filename, pid_t base_pid, pid_t tested_pid, uid_t uid, gid_t gid)
{
    const int dest_fd = open(dest_filename, O_CREAT | O_WRONLY | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (dest_fd < 0)
    {
        pwarn_msg("Failed to create %s", dest_filename);
        return -3;
    }

    FILE *const dest = xfdopen(dest_fd, "a");

    const int base_pid_proc_fd = open_proc_pid_dir(base_pid);
    const int tested_pid_proc_fd = open_proc_pid_dir(tested_pid);

    int r = dump_namespace_diff_at(base_pid_proc_fd, tested_pid_proc_fd, dest);

    close(tested_pid_proc_fd);
    close(base_pid_proc_fd);

    if (uid != (uid_t)-1L)
    {
        if (fchown(dest_fd, uid, gid) < 0)
        {
            perror_msg("Can't change '%s' ownership to %lu:%lu", dest_filename, (long)uid, (long)gid);
            r = -4;
        }
    }

    fclose(dest);
    if (r < 0)
        unlink(dest_filename);

    return r;
}

int dump_namespace_diff_at(int base_pid_proc_fd, int tested_pid_proc_fd, FILE *dest)
{
    struct ns_ids base_ids;
    struct ns_ids tested_ids;

    if (get_ns_ids_at(base_pid_proc_fd, &base_ids) != 0)
    {
        log_notice("Failed to get base namesapce IDs");
        return -1;
    }

    if (get_ns_ids_at(tested_pid_proc_fd, &tested_ids) != 0)
    {
        log_notice("Failed to get tested namesapce IDs");
        return -2;
    }

    for (size_t i = 0; i < ARRAY_SIZE(libreport_proc_namespaces); ++i)
    {
        const char *status = "unknown";

        if (base_ids.nsi_ids[i] != PROC_NS_UNSUPPORTED)
            status = base_ids.nsi_ids[i] == tested_ids.nsi_ids[i] ? "default" : "own";

        fprintf(dest, "%s : %s\n", libreport_proc_namespaces[i], status);
    }

    return 0;
}

int dump_namespace_diff(const char *dest_filename, pid_t base_pid, pid_t tested_pid)
{
    return dump_namespace_diff_ext(dest_filename, base_pid, tested_pid, /*UID*/-1, /*GID*/-1);
}

void mountinfo_destroy(struct mountinfo *mntnf)
{
    for (size_t i = 0; i < ARRAY_SIZE(mntnf->mntnf_items); ++i)
        free(mntnf->mntnf_items[i]);
}

static int _read_mountinfo_word(FILE *fin)
{
    int c;
    int pre_c = 0;
    while ((c = fgetc(fin)) != EOF && (pre_c == '\\' || c != ' ') && c != '\n')
        pre_c = c;

    return c;
}

/* Reads a word from a file and checks if the word equals the given string. */
static int _read_mountinfo_word_was(FILE *fin, const char *ptr)
{
    int c;
    int pre_c = 0;
    while (((c = fgetc(fin)) != EOF) && (*ptr != '\0') && (c == *ptr))
    {
        pre_c = c;
        ++ptr;
    }

    const int read_full_word = ((pre_c != '\\' && c == ' ') || c == '\n' || c == EOF);
    if (*ptr == '\0' && read_full_word)
        return 0;

    /* c cannot be NULL unless we are reading a binary file */
    if (read_full_word)
        return c;

    /* Read rest of the word. */
    return _read_mountinfo_word(fin);
}

int get_mountinfo_for_mount_point(FILE *fin, struct mountinfo *mntnf, const char *mnt_point)
{
    int r = 0;

    memset(mntnf->mntnf_items, 0, sizeof(mntnf->mntnf_items));

    long pos_bck = 0;
    int c = 0;
    unsigned fn;
    while (1)
    {
        pos_bck = ftell(fin);
        if (pos_bck < 0)
        {
            pwarn_msg("ftell");
            r = -1;
            goto get_mount_info_cleanup;
        }

        /* the 5th field is mount point */
        for (fn = 0; fn < 4; ++fn)
        {
            if ((c = _read_mountinfo_word(fin)) == EOF)
                break;
        }

        if (c == EOF)
        {
            log_notice("Mountinfo line does not have enough fields %d", fn);
            r = 1;
            goto get_mount_info_cleanup;
        }

        /* compare mnt_point to the 5th field value */
        c = _read_mountinfo_word_was(fin, mnt_point);
        if (c == EOF)
        {
            log_notice("Mountinfo line does not have the mount point field");
            r = 2;
            goto get_mount_info_cleanup;
        }

        /* if true, then the current line is the one we are looking for */
        if (c == 0)
            break;

        /* go to the next line */
        while (((c = fgetc(fin)) != EOF) && c != '\n')
            ;

        if (c == EOF)
        {
            r = -ENOKEY;
            goto get_mount_info_cleanup;
        }
    }

    /* seek to the beginning of current line */
    fseek(fin, pos_bck, SEEK_SET);
    for (fn = 0; fn < ARRAY_SIZE(mntnf->mntnf_items); )
    {
        pos_bck = ftell(fin);
        if (pos_bck < 0)
        {
            pwarn_msg("ftell");
            r = -3;
            goto get_mount_info_cleanup;
        }

        if (fn == MOUNTINFO_INDEX_OPTIONAL_FIELDS)
        {
            /* Eat all optional fields delimited by -. */
            while((c = _read_mountinfo_word_was(fin, "-")) != 0 && c != EOF)
                ;
        }
        else
        {
            c = _read_mountinfo_word(fin);
        }

        if (c == EOF && fn != (ARRAY_SIZE(mntnf->mntnf_items) - 1))
        {
            log_notice("Unexpected end of file");
            r = -ENODATA;
            goto get_mount_info_cleanup;
        }

        /* we are standing on ' ', so len is +1 longer than the string we want to copy*/
        const long pos_cur = ftell(fin);
        if (pos_cur < 0)
        {
            pwarn_msg("ftell");
            r = -4;
            goto get_mount_info_cleanup;
        }

        size_t len = (pos_cur - pos_bck);
        mntnf->mntnf_items[fn] = xmalloc(sizeof(char) * (len));

        len -= c != EOF; /* we are standing on ' ' (except EOF) */

        if (fseek(fin, pos_bck, SEEK_SET) < 0)
        {
            pwarn_msg("fseek");
            goto get_mount_info_cleanup;
        }

        if (fread(mntnf->mntnf_items[fn], sizeof(char), len, fin) != len)
        {
            pwarn_msg("fread");
            goto get_mount_info_cleanup;
        }

        if (fn == MOUNTINFO_INDEX_OPTIONAL_FIELDS)
        {
            /* Ignore ' -' that delimits optional fields. */
            /* Handle also the case where optional fields contains only -. */
            mntnf->mntnf_items[fn][len >= 2 ? len - 2 : 0] = '\0';
        }
        else
            mntnf->mntnf_items[fn][len] = '\0';

        /* Don't advance File if the last read character is EOF */
        if (c != EOF && fseek(fin, 1, SEEK_CUR) < 0)
        {
            pwarn_msg("fseek");
            goto get_mount_info_cleanup;
        }

        ++fn;
    }

get_mount_info_cleanup:
    if (r)
        mountinfo_destroy(mntnf);

    return r;
}

static int proc_ns_eq(struct ns_ids *lhs_ids, struct ns_ids *rhs_ids, int neg)
{
    for (size_t i = 0; i < ARRAY_SIZE(lhs_ids->nsi_ids); ++i)
        if (    lhs_ids->nsi_ids[i] != PROC_NS_UNSUPPORTED
             && (neg ? lhs_ids->nsi_ids[i] == rhs_ids->nsi_ids[i]
                     : lhs_ids->nsi_ids[i] != rhs_ids->nsi_ids[i]))
            return 1;

    return 0;
}

static int get_process_ppid_at(int pid_proc_fd, pid_t *ppid)
{
    int r = 0;
    const int stat_fd = openat(pid_proc_fd, "stat", O_RDONLY | O_CLOEXEC);
    if (stat_fd < 0)
    {
        pwarn_msg("Failed to open stat file");
        return -1;
    }

    FILE *const stat_file = xfdopen(stat_fd, "r");
    const int p = fscanf(stat_file, "%*d %*s %*c %d", ppid);
    if (p != 1)
    {
        log_notice("Failed to parse stat line %d\n", p);
        r = -2;
    }

    if (stat_file != NULL)
        fclose(stat_file);

    return r;
}

int get_pid_of_container(pid_t pid, pid_t *init_pid)
{
    const int pid_proc_fd = open_proc_pid_dir(pid);
    if (pid_proc_fd < 0)
        return pid_proc_fd;
    const int r = get_pid_of_container_at(pid_proc_fd, init_pid);
    close (pid_proc_fd);
    return r;
}

int get_pid_of_container_at(int pid_proc_fd, pid_t *init_pid)
{
    int r = 0;
    pid_t ppid = 0;
    int cpid_proc_fd = dup(pid_proc_fd);
    if (cpid_proc_fd < 0)
    {
        log_notice("Failed to duplicate /proc/[pid] directory FD");
        return -4;
    }

    struct ns_ids pid_ids;
    if (get_ns_ids_at(cpid_proc_fd, &pid_ids) != 0)
    {
        log_notice("Failed to get process's IDs");
        close(cpid_proc_fd);
        return -1;
    }

    while (1)
    {
        if (get_process_ppid_at(cpid_proc_fd, &ppid) != 0)
        {
            r = -1;
            break;
        }

        if (ppid == 1)
            break;

        const int ppid_proc_fd = open_proc_pid_dir(ppid);
        if (ppid_proc_fd < 0)
        {
            log_notice("Failed to open parent's /proc/[pid] directory");
            r = -3;
            break;
        }

        struct ns_ids ppid_ids;
        if (get_ns_ids_at(ppid_proc_fd, &ppid_ids) != 0)
        {
            log_notice("Failed to get parent's (%d) IDs", ppid);
            close(ppid_proc_fd);
            r = -2;
            break;
        }

        /* If any pid's  NS differs from parent's NS, then parent is pid's container. */
        if (proc_ns_eq(&pid_ids, &ppid_ids, 0) != 0)
        {
            close(ppid_proc_fd);
            break;
        }

        close(cpid_proc_fd);
        cpid_proc_fd = ppid_proc_fd;
    }

    close(cpid_proc_fd);

    if (r == 0)
        *init_pid = ppid;

    return r;
}

int open_proc_pid_dir(pid_t pid)
{
    static char proc_dir_path[sizeof("/proc/%lu") + sizeof(long)*3];
    sprintf(proc_dir_path, "/proc/%lu", (long)pid);
    return open(proc_dir_path, O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_PATH);
}

/* Compare mount points of pid 1 and the tested pid.
 * Bare in mind that /proc/[pid]/root might be affected by chroot and we want
 * to be able to answer 'yes' also for chrooted processes within a container.
 */
int process_has_own_root_at(int pid_proc_fd)
{
    int r = -1;
    struct mountinfo pid_root;
    errno = 0;
    int mnt_fd = openat(pid_proc_fd, "mountinfo", O_RDONLY);
    if (mnt_fd < 0)
    {
        r = -errno;
        pnotice_msg("failed to open '/proc/[pid]/mountinfo'");
        return r;
    }

    FILE *fin = fdopen(mnt_fd, "r");
    if (fin == NULL)
    {
        /* This can happen only if there is a bug or we ran out of memory */
        r = -errno;
        notice_msg("fdopen(openat([pid's fd], mountinfo))");

        close(mnt_fd);
        return r;
    }

    r = get_mountinfo_for_mount_point(fin, &pid_root, "/");
    fclose(fin);
    if (r)
    {
        log_notice("cannot get mount info for [pid]'s /");
        return -ENOKEY;
    }

    struct mountinfo system_root;
    fin = fopen("/proc/1/mountinfo", "r");
    if (fin == NULL)
    {
        r = -errno;
        pnotice_msg("fopen(/proc/1/mountinfo)");

        mountinfo_destroy(&pid_root);
        return r;
    }

    r = get_mountinfo_for_mount_point(fin, &system_root, "/");
    fclose(fin);
    if (r)
    {
        mountinfo_destroy(&pid_root);

        log_notice("cannot get line for / from /proc/1/mountinfo");
        return -ENOKEY;
    }

    /* Compare the fields 10 (mount source) and 4 (root). */
    /* See man 5 proc for more details. */
    r = (   strcmp(MOUNTINFO_MOUNT_SOURCE(system_root), MOUNTINFO_MOUNT_SOURCE(pid_root)) != 0
         || strcmp(MOUNTINFO_ROOT        (system_root), MOUNTINFO_ROOT        (pid_root)) != 0);

    mountinfo_destroy(&system_root);
    mountinfo_destroy(&pid_root);

    return r;
}

int process_has_own_root(pid_t pid)
{
    const int pid_proc_fd = open_proc_pid_dir(pid);
    if (pid_proc_fd < 0)
    {
        perror_msg("Cannot open directory of process %d", pid);
        return -3;
    }

    const int ret = process_has_own_root_at(pid_proc_fd);
    close(pid_proc_fd);

    return ret;
}
