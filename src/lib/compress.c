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

#include <lzma.h>

static const uint8_t s_xz_magic[6] = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };

static bool
is_format(const char *name, const uint8_t *header, size_t hl, const uint8_t *magic, size_t ml)
{
    if (hl < ml)
    {
        log_warning("Too short header to detect '%s' file format.", name);
        return false;
    }

    return memcmp(header, magic, ml) == 0;
}

static int
decompress_fd_xz(int fdi, int fdo)
{
    uint8_t buf_in[BUFSIZ];
    uint8_t buf_out[BUFSIZ];

    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, 0);
    if (ret != LZMA_OK)
    {
        close(fdi);
        close(fdo);
        log_error("Failed to initialize XZ decoder: code %d", ret);
        return -ENOMEM;
    }

    lzma_action action = LZMA_RUN;

    strm.next_out = buf_out;
    strm.avail_out = sizeof(buf_out);

    for (;;)
    {
        if (strm.avail_in == 0 && action == LZMA_RUN)
        {
            strm.next_in = buf_in;
            strm.avail_in = safe_read(fdi, buf_in, sizeof(buf_in));

            if (strm.avail_in < 0)
            {
                perror_msg("Failed to read source core file");
                close(fdi);
                close(fdo);
                lzma_end(&strm);
                return -1;
            }

            if (strm.avail_in == 0)
                action = LZMA_FINISH;
        }

        ret = lzma_code(&strm, action);

        if (strm.avail_out == 0 || ret == LZMA_STREAM_END)
        {
            const ssize_t n = sizeof(buf_out) - strm.avail_out;
            if (n != safe_write(fdo, buf_out, n))
            {
                perror_msg("Failed to write decompressed data");
                close(fdi);
                close(fdo);
                lzma_end(&strm);
                return -1;
            }

            if (ret == LZMA_STREAM_END)
            {
                log_debug("Successfully decompressed coredump.");
                break;
            }

            strm.next_out = buf_out;
            strm.avail_out = sizeof(buf_out);
        }
    }

    return 0;
}

int
decompress_fd(int fdi, int fdo)
{
    uint8_t header[6];

    if (sizeof(header) != safe_read(fdi, header, sizeof(header)))
    {
        perror_msg("Failed to read header bytes");
        return -1;
    }

    xlseek(fdi, 0, SEEK_SET);

    if (is_format("xz", header, sizeof(header), s_xz_magic, sizeof(s_xz_magic)))
        return decompress_fd_xz(fdi, fdo);

    error_msg("Unsupported file format");
    return -1;
}

int
decompress_file(const char *path_in, const char *path_out, mode_t mode_out)
{
    int fdi = open(path_in, O_RDONLY | O_CLOEXEC);
    if (fdi < 0)
    {
        perror_msg("Could not open file: %s", path_in);
        return -1;
    }

    int fdo = open(path_out, O_WRONLY | O_CLOEXEC | O_EXCL | O_CREAT, mode_out);
    if (fdo < 0)
    {
        close(fdi);
        perror_msg("Could not create file: %s", path_out);
        return -1;
    }

    int ret = decompress_fd(fdi, fdo);
    close(fdi);
    close(fdo);

    if (ret != 0)
        unlink(path_out);

    return ret;
}
