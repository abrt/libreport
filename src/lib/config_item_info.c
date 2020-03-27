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
    config_item_info_t *info = (config_item_info_t *)libreport_xzalloc(sizeof(*info));
    info->name = libreport_xstrdup(name);
    return info;
}

void free_config_info(config_item_info_t *info)
{
    if (info == NULL)
        return;

    free(info->name);
    free(info->screen_name);
    free(info->description);
    free(info->long_desc);

    free(info);
}

void ci_set_screen_name(config_item_info_t *ci, const char *screen_name)
{
    free(ci->screen_name);
    ci->screen_name = libreport_xstrdup(screen_name);
}

void ci_set_description(config_item_info_t *ci, const char *description)
{
    free(ci->description);
    ci->description = libreport_xstrdup(description);
}

void ci_set_long_desc(config_item_info_t *ci, const char *long_description)
{
    free(ci->long_desc);
    ci->long_desc = libreport_xstrdup(long_description);
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
