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

#pragma once

#include <glib.h>
#include "libreport_types.h"

G_BEGIN_DECLS

struct dump_dir;

enum {
    CD_FLAG_BIN           = (1 << 0),
    CD_FLAG_TXT           = (1 << 1),
    CD_FLAG_ISEDITABLE    = (1 << 2),
    CD_FLAG_ISNOTEDITABLE = (1 << 3),
    /* Show this element in "short" info (report-cli -l) */
    CD_FLAG_LIST          = (1 << 4),
    CD_FLAG_UNIXTIME      = (1 << 5),
    /* If element is HUGE text, it is not read into memory (it can OOM the machine).
     * Instead, it is treated as binary (CD_FLAG_BIN), but also has CD_FLAG_BIGTXT
     * bit set in flags. This allows to set proper MIME type when it gets attached
     * to a bug report etc.
     */
    CD_FLAG_BIGTXT        = (1 << 6),
};

#define PROBLEM_ITEM_UNINITIALIZED_SIZE ((unsigned long)-1)

struct problem_item {
    char    *content;
    unsigned flags;
    unsigned long size;
    /* Used by UI for presenting "item allowed/not allowed" checkboxes: */
    int      selected_by_user;     /* 0 "don't know", -1 "no", 1 "yes" */
    int      allowed_by_reporter;  /* 0 "no", 1 "yes" */
    int      default_by_reporter;  /* 0 "no", 1 "yes" */
    int      required_by_reporter; /* 0 "no", 1 "yes" */
};
typedef struct problem_item problem_item;

char *problem_item_format(struct problem_item *item);

int problem_item_get_size(struct problem_item *item, unsigned long *size);

/* In-memory problem data structure and accessors */

typedef GHashTable problem_data_t;

problem_data_t *problem_data_new(void);

static inline void problem_data_free(problem_data_t *problem_data)
{
    //TODO: leaks problem item;
    if (problem_data)
        g_hash_table_destroy(problem_data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(problem_data_t, problem_data_free);

void problem_data_add_basics(problem_data_t *pd);

void problem_data_add_current_process_data(problem_data_t *pd);

void problem_data_add(problem_data_t *problem_data,
                const char *name,
                const char *content,
                unsigned flags);
struct problem_item *problem_data_add_ext(problem_data_t *problem_data,
                const char *name,
                const char *content,
                unsigned flags,
                unsigned long size);
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

/* Returns all element names stored in problem_data */
static inline GList *problem_data_get_all_elements(problem_data_t *problem_data)
{
    return g_hash_table_get_keys(problem_data);
}

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
void problem_data_get_osinfo(problem_data_t *problem_data, GHashTable *osinfo);

int problem_data_send_to_abrt(problem_data_t* problem_data);

/* Conversions between in-memory and on-disk formats */

/* Low level function reading data of dump dir elements
 *
 * @param dd Dump directory
 * @param name Requested element
 * @param content If the element is of type CD_FLAG_TXT, its contents will
 *        loaded to malloced memory and the pointer will be store here.
 * @param type_flags One of the following : CD_FLAG_BIN, CD_FLAG_TXT, (CD_FLAG_BIGTXT + CD_FLAG_BIN)
 * @param fd If not NULL, the file descriptor used to read data will not be
 *        closed and will be passed out of the function in this argument.
 * @return On errors, negative number; otherwise 0.
 */
int problem_data_load_dump_dir_element(struct dump_dir *dd, const char *name, char **content, int *type_flags, int *fd);

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
struct dump_dir *create_dump_dir_from_problem_data_ext(problem_data_t *problem_data, const char *base_dir_name, uid_t uid);

/**
  @brief Saves the problem data object in opened dump directory

  @param dd Dump directory
  @param problem_data Problem data object to save
  @return 0 on success; otherwise non-zero value
 */
int save_problem_data_in_dump_dir(struct dump_dir *dd, problem_data_t *problem_data);

enum {
    PROBLEM_REPRODUCIBLE_UNKNOWN,
    PROBLEM_REPRODUCIBLE_YES,
    PROBLEM_REPRODUCIBLE_RECURRENT,

    _PROBLEM_REPRODUCIBLE_MAX_,
};

int get_problem_data_reproducible(problem_data_t *problem_data);
const char *get_problem_data_reproducible_name(int reproducible);

G_END_DECLS
