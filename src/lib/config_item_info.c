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

#include "internal_libreport.h"

struct config_item_info
{
    char *name;     //the event name (from it's filename)
    char *screen_name; //ui friendly name of the event: "Bugzilla" "RedHat Support Upload"
    char *description; // "Report to..."/"Save to file". Should be one sentence, not long
    char *long_desc;  // Long(er) explanation, if needed

};


config_item_info_t *new_config_info(const char *name)
{
    config_item_info_t *info = g_new0(config_item_info_t, 1);
    info->name = g_strdup(name);
    return info;
}

void free_config_info(config_item_info_t *info)
{
    if (info == NULL)
        return;

    g_free(info->name);
    g_free(info->screen_name);
    g_free(info->description);
    g_free(info->long_desc);

    g_free(info);
}

void ci_set_screen_name(config_item_info_t *ci, const char *screen_name)
{
    g_free(ci->screen_name);
    ci->screen_name = g_strdup(screen_name);
}

void ci_set_description(config_item_info_t *ci, const char *description)
{
    g_free(ci->description);
    ci->description = g_strdup(description);
}

void ci_set_long_desc(config_item_info_t *ci, const char *long_description)
{
    g_free(ci->long_desc);
    ci->long_desc = g_strdup(long_description);
}

const char *ci_get_screen_name(config_item_info_t *ci)
{
    return ci->screen_name;
}

const char *ci_get_name(config_item_info_t *ci)
{
    return ci->name;
}

const char *ci_get_description(config_item_info_t *ci)
{
    return ci->description;
}

const char *ci_get_long_desc(config_item_info_t *ci)
{
    return ci->long_desc;
}
