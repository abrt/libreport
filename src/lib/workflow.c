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

#include "event_config.h"
#include "workflow.h"
#include "internal_libreport.h"

struct workflow
{
    config_item_info_t *info;
    int priority; // direct correlation: higher number -> higher priority

    GList *events; //list of event_option_t
};


GHashTable *g_workflow_list;

workflow_t *new_workflow(const char *name)
{
    workflow_t *w = libreport_xzalloc(sizeof(*w));
    w->info = new_config_info(name);
    return w;
}

void free_workflow(workflow_t *w)
{
    if (!w)
        return;

    free_config_info(w->info);
    g_list_free_full(w->events, (GDestroyNotify)free_event_config);
    free(w);
}

void free_workflow_list(GHashTable **wl)
{
    if (*wl != NULL)
    {
        g_hash_table_destroy(*wl);
        *wl = NULL;
    }
}

workflow_t *get_workflow(const char *name)
{
    if (!g_workflow_list)
        return NULL;
    /* @@ FIXME: SYMLINKS@!!!
    if (g_event_config_symlinks)
    {
        char *link = g_hash_table_lookup(g_event_config_symlinks, name);
        if (link)
            name = link;
    }
    */
    return g_hash_table_lookup(g_workflow_list, name);
}

static gint file_obj_cmp(file_obj_t *file, const char *filename)
{
    gint cmp = strcmp(file->filename, filename);
    return cmp;
}

static void load_workflow_config(const char *name,
                           GList *available_wfs,
                           GHashTable *wf_list)
{
    GList *wf_file = g_list_find_custom(available_wfs, name, (GCompareFunc)file_obj_cmp);
    if (wf_file)
    {
        file_obj_t *file = (file_obj_t *)wf_file->data;
        workflow_t *workflow = new_workflow(file->filename);
        load_workflow_description_from_file(workflow, file->fullpath);
        log_info("Adding '%s' to workflows\n", file->filename);
        g_hash_table_insert(wf_list, libreport_xstrdup(file->filename), workflow);
    }
}

GHashTable *load_workflow_config_data_from_list(GList *wf_names,
                                                const char *path)
{
    GList *wfs = wf_names;
    GHashTable *wf_list = g_hash_table_new_full(
                         g_str_hash,
                         g_str_equal,
                         g_free,
                         (GDestroyNotify) free_workflow
        );

    if (path == NULL)
        path = WORKFLOWS_DIR;

    GList *workflow_files = libreport_get_file_list(path, "xml");
    while(wfs)
    {
        load_workflow_config((const char *)wfs->data, workflow_files, wf_list);
        wfs = g_list_next(wfs);
    }
    libreport_free_file_list(workflow_files);

    return wf_list;
}

GHashTable *load_workflow_config_data(const char *path)
{
    if (g_workflow_list)
        return g_workflow_list;

    g_workflow_list = g_hash_table_new_full(
                                    g_str_hash,
                                    g_str_equal,
                                    g_free,
                                    (GDestroyNotify) free_workflow
    );

    if (path == NULL)
        path = WORKFLOWS_DIR;

    GList *workflow_files = libreport_get_file_list(path, "xml");
    while (workflow_files)
    {
        file_obj_t *file = (file_obj_t *)workflow_files->data;

        workflow_t *workflow = get_workflow(file->filename);
        bool nw_workflow = (!workflow);
        if (nw_workflow)
            workflow = new_workflow(file->filename);

        load_workflow_description_from_file(workflow, file->fullpath);

        if (nw_workflow)
            g_hash_table_replace(g_workflow_list, libreport_xstrdup(wf_get_name(workflow)), workflow);

        libreport_free_file_obj(file);
        workflow_files = g_list_delete_link(workflow_files, workflow_files);
    }

    return g_workflow_list;
}

config_item_info_t *workflow_get_config_info(workflow_t *w)
{
    return w->info;
}

GList *wf_get_event_list(workflow_t *w)
{
    return w->events;
}

GList *wf_get_event_names(workflow_t *w)
{
    GList *wf_event_list = wf_get_event_list(w);
    GList *event_names = NULL;

    while (wf_event_list)
    {
        /* Since appending is inefficient for GLib doubly-linked lists, we prepend
         * here and reverse the list just before returning.
         */
        event_names = g_list_prepend(event_names, libreport_xstrdup(ec_get_name(wf_event_list->data)));
        wf_event_list = g_list_next(wf_event_list);
    }

    return g_list_reverse(event_names);
}

const char *wf_get_name(workflow_t *w)
{
    return ci_get_name(workflow_get_config_info(w));
}

const char *wf_get_screen_name(workflow_t *w)
{
    return ci_get_screen_name(workflow_get_config_info(w));
}

const char *wf_get_description(workflow_t *w)
{
    return ci_get_description(workflow_get_config_info(w));
}

const char *wf_get_long_desc(workflow_t *w)
{
    return ci_get_long_desc(workflow_get_config_info(w));
}

int wf_get_priority(workflow_t *w)
{
    return w->priority;
}

void wf_set_screen_name(workflow_t *w, const char* screen_name)
{
    ci_set_screen_name(workflow_get_config_info(w), screen_name);
}

void wf_set_description(workflow_t *w, const char* description)
{
    ci_set_description(workflow_get_config_info(w), description);
}

void wf_set_long_desc(workflow_t *w, const char* long_desc)
{
    ci_set_long_desc(workflow_get_config_info(w), long_desc);
}

void wf_add_event(workflow_t *w, event_config_t *ec)
{
    w->events = g_list_append(w->events, ec);
    log_info("added to ev list: '%s'", ec_get_screen_name(ec));
}

void wf_set_priority(workflow_t *w, int priority)
{
    w->priority = priority;
}

/*
 * Returns a negative integer if the first value comes before the second, 0 if
 * they are equal, or a positive integer if the first value comes after the
 * second.
 */
int wf_priority_compare(const workflow_t *first, const workflow_t *second)
{
    return second->priority - first->priority;
}
