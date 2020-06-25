#include <glib.h>
#include <internal_libreport.h>
#include <stdlib.h>
#include <unistd.h>

static void
test_decompression(const void *user_data)
{
    char template[] = "/tmp/libreport-test_decompression-XXXXXX";
    int in_fd;
    int out_fd;
    int result;

    in_fd = open(user_data, O_RDONLY);
    g_assert_cmpint(in_fd, !=, -1);

    out_fd = mkstemp(template);
    g_assert_cmpint(out_fd, !=, -1);

    result = libreport_decompress_fd(in_fd, out_fd);
    g_assert_cmpint(result, ==, 0);

    lseek(out_fd, 0, SEEK_SET);

    close(in_fd);

    in_fd = open("data/compressed-file", O_RDONLY);
    g_assert_cmpint(in_fd, !=, -1);

    for (; ; )
    {
        char in_buf[256] = { 0 };
        ssize_t in_read;
        char out_buf[256] = { 0 };
        ssize_t out_read;

        in_read = read(in_fd, in_buf, sizeof(in_buf));
        g_assert_cmpint(in_read, !=, -1);

        out_read = read(out_fd, out_buf, sizeof(out_buf));
        g_assert_cmpint(out_read, !=, -1);

        g_assert_cmpmem(in_buf, in_read, out_buf, out_read);

        if (0 == in_read || 0 == out_read)
        {
            break;
        }
    }

    close(in_fd);
    close(out_fd);

    unlink(template);
}

int
main(int    argc,
     char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_data_func("/decompression/lz4", "data/compressed-file.lz4", test_decompression);
    g_test_add_data_func("/decompression/lzma", "data/compressed-file.xz", test_decompression);

    return g_test_run();
}
