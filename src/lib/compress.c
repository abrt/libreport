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

#if HAVE_LZMA
# include <lzma.h>
#else
# define LR_DECOMPRESS_FORK_EXECVP
#endif

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


#ifdef LR_DECOMPRESS_FORK_EXECVP
static int
decompress_using_fork_execvp(const char** cmd, int fdi, int fdo)
{
    pid_t child = fork();
    if (child < 0)
    {
        VERB1 perror_msg("fork() for decompression");
        return -1;
    }

    if (child == 0)
    {
        close(STDIN_FILENO);
        if (dup2(fdi, STDIN_FILENO) < 0)
        {
            VERB1 perror_msg("Decompression failed: dup2(fdi, STDIN_FILENO)");
            exit(EXIT_FAILURE);
        }

        close(STDOUT_FILENO);
        if (dup2(fdo, STDOUT_FILENO) < 0)
        {
            VERB1 perror_msg("Decompression failed: dup2(fdo, STDOUT_FILENO)");
            exit(EXIT_FAILURE);
        }

        execvp(cmd[0], (char **)cmd);

        VERB1 perror_msg("Decompression failed: execlp('%s')", cmd[0]);
        exit(EXIT_FAILURE);
    }

    int status = 0;
    int r = safe_waitpid(child, &status, 0);
    if (r < 0)
    {
        VERB1 perror_msg("Decompression failed: waitpid($1) failed");
        return -2;
    }

    if (!WIFEXITED(status))
    {
        log_info("Decompression process returned abnormally");
        return -3;
    }

    if (WEXITSTATUS(status) != 0)
    {
        log_info("Decompression process exited with %d", WEXITSTATUS(r));
        return -4;
    }

    return 0;
}
#endif

static int
decompress_fd_xz(int fdi, int fdo)
{
#if HAVE_LZMA
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
#else /*HAVE_LZMA*/
    const char *cmd[] = { "xzcat", "-d", "-", NULL };
    return decompress_using_fork_execvp(cmd, fdi, fdo);
#endif /*HAVE_LZMA*/
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
decompress_file_ext_at(const char *path_in, int dir_fd, const char *path_out, mode_t mode_out,
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

    int ret = decompress_fd(fdi, fdo);
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

int decompress_file(const char *path_in, const char *path_out, mode_t mode_out)
{
    return decompress_file_ext_at(path_in, AT_FDCWD, path_out, mode_out, -1, -1,
            O_RDONLY, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC);
}
