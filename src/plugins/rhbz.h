/*
    Copyright (C) 2011  ABRT team
    Copyright (C) 2011  RedHat Inc

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

#ifndef RHBZ_H
#define RHBZ_H

/* include/stdint.h: typedef int int32_t;
 * include/xmlrpc-c/base.h: typedef int32_t xmlrpc_int32;
 */

#include "abrt_xmlrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUGZILLA_VERSION(a,b,c) ((unsigned)(((a) << 16) + ((b) << 8) + (c)))

enum {
    RHBZ_MANDATORY_MEMB      = (1 << 0),
    RHBZ_READ_STR            = (1 << 1),
    RHBZ_READ_INT            = (1 << 2),
    RHBZ_MINOR_UPDATE        = (1 << 3),
    RHBZ_PRIVATE             = (1 << 4),
    RHBZ_BINARY_ATTACHMENT   = (1 << 5),
};

#define IS_MANDATORY(flags) ((flags) & RHBZ_MANDATORY_MEMB)
#define IS_READ_STR(flags) ((flags) & RHBZ_READ_STR)
#define IS_READ_INT(flags) ((flags) & RHBZ_READ_INT)
#define IS_MINOR_UPDATE(flags) ((flags) & RHBZ_MINOR_UPDATE)
#define IS_PRIVATE(flags) ((flags) & RHBZ_PRIVATE)

struct bug_info {
    int bi_id;
    int bi_dup_id;
    unsigned bi_best_bt_rating;

    char *bi_status;
    char *bi_resolution;
    char *bi_reporter;
    char *bi_product;
    char *bi_platform;

    GList *bi_cc_list;
    GList *bi_comments;
};

struct bug_info *new_bug_info();
void free_bug_info(struct bug_info *bz);

bool rhbz_login(struct abrt_xmlrpc *ax, const char *login, const char *password);

void rhbz_mail_to_cc(struct abrt_xmlrpc *ax, int bug_id, const char *mail, int flags);

void rhbz_add_comment(struct abrt_xmlrpc *ax, int bug_id, const char *comment,
                      int flags);

void rhbz_set_url(struct abrt_xmlrpc *ax, int bug_id, const char *url, int flags);

void rhbz_close_as_duplicate(struct abrt_xmlrpc *ax, int bug_id,
                             int duplicate_bug,
                             int flags);

void *rhbz_bug_read_item(const char *memb, xmlrpc_value *xml, int flags);

void rhbz_logout(struct abrt_xmlrpc *ax);

xmlrpc_value *rhbz_get_member(const char *member, xmlrpc_value *xml);

unsigned rhbz_array_size(xmlrpc_value *xml);

xmlrpc_value *rhbz_array_item_at(xmlrpc_value *xml, int pos);

int rhbz_get_bug_id_from_array0(xmlrpc_value *xml, unsigned ver);

int rhbz_new_bug(struct abrt_xmlrpc *ax,
                problem_data_t *problem_data,
                const char *product,
                const char *version,
                const char *bzsummary,
                const char *bzcomment,
                bool private,
                GList *group);

int rhbz_attach_blob(struct abrt_xmlrpc *ax, const char *bug_id,
                const char *att_name, const char *data, int data_len, int flags);

int rhbz_attach_fd(struct abrt_xmlrpc *ax, const char *bug_id,
                const char *att_name, int fd, int flags);

GList *rhbz_bug_cc(xmlrpc_value *result_xml);

struct bug_info *rhbz_bug_info(struct abrt_xmlrpc *ax, int bug_id);


struct bug_info *rhbz_find_origin_bug_closed_duplicate(struct abrt_xmlrpc *ax,
                                                       struct bug_info *bi);
unsigned rhbz_version(struct abrt_xmlrpc *ax);

xmlrpc_value *rhbz_search_duphash(struct abrt_xmlrpc *ax,
                        const char *product, const char *version, const char *component,
                        const char *duphash);

xmlrpc_value *rhbz_get_sub_components(struct abrt_xmlrpc *ax, const char *product, const char *component);

char *rhbz_get_default_sub_component(const char *component, xmlrpc_value *sub_components);

#ifdef __cplusplus
}
#endif

#endif
