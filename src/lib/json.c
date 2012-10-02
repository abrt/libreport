/*
    Copyright (C) 2012  ABRT team
    Copyright (C) 2012  RedHat Inc

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

#include <json/json.h>
#include <btparser/thread.h>
#include <btparser/core-backtrace.h>

#include "internal_libreport.h"
#include "ureport.h"
#include "libreport_curl.h"


/* on success 1 returned, on error zero is returned and appropriate value
 * is returned as third argument. You should never read third argument when
 * function fails
 *
 * json-c library doesn't have any json_object_new_long,
 * thus we have to use only int
 */
static int get_pd_int_item(problem_data_t *pd, const char *key, int *result)
{
    if (!pd || !key)
        return 0;

    char *pd_item = problem_data_get_content_or_NULL(pd, key);
    if (!pd_item)
    {
        VERB1 log("warning: '%s' is not an item in problem directory", key);
        return 0;
    }

    errno = 0;
    char *e;
    long i = strtol(pd_item, &e, 10);
    if (errno || pd_item == e || *e != '\0' || (int) i != i)
        return 0;

    *result = i;
    return 1;
}

static void ureport_add_int(struct json_object *ur, const char *key, int i)
{
    struct json_object *jint = json_object_new_int(i);
    if (!jint)
        die_out_of_memory();

    json_object_object_add(ur, key, jint);
}

static void ureport_add_str(struct json_object *ur, const char *key,
                            const char *s)
{
    struct json_object *jstring = json_object_new_string(s);
    if (!jstring)
        die_out_of_memory();

    json_object_object_add(ur, key, jstring);
}

static void ureport_add_os(struct json_object *ur, problem_data_t *pd)
{
    /* paranoia - we only need OS_RELEASE_IN_ROOTDIR,
       but check that ROOTDIR also exists */
    char *pd_item = problem_data_get_content_or_NULL(pd, FILENAME_ROOTDIR);
    if (pd_item)
        pd_item = problem_data_get_content_or_NULL(pd, FILENAME_OS_RELEASE_IN_ROOTDIR);

    if (!pd_item)
        pd_item = problem_data_get_content_or_NULL(pd, FILENAME_OS_RELEASE);

    if (!pd_item)
        return;

    struct json_object *jobject = json_object_new_object();
    if (!jobject)
        die_out_of_memory();

    char *name, *version;
    parse_release_for_rhts(pd_item, &name, &version);

    ureport_add_str(jobject, "name", name);
    ureport_add_str(jobject, "version", version);

    free(name);
    free(version);

    json_object_object_add(ur, "os", jobject);
}

static void ureport_add_type(struct json_object *ur, problem_data_t *pd)
{
    char *pd_item = problem_data_get_content_or_NULL(pd, FILENAME_ANALYZER);
    if (!pd_item)
        return;

    if (!strcmp(pd_item, "CCpp"))
        ureport_add_str(ur, "type", "USERSPACE");
    if (!strcmp(pd_item, "Python"))
        ureport_add_str(ur, "type", "PYTHON");
    if (!strcmp(pd_item, "Kerneloops"))
        ureport_add_str(ur, "type", "KERNELOOPS");
}

static void ureport_add_core_backtrace(struct json_object *ur, problem_data_t *pd)
{
    char *pd_item = problem_data_get_content_or_NULL(pd, FILENAME_CORE_BACKTRACE);
    if (!pd_item)
        return;

    struct btp_thread *core_bt = btp_load_core_backtrace(pd_item);
    if (!core_bt)
        return;

    struct json_object *jarray = json_object_new_array();
    if (!jarray)
        die_out_of_memory();

    struct btp_frame *frame;
    unsigned frame_nr = 0;
    for (frame = core_bt->frames; frame; frame = frame->next)
    {
        struct frame_aux *aux = frame->user_data;

        struct json_object *item = json_object_new_object();
        if (!item)
            die_out_of_memory();

        if (aux->filename)
            ureport_add_str(item, "path", aux->filename);

        if (frame->function_name)
            ureport_add_str(item, "funcname", frame->function_name);

        if (aux->build_id)
            ureport_add_str(item, "buildid", aux->build_id);

        if (aux->fingerprint)
            ureport_add_str(item, "funchash", aux->fingerprint);

        /* always add offset - even offset 0 is valid */
        ureport_add_int(item, "offset", (uintmax_t)frame->address);

        ureport_add_int(item, "frame", frame_nr++);
        ureport_add_int(item, "thread", 0);


        json_object_array_add(jarray, item);
    }

    btp_thread_free(core_bt);

    json_object_object_add(ur, FILENAME_CORE_BACKTRACE, jarray);
}

static void ureport_add_item_str(struct json_object *ur, problem_data_t *pd,
                                 const char *key, const char *rename)
{
        char *pd_item = problem_data_get_content_or_NULL(pd, key);
        if (!pd_item)
            return;

        ureport_add_str(ur, (rename) ?: key, pd_item);
}

