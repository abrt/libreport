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
#ifndef LIBREPORT_EVENT_CONFIG_H
#define LIBREPORT_EVENT_CONFIG_H

#include <stdbool.h>
#include <glib.h>
#include "problem_data.h"
#include "config_item_info.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    OPTION_TYPE_TEXT,
    OPTION_TYPE_BOOL,
    OPTION_TYPE_PASSWORD,
    OPTION_TYPE_NUMBER,
    OPTION_TYPE_HINT_HTML,
    OPTION_TYPE_INVALID,
} option_type_t;

/*
 * struct to hold information about config options
 * it's supposed to hold information about:
 *   type -> which designates the widget used to display it and we can do some test based on the type
 *   label
 *   allowed value(s) -> regexp?
 *   name -> env variable name
 *   value -> value retrieved from the gui, so when we want to set the env
 *            evn variables, we can just traverse the list of the options
 *            and set the env variables according to name:value in this structure
 */
typedef struct
{
    char *eo_name; //name of the value which should be used for env variable
    char *eo_value;
    char *eo_label;
    char *eo_note_html;
    option_type_t eo_type;
    int eo_allow_empty;
    //char *description; //can be used as tooltip in gtk app
    //char *allowed_value;
    //int required;
    bool is_advanced;
} event_option_t;

/*
 * struct holds
 *   invopt_name = name of the option with invalid value
 *   invopt_error = string of the error message
 */
typedef struct
{
    char *invopt_name;
    char *invopt_error;
} invalid_option_t;

event_option_t *new_event_option(void);
void free_event_option(event_option_t *p);

//structure to hold the option data
typedef struct
{
    config_item_info_t *info;

    char *ec_creates_items;
    char *ec_requires_items;
    char *ec_exclude_items_by_default;
    char *ec_include_items_by_default;
    char *ec_exclude_items_always;
    bool  ec_exclude_binary_items;
    long  ec_minimal_rating;
    bool  ec_skip_review;
    bool  ec_sending_sensitive_data;
    bool  ec_supports_restricted_access;
    char *ec_restricted_access_option;
    bool  ec_requires_details;

    GList *ec_imported_event_names;
    GList *options;
} event_config_t;

event_config_t *new_event_config(const char *name);
config_item_info_t *ec_get_config_info(event_config_t * ec);
const char *ec_get_screen_name(event_config_t *ec);
void ec_set_screen_name(event_config_t *ec, const char *screen_name);

const char *ec_get_description(event_config_t *ec);
void ec_set_description(event_config_t *ec, const char *description);

const char *ec_get_name(event_config_t *ec);
const char *ec_get_long_desc(event_config_t *ec);
void ec_set_long_desc(event_config_t *ec, const char *long_desc);
bool ec_is_configurable(event_config_t* ec);

/* Returns True if the event is configured to create ticket with restricted
 * access.
 */
bool ec_restricted_access_enabled(event_config_t *ec);

void free_event_config(event_config_t *p);

invalid_option_t *new_invalid_option(void);
void free_invalid_options(invalid_option_t* p);

void load_event_description_from_file(event_config_t *event_config, const char* filename);

// (Re)loads data from /etc/abrt/events/*.{conf,xml}
GHashTable *load_event_config_data(void);
/* Frees all loaded data */
void free_event_config_data(void);
event_config_t *get_event_config(const char *event_name);
event_option_t *get_event_option_from_list(const char *option_name, GList *event_options);

/* for debugging */
void ec_print(event_config_t *ec);

extern GHashTable *g_event_config_list;   // for iterating through entire list of all loaded configs

GList *export_event_config(const char *event_name);
void unexport_event_config(GList *env_list);

GList *get_options_with_err_msg(const char *event_name);

/*
 * Checks usability of problem's backtrace rating against required rating level
 * from event configuration.
 *
 * @param cfg an event configuration
 * @param pd a checked problem data
 * @param description an output parameter for a description of rating
 * usability. If the variable holds NULL after function call no description is
 * available. The description can be provided even if backtrace rating is
 * acceptable. Can be NULL.
 * @param detail an output parameter for a more details about rating usability.
 * If the variable holds NULL after function call no description is available.
 * The detail can be provided even if backtrace rating is acceptable. Can be
 * NULL.
 * @returns true if rating is usable or above usable; otherwise false
 */
bool check_problem_rating_usability(const event_config_t *cfg,
                                    problem_data_t       *pd,
                                    char                 **description,
                                    char                 **detail);

/**
 * Expand suffixed star wildcard in an event name.
 *
 * Returns the expanded list of event names matching the pattern. If no
 * wildcard is present, returns the singleton list with the original event
 * only. Returns NULL if no matching events could be found.
 */
GList *expand_event_wildcard(const gchar *event_name, gsize event_len);

/**
 * Expand '*' wildcards in an event chain.
 *
 * Returns the expanded list of event names.
 */
GList *expand_event_chain_wildcards(GList *chain);

#ifdef __cplusplus
}
#endif

#endif
