/*
 * Utility routines.
 *
 * Copyright (C) 2010  ABRT team
 * Copyright (C) 2010  RedHat Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "internal_libreport.h"

/* Die with an error message if we can't read the entire buffer. */
void xread(int fd, void *buf, size_t count)
{
    if (count)
    {
        ssize_t size = full_read(fd, buf, count);
        if ((size_t)size != count)
            error_msg_and_die("short read");
    }
}

ssize_t safe_read(int fd, void *buf, size_t count)
{
    ssize_t n;

    do {
        n = read(fd, buf, count);
    } while (n < 0 && errno == EINTR);

    return n;
}

ssize_t safe_write(int fd, const void *buf, size_t count)
{
    ssize_t n;

    do {
        n = write(fd, buf, count);
    } while (n < 0 && errno == EINTR);

    return n;
}

ssize_t full_read(int fd, void *buf, size_t len)
{
    ssize_t cc;
    ssize_t total;

    total = 0;

    while (len)
    {
        cc = safe_read(fd, buf, len);

        if (cc < 0)
        {
            if (total)
            {
                /* we already have some! */
                /* user can do another read to know the error code */
                return total;
            }
            return cc; /* read() returns -1 on failure. */
        }
        if (cc == 0)
            break;
        buf = ((char *)buf) + cc;
        total += cc;
        len -= cc;
    }

    return total;
}

ssize_t full_write(int fd, const void *buf, size_t len)
{
    ssize_t cc;
    ssize_t total;

    total = 0;

    while (len)
    {
        cc = safe_write(fd, buf, len);

        if (cc < 0)
        {
            if (total)
            {
                /* we already wrote some! */
                /* user can do another write to know the error code */
                return total;
            }
            return cc;  /* write() returns -1 on failure. */
        }

        total += cc;
        buf = ((const char *)buf) + cc;
        len -= cc;
    }

    return total;
}

ssize_t full_write_str(int fd, const char *buf)
{
    return full_write(fd, buf, strlen(buf));
}

/* Read (potentially big) files in one go. File size is estimated
 * by stat. Extra '\0' byte is appended.
 */
void* xmalloc_read(int fd, size_t *maxsz_p)
{
    char *buf;
    size_t size, rd_size, total;
    size_t to_read;

    to_read = maxsz_p ? *maxsz_p : (INT_MAX - 4095); /* max to read */

    /* Estimate file size */
    {
        struct stat st;
        st.st_size = 0; /* in case fstat fails, assume 0 */
        fstat(fd, &st);
        /* /proc/N/stat files report st_size 0 */
        /* In order to make such files readable, we add small const (4k) */
        size = (st.st_size | 0xfff) + 1;
    }

    total = 0;
    buf = NULL;
    while (1) {
        if (to_read < size)
            size = to_read;
        buf = xrealloc(buf, total + size + 1);
        rd_size = full_read(fd, buf + total, size);
        if ((ssize_t)rd_size == (ssize_t)(-1)) { /* error */
            free(buf);
            return NULL;
        }
        total += rd_size;
        if (rd_size < size) /* EOF */
            break;
        if (to_read <= rd_size)
            break;
        to_read -= rd_size;
        /* grow by 1/8, but in [1k..64k] bounds */
        size = ((total / 8) | 0x3ff) + 1;
        if (size > 64*1024)
            size = 64*1024;
    }
    buf = xrealloc(buf, total + 1);
    buf[total] = '\0';

    if (maxsz_p)
        *maxsz_p = total;
    return buf;
}

void* xmalloc_open_read_close(const char *filename, size_t *maxsz_p)
{
    char *buf;
    int fd;

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return NULL;

    buf = xmalloc_read(fd, maxsz_p);
    close(fd);
    return buf;
}

void* xmalloc_xopen_read_close(const char *filename, size_t *maxsz_p)
{
    void *buf = xmalloc_open_read_close(filename, maxsz_p);
    if (!buf)
        perror_msg_and_die("Can't read '%s'", filename);
    return buf;
}
