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

/*
 * Utility routines.
 *
 */
#include "internal_libreport.h"

#define CONFIG_FEATURE_COPYBUF_KB 4

static const char msg_write_error[] = "write error";
static const char msg_read_error[] = "read error";

static off_t full_fd_action(int src_fd, int dst_fd, off_t size, int flags)
{
	int status = -1;
	off_t total = 0;
	int last_was_seek = 0;
#if CONFIG_FEATURE_COPYBUF_KB <= 4
	char buffer[CONFIG_FEATURE_COPYBUF_KB * 1024];
	enum { buffer_size = sizeof(buffer) };
#else
	char *buffer;
	int buffer_size;

	/* We want page-aligned buffer, just in case kernel is clever
	 * and can do page-aligned io more efficiently */
	buffer = mmap(NULL, CONFIG_FEATURE_COPYBUF_KB * 1024,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANON,
			/* ignored: */ -1, 0);
	buffer_size = CONFIG_FEATURE_COPYBUF_KB * 1024;
	if (buffer == MAP_FAILED) {
		buffer = alloca(4 * 1024);
		buffer_size = 4 * 1024;
	}
#endif

	if (src_fd < 0)
		goto out;

	if (!size) {
		size = buffer_size;
		status = 1; /* copy until eof */
	}

	while (1) {
		ssize_t rd, towrite;

		rd = safe_read(src_fd, buffer, buffer_size);

		if (!rd) { /* eof - all done */
			if (last_was_seek) {
				if (lseek(dst_fd, -1, SEEK_CUR) < 0
				 || safe_write(dst_fd, "", 1) != 1
				) {
					perror_msg("%s", msg_write_error);
					break;
				}
			}
			status = 0;
			break;
		}
		if (rd < 0) {
			perror_msg("%s", msg_read_error);
			break;
		}
		/* Add read Bytes before quiting the loop, because the caller
		 * needs to be able to detect overflows (the return value > size). */
		total += rd;
		towrite = rd > size ? size : rd;
		if (towrite == 0) {
			/* no more Bytes to write - all done */
			status = 0;
			break;
		}
		/* dst_fd == -1 is a fake, else... */
		if (dst_fd >= 0) {
			if (flags & COPYFD_SPARSE) {
				ssize_t cnt = towrite;
				while (--cnt >= 0)
					if (buffer[cnt] != 0)
						goto need2write;
				if (lseek(dst_fd, towrite, SEEK_CUR) < 0) {
					flags &= ~COPYFD_SPARSE;
					goto need2write;
				}
				last_was_seek = 1;
			} else {
 need2write:
				{
				    ssize_t wr = full_write(dst_fd, buffer, towrite);
				    if (wr < towrite) {
				        perror_msg("%s", msg_write_error);
				        break;
				    }
				    last_was_seek = 0;
				}
			}
		}
		if (status < 0) { /* if we aren't copying till EOF... */
			size -= towrite;
		}
	}
 out:

#if CONFIG_FEATURE_COPYBUF_KB > 4
	if (buffer_size != 4 * 1024)
		munmap(buffer, buffer_size);
#endif
	return status ? -1 : total;
}

off_t copyfd_ext_at(int src, int dir_fd, const char *name, int mode, uid_t uid, gid_t gid, int open_flags, int copy_flags, off_t size)
{
    int dst = openat(dir_fd, name, open_flags, mode);
    if (dst < 0)
    {
        perror_msg("Can't open '%s'", name);
        return -1;
    }
    off_t r = full_fd_action(src, dst, size, copy_flags);
    if (uid != (uid_t)-1L)
    {
        if (fchown(dst, uid, gid) == -1)
        {
            r = -2;
            perror_msg("Can't change ownership of '%s' to %lu:%lu", name, (long)uid, (long)gid);
        }
    }
    close(dst);
    return r;
}

off_t copyfd_size(int fd1, int fd2, off_t size, int flags)
{
	if (size) {
		off_t read = full_fd_action(fd1, fd2, size, flags);
		/* full_fd_action() writes only up to the size Bytes but returns the
		 * number of read Bytes. Callers of this function expect
		 * the return value not being greater then the size argument. */
		return read > size ? size : read;
	}
	return 0;
}

void copyfd_exact_size(int fd1, int fd2, off_t size)
{
	off_t sz = copyfd_size(fd1, fd2, size, /*flags:*/ 0);
	if (sz == size)
		return;
	if (sz != -1)
		error_msg_and_die("short read");
	/* if sz == -1, copyfd_XX already complained */
	xfunc_die();
}

off_t copyfd_eof(int fd1, int fd2, int flags)
{
	return full_fd_action(fd1, fd2, 0, flags);
}

off_t copy_file_ext_at(const char *src_name, int dir_fd, const char *name, int mode, uid_t uid, gid_t gid, int src_flags, int dst_flags)
{
    off_t r;
    int src = open(src_name, src_flags);
    if (src < 0)
    {
        perror_msg("Can't open '%s'", src_name);
        return -1;
    }

    r = copyfd_ext_at(src, dir_fd, name, mode, uid, gid, dst_flags,
            /*copy flags*/0,
            /*read all data*/0);

    close(src);
    return r;
}

off_t copy_file_at(const char *src_name, int dir_fd, const char *name, int mode)
{
    return copy_file_ext_at(src_name, dir_fd, name, mode, -1, -1,
            O_RDONLY, O_WRONLY | O_TRUNC | O_CREAT);
}

off_t copy_file(const char *src_name, const char *dst_name, int mode)
{
    return copy_file_ext(src_name, dst_name, mode, -1, -1,
            O_RDONLY, O_WRONLY | O_TRUNC | O_CREAT);
}
