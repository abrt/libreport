/*
    Copyright (C) 2009  Jiri Moskovcak (jmoskovc@redhat.com)
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

double libreport_get_dirsize(const char *pPath)
{
    DIR *dp = opendir(pPath);
    if (dp == NULL)
        return 0;

    struct dirent *ep;
    struct stat statbuf;
    double size = 0;
    while ((ep = readdir(dp)) != NULL)
    {
        if (libreport_dot_or_dotdot(ep->d_name))
            continue;
        char *dname = libreport_concat_path_file(pPath, ep->d_name);
        if (lstat(dname, &statbuf) != 0)
        {
            goto next;
        }
        if (S_ISDIR(statbuf.st_mode))
        {
            size += libreport_get_dirsize(dname);
        }
        else if (S_ISREG(statbuf.st_mode))
        {
            size += statbuf.st_size;
        }
 next:
        free(dname);
    }
    closedir(dp);
    return size;
}

static bool this_is_a_dd(const char *dirname)
{
    /* Prevent libreport_get_dirsize_find_largest_dir() from flooding log
     * with "is not a problem directory" messages
     * if there are stray dirs in /var/spool/abrt:
     */
    int sv_logmode = libreport_logmode;
    libreport_logmode = 0;

    struct dump_dir *dd = dd_opendir(dirname,
                /*flags:*/ DD_OPEN_READONLY | DD_FAIL_QUIETLY_ENOENT | DD_FAIL_QUIETLY_EACCES
    );
    dd_close(dd);

    libreport_logmode = sv_logmode;
    return dd != NULL;
}

double libreport_get_dirsize_find_largest_dir(
        const char *pPath,
        char **worst_dir,
        const char *excluded)
{
    if (worst_dir)
        *worst_dir = NULL;

    DIR *dp = opendir(pPath);
    if (dp == NULL)
        return 0;

    time_t cur_time = time(NULL);
    struct dirent *ep;
    struct stat statbuf;
    double size = 0;
    double maxsz = 0;
    while ((ep = readdir(dp)) != NULL)
    {
        if (libreport_dot_or_dotdot(ep->d_name))
            continue;
        char *dname = libreport_concat_path_file(pPath, ep->d_name);
        if (lstat(libreport_concat_path_file(dname, "sosreport.log"), &statbuf) == 0)
        {
            log_debug("Skipping %s': sosreport is being generated.", dname);
            goto next;
        }
        if (lstat(libreport_concat_path_file(dname, ".lock"), &statbuf) == 0)
        {
            log_warning("Skipping %s: directory locked. Is a backtrace being generated?", dname);
            size += libreport_get_dirsize(dname);
            goto next;
        }
        if (lstat(dname, &statbuf) != 0)
        {
            goto next;
        }
        if (S_ISDIR(statbuf.st_mode))
        {
            double sz = libreport_get_dirsize(dname);
            size += sz;

            if (worst_dir && (!excluded || strcmp(excluded, ep->d_name) != 0))
            {
                /* Calculate "weighted" size and age
                 * w = sz_kbytes * age_mins
                 */
                sz /= 1024;
                long age = (cur_time - statbuf.st_mtime) / 60;
                if (age > 0)
                    sz *= age;

                if (sz > maxsz)
                {
                    if (!this_is_a_dd(dname))
                    {
                        log_notice("'%s' isn't a problem directory, probably a stray directory?", dname);
                    }
                    else
                    {
                        maxsz = sz;
                        free(*worst_dir);
                        *worst_dir = libreport_xstrdup(ep->d_name);
                    }
                }
            }
        }
        else if (S_ISREG(statbuf.st_mode))
        {
            size += statbuf.st_size;
        }
 next:
        free(dname);
    }
    closedir(dp);
    return size;
}
