/*
    Copyright (C) 2011  ABRT Team
    Copyright (C) 2011  RedHat inc.

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
#include <rpm/rpmts.h>
#include <rpm/rpmcli.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmpgp.h>

#include "internal_libreport.h"
#include "abrt_curl.h"
#include "client.h"

//699198,705037,705036

/* bodhi returns json structure

{
   "num_items":2,
   "title":"2 updats found",
   "tg_flash":null,
   "updates":[
      {
         "status":"stable",
         "close_bugs":true,
         "request":null,
         "date_submitted":"2011-03-18 17:25:14",
         "unstable_karma":-3,
         "submitter":"twaugh",
         "critpath":false,
         "approved":null,
         "stable_karma":2,
         "date_pushed":"2011-03-19 05:34:27",
         "builds":[
            {
               "nvr":"system-config-printer-1.3.2-1.fc15",
               "package":{
                  "suggest_reboot":false,
                  "committers":[
                     "twaugh",
                     "jpopelka"
                  ],
                  "name":"system-config-printer"
               }
            },
            {
            ....
            }
         ],
         "title":"system-config-printer-1.3.2-1.fc15",
         "notes":"This update fixes several bugs and re-enables translations that were accidentally disabled.",
         "date_modified":null,
         "nagged":null,
         "bugs":[
            {
               "bz_id":685098,
               "security":false,
               "parent":false,
               "title":"[abrt] system-config-printer-1.3.1-1.fc15: authconn.py:197:_connect:RuntimeError: failed to connect to server"
            },
            {
            ...
            }
         ],
         "comments":[
            {
               "group":null,
               "karma":0,
               "anonymous":false,
               "author":"bodhi",
               "timestamp":"2011-03-18 17:26:34",
               "text":"This update has been submitted for testing by twaugh. "
            },
            {
            ...
            }
         ],
         "critpath_approved":false,
         "updateid":"FEDORA-2011-3596",
         "karma":0,
         "release":{
            "dist_tag":"dist-f15",
            "id_prefix":"FEDORA",
            "locked":false,
            "name":"F15",
            "long_name":"Fedora 15"
         },
         "type":"bugfix"
      },
      {
      ...
      }
   ]
}
*/

static const char *const bodhi_url = "https://admin.fedoraproject.org/updates/%s";

struct bodhi {
    GList *nvr;
    GList *name;
    char *date_pushed;
    char *status;
    char *dist_tag;
    int karma;

    GList *bz_ids;
};

enum {
    BODHI_READ_STR,
    BODHI_READ_INT,
    BODHI_READ_JSON_OBJ,
};

#if 0
static void free_bodhi_list(GList *bodhi_list)
{
    if (!bodhi_list)
        return;

    for (GList *li = bodhi_list; li; li = li->next)
    {
        struct bodhi *b = (struct bodhi *) li->data;
        list_free_with_free(b->bz_ids);
        list_free_with_free(b->nvr);
        list_free_with_free(b->name);
        free(b);
    }

    g_list_free(bodhi_list);
}
#endif

static void bodhi_read_value(json_object *json, const char *item_name,
                             void *value, int flags)
{
    json_object *j = json_object_object_get(json, item_name);
    if (!j)
    {
        error_msg("'%s' section is not available", item_name);
        return;
    }

    switch (flags) {
    case BODHI_READ_INT:
        *(int *) value = json_object_get_int(j);
        break;
    case BODHI_READ_STR:
        *(char **) value = (char *) xstrdup(json_object_to_json_string(j));
        value = strtrimch(*(char **) value, '"');
        break;
    case BODHI_READ_JSON_OBJ:
        *(json_object **) value = (json_object *) j;
        break;
    };
}

#if 0
static void print_bodhi(struct bodhi *b)
{
    for (GList *l = b->nvr; l; l = l->next)
        printf("'%s' ", (char *)l->data);

    for (GList *l = b->name; l; l = l->next)
        printf("'%s' ", (char *)l->data);

    if (b->date_pushed)
        printf(" '%s'", b->date_pushed);

    if (b->status)
        printf(" '%s'", b->status);

    if (b->dist_tag)
        printf(" '%s'", b->dist_tag);

    printf(" %i", b->karma);


/*
    for (GList *li = b->bz_ids; li; li = li->next)
        printf(" %i", *(int*) li->data);
*/
    puts("");
}
#endif

