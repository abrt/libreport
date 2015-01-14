/*
    String buffer implementation

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
*/

#include "internal_libreport.h"

int
is_comment_dup(GList *comments, const char *comment)
{
    char * const trim_comment = trim_all_whitespace(comment);
    bool same_comments = false;

    for (GList *l = comments; l && !same_comments; l = l->next)
    {
        const char * const comment_body = (const char *) l->data;
        char * const trim_comment_body = trim_all_whitespace(comment_body);
        same_comments = (strcmp(trim_comment_body, trim_comment) == 0);
        free(trim_comment_body);
    }

    free(trim_comment);
    return same_comments;
}

unsigned
comments_find_best_bt_rating(GList *comments)
{
    if (comments == NULL)
        return 0;

    unsigned best_rating = 0;
    for (GList *l = comments; l; l = l->next)
    {
        char *comment_body = (char *) l->data;

        char *start_rating_line = strstr(comment_body, FILENAME_RATING": ");
        if (!start_rating_line)
        {
            log_debug(_("Note does not contain rating"));
            continue;
        }

        start_rating_line += strlen(FILENAME_RATING": ");

        errno = 0;
        char *e;
        long rating = strtoul(start_rating_line, &e, 10);
        /*
         * Note: we intentionally check for '\n'. Any other terminator
         * (even '\0') is not ok in this case.
         */
        if (errno || e == start_rating_line || (*e != '\n' && *e != '\r') || (unsigned long)rating > UINT_MAX)
        {
            /* error / no digits / illegal trailing chars */
            continue;
        }

        if (rating > best_rating)
            best_rating = rating;
    }

    return best_rating;
}

