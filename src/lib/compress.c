/*
    Copyright (C) 2015  ABRT team
    Copyright (C) 2015  RedHat Inc

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

#include <archive.h>

int libreport_decompress_fd(int fdi, int fdo)
{
#define kiB * 1024
#define BUFFER_SIZE 16 kiB
    int retval;
    struct archive *archive;
    struct archive_entry *entry;
    int r;

    retval = 0;
    archive = archive_read_new();

    archive_read_support_filter_all(archive);
    archive_read_support_format_raw(archive);

    r = archive_read_open_fd(archive, fdi, BUFFER_SIZE);
    if (r != ARCHIVE_OK)
    {
        const char *error_string;

        error_string = archive_error_string(archive);

        log_error("Reading archive failed: %s", error_string);

        retval = -1;

        goto cleanup;
    }
    r = archive_read_next_header(archive, &entry);
    if (r != ARCHIVE_OK)
    {
        const char *error_string;

        error_string = archive_error_string(archive);

        log_error("Reading archive header failed: %s", error_string);

        retval = -1;

        goto cleanup;
    }

    for (; ; )
    {
        uint8_t buffer[BUFFER_SIZE] = { 0 };
        ssize_t size;

        size = archive_read_data(archive, buffer, sizeof(buffer));
        if (size < 0)
        {
            const char *error_string;

            error_string = archive_error_string(archive);

            log_error("Reading compressed data failed: %s", error_string);

            retval = -1;

            break;
        }
        if (size == 0)
        {
            break;
        }

        if (libreport_safe_write(fdo, buffer, size) < 0)
        {
            retval = -1;

            log_error("Failed to write out decompressed data");

            break;
        }
    }

cleanup:
    archive_read_free(archive);

    return retval;
}

int libreport_decompress_file_ext_at(const char *path_in, int dir_fd, const char *path_out, mode_t mode_out,
                       uid_t uid, gid_t gid, int src_flags, int dst_flags)
{
    int fdi = open(path_in, src_flags);
    if (fdi < 0)
    {
        perror_msg("Could not open file: %s", path_in);
        return -1;
    }

    int fdo = openat(dir_fd, path_out, dst_flags, mode_out);
    if (fdo < 0)
    {
        close(fdi);
        perror_msg("Could not create file: %s", path_out);
        return -1;
    }

    int ret = libreport_decompress_fd(fdi, fdo);
    close(fdi);
    if (uid != (uid_t)-1L)
    {
        if (fchown(fdo, uid, gid) == -1)
        {
            perror_msg("Can't change ownership of '%s' to %lu:%lu", path_out, (long)uid, (long)gid);
            ret = -1;
        }
    }
    close(fdo);

    if (ret != 0)
        unlinkat(dir_fd, path_out, /*only files*/0);

    return ret;
}

int libreport_decompress_file(const char *path_in, const char *path_out, mode_t mode_out)
{
    return libreport_decompress_file_ext_at(path_in, AT_FDCWD, path_out, mode_out, -1, -1,
            O_RDONLY, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC);
}