static void ureport_add_item_int(struct json_object *ur, problem_data_t *pd,
                                 const char *key, const char *rename)
{
    int nr;
    int stat = get_pd_int_item(pd, key, &nr);
    if (!stat)
        return;

    ureport_add_int(ur, (rename) ?: key, nr);
}

static void ureport_add_pkg(struct json_object *ur, problem_data_t *pd)
{
    struct json_object *jobject = json_object_new_object();
    if (!jobject)
        die_out_of_memory();

    ureport_add_item_int(jobject, pd, FILENAME_PKG_EPOCH, "epoch");
    ureport_add_item_str(jobject, pd, FILENAME_PKG_NAME, "name");
    ureport_add_item_str(jobject, pd, FILENAME_PKG_VERSION, "version");
    ureport_add_item_str(jobject, pd, FILENAME_PKG_RELEASE, "release");
    ureport_add_item_str(jobject, pd, FILENAME_PKG_ARCH, "architecture");

    json_object_object_add(ur, "installed_package", jobject);
}

static void ureport_add_related_pkgs(struct json_object *ur, problem_data_t *pd)
{
    // TODO: populate this field
    struct json_object *jobject = json_object_new_array();
    json_object_object_add(ur, "related_packages", jobject);
}

static void ureport_add_reporter(struct json_object *ur, const char *name, const char *version)
{
    struct json_object *jobject = json_object_new_object();
    if (!jobject)
        die_out_of_memory();

    ureport_add_str(jobject, "name", name);
    ureport_add_str(jobject, "version", version);

    json_object_object_add(ur, "reporter", jobject);
}

char *new_json_ureport(problem_data_t *pd)
{
    struct json_object *ureport = json_object_new_object();
    if (!ureport)
        die_out_of_memory();

    ureport_add_item_str(ureport, pd, "user_type", NULL);
    ureport_add_item_int(ureport, pd, "uptime", NULL);

   /* mandatory, but not in problem-dir
    *
    * ureport_add_item_int(ureport, pd, "crash_thread", NULL);
    */
    ureport_add_int(ureport, "ureport_version", 1);
    ureport_add_int(ureport, "crash_thread", 0);

    ureport_add_item_str(ureport, pd, FILENAME_ARCHITECTURE, NULL);
    ureport_add_item_str(ureport, pd, FILENAME_EXECUTABLE, NULL);
    ureport_add_item_str(ureport, pd, FILENAME_REASON, NULL);
    ureport_add_item_str(ureport, pd, FILENAME_COMPONENT, NULL);

    ureport_add_type(ureport, pd);

    ureport_add_pkg(ureport, pd);
    ureport_add_related_pkgs(ureport, pd);
    ureport_add_os(ureport, pd);

    ureport_add_core_backtrace(ureport, pd);
    ureport_add_reporter(ureport, "ABRT", VERSION);

    char *j = xstrdup(json_object_to_json_string(ureport));
    json_object_put(ureport);

    return j;
}

char *new_json_attachment(const char *bthash, const char *type, const char *data)
{
    struct json_object *attachment = json_object_new_object();
    if (!attachment)
        die_out_of_memory();

    ureport_add_str(attachment, "bthash", bthash);
    ureport_add_str(attachment, "type", type);
    ureport_add_str(attachment, "data", data);

    char *result = xstrdup(json_object_to_json_string(attachment));
    json_object_put(attachment);

    return result;
}

struct post_state *post_ureport(problem_data_t *pd, struct ureport_server_config *config)
{
    int flags = POST_WANT_BODY | POST_WANT_ERROR_MSG;

    if (config->ur_ssl_verify)
        flags |= POST_WANT_SSL_VERIFY;

    struct post_state *post_state = new_post_state(flags);

    static const char *headers[] = {
        "Accept: application/json",
        "Connection: close",
        NULL,
    };

    char *json_ureport = new_json_ureport(pd);

    post_string_as_form_data(post_state, config->ur_url, "application/json",
                     headers, json_ureport);

    free(json_ureport);

    return post_state;
}

struct post_state *ureport_attach_rhbz(const char *bthash, int rhbz_bug_id,
                                       struct ureport_server_config *config)
{
    int flags = POST_WANT_BODY | POST_WANT_ERROR_MSG;

    if (config->ur_ssl_verify)
        flags |= POST_WANT_SSL_VERIFY;

    struct post_state *post_state = new_post_state(flags);

    static const char *headers[] = {
        "Accept: application/json",
        "Connection: close",
        NULL,
    };

    char *str_bug_id = xasprintf("%d", rhbz_bug_id);
    char *json_attachment = new_json_attachment(bthash, "RHBZ", str_bug_id);
    post_string_as_form_data(post_state, config->ur_url, "application/json",
                             headers, json_attachment);
    free(str_bug_id);
    free(json_attachment);

    return post_state;
}
