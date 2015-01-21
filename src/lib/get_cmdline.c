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

int dump_fd_info(const char *dest_filename, const char *proc_pid_fd_path)
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

    stream = fopen(dest_filename, "w");
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
    fclose(stream);

    if (r == 0 && errno != 0)
        r = -errno;

    closedir(proc_fd_dir);
    close(proc_fdinfo_fd);
    free(buffer);

    return r;
}
