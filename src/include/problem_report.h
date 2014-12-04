/*
    Copyright (C) 2014  ABRT team
    Copyright (C) 2014  RedHat Inc

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
#ifndef LIBREPORT_PROBLEM_REPORT_H
#define LIBREPORT_PROBLEM_REPORT_H

#include <glib.h>
#include <stdio.h>
#include "problem_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PR_SEC_SUMMARY "summary"
#define PR_SEC_DESCRIPTION "description"

/*
 * The problem report structure represents a problem data formatted according
 * to a format string.
 *
 * A problem report is composed of well-known sections:
 *   - summary
 *   - descritpion
 *   - attach
 *
 * and custom sections accessed by:
 *   problem_report_get_section();
 */
struct problem_report;
typedef struct problem_report problem_report_t;

/*
 * Helpers for easily switching between FILE and struct strbuf
 */

/*
 * Type of buffer used by Problem report
 */
typedef FILE problem_report_buffer;

/*
 * Wrapper for the proble buffer's formated output function.
 */
#define problem_report_buffer_printf(buf, fmt, ...)\
    fprintf((buf), (fmt), ##__VA_ARGS__)


/*
 * Get a section buffer
 *
 * Use this function if you need to amend something to a formatted section.
 *
 * @param self Problem report
 * @param section_name Name of required section
 * @return Always valid pointer to a section buffer
 */
problem_report_buffer *problem_report_get_buffer(const problem_report_t *self,
        const char *section_name);

/*
 * Get Summary string
 *
 * The returned pointer is valid as long as you perform no further output to
 * the summary buffer.
 *
 * @param self Problem report
 * @return Non-NULL pointer to summary data
 */
const char *problem_report_get_summary(const problem_report_t *self);

/*
 * Get Description string
 *
 * The returned pointer is valid as long as you perform no further output to
 * the description buffer.
 *
 * @param self Problem report
 * @return Non-NULL pointer to description data
 */
const char *problem_report_get_description(const problem_report_t *self);

/*
 * Get Section's string
 *
 * The returned pointer is valid as long as you perform no further output to
 * the section's buffer.
 *
 * @param self Problem report
 * @param section_name Name of the required section
 * @return Non-NULL pointer to description data
 */
const char *problem_report_get_section(const problem_report_t *self,
        const char *section_name);

/*
 * Get GList of the problem data items that are to be attached
 *
 * @param self Problem report
 * @return A pointer to GList (NULL means empty list)
 */
GList *problem_report_get_attachments(const problem_report_t *self);

/*
 * Releases all resources allocated by a problem report
 *
 * @param self Problem report
 */
void problem_report_free(problem_report_t *self);


/*
 * An enum of Extra section flags
 */
enum problem_formatter_section_flags {
    PFFF_REQUIRED = 1 << 0, ///< section must be present in the format spec
};

/*
 * The problem formatter structure formats a problem data according to a format
 * string and stores result a problem report.
 *
 * The problem formatter uses '%reason%' as %summary section format string, if
 * %summary is not provided by a format string.
 */
struct problem_formatter;
typedef struct problem_formatter problem_formatter_t;

/*
 * Constructs a new problem formatter.
 *
 * @return Non-NULL pointer to the new problem formatter
 */
problem_formatter_t *problem_formatter_new(void);

/*
 * Releases all resources allocated by a problem formatter
 *
 * @param self Problem formatter
 */
void problem_formatter_free(problem_formatter_t *self);

/*
 * Adds a new recognized section
 *
 * The problem formatter ignores a section in the format spec if the section is
 * not one of the default nor added by this function.
 *
 * How the problem formatter handles these extra sections:
 *
 * A custom section is something like %description section. %description is the
 * default section where all text (sub)sections are stored. If the formatter
 * finds the custom section in format string, then starts storing text
 * (sub)sections in the custom section.
 *
 * (%description)    |:: comment
 * (%description)    |
 * (%description)    |Package:: package
 * (%description)    |
 * (%additiona_info) |%additional_info::
 * (%additiona_info) |%reporter%
 * (%additiona_info) |User:: user_name,uid
 * (%additiona_info) |
 * (%additiona_info) |Directories:: root,cwd
 *
 *
 * @param self Problem formatter
 * @param name Name of the added section
 * @param flags Info about the added section
 * @return Zero on success. -EEXIST if the name is already known by the formatter
 */
int problem_formatter_add_section(problem_formatter_t *self, const char *name, int flags);

/*
 * Loads a problem format from a string.
 *
 * @param self Problem formatter
 * @param fmt Format
 * @return Zero on success or number of warnings (e.g. missing section,
 * unrecognized section).
 */
int problem_formatter_load_string(problem_formatter_t* self, const char *fmt);


/*
 * Loads a problem format from a file.
 *
 * @param self Problem formatter
 * @param pat Path to the format file
 * @return Zero on success or number of warnings (e.g. missing section,
 * unrecognized section).
 */
int problem_formatter_load_file(problem_formatter_t* self, const char *path);

/*
 * Creates a new problem report, formats the data according to the loaded
 * format string and stores output in the report.
 *
 * @param self Problem formatter
 * @param data Problem data to format
 * @param report Pointer where the created problem report is to be stored
 * @return Zero on success, otherwise non-zero value.
 */
int problem_formatter_generate_report(const problem_formatter_t *self, problem_data_t *data, problem_report_t **report);

#ifdef __cplusplus
}
#endif

#endif // LIBREPORT_PROBLEM_REPORT_H
