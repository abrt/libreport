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
#include <syslog.h>
#include "internal_libreport.h"

void (*g_custom_logger)(const char*);
const char *msg_prefix = "";
const char *msg_eol = "\n";
int logmode = LOGMODE_STDIO;
int xfunc_error_retval = EXIT_FAILURE;
int g_verbose;

void set_xfunc_error_retval(int retval)
{
    xfunc_error_retval = retval;
}

/* [p]error_msg[_and_die] must be safe after fork in multi-threaded programs.
 * Therefore we avoid stdio, fflush(), and use _exit() instead of exit().
 *
 */
void xfunc_die(void)
{
    _exit(xfunc_error_retval);
}

/* If set to 0, will use malloc for long messages */
#define USE_ALLOCA 1

static void verror_msg_helper(const char *s,
                              va_list p,
                              const char *strerr,
                              int flags)
{
    if (!logmode)
        return;

    /* This is ugly and costs +60 bytes compared to multiple
     * fprintf's, but is guaranteed to do a single write.
     * This is needed for e.g. when multiple children
     * can produce log messages simultaneously. */

    int prefix_len = msg_prefix[0] ? strlen(msg_prefix) + 2 : 0;
    int strerr_len = strerr ? strlen(strerr) : 0;
    int msgeol_len = strlen(msg_eol);
    int used;

    /* Try to format the message in a fixed buffer.
     * This eliminates malloc.
     * Main reason isn't the speed optimization, but to make
     * short logging safe after fork in multithreaded libraries.
     */
    char buf[1024];
    va_list p2;
    va_copy(p2, p);
    if (prefix_len < sizeof(buf))
        used = vsnprintf(buf + prefix_len, sizeof(buf) - prefix_len, s, p2);
    else
        used = vsnprintf(buf, 0, s, p2);
    va_end(p2);

    char *msg = buf;

    /* +3 is for ": " before strerr and for terminating NUL */
    unsigned total_len = prefix_len + used + strerr_len + msgeol_len + 3;

#if USE_ALLOCA
    if (total_len >= sizeof(buf))
    {
        msg = alloca(total_len);
        used = vsnprintf(msg + prefix_len, total_len - prefix_len, s, p);
    }
#else
#define LOGMODE_DIE ((unsigned)INT_MAX + 1)
    char *malloced = NULL;
    if (total_len >= sizeof(buf))
    {
        /* Nope, need to malloc the buffer.
         * Can't use xmalloc: it calls error_msg_and_die on failure,
         * that will result in a recursion.
         */
        msg = malloced = malloc(total_len);
        if (!msg)
        {
            /* Same as xmalloc error */
            msg = strcpy(buf, "Out of memory, exiting\n");
            used = strlen(msg) - 1;
            msgeol_len = 1; /* '\n' */
            prefix_len = 0;
            flags |= LOGMODE_DIE;
            goto send_it;
        }
        used = vsnprintf(msg + prefix_len, total_len - prefix_len, s, p);
    }
#endif

    if (prefix_len) {
        char *p;
        used += prefix_len;
        p = stpcpy(msg, msg_prefix);
        p[0] = ':';
        p[1] = ' ';
    }
    if (strerr) {
        if (s[0]) {
            msg[used++] = ':';
            msg[used++] = ' ';
        }
        strcpy(&msg[used], strerr);
        used += strerr_len;
    }
    strcpy(&msg[used], msg_eol);

#if !USE_ALLOCA
 send_it:
#endif
    if (flags & LOGMODE_STDIO) {
        /*fflush(stdout); - unsafe after fork! */
        full_write(STDERR_FILENO, msg, used + msgeol_len);
    }
    msg[used] = '\0'; /* remove msg_eol (usually "\n") */
    if (flags & LOGMODE_SYSLOG) {
        syslog(LOG_ERR, "%s", msg + prefix_len);
    }
    if ((flags & LOGMODE_CUSTOM) && g_custom_logger) {
        g_custom_logger(msg + prefix_len);
    }

#if !USE_ALLOCA
    free(malloced);

    if (flags & LOGMODE_DIE)
        xfunc_die();
#endif
}

void log_msg(const char *s, ...)
{
    va_list p;

    va_start(p, s);
    verror_msg_helper(s, p, NULL, logmode);
    va_end(p);
}

void error_msg(const char *s, ...)
{
    va_list p;

    va_start(p, s);
    verror_msg_helper(s, p, NULL, (logmode | LOGMODE_CUSTOM));
    va_end(p);
}

void error_msg_and_die(const char *s, ...)
{
    va_list p;

    va_start(p, s);
    verror_msg_helper(s, p, NULL, (logmode | LOGMODE_CUSTOM));
    va_end(p);
    xfunc_die();
}

void perror_msg(const char *s, ...)
{
    va_list p;

    va_start(p, s);
    /* Guard against "<error message>: Success" */
    verror_msg_helper(s, p, errno ? strerror(errno) : NULL, (logmode | LOGMODE_CUSTOM));
    va_end(p);
}

void perror_msg_and_die(const char *s, ...)
{
    va_list p;

    va_start(p, s);
    /* Guard against "<error message>: Success" */
    verror_msg_helper(s, p, errno ? strerror(errno) : NULL, (logmode | LOGMODE_CUSTOM));
    va_end(p);
    xfunc_die();
}

void die_out_of_memory(void)
{
    error_msg_and_die("Out of memory, exiting");
}
