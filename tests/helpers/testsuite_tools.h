/*
    Copyright (C) 2015  ABRT team <crash-catcher@lists.fedorahosted.org>
    Copyright (C) 2015  RedHat inc.

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

    ----

    Helper functions
*/

#include "testsuite.h"

/* Creates a new dump directory in a new temporary directory
 *
 * @param uid Owner's uid
 * @param mode Dump dir mode or -1 for the sane default value 0640.
 * @param ts_falgs Unused
 */
static struct dump_dir *testsuite_dump_dir_create(uid_t uid, mode_t mode, int ts_flags)
{
    char dump_dir_name[] = "/tmp/XXXXXX/dump_dir";

    char *last_slash = strrchr(dump_dir_name, '/');
    *last_slash = '\0';

    if (mkdtemp(dump_dir_name) == NULL) {
        perror("mkdtemp()");
        abort();
    }

    fprintf(stdout, "Test temp directory: %s\n", dump_dir_name);
    fflush(stdout);

    *last_slash = '/';

    struct dump_dir *dd = dd_create(dump_dir_name, uid, mode == (mode_t)-1 ? 0640 : mode);
    assert(dd != NULL);

    return dd;
}

/* Removes the dump directory in and the temporary directory
 *
 * See testsuite_dump_dir_create()
 */
static void testsuite_dump_dir_delete(struct dump_dir *dd)
{
    char *tmp_dir = xstrndup(dd->dd_dirname, strrchr(dd->dd_dirname, '/') - dd->dd_dirname);
    assert(dd_delete(dd) == 0);

    if(rmdir(tmp_dir) != 0)
    {
        perror("rmdir()");
        abort();
    }

    free(tmp_dir);
}
