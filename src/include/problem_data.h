/*
    Copyright (C) 2009  Abrt team.
    Copyright (C) 2009  RedHat inc.

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

/** @file problem_data.h */

#ifndef LIBREPORT_PROBLEM_DATA_H_
#define LIBREPORT_PROBLEM_DATA_H_

#include "libreport_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dump_dir;

enum {
    CD_FLAG_BIN           = (1 << 0),
    CD_FLAG_TXT           = (1 << 1),
    CD_FLAG_ISEDITABLE    = (1 << 2),
    CD_FLAG_ISNOTEDITABLE = (1 << 3),
    /* Show this element in "short" info (report-cli -l) */
    CD_FLAG_LIST          = (1 << 4),
    CD_FLAG_UNIXTIME      = (1 << 5),
};

struct problem_item {
    char    *content;
    unsigned flags;
    /* Used by UI for presenting "item allowed/not allowed" checkboxes: */
    int      selected_by_user;     /* 0 "don't know", -1 "no", 1 "yes" */
    int      allowed_by_reporter;  /* 0 "no", 1 "yes" */
    int      default_by_reporter;  /* 0 "no", 1 "yes" */
    int      required_by_reporter; /* 0 "no", 1 "yes" */
};
typedef struct problem_item problem_item;

char *problem_item_format(struct problem_item *item);


/* In-memory problem data structure and accessors */

typedef GHashTable problem_data_t;

problem_data_t *problem_data_new(void);

static inline void problem_data_free(problem_data_t *problem_data)
{
    //TODO: leaks problem item;
    if (problem_data)
        g_hash_table_destroy(problem_data);
}

void problem_data_add_basics(problem_data_t *pd);

void problem_data_add_current_process_data(problem_data_t *pd);

void problem_data_add(problem_data_t *problem_data,
                const char *name,
                const char *content,
                unsigned flags);
void problem_data_add_text_noteditable(problem_data_t *problem_data,
                const char *name,
                const char *content);
void problem_data_add_text_editable(problem_data_t *problem_data,
                const char *name,
                const char *content);
/* "name" can be NULL: */
void problem_data_add_file(problem_data_t *pd, const char *name, const char *path);

static inline struct problem_item *problem_data_get_item_or_NULL(problem_data_t *problem_data, const char *key)
{
    return (struct problem_item *)g_hash_table_lookup(problem_data, key);
}
char *problem_data_get_content_or_NULL(problem_data_t *problem_data, const char *key);
/* Aborts if key is not found: */
char *problem_data_get_content_or_die(problem_data_t *problem_data, const char *key);

/**
  @brief Loads key value pairs from os_info item in to the osinfo argument

  The function expects that osinfo data are stored in format of os-release(5).

  The Function at first step tries to load the data from os_info obtained from
  chrooted directory. If the chrooted data doesn't exist the function loads
  os_info from the data obtained from the standard path. If the os_info item is
  missing the function adds PRETTY_NAME key with a content of the os_release
  item.

  @param problem_data Problem data object to read the os_info items
  @param osinfo String string map where loaded key value pairs are saved
 */
void problem_data_get_osinfo(problem_data_t *problem_data, map_string_t *osinfo);

int problem_data_send_to_abrt(problem_data_t* problem_data);

/* Conversions between in-memory and on-disk formats */

void problem_data_load_from_dump_dir(problem_data_t *problem_data, struct dump_dir *dd, char **excluding);

problem_data_t *create_problem_data_from_dump_dir(struct dump_dir *dd);
/* Helper for typical operation in reporters: */
problem_data_t *create_problem_data_for_reporting(const char *dump_dir_name);

/**
  @brief Saves the problem data object

  @param problem_data Problem data object to save
  @param base_dir_name Location to store the problem data
*/
struct dump_dir *create_dump_dir_from_problem_data(problem_data_t *problem_data, const char *base_dir_name);

#ifdef __cplusplus
}
#endif

#endif
