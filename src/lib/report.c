/*
    Copyright (C) 2011  ABRT Team
    Copyright (C) 2011  RedHat inc.

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
#include "report.h"
#include "internal_libreport.h"

int report_problem_in_dir(const char *dirname, int flags)
{
    /* Prepare it before fork, to avoid thread-unsafe setenv there */
    char *prgname = (char*) g_get_prgname();
    if (prgname)
        prgname = xasprintf("LIBREPORT_PRGNAME=%s", prgname);

    fflush(NULL);

    pid_t pid = fork();
    if (pid < 0) /* error */
    {
        perror_msg("fork");
        return -1;
    }

    if (pid == 0) /* child */
    {
        const char *event_name;
        const char *path, *path1, *path2;
        char *args[7], **pp;

        /* Graphical tool */
        event_name = "report-gui";
        path1 = BIN_DIR"/report-gtk";
        path2 = "report-gtk";
        pp = args;
        *pp++ = (char *)"report-gtk";
        if (flags & LIBREPORT_DEL_DIR)
            *pp++ = (char *)"--delete";
        *pp++ = (char *)"--";
        *pp++ = (char *)dirname;
        *pp = NULL;

        if (prgname)
            putenv(prgname);

        if (flags & LIBREPORT_RUN_NEWT)
        {
            /* we want to run newt first */
            event_name = "report-cli";
            path1 = BIN_DIR"/report-newt";
            path2 = "report-newt";
            pp = args;
            *pp++ = (char *)"report-newt";
            if (flags & LIBREPORT_DEL_DIR)
                *pp++ = (char *)"--delete";
            *pp++ = (char *)"--";
            *pp++ = (char *)dirname;
            *pp = NULL;
        }
        else if (!getenv("DISPLAY") || (flags & LIBREPORT_RUN_CLI))
        {
            /* GUI won't work, use command line tool instead */
            event_name = "report-cli";
            path1 = BIN_DIR"/report-cli";
            path2 = "report-cli";
            pp = args;
            *pp++ = (char *)"report-cli";
            if (flags & LIBREPORT_DEL_DIR)
                *pp++ = (char *)"--delete";
            *pp++ = (char *)"-e";
            *pp++ = (char *)"report-cli";
            *pp++ = (char *)"--";
            *pp++ = (char *)dirname;
            *pp = NULL;
        }

        /* Some callers set SIGCHLD to SIG_IGN.
         * However, reporting spawns child processes.
         * Suppressing child death notification terribly confuses some of them.
         * Just in case, undo it.
         * Note that we do it in the child, so the parent is never affected.
         */
        signal(SIGCHLD, SIG_DFL);

        if (!(flags & (LIBREPORT_WAIT | LIBREPORT_GETPID)))
        {
            /* Caller doesn't want to wait for completion (!LIBREPORT_WAIT),
             * and doesn't want to have pid returned (!LIBREPORT_GETPID).
             * Create a grandchild, and then exit.
             * This reparents grandchild to init, and makes waitpid
             * in parent detect our exit and return almost immediately.
             */
            pid_t pid = fork();
            if (pid < 0) /* error */
                perror_msg_and_die("fork");
            if (pid != 0) /* not grandchild */
            {
                /* And now we exit: */
                _exit(0);
            }
            /* There's an alternative approach to achieve this,
             * instead of using --delete.
             * We can create yet another intermediate process which
             * waits for reporting child to finish, and then
             * removes temporary dump dir.
             * Pros: deletion becomes more robust.
             * Even if child crashes, dir will be deleted.
             * Cons: having another process would use some resources,
             * and we'll need to at least close all open file descriptors,
             * and reopen stdio to /dev/null. We also might keep
             * a lot of libraries loaded:
             * who knows what parent process links against.
             * (can be worked around by exec'ing a "wait & delete" helper)
             */
        }

        struct run_event_state *run_state = new_run_event_state();
        int r = run_event_on_dir_name(run_state, dirname, event_name);
        int no_such_event = (r == 0 && run_state->children_count == 0);
        free_run_event_state(run_state);
        if (!no_such_event)
        {
            if (flags & LIBREPORT_DEL_DIR)
            {
                struct dump_dir *dd = dd_opendir(dirname, 0);
                if (dd)
                    dd_delete(dd);
            }
            _exit(r);
        }
        /* No "report-cli/gui" event found, do it old-style */

        path = path1;
        log_info("Executing: %s", path);
        execv(path, args);
        /* Did not find the desired executable in the installation directory.
         * Trying to find it in PATH.
         */
        path = path2;
        execvp(path, args);
        perror_msg_and_die("Can't execute %s", path);
    }

    /* parent */
    free(prgname);

    if (!(flags & LIBREPORT_WAIT) && (flags & LIBREPORT_GETPID))
        return pid;

    /* we are here either if LIBREPORT_WAIT (caller wants exitcode)
     * or !LIBREPORT_GETPID (caller doesn't want to have a child).
     * In both cases, we need to wait for child:
     */
    int status;
    pid = safe_waitpid(pid, &status, 0);
    if (pid <= 0)
    {
        perror_msg("waitpid");
        return -1;
    }

    if (WIFEXITED(status))
    {
        log_info("reporting finished with exitcode %d", WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }
    /* child died from a signal: WIFSIGNALED(status) should be true */
    log_info("reporting killed by signal %d", WTERMSIG(status));
    return WTERMSIG(status) + 128;
}

int report_problem_in_memory(problem_data_t *pd, int flags)
{
    int result = 0;
    struct dump_dir *dd = create_dump_dir_from_problem_data(pd, LARGE_DATA_TMP_DIR);
    if (!dd)
        return -1;
    char *dir_name = xstrdup(dd->dd_dirname);
    dd_close(dd);
    log_info("Temp problem dir: '%s'", dir_name);

    if (!(flags & LIBREPORT_WAIT))
        flags |= LIBREPORT_DEL_DIR;
    result = report_problem_in_dir(dir_name, flags);

    /* If we waited for reporter to finish, we should clean up the tmp dir
     * (if we didn't, cleaning up will be done by reporting child process later).
     * We can also reload the problem data if requested.
     */
    if (flags & LIBREPORT_WAIT)
    {
        if (flags & LIBREPORT_RELOAD_DATA)
            g_hash_table_remove_all(pd);
        dd = dd_opendir(dir_name, 0);
        if (dd)
        {
            if (flags & LIBREPORT_RELOAD_DATA)
                problem_data_load_from_dump_dir(pd, dd, NULL);
            dd_delete(dd);
        }
    }

    free(dir_name);
    return result;
}

int report_problem(problem_data_t *pd)
{
    return report_problem_in_memory(pd, LIBREPORT_NOWAIT);
}
