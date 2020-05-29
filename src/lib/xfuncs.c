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
#include "internal_libreport.h"

/* Turn on nonblocking I/O on a fd */
int libreport_ndelay_on(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags & O_NONBLOCK)
        return 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int libreport_ndelay_off(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (!(flags & O_NONBLOCK))
        return 0;
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

int libreport_close_on_exec_on(int fd)
{
    return fcntl(fd, F_SETFD, FD_CLOEXEC);
}

char *libreport_xstrdup_between(const char *src, const char *open, const char *close)
{
    const char *start = strstr(src, open);
    if (start == NULL)
    {
        log_debug("Open tag not found: '%s'", open);
        return NULL;
    }

    start += strlen(open);

    const char *stop = strstr(start, close);
    if (stop == NULL)
    {
        log_debug("Close tag not found: '%s'", close);
        return NULL;
    }

    return g_strndup(start, stop - start);
}

int libreport_xdup(int from)
{
    int fd = dup(from);
    if (fd < 0)
        perror_msg_and_die("Can't duplicate file descriptor");
    return fd;
}

void libreport_xdup2(int from, int to)
{
    if (dup2(from, to) != to)
        perror_msg_and_die("Can't duplicate file descriptor");
}

// "Renumber" opened fd
void libreport_xmove_fd(int from, int to)
{
    if (from == to)
        return;
    libreport_xdup2(from, to);
    close(from);
}

// Die with an error message if we can't write the entire buffer.
void libreport_xwrite(int fd, const void *buf, size_t count)
{
    if (count == 0)
        return;
    ssize_t size = libreport_full_write(fd, buf, count);
    if ((size_t)size != count)
        error_msg_and_die("short write");
}

void libreport_xwrite_str(int fd, const char *str)
{
    libreport_xwrite(fd, str, strlen(str));
}

// Die with an error message if we can't lseek to the right spot.
off_t libreport_xlseek(int fd, off_t offset, int whence)
{
    off_t off = lseek(fd, offset, whence);
    if (off == (off_t)-1) {
        if (whence == SEEK_SET)
            perror_msg_and_die("lseek(%llu)", (long long)offset);
        perror_msg_and_die("lseek");
    }
    return off;
}

char* libreport_xvasprintf(const char *format, va_list p)
{
    int r;
    char *string_ptr;

#if 1
    // GNU extension
    r = vasprintf(&string_ptr, format, p);
#else
    // Bloat for systems that haven't got the GNU extension.
    va_list p2;
    va_copy(p2, p);
    r = vsnprintf(NULL, 0, format, p);
    string_ptr = g_malloc(r+1);
    r = vsnprintf(string_ptr, r+1, format, p2);
    va_end(p2);
#endif

    if (r < 0)
        libreport_die_out_of_memory();
    return string_ptr;
}

void libreport_xsetenv(const char *key, const char *value)
{
    if (setenv(key, value, 1))
        libreport_die_out_of_memory();
}

void libreport_safe_unsetenv(const char *var_val)
{
    //char *name = g_strndup(var_val, strchrnul(var_val, '=') - var_val);
    //unsetenv(name);
    //free(name);

    /* Avoid malloc/free (name is usually very short) */
    unsigned len = strchrnul(var_val, '=') - var_val;
    char name[len + 1];
    memcpy(name, var_val, len);
    name[len] = '\0';
    unsetenv(name);
}

// Die with an error message if we can't open a new socket.
int libreport_xsocket(int domain, int type, int protocol)
{
    int r = socket(domain, type, protocol);
    if (r < 0)
    {
        const char *s = "INET";
        if (domain == AF_PACKET) s = "PACKET";
        if (domain == AF_NETLINK) s = "NETLINK";
        if (domain == AF_INET6) s = "INET6";
        perror_msg_and_die("socket(AF_%s)", s);
    }

    return r;
}

// Die with an error message if we can't bind a socket to an address.
void libreport_xbind(int sockfd, struct sockaddr *my_addr, socklen_t addrlen)
{
    if (bind(sockfd, my_addr, addrlen))
        perror_msg_and_die("bind");
}

// Die with an error message if we can't listen for connections on a socket.
void libreport_xlisten(int s, int backlog)
{
    if (listen(s, backlog))
        perror_msg_and_die("listen");
}

// Die with an error message if sendto failed.
// Return bytes sent otherwise
ssize_t libreport_xsendto(int s, const void *buf, size_t len,
                const struct sockaddr *to,
                socklen_t tolen)
{
    ssize_t ret = sendto(s, buf, len, 0, to, tolen);
    if (ret < 0)
    {
        close(s);
        perror_msg_and_die("sendto");
    }
    return ret;
}

// libreport_xstat() - a stat() which dies on failure with meaningful error message
void libreport_xstat(const char *name, struct stat *stat_buf)
{
    if (stat(name, stat_buf))
        perror_msg_and_die("Can't stat '%s'", name);
}

off_t libreport_fstat_st_size_or_die(int fd)
{
    struct stat statbuf;
    if (fstat(fd, &statbuf))
        perror_msg_and_die("Can't stat");
    return statbuf.st_size;
}

off_t libreport_stat_st_size_or_die(const char *filename)
{
    struct stat statbuf;
    if (stat(filename, &statbuf))
        perror_msg_and_die("Can't stat '%s'", filename);
    return statbuf.st_size;
}

// Die if we can't open a file and return a fd
int libreport_xopen3(const char *pathname, int flags, int mode)
{
    int ret;
    ret = open(pathname, flags, mode);
    if (ret < 0)
        perror_msg_and_die("Can't open '%s'", pathname);
    return ret;
}

void libreport_xunlinkat(int dir_fd, const char *pathname, int flags)
{
    if (unlinkat(dir_fd, pathname, flags))
        perror_msg_and_die("Can't remove file '%s'", pathname);
}

#if 0 //UNUSED
// Warn if we can't open a file and return a fd.
int open3_or_warn(const char *pathname, int flags, int mode)
{
    int ret;
    ret = open(pathname, flags, mode);
    if (ret < 0)
        perror_msg("Can't open '%s'", pathname);
    return ret;
}

// Warn if we can't open a file and return a fd.
int open_or_warn(const char *pathname, int flags)
{
    return open3_or_warn(pathname, flags, 0666);
}
#endif

/* Just testing dent->d_type == DT_REG is wrong: some filesystems
 * do not report the type, they report DT_UNKNOWN for every dirent
 * (and this is not a bug in filesystem, this is allowed by standards).
 */
int libreport_is_regular_file_at(struct dirent *dent, int dir_fd)
{
    if (dent->d_type == DT_REG)
        return 1;
    if (dent->d_type != DT_UNKNOWN)
        return 0;

    struct stat statbuf;
    int r = fstatat(dir_fd, dent->d_name, &statbuf, AT_SYMLINK_NOFOLLOW);

    return r == 0 && S_ISREG(statbuf.st_mode);
}

int libreport_is_regular_file(struct dirent *dent, const char *dirname)
{
    int dir_fd = open(dirname, O_DIRECTORY);
    if (dir_fd < 0)
        return 0;
    int r = libreport_is_regular_file_at(dent, dir_fd);
    close(dir_fd);
    return r;
}

/* Is it "." or ".."? */
/* abrtlib candidate */
bool libreport_dot_or_dotdot(const char *filename)
{
    if (filename[0] != '.') return false;
    if (filename[1] == '\0') return true;
    if (filename[1] != '.') return false;
    if (filename[2] == '\0') return true;
    return false;
}

/* Find out if the last character of a string matches the one given.
 * Don't underrun the buffer if the string length is 0.
 */
char *libreport_last_char_is(const char *s, int c)
{
    if (s && *s)
    {
        s += strlen(s) - 1;
        if ((unsigned char)*s == c)
            return (char*)s;
    }
    return NULL;
}

bool libreport_string_to_bool(const char *s)
{
    if (s[0] == '1' && s[1] == '\0')
        return true;
    if (strcasecmp(s, "on") == 0)
        return true;
    if (strcasecmp(s, "yes") == 0)
        return true;
    if (strcasecmp(s, "true") == 0)
        return true;
    return false;
}

void libreport_xseteuid(uid_t euid)
{
    if (seteuid(euid) != 0)
        perror_msg_and_die("Can't set %cid %lu", 'u', (long)euid);
}

void libreport_xsetegid(gid_t egid)
{
    if (setegid(egid) != 0)
        perror_msg_and_die("Can't set %cid %lu", 'g', (long)egid);
}

void libreport_xsetreuid(uid_t ruid, uid_t euid)
{
    if (setreuid(ruid, euid) != 0)
        perror_msg_and_die("Can't set %cid %lu", 'u', (long)ruid);
}

void libreport_xsetregid(gid_t rgid, gid_t egid)
{
    if (setregid(rgid, egid) != 0)
        perror_msg_and_die("Can't set %cid %lu", 'g', (long)rgid);
}

FILE *libreport_xfdopen(int fd, const char *mode)
{
    FILE *const r = fdopen(fd, mode);
    if (NULL == r)
        perror_msg_and_die("Can't open file descriptor %d as FILE", fd);
    return r;
}