static GList *bodhi_parse_json(json_object *json, const char *release)
{

    int num_items = 0;
    bodhi_read_value(json, "num_items", &num_items, BODHI_READ_INT);
    if (num_items <= 0)
        return NULL;

    json_object *updates = NULL;
    bodhi_read_value(json, "updates", &updates, BODHI_READ_JSON_OBJ);
    if (!updates)
        return NULL;

    int updates_len = json_object_array_length(updates);

    GList *bodhi_list = NULL;
    for (int i = 0; i < updates_len; ++i)
    {
        json_object *updates_item = json_object_array_get_idx(updates, i);

        /* some of item are null */
        if (!updates_item)
            continue;

        struct bodhi *b = xzalloc(sizeof(struct bodhi));
        bodhi_read_value(updates_item, "date_pushed", &b->date_pushed, BODHI_READ_STR);
        bodhi_read_value(updates_item, "karma", &b->karma, BODHI_READ_INT);
        bodhi_read_value(updates_item, "status", &b->status, BODHI_READ_STR);

        json_object *release_item = NULL;
        bodhi_read_value(updates_item, "release", &release_item, BODHI_READ_JSON_OBJ);
        if (release_item)
            bodhi_read_value(release_item, "dist_tag", &b->dist_tag, BODHI_READ_STR);

        json_object *bugs = NULL;
        bodhi_read_value(updates_item, "bugs", &release_item, BODHI_READ_JSON_OBJ);
        if (bugs)
        {
            for (int j = 0; j < json_object_array_length(bugs); ++j)
            {
                int *bz_id = xmalloc(sizeof(int));
                json_object *bug_item = json_object_array_get_idx(bugs, j);
                bodhi_read_value(bug_item, "bz_id", bz_id, BODHI_READ_INT);
                b->bz_ids = g_list_append(b->bz_ids, bz_id);
            }
        }

        json_object *builds_item = NULL;
        bodhi_read_value(updates_item, "builds", &builds_item, BODHI_READ_JSON_OBJ);
        if (builds_item)
        {
            int builds_len = json_object_array_length(builds_item);
            for (int k = 0; k < builds_len; ++k)
            {
                char *nvr = NULL, *name = NULL;
                json_object *build = json_object_array_get_idx(builds_item, k);

                bodhi_read_value(build, "nvr", &nvr, BODHI_READ_STR);
                b->nvr = g_list_append(b->nvr, nvr);

                json_object *package = NULL;
                bodhi_read_value(build, "package", &package, BODHI_READ_JSON_OBJ);
                bodhi_read_value(package, "name", &name, BODHI_READ_STR);
                b->name = g_list_append(b->name, name);
            }
        }

        bodhi_list = g_list_append(bodhi_list, b);

//        print_bodhi(b);
    }

    return bodhi_list;
}

static GList *bodhi_query_list(const char *query, const char *release)
{
    char *bodhi_url_bugs = xasprintf(bodhi_url, "list");

    abrt_post_state_t *post_state = new_abrt_post_state(
        ABRT_POST_WANT_BODY|ABRT_POST_WANT_SSL_VERIFY);

    const char *headers[] = {
        "Accept: application/json",
        NULL
    };

    log(_("Search for a new updates"));
    abrt_post_string(post_state, bodhi_url_bugs, "application/x-www-form-urlencoded",
                     headers, query);
    free(bodhi_url_bugs);

//    log("%s", post_state->body);

    json_object *json = json_tokener_parse(post_state->body);
    if (is_error(json))
        error_msg_and_die("fatal: unable parse response from bodhi server");

    GList *bodhi_list = bodhi_parse_json(json, release);
    json_object_put(json);
    free_abrt_post_state(post_state);

    return bodhi_list;
}

