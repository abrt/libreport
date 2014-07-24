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
#include <systemd/sd-journal.h>
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

static bool should_log(int level)
{
    // LOG_DEBUG = 7, LOG_INFO = 6, LOG_NOTICE = 5, LOG_WARNING = 4, LOG_ERR = 3
    // output only messages with LOG_ERR by default, overridden by g_verbose
    if(
          (g_verbose == 0 && level <= LOG_WARNING) ||
          (g_verbose == 1 && level <= LOG_NOTICE) ||
          (g_verbose == 2 && level <= LOG_INFO) ||
          (g_verbose == 3)
      )
      return true;

    return false;
}


static void log_handler(int level,
                        const char *format,
                        va_list p,
                        const char *strerr, /* perror messages */
                        int flags,
                        const char *file,
                        int line,
                        const char *func)
{
    if (!logmode || !should_log(level))
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
        used = vsnprintf(buf + prefix_len, sizeof(buf) - prefix_len, format, p2);
    else
        used = vsnprintf(buf, 0, format, p2);
    va_end(p2);

    char *msg = buf;

    /* +3 is for ": " before strerr and for terminating NUL */
    unsigned total_len = prefix_len + used + strerr_len + msgeol_len + 3;

    if (total_len >= sizeof(buf))
    {
        msg = alloca(total_len);
        used = vsnprintf(msg + prefix_len, total_len - prefix_len, format, p);
    }

    if (prefix_len) {
        char *p;
        used += prefix_len;
        p = stpcpy(msg, msg_prefix);
        p[0] = ':';
        p[1] = ' ';
    }
    if (strerr) {
        if (format[0]) {
            msg[used++] = ':';
            msg[used++] = ' ';
        }
        strcpy(&msg[used], strerr);
        used += strerr_len;
    }
    strcpy(&msg[used], msg_eol);

    if (flags & LOGMODE_STDIO) {
        full_write(STDERR_FILENO, msg, used + msgeol_len);
    }
    msg[used] = '\0'; /* remove msg_eol (usually "\n") */
    if (flags & LOGMODE_SYSLOG) {
        syslog(level, "%s", msg + prefix_len);
    }

    if ((flags & LOGMODE_CUSTOM) && g_custom_logger) {
        g_custom_logger(msg + prefix_len);
    }

    if (flags & LOGMODE_JOURNAL) {
        sd_journal_send("MESSAGE=%s", msg + prefix_len,
                        "PRIORITY=%d", level,
                        "CODE_FILE=%s", file,
                        "CODE_LINE=%d", line,
                        "CODE_FUNC=%s", func,
                        "SYSLOG_FACILITY=1",
                        NULL);
    }
}

void log_wrapper(int level,
                 const char *file,
                 int line,
                 const char *func,
                 bool process_perror,
                 bool use_custom_logger,
                 const char *format,
                 ...)
{
    va_list p;

    va_start(p, format);
    log_handler(level,
                format,
                p,
                (process_perror && errno) ? strerror(errno) : NULL, /* Guard against "<error message>: Success" */
                logmode | (use_custom_logger ? LOGMODE_CUSTOM : 0),
                file,
                line,
                func);
    va_end(p);
}

void log_and_die_wrapper(int level,
                         const char *file,
                         int line,
                         const char *func,
                         bool process_perror,
                         bool use_custom_logger,
                         const char *format,
                         ...)
{
    va_list p;

    va_start(p, format);
    log_handler(level,
                format,
                p,
                (process_perror && errno) ? strerror(errno) : NULL, /* Guard against "<error message>: Success" */
                logmode | (use_custom_logger ? LOGMODE_CUSTOM : 0),
                file,
                line,
                func);
    va_end(p);
    xfunc_die();
}


void die_out_of_memory(void)
{
    error_msg_and_die("Out of memory, exiting");
}
