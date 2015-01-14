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

#ifndef REPORTERS_H
#define REPORTERS_H

#ifdef __cplusplus
extern "C" {
#endif

#define is_comment_dup libreport_is_comment_dup
int is_comment_dup(GList *comments, const char *comment);
#define comments_find_best_bt_rating libreport_comments_find_best_bt_rating
unsigned comments_find_best_bt_rating(GList *comments);

#ifdef __cplusplus
}
#endif

#endif