static char *rpm_get_nvr_by_pkg_name(const char *pkg_name)
{
    int status = rpmReadConfigFiles((const char *) NULL, (const char *) NULL);
    if (status)
        error_msg_and_die("error reading rc files");

    char* nvr = NULL;
    const char *errmsg = NULL;

    rpmts ts = rpmtsCreate();
    rpmdbMatchIterator iter = rpmtsInitIterator(ts, RPMTAG_NAME, pkg_name, 0);
    Header header = rpmdbNextIterator(iter);

    if (!header)
        goto error;

    nvr = headerFormat(header, "%{name}-%{version}-%{release}", &errmsg);

    if (!nvr && errmsg)
        error_msg("cannot get nvr. reason: %s", errmsg);

error:
    rpmdbFreeIterator(iter);
    rpmtsFree(ts);

    return nvr;
}

int main(int argc, char **argv)
{
    abrt_init(argv);
    enum {
        OPT_v = 1 << 0,
        OPT_b = 1 << 1,
        OPT_r = 1 << 2,
    };

    const char *bugs = NULL, *release = NULL, *dump_dir = ".";
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING('b', "bugs", &bugs, "ID1[,ID2,...]" , _("List of bug ids")),
        OPT_OPTSTRING('r', "release", &release, "RELEASE", _("Specify a release")),
        OPT__DUMP_DIR(&dump_dir),
        OPT_END()
    };

    const char *program_usage_string = _(
        "& [-v] [-r[RELEASE]] (-b ID1[,ID2,...] | PKG-NAME) [PKG-NAME]... \n"
        "\n"
        "Search for a new updates in bodhi server"
    );

    unsigned opts =  parse_opts(argc, argv, program_options, program_usage_string);

    if (!bugs && !argv[optind])
        show_usage_and_die(program_usage_string, program_options);

    struct strbuf *query = strbuf_new();
    if (bugs)
        query = strbuf_append_strf(query, "bugs=%s&", bugs);

    if (opts & OPT_r)
    {
        if (release)
        {
            query = strbuf_append_strf(query, "release=%s&", release);
        }
        else
        {
            problem_data_t *problem_data = create_problem_data_for_reporting(dump_dir);
            if (!problem_data)
                xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

            release = get_problem_item_content_or_NULL(problem_data, FILENAME_OS_RELEASE);
            //COMPAT, remove in abrt-2.1
            if (!release) release = get_problem_item_content_or_die(problem_data, "release");
            char *product = NULL, *version = NULL;
            parse_release_for_bz(release, &product, &version);
            query = strbuf_append_strf(query, "release=f%s&", version);
        }
    }

    if (argv[optind])
        query = strbuf_append_strf(query, "package=%s&", argv[optind]);

    if (query->buf[query->len - 1] == '&')
        query->buf[query->len - 1] = '\0';

    GList *update_list = bodhi_query_list(query->buf, release);
    strbuf_free(query);

    if (!update_list)
        return 0;

    struct strbuf *q = strbuf_new();
    for (GList *l = update_list; l; l = l->next)
    {
        struct bodhi *b = (struct bodhi *) l->data;
        for (GList *nvr = b->nvr, *name = b->name;
             nvr && name;
             nvr = nvr->next, name = name->next)
        {
            char *rpm_pkg_nvr = rpm_get_nvr_by_pkg_name((char *) name->data);
            /* package is not installed on system, but have an update which fixes bug */
            if (!rpm_pkg_nvr)
            {
                strbuf_append_strf(q, " %s", (char *) nvr->data);
                continue;
            }

            if (rpmvercmp((char *)nvr->data, rpm_pkg_nvr) <= 0)
                continue;

            strbuf_append_strf(q, " %s", (char *) nvr->data);
        }
    }

    if (!q->len)
        return 0;

    strbuf_prepend_str(q, _("Abrt found a new update which fix your problem. Please run "
                            "before submitting bug: pkcon update --repo-enable=fedora --repo"
                            "-repo=updates-testing"));

    strbuf_append_str(q, _(". Do you want to continue with reporting bug?"));
    return !ask_yes_no(q->buf);
}
