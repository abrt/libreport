/*
    Copyright (C) 2011  ABRT team
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
#ifndef LIBREPORT_WORKFLOW_H
#define LIBREPORT_WORKFLOW_H

#include <glib.h>
#include "event_config.h"
#include "config_item_info.h"

typedef struct workflow workflow_t;

extern GHashTable *g_workflow_list;

workflow_t *new_workflow(const char *name);
workflow_t *get_workflow(const char *name);
void free_workflow(workflow_t *w);

void load_workflow_description_from_file(workflow_t *w, const char *filename);
config_item_info_t *workflow_get_config_info(workflow_t *w);
const char *wf_get_name(workflow_t *w);
GList *wf_get_event_list(workflow_t *w);
GList *wf_get_event_names(workflow_t *w);
const char *wf_get_screen_name(workflow_t *w);
const char *wf_get_description(workflow_t *w);
const char *wf_get_long_desc(workflow_t *w);
int wf_get_priority(workflow_t *w);

void wf_set_screen_name(workflow_t *w, const char* screen_name);
void wf_set_description(workflow_t *w, const char* description);
void wf_set_long_desc(workflow_t *w, const char* long_desc);
void wf_add_event(workflow_t *w, event_config_t *ec);
void wf_set_priority(workflow_t *w, int priority);

/*
 * Returns a negative integer if the first value comes before the second, 0 if
 * they are equal, or a positive integer if the first value comes after the
 * second.
 */
int wf_priority_compare(const workflow_t *first, const workflow_t *second);

GHashTable *load_workflow_config_data_from_list(GList *wf_names, const char *path);

#endif
