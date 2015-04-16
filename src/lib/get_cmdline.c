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

static char* get_escaped(const char *path, char separator)
{
    char *escaped = NULL;

    int fd = open(path, O_RDONLY);
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

char* get_cmdline(pid_t pid)
{
    char path[sizeof("/proc/%lu/cmdline") + sizeof(long)*3];
    snprintf(path, sizeof(path), "/proc/%lu/cmdline", (long)pid);
    return get_escaped(path, ' ');
}

char* get_environ(pid_t pid)
{
    char path[sizeof("/proc/%lu/environ") + sizeof(long)*3];
    snprintf(path, sizeof(path), "/proc/%lu/environ", (long)pid);
    return get_escaped(path, '\n');
}

char* get_executable(pid_t pid)
{
    char buf[sizeof("/proc/%lu/exe") + sizeof(long)*3];

    sprintf(buf, "/proc/%lu/exe", (long)pid);
    char *executable = malloc_readlink(buf);
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

char* get_cwd(pid_t pid)
{
    char buf[sizeof("/proc/%lu/cwd") + sizeof(long)*3];
    sprintf(buf, "/proc/%lu/cwd", (long)pid);
    return malloc_readlink(buf);
}

char* get_rootdir(pid_t pid)
{
    char buf[sizeof("/proc/%lu/root") + sizeof(long)*3];
    sprintf(buf, "/proc/%lu/root", (long)pid);
    return malloc_readlink(buf);
}

int get_fsuid(const char *proc_pid_status)
{
    int real, euid, saved;
    /* if we fail to parse the uid, then make it root only readable to be safe */
    int fs_uid = 0;

    const char *line = proc_pid_status; /* never NULL */
    for (;;)
    {
        if (strncmp(line, "Uid", 3) == 0)
        {
            int n = sscanf(line, "Uid:\t%d\t%d\t%d\t%d\n", &real, &euid, &saved, &fs_uid);
            if (n != 4)
                return -1;
            break;
        }
        line = strchr(line, '\n');
        if (!line)
            break;
        line++;
    }

    return fs_uid;
}

int dump_fd_info_ext(const char *dest_filename, const char *proc_pid_fd_path, uid_t uid, gid_t gid)
{
    DIR *proc_fd_dir = NULL;
    int proc_fdinfo_fd = -1;
    char *buffer = NULL;
    FILE *stream = NULL;
    const char *fddelim = "";
    struct dirent *dent = NULL;
    int r = 0;

    proc_fd_dir = opendir(proc_pid_fd_path);
    if (!proc_fd_dir)
    {
        r = -errno;
        goto dumpfd_cleanup;
    }

    proc_fdinfo_fd = openat(dirfd(proc_fd_dir), "../fdinfo", O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC|O_PATH);
    if (proc_fdinfo_fd < 0)
    {
        r = -errno;
        goto dumpfd_cleanup;
    }

    stream = fopen(dest_filename, "wex");
    if (!stream)
    {
        r = -ENOMEM;
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

        fprintf(stream, "%s%s:%s\n", fddelim, dent->d_name, fdname);
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
            fputs(line, stream);
        }

dumpfd_next_fd:
        fclose(fdinfo);
        free(fdname);
    }

dumpfd_cleanup:
    errno = 0;

    if (stream != NULL)
    {
        if (uid != (uid_t)-1L)
        {
            const int stream_fd = fileno(stream);
            r = fchown(stream_fd, uid, gid);
            if (r < 0)
            {
                perror_msg("Can't change '%s' ownership to %lu:%lu", dest_filename, (long)uid, (long)gid);
                fclose(stream);
                unlink(dest_filename);
                stream = NULL;
            }
        }

        if (stream != NULL)
            fclose(stream);
    }

    if (r == 0 && errno != 0)
        r = -errno;

    closedir(proc_fd_dir);
    close(proc_fdinfo_fd);
    free(buffer);

    return r;
}

int dump_fd_info(const char *dest_filename, const char *proc_pid_fd_path)
{
    return dump_fd_info_ext(dest_filename, proc_pid_fd_path, /*UID*/-1, /*GID*/-1);
}

int get_env_variable(pid_t pid, const char *name, char **value)
{
    char path[sizeof("/proc/%lu/environ") + sizeof(long)*3];
    snprintf(path, sizeof(path), "/proc/%lu/environ", (long)pid);

    FILE *fenv = fopen(path, "re");
    if (fenv == NULL)
    {
        pwarn_msg("Failed to open environ file");
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
        while ((c = fgetc(fenv)) != EOF && c !='\0')
            ++i;

        /* Go to the next entry if the read entry isn't the searched variable */
        if (skip)
            continue;

        *value = xmalloc(i+1);

        /* i+1 because we didn't count '\0'  */
        if (fseek(fenv, -(i+1), SEEK_CUR) < 0)
            error_msg_and_die("Failed to seek");

        if (fread(*value, 1, i, fenv) != i)
            error_msg_and_die("Failed to read value");

        (*value)[i] = '\0';

        break;
    }

    fclose(fenv);
    return 0;
}

int get_ns_ids(pid_t pid, struct ns_ids *ids)
{
    int r = 0;
    static char ns_dir_path[sizeof("/proc/%lu/ns") + sizeof(long)*3];
    sprintf(ns_dir_path, "/proc/%lu/ns", (long)pid);

    DIR *ns_dir_fd = opendir(ns_dir_path);
    if (ns_dir_fd == NULL)
    {
        pwarn_msg("Failed to open ns path");
        return -errno;
    }

    for (size_t i = 0; i < ARRAY_SIZE(libreport_proc_namespaces); ++i)
    {
        struct stat stbuf;
        if (fstatat(dirfd(ns_dir_fd), libreport_proc_namespaces[i], &stbuf, /* flags */0) != 0)
        {
            if (errno != ENOENT)
            {
                r = (i + 1);
                goto get_ns_ids_cleanup;
            }

            ids->nsi_ids[i] = PROC_NS_UNSUPPORTED;
            continue;
        }

        ids->nsi_ids[i] = stbuf.st_ino;
    }

get_ns_ids_cleanup:
    closedir(ns_dir_fd);

    return r;
}

int dump_namespace_diff_ext(const char *dest_filename, pid_t base_pid, pid_t tested_pid, uid_t uid, gid_t gid)
{
    struct ns_ids base_ids;
    struct ns_ids tested_ids;

    if (get_ns_ids(base_pid, &base_ids) != 0)
    {
        log_notice("Failed to get base namesapce IDs");
        return -1;
    }

    if (get_ns_ids(tested_pid, &tested_ids) != 0)
    {
        log_notice("Failed to get tested namesapce IDs");
        return -2;
    }

    FILE *fout = fopen(dest_filename, "wex");
    if (fout == NULL)
    {
        pwarn_msg("Failed to create %s", dest_filename);
        return -3;
    }

    for (size_t i = 0; i < ARRAY_SIZE(libreport_proc_namespaces); ++i)
    {
        const char *status = "unknown";

        if (base_ids.nsi_ids[i] != PROC_NS_UNSUPPORTED)
            status = base_ids.nsi_ids[i] == tested_ids.nsi_ids[i] ? "default" : "own";

        fprintf(fout, "%s : %s\n", libreport_proc_namespaces[i], status);
    }

    if (uid != (uid_t)-1L)
    {
        int fout_fd = fileno(fout);
        if (fchown(fout_fd, uid, gid) < 0)
        {
            perror_msg("Can't change '%s' ownership to %lu:%lu", dest_filename, (long)uid, (long)gid);
            fclose(fout);
            unlink(dest_filename);
            return -4;
        }
    }

    fclose(fout);
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

int get_mountinfo_for_mount_point(FILE *fin, struct mountinfo *mntnf, const char *mnt_point)
{
    int r = 0;

    memset(mntnf->mntnf_items, 0, sizeof(mntnf->mntnf_items));

    long pos_bck = 0;
    int c = 0;
    int pre_c;
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

        fn = 0;
        pre_c = c;
        while ((c = fgetc(fin)) != EOF)
        {
            /* read till eol count fields (words with escaped space) */
            fn += (c == '\n' || (pre_c != '\\' && c == ' '));
            /* the 4th field is mount point */
            if (fn >= 4)
                break;
        }

        if (c == EOF)
        {
            if (pre_c != '\n')
            {
                log_notice("Mountinfo line does not have enough fields %d\n", fn);
                r = 1;
            }
            goto get_mount_info_cleanup;
        }

        const char *ptr = mnt_point;
        /* compare mnt_point to the 4th field value */
        while (((c = fgetc(fin)) != EOF) && (*ptr != '\0') && (c == *ptr))
            ++ptr;

        if (c == EOF)
        {
            log_notice("Mountinfo line does not have root field\n");
            r = 1;
            goto get_mount_info_cleanup;
        }

        /* if true, then the current line is the one we are looking for */
        if (*ptr == '\0' && c == ' ')
            break;

        /* go to the next line */
        while (((c = fgetc(fin)) != EOF) && c != '\n')
            ;

        if (c == EOF)
        {
            r = -1;
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
            r = -1;
            goto get_mount_info_cleanup;
        }

        /* read entire field (a word with escaped space) */
        while ((c = fgetc(fin)) != EOF && (pre_c == '\\' || c != ' ') && c != '\n')
            ;

        if (c == EOF && fn != (ARRAY_SIZE(mntnf->mntnf_items) - 1))
        {
            log_notice("Unexpected end of file");
            r = 1;
            goto get_mount_info_cleanup;
        }

        /* we are standing on ' ', so len is +1 longer than the string we want to copy*/
        const long pos_cur = ftell(fin);
        if (pos_cur < 0)
        {
            pwarn_msg("ftell");
            r = -1;
            goto get_mount_info_cleanup;
        }

        size_t len = (pos_cur - pos_bck);
        mntnf->mntnf_items[fn] = xmalloc(sizeof(char) * (len));

        --len; /* we are standing on ' ' */

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

        mntnf->mntnf_items[fn][len] = '\0';

        if (fseek(fin, 1, SEEK_CUR) < 0)
        {
            pwarn_msg("fseek");
            goto get_mount_info_cleanup;
        }

        /* ignore optional fields
           'shared:X' 'master:X' 'propagate_from:X' 'unbindable'
         */
        if (   strncmp("shared:", mntnf->mntnf_items[fn], strlen("shared:")) == 0
            || strncmp("master:", mntnf->mntnf_items[fn], strlen("master:")) == 0
            || strncmp("propagate_from:", mntnf->mntnf_items[fn], strlen("propagate_from:")) == 0
            || strncmp("unbindable", mntnf->mntnf_items[fn], strlen("unbindable")) == 0)
        {
            free(mntnf->mntnf_items[fn]);
            mntnf->mntnf_items[fn] = NULL;
            continue;
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

static int get_process_ppid(pid_t pid, pid_t *ppid)
{
    int r = 0;
    static char stat_path[sizeof("/proc/%lu/stat") + sizeof(long)*3];
    sprintf(stat_path, "/proc/%lu/stat", (long)pid);

    FILE *stat_file = fopen(stat_path, "re");
    if (stat_file == NULL)
    {
        pwarn_msg("Failed to open stat file");
        r = -1;
        goto get_process_ppid_cleanup;
    }

    int p = fscanf(stat_file, "%*d %*s %*c %d", ppid);
    if (p != 1)
    {
        log_notice("Failed to parse stat line %d\n", p);
        r = -2;
        goto get_process_ppid_cleanup;
    }

get_process_ppid_cleanup:
    if (stat_file != NULL)
        fclose(stat_file);

    return r;
}

int get_pid_of_container(pid_t pid, pid_t *init_pid)
{
    pid_t cpid = pid;
    pid_t ppid = 0;

    struct ns_ids pid_ids;
    if (get_ns_ids(pid, &pid_ids) != 0)
    {
        log_notice("Failed to get process's IDs");
        return -1;
    }

    while (1)
    {
        if (get_process_ppid(cpid, &ppid) != 0)
            return -1;

        if (ppid == 1)
            break;

        struct ns_ids ppid_ids;
        if (get_ns_ids(ppid, &ppid_ids) != 0)
        {
            log_notice("Failed to get parent's IDs");
            return -2;
        }

        /* If any pid's  NS differs from parent's NS, then parent is pid's container. */
        if (proc_ns_eq(&pid_ids, &ppid_ids, 0) != 0)
            break;

        cpid = ppid;
    }

    *init_pid = ppid;
    return 0;
}

int process_has_own_root(pid_t pid)
{
    static char root_path[sizeof("/proc/%lu/root") + sizeof(long)*3];
    sprintf(root_path, "/proc/%lu/root", (long)pid);

    struct stat root_buf;
    if (stat("/", &root_buf) < 0)
    {
        perror_msg("Failed to get stat for '/'");
        return -1;
    }

    struct stat proc_buf;
    if (stat(root_path, &proc_buf) < 0)
    {
        perror_msg("Failed to get stat for '%s'", root_path);
        return -2;
    }

    return proc_buf.st_ino != root_buf.st_ino;
}
