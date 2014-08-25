/*
    Copyright (C) 2014  ABRT Team
    Copyright (C) 2014  RedHat inc.

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
#ifndef _PROBLEM_DETAILS_DIALOG_H
#define _PROBLEM_DETAILS_DIALOG_H

#include <gtk/gtk.h>
#include "problem_data.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

GtkWidget *problem_details_dialog_new(problem_data_t *problem, GtkWindow *parent);
GtkWidget *problem_details_dialog_new_for_dir(const char *dir, GtkWindow *parent);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _PROBLEM_DETAILS_DIALOG_H */

