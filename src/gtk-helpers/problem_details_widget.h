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
#ifndef _PROBLEM_DETAILS_WIDGET_H
#define _PROBLEM_DETAILS_WIDGET_H

#include <gtk/gtk.h>
#include "problem_data.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

G_BEGIN_DECLS

#define TYPE_PROBLEM_DETAILS_WIDGET            (problem_details_widget_get_type())
#define PROBLEM_DETAILS_WIDGET(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_PROBLEM_DETAILS_WIDGET, ProblemDetailsWidget))
#define PROBLEM_DETAILS_WIDGET_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_PROBLEM_DETAILS_WIDGET, ProblemDetailsWidgetClass))
#define IS_PROBLEM_DETAILS_WIDGET(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_PROBLEM_DETAILS_WIDGET))
#define IS_PROBLEM_DETAILS_WIDGET_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_PROBLEM_DETAILS_WIDGET))
#define PROBLEM_DETAILS_WIDGET_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_PROBLEM_DETAILS_WIDGET, ProblemDetailsWidgetClass))

typedef struct _ProblemDetailsWidget        ProblemDetailsWidget;
typedef struct _ProblemDetailsWidgetClass   ProblemDetailsWidgetClass;
typedef struct ProblemDetailsWidgetPrivate  ProblemDetailsWidgetPrivate;

struct _ProblemDetailsWidget {
   GtkGrid    parent_instance;
   ProblemDetailsWidgetPrivate *priv;
};

struct _ProblemDetailsWidgetClass {
   GtkGridClass parent_class;
};

GType problem_details_widget_get_type (void) G_GNUC_CONST;

ProblemDetailsWidget *problem_details_widget_new(problem_data_t *problem);

G_END_DECLS

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _PROBLEM_DETAILS_WIDGET_H */

