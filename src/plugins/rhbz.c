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

/* Bugzilla API doc:
 * http://www.bugzilla.org/docs/4.2/en/html/api/Bugzilla/WebService.html
 *
 * To make libxmlrpc print debug info to stdout (stderr?),
 * export XMLRPC_TRACE_XML=1
 */

#include "internal_libreport.h"
#include "rhbz.h"

#define MAX_HOPS            5
#define MAX_SUMMARY_LENGTH  255


//#define DEBUG
#ifdef DEBUG
#define func_entry() log_warning("-- %s", __func__)
#define func_entry_str(x) log_warning("-- %s\t%s", __func__, (x))
#else
#define func_entry()
#define func_entry_str(x)
#endif

struct bug_info *new_bug_info()
{
    func_entry();

    struct bug_info *bi = g_new0(struct bug_info, 1);
    bi->bi_dup_id = -1;

    return bi;
}

void free_bug_info(struct bug_info *bi)
{
    func_entry();

    if (!bi)
        return;

    free(bi->bi_status);
    free(bi->bi_resolution);
    free(bi->bi_reporter);
    free(bi->bi_product);

    libreport_list_free_with_free(bi->bi_cc_list);

    free(bi);
}

static GList *rhbz_comments(struct abrt_xmlrpc *ax, int bug_id)
{
    func_entry();

    /* http://www.bugzilla.org/docs/4.2/en/html/api/Bugzilla/WebService/Bug.html#comments */
    /*
     * <methodResponse>
     *   <params><param>
     *     <value><struct>
     *       <member><name>bugs</name>
     *       <value><struct>
     *         <member><name>BUG_ID</name>
     *         <value><struct>
     *           <member><name>comments</name>
     *           <value><array>
     * ...
     */
    xmlrpc_value *xml_response = abrt_xmlrpc_call(ax, "Bug.comments", "{s:(i)}",
                                                                      "ids", bug_id);
    /* bugs
     *     This is used for bugs specified in ids. This is a hash, where the
     *     keys are the numeric ids of the bugs, and the value is a hash with a
     *     single key, comments, which is an array of comments.
     */
    xmlrpc_value *bugs_memb = rhbz_get_member("bugs", xml_response);

    /* Get hash value assigned to bug_id key */
    g_autofree char *item = g_strdup_printf("%d", bug_id);
    xmlrpc_value *item_memb = rhbz_get_member(item, bugs_memb);

    /* Get array of comments */
    xmlrpc_value *comments_memb = rhbz_get_member("comments", item_memb);

    xmlrpc_env env;
    xmlrpc_env_init(&env);

    int comments_memb_size = rhbz_array_size(comments_memb);

    GList *comments = NULL;
    for (int i = 0; i < comments_memb_size; ++i)
    {
        xmlrpc_value* item = NULL;
        xmlrpc_array_read_item(&env, comments_memb, i, &item);
        if (env.fault_occurred)
            abrt_xmlrpc_die(&env);

        char *comment_body = rhbz_bug_read_item("text", item, RHBZ_READ_STR);
        /* attachments are sometimes without comments -- skip them */
        if (comment_body)
            comments = g_list_prepend(comments, comment_body);

        xmlrpc_DECREF(item);
    }

    xmlrpc_env_clean(&env);

    xmlrpc_DECREF(comments_memb);
    xmlrpc_DECREF(item_memb);
    xmlrpc_DECREF(bugs_memb);
    xmlrpc_DECREF(xml_response);

    return g_list_reverse(comments);
}

static unsigned find_best_bt_rating_in_comments(GList *comments)
{
    func_entry();

    if (!comments)
        return 0;

    int best_bt_rating = 0;

    for (GList *l = comments; l; l = l->next)
    {
        char *comment_body = (char *) l->data;

        char *start_rating_line = strstr(comment_body, FILENAME_RATING": ");
        if (!start_rating_line)
        {
            log_debug("comment does not contain rating");
            continue;
        }

        start_rating_line += strlen(FILENAME_RATING": ");

        errno = 0;
        char *e;
        long rating = strtoul(start_rating_line, &e, 10);
        /*
         * Note: we intentionally check for '\n'. Any other terminator
         * (even '\0') is not ok in this case.
         */
        if (errno || e == start_rating_line || *e != '\n' || (unsigned long)rating > UINT_MAX)
        {
            /* error / no digits / illegal trailing chars */
            continue;
        }

        if (rating > best_bt_rating)
            best_bt_rating = rating;
    }

    return best_bt_rating;
}

bool rhbz_login(struct abrt_xmlrpc *ax, const char *login, const char *password)
{
    func_entry();

    xmlrpc_env env;
    xmlrpc_value *result = abrt_xmlrpc_call_full(&env, ax, "User.login", "{s:s,s:s}",
                                                 "login", login, "password", password);

    if (env.fault_occurred)
    {
        /* 300 (Invalid Username or Password) - https://wiki.mozilla.org/Bugzilla:WebServices:Errors */
        if (env.fault_code != 300)
            abrt_xmlrpc_die(&env);

        log_notice("xmlrpc fault: (%d) %s", env.fault_code, env.fault_string);
        return false;
    }

    g_autofree char *token = rhbz_bug_read_item("token", result, RHBZ_READ_STR);
    if (token != NULL)
    {
        log_debug("Adding session param Bugzilla_token");
        abrt_xmlrpc_client_add_session_param_string(&env, ax, "Bugzilla_token", token);
    }

//TODO: with URL like http://bugzilla.redhat.com (that is, with http: instead of https:)
//we are getting this error:
//Logging into Bugzilla at http://bugzilla.redhat.com
//Can't login. Server said: HTTP response code is 301, not 200
//But this is a 301 redirect! We _can_ follow it if we configure curl to understand that!
    xmlrpc_DECREF(result);
    return true;
}

xmlrpc_value *rhbz_get_member(const char *member, xmlrpc_value *xml)
{
    func_entry_str(member);

    xmlrpc_env env;
    xmlrpc_env_init(&env);

    xmlrpc_value *value = NULL;
    /* The xmlrpc_struct_find_value functions consider "not found" to be
     * a normal result. If a member of the structure with the specified key
     * exists, it returns it as a handle to an xmlrpc_value. If not, it returns
     * NULL in place of that handle.
     */
    xmlrpc_struct_find_value(&env, xml, member, &value);
    if (env.fault_occurred)
        abrt_xmlrpc_error(&env);

    return value;
}

/* The only way this can fail is if arrayP is not actually an array XML-RPC
 * value. So it is usually not worth checking *envP.
 * die or return size of array
 */
unsigned rhbz_array_size(xmlrpc_value *xml)
{
    func_entry();

    xmlrpc_env env;
    xmlrpc_env_init(&env);

    unsigned size = xmlrpc_array_size(&env, xml);
    if (env.fault_occurred)
        abrt_xmlrpc_die(&env);

    return size;
}

xmlrpc_value *rhbz_array_item_at(xmlrpc_value *xml, int pos)
{
    func_entry();

    xmlrpc_env env;
    xmlrpc_env_init(&env);

    xmlrpc_value* item = NULL;
    xmlrpc_array_read_item(&env, xml, pos, &item);
    if (env.fault_occurred)
        abrt_xmlrpc_die(&env);

    xmlrpc_env_clean(&env);

    return item;
}

unsigned rhbz_version(struct abrt_xmlrpc *ax)
{
    func_entry();

    xmlrpc_value *result;
    result = abrt_xmlrpc_call(ax, "Bugzilla.version", "{}");
    g_autofree char *version = NULL;
    if (result)
        version = rhbz_bug_read_item("version", result, RHBZ_READ_STR);
    if (!result || !version)
        error_msg_and_die("Can't determine %s", "Bugzilla.version");
    xmlrpc_DECREF(result);

    strchrnul(version, '-')[0] = '\0';

    /* format must be x.y.z */
    char *vp;
    int i = 0, v[3] = {0, 0, 0};

    for (vp = version; i < 3; vp = NULL, ++i)
    {
        char *tok = strtok(vp, ".");
        if (!tok)
            break;

        v[i] = strtoul(tok, NULL, 10);
    }

    return BUGZILLA_VERSION(v[0], v[1], v[2]);
}

/* die or return bug id; each bug must have bug id otherwise xml is corrupted */
int rhbz_get_bug_id_from_array0(xmlrpc_value* xml, unsigned ver)
{
    func_entry();

    xmlrpc_env env;
    xmlrpc_env_init(&env);

    xmlrpc_value *item = NULL;
    xmlrpc_array_read_item(&env, xml, 0, &item);
    if (env.fault_occurred)
        abrt_xmlrpc_die(&env);

    const char *id;
    if (ver >= BUGZILLA_VERSION(4,2,0))
        id = "id";
    else
        id = "bug_id";

    xmlrpc_value *bug;
    bug = rhbz_get_member(id, item);
    xmlrpc_DECREF(item);
    if (!bug)
        error_msg_and_die("Can't get member '%s' from bug data", id);

    int bug_id = -1;
    xmlrpc_read_int(&env, bug, &bug_id);
    xmlrpc_DECREF(bug);
    if (env.fault_occurred)
        abrt_xmlrpc_die(&env);

    log_debug("found bug_id %i", bug_id);
    return bug_id;
}

/* die when mandatory value is missing (set flag RHBZ_MANDATORY_MEMB)
 * or return appropriate string or NULL when fail;
 */
// TODO: npajkovs: add flag to read xmlrpc_read_array_item first
void *rhbz_bug_read_item(const char *memb, xmlrpc_value *xml, int flags)
{
    func_entry();

    xmlrpc_env env;
    xmlrpc_env_init(&env);

    xmlrpc_value *member = rhbz_get_member(memb, xml);

    const char *string = NULL;

    if (!member)
        goto die;

    if (IS_READ_STR(flags))
    {
        xmlrpc_read_string(&env, member, &string);
        xmlrpc_DECREF(member);
        if (env.fault_occurred)
            abrt_xmlrpc_die(&env);

        if (!*string)
            goto die;

        log_debug("found %s: '%s'", memb, string);
        return (void*)string;
    }

    if (IS_READ_INT(flags))
    {
        int *integer = g_malloc(sizeof(int));
        xmlrpc_read_int(&env, member, integer);
        xmlrpc_DECREF(member);
        if (env.fault_occurred)
            abrt_xmlrpc_die(&env);

        log_debug("found %s: '%i'", memb, *integer);
        return (void*)integer;
    }
die:
    free((void*)string);
    if (IS_MANDATORY(flags))
        error_msg_and_die(_("Looks like corrupted xml response, because '%s'"
                            " member is missing."), memb);

    return NULL;
}

GList *rhbz_bug_cc(xmlrpc_value* result_xml)
{
    func_entry();

    xmlrpc_env env;
    xmlrpc_env_init(&env);

    xmlrpc_value* cc_member = rhbz_get_member("cc", result_xml);
    if (!cc_member)
        return NULL;

    unsigned array_size = rhbz_array_size(cc_member);

    log_debug("count members on cc %i", array_size);
    GList *cc_list = NULL;

    for (unsigned i = 0; i < array_size; ++i)
    {
        xmlrpc_value* item = NULL;
        xmlrpc_array_read_item(&env, cc_member, i, &item);
        if (env.fault_occurred)
            abrt_xmlrpc_die(&env);

        if (!item)
            continue;

        g_autofree const char* cc = NULL;
        xmlrpc_read_string(&env, item, &cc);
        xmlrpc_DECREF(item);
        if (env.fault_occurred)
            abrt_xmlrpc_die(&env);

        if (*cc != '\0')
        {
            cc_list = g_list_append(cc_list, (char*)cc);
            log_debug("member on cc is %s", cc);
            continue;
        }
    }
    xmlrpc_DECREF(cc_member);
    return cc_list;
}

struct bug_info *rhbz_bug_info(struct abrt_xmlrpc *ax, int bug_id)
{
    func_entry();

    struct bug_info *bz = new_bug_info();

    /* http://www.bugzilla.org/docs/4.2/en/html/api/Bugzilla/WebService/Bug.html#get
     *
     * <methodResponse>
     * <params>
     *   <param><value><struct>
     *     <member><name>faults</name><value><array><data/></array></value></member>
     *     <member><name>bugs</name>
     *        <value><array><data>
     *        ...
     */
    xmlrpc_value *xml_bug_response = abrt_xmlrpc_call(ax, "Bug.get", "{s:(i)}",
                                                          "ids", bug_id);

    xmlrpc_value *bugs_memb = rhbz_get_member("bugs", xml_bug_response);
    xmlrpc_value *bug_item = rhbz_array_item_at(bugs_memb, 0);

    g_autofree int *ret = (int*)rhbz_bug_read_item("id", bug_item,
                                        RHBZ_MANDATORY_MEMB | RHBZ_READ_INT);
    bz->bi_id = *ret;
    bz->bi_product = rhbz_bug_read_item("product", bug_item,
                                        RHBZ_MANDATORY_MEMB | RHBZ_READ_STR);
    bz->bi_reporter = rhbz_bug_read_item("creator", bug_item,
                                         RHBZ_MANDATORY_MEMB | RHBZ_READ_STR);
    bz->bi_status = rhbz_bug_read_item("status", bug_item,
                                       RHBZ_MANDATORY_MEMB | RHBZ_READ_STR);
    bz->bi_resolution = rhbz_bug_read_item("resolution", bug_item,
                                           RHBZ_READ_STR);
    bz->bi_platform = rhbz_bug_read_item("platform", bug_item,
                                           RHBZ_READ_STR);

    if (strcmp(bz->bi_status, "CLOSED") == 0 && !bz->bi_resolution)
        error_msg_and_die(_("Bug %i is CLOSED, but it has no RESOLUTION"), bz->bi_id);

    ret = (int*)rhbz_bug_read_item("dupe_of", bug_item,
                                   RHBZ_READ_INT);
    if (strcmp(bz->bi_status, "CLOSED") == 0
        && strcmp(bz->bi_resolution, "DUPLICATE") == 0
        && !ret)
    {
        error_msg_and_die(_("Bug %i is CLOSED as DUPLICATE, but it has no DUP_ID"),
                            bz->bi_id);
    }

    bz->bi_dup_id = (ret) ? *ret: -1;

    bz->bi_cc_list = rhbz_bug_cc(bug_item);

    bz->bi_comments = rhbz_comments(ax, bug_id);
    bz->bi_best_bt_rating = find_best_bt_rating_in_comments(bz->bi_comments);

    xmlrpc_DECREF(bugs_memb);
    xmlrpc_DECREF(bug_item);
    xmlrpc_DECREF(xml_bug_response);

    return bz;
}

int rhbz_new_bug(struct abrt_xmlrpc *ax,
                problem_data_t *problem_data,
                const char *product,
                const char *version,
                const char *bzsummary,
                const char *bzcomment,
                bool private,
                GList *group)
{
    func_entry();

    if (group)
        log_debug("# of groups %d", g_list_length(group));

    const char *component    = problem_data_get_content_or_NULL(problem_data,
                                                                FILENAME_COMPONENT);
    const char *arch         = problem_data_get_content_or_NULL(problem_data,
                                                                FILENAME_ARCHITECTURE);
    const char *duphash      = problem_data_get_content_or_NULL(problem_data,
                                                                FILENAME_DUPHASH);
//COMPAT, remove after 2.1 release
    if (!duphash) duphash    = problem_data_get_content_or_NULL(problem_data,
                                                                "global_uuid");

    g_autofree char *summary = libreport_shorten_string_to_length(bzsummary, MAX_SUMMARY_LENGTH);

    GString *status_whiteboard = g_string_new(NULL);
    g_string_append_printf(status_whiteboard, "abrt_hash:%s;", duphash);

    {   /* Add fields from /etc/os-release to Whiteboard for simple metrics. */
        map_string_t *osinfo = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
        problem_data_get_osinfo(problem_data, osinfo);

        /* This is the highest abstraction level I am willing to introduce now.
         *
         * The lines below can be either reduced to the body of the for loop
         * or the opts variable can be dynamically initialized
         * or you can simply add an another /etc/os-release option name
         * (e.g. BUILD_ID).
         */
        const char *const opts[] = { "VARIANT_ID", NULL };
        for (const char *const *iter = opts; *iter != NULL; ++iter)
        {
            const char *v = g_hash_table_lookup(osinfo, *iter);
            if (v != NULL)
            {
                /* semi-colon (;) is the delimiter because /etc/os-release *_ID
                 * options does not permit the ';' character in values
                 */
                g_string_append_printf(status_whiteboard, "%s=%s;", *iter, v);
            }
        }

        if (osinfo)
            g_hash_table_destroy(osinfo);
    }

    xmlrpc_env env;
    xmlrpc_env_init(&env);

    xmlrpc_value *params = abrt_xmlrpc_params_new(&env);

    abrt_xmlrpc_params_add_string(&env, params, "product", product);
    abrt_xmlrpc_params_add_string(&env, params, "component", component);
    abrt_xmlrpc_params_add_string(&env, params, "version", version);
    abrt_xmlrpc_params_add_string(&env, params, "summary", summary);
    abrt_xmlrpc_params_add_string(&env, params, "description", bzcomment);
    abrt_xmlrpc_params_add_string(&env, params, "status_whiteboard", status_whiteboard->str);

    if(arch)
        abrt_xmlrpc_params_add_string(&env, params, "platform", arch);

    if (private)
    {
        if (group)
        {
            xmlrpc_value *xmlrpc_groups = abrt_xmlrpc_array_new(&env);

            for (GList *l = group; l; l = l->next)
                abrt_xmlrpc_array_append_string(&env, xmlrpc_groups, l->data);

            abrt_xmlrpc_params_add_array(&env, params, "groups", xmlrpc_groups);

            xmlrpc_DECREF(xmlrpc_groups);
        }
        else
        {
            error_msg(_("A private ticket creation has been requested, but no groups were specified, please see https://github.com/abrt/abrt/wiki/FAQ#creating-private-bugzilla-tickets for more info"));
            return -1;
        }
    }

    xmlrpc_value* result = abrt_xmlrpc_call_params(&env, ax, "Bug.create", params);

    xmlrpc_DECREF(params);
    xmlrpc_env_clean(&env);

    g_string_free(status_whiteboard, TRUE);

    if (!result)
        return -1;

    g_autofree int *r = rhbz_bug_read_item("id", result, RHBZ_MANDATORY_MEMB | RHBZ_READ_INT);
    xmlrpc_DECREF(result);
    int new_bug_id = *r;

    log_warning(_("New bug id: %i"), new_bug_id);
    return new_bug_id;
}

/* suppress mail notify by {s:i} (minor_update:1) (driven by flag) */
int rhbz_attach_blob(struct abrt_xmlrpc *ax, const char *bug_id,
                const char *filename, const char *data, int data_len, int flags)
{
    func_entry();

    if (0 == data_len)
    {
        log_notice("not attaching an empty file: '%s'", filename);
        /* Return SUCCESS */
        return 0;
    }

    g_autofree char *fn = g_strdup_printf("File: %s", filename);
    xmlrpc_value* result;
    int minor_update = !!IS_MINOR_UPDATE(flags);

    /* http://www.bugzilla.org/docs/4.2/en/html/api/Bugzilla/WebService/Bug.html#add_attachment
     *
     * XMLRPC format options:
     *   s -> string,  single argument (char* value)
     *   i -> integer, single argument (int value)
     *   6 -> base64,  two arguments (char* plain data which will be encoded by xmlrpc-c to base64,
     *                                size_t number of bytes to encode)
     */
    result = abrt_xmlrpc_call(ax, "Bug.add_attachment", "{s:(s),s:s,s:s,s:s,s:6,s:i}",
                "ids", bug_id,
                "summary", fn,
                "file_name", filename,
                "content_type", (flags & RHBZ_BINARY_ATTACHMENT) ? "application/octet-stream" : "text/plain",
                /* base64 type requires two arguments: char* (plain data) and size_t (length)
                 * ! xmlrpc-c takes care about encoding to base64 !
                 */
                "data", data, (size_t)data_len,

                /* If set to true, this is considered a minor update and no mail is sent to users who do not want
                 * minor update emails. If current user is not in the minor_update_group, this parameter is simply
                 * ignored.
                 */
                "minor_update", minor_update
    );

    if (!result)
        return -1;

    xmlrpc_DECREF(result);

    return 0;
}

int rhbz_attach_fd(struct abrt_xmlrpc *ax, const char *bug_id,
                const char *att_name, int fd, int flags)
{
    func_entry();

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0)
    {
        perror_msg("Can't lseek '%s'", att_name);
        return -1;
    }

    /* bugzilla limit is 20MB
     * attaching more then bugzilla's limit could cause that xmlrpc-c fails
     * somewhere inside itself.
     * https://bugzilla.redhat.com/show_bug.cgi?id=741980
     */
    if (size >= (20 * 1024 * 1024))
    {
        error_msg("Can't upload '%s', it's too large (%llu bytes)", att_name, (long long)size);
        return -1;
    }
    lseek(fd, 0, SEEK_SET);

//TODO: need to have a method of attaching huge files (IOW: 1Gb read isn't good).

    g_autofree char *data = g_malloc(size);
    ssize_t r = libreport_full_read(fd, data, size);
    if (r < 0)
    {
        perror_msg("Can't read '%s'", att_name);
        return -1;
    }

    int res = rhbz_attach_blob(ax, bug_id, att_name, data, size, flags);
    return res;
}

void rhbz_logout(struct abrt_xmlrpc *ax)
{
    func_entry();

    xmlrpc_env env;
    xmlrpc_value *result = abrt_xmlrpc_call_full(&env, ax, "User.logout", "{}");

    if (env.fault_occurred)
        log_warning("xmlrpc fault: (%d) %s", env.fault_code, env.fault_string);

    if (result)
        xmlrpc_DECREF(result);
}

struct bug_info *rhbz_find_origin_bug_closed_duplicate(struct abrt_xmlrpc *ax,
                                                       struct bug_info *bi)
{
    func_entry();

    struct bug_info *bi_tmp = new_bug_info();
    bi_tmp->bi_id = bi->bi_id;
    bi_tmp->bi_dup_id = bi->bi_dup_id;

    for (int ii = 0; ii <= MAX_HOPS; ii++)
    {
        if (ii == MAX_HOPS)
            error_msg_and_die(_("Bugzilla couldn't find parent of bug %d"), bi->bi_id);

        log_warning("Bug %d is a duplicate, using parent bug %d", bi_tmp->bi_id, bi_tmp->bi_dup_id);
        int bug_id = bi_tmp->bi_dup_id;

        free_bug_info(bi_tmp);
        bi_tmp = rhbz_bug_info(ax, bug_id);

        // found a bug which is not CLOSED as DUPLICATE
        if (bi_tmp->bi_dup_id == -1)
            break;
    }

    return bi_tmp;
}

/* suppress mail notify by {s:i} (minor_update:1) */
void rhbz_mail_to_cc(struct abrt_xmlrpc *ax, int bug_id, const char *mail, int flags)
{
    func_entry();

    xmlrpc_value *result;
    int minor_update = !!IS_MINOR_UPDATE(flags);
#if 0 /* Obsolete API */
    result = abrt_xmlrpc_call(ax, "Bug.update", "({s:i,s:{s:(s),s:i}})",
                              "ids", bug_id,
                              "updates", "add_cc", mail,
                                         "minor_update", minor_update
    );
#endif
    /* Bugzilla 4.0+ uses this API: */
    result = abrt_xmlrpc_call(ax, "Bug.update", "{s:i,s:{s:(s),s:i}}",
                              "ids", bug_id,
                              "cc", "add", mail,
                                    "minor_update", minor_update
    );
    if (result)
        xmlrpc_DECREF(result);

    /* TODO: check that result does indicate that CC was updated.
     * The structure I see from Bugzilla 4.2:
     * <struct>
     * <member><name>bugs</name><value>
     *   <array><data><value><struct>
     *     <member><name>changes</name><value><struct>
     *       <member><name>cc</name><value><struct>
     *         <member><name>removed</name><value><string /></value></member>
     *         <member><name>added</name><value><string>USER@HOST.COM</string></value></member>
     *         </struct></value></member>
     *       </struct></value></member>
     *     <member><name>last_change_time</name><value><dateTime.iso8601>20120828T11:09:04</dateTime.iso8601></value></member>
     *     <member><name>id</name><value><int>NNNNNNN</int></value></member>
     *     <member><name>alias</name><value><array><data /></array></value></member>
     *     </struct></value></data>
     *   </array></value>
     * </member></struct>
     */
}

void rhbz_add_comment(struct abrt_xmlrpc *ax, int bug_id, const char *comment,
                      int flags)
{
    func_entry();

    int private = !!IS_PRIVATE(flags);
    int minor_update = !!IS_MINOR_UPDATE(flags);

    xmlrpc_value *result;
    result = abrt_xmlrpc_call(ax, "Bug.add_comment", "{s:i,s:s,s:b,s:i}",
                              "id", bug_id, "comment", comment,
                              "private", private, "minor_update", minor_update);

    if (result)
        xmlrpc_DECREF(result);
}

void rhbz_set_url(struct abrt_xmlrpc *ax, int bug_id, const char *url, int flags)
{
    func_entry();

    const int minor_update = !!IS_MINOR_UPDATE(flags);
    xmlrpc_value *result = abrt_xmlrpc_call(ax, "Bug.update", "{s:i,s:s,s:i}",
                              "ids", bug_id,
                              "url", url,

                /* If set to true, this is considered a minor update and no mail is sent to users who do not want
                 * minor update emails. If current user is not in the minor_update_group, this parameter is simply
                 * ignored.
                 */
                              "minor_update", minor_update
    );

    if (result)
        xmlrpc_DECREF(result);
}

void rhbz_close_as_duplicate(struct abrt_xmlrpc *ax, int bug_id,
                        int duplicate_bug,
                        int flags)
{
    func_entry();

    const int minor_update = !!IS_MINOR_UPDATE(flags);
    xmlrpc_value *result = abrt_xmlrpc_call(ax, "Bug.update", "{s:i,s:s,s:s,s:i,s:i}",
                              "ids", bug_id,
                              "status", "CLOSED",
                              "resolution", "DUPLICATE",
                              "dupe_of", duplicate_bug,

                /* If set to true, this is considered a minor update and no mail is sent to users who do not want
                 * minor update emails. If current user is not in the minor_update_group, this parameter is simply
                 * ignored.
                 */
                              "minor_update", minor_update
    );

    if (result)
        xmlrpc_DECREF(result);
}

xmlrpc_value *rhbz_search_duphash(struct abrt_xmlrpc *ax,
                        const char *product,
                        const char *version,
                        const char *component,
                        const char *duphash)
{
    GString *query = g_string_new(NULL);

    g_string_append_printf(query, "ALL whiteboard:\"%s\"", duphash);

    if (product)
        g_string_append_printf(query, " product:\"%s\"", product);

    if (version)
        g_string_append_printf(query, " version:\"%s\"", version);

    if (component)
        g_string_append_printf(query, " component:\"%s\"", component);

    g_autofree char *s = g_string_free(query, FALSE);
    log_debug("search for '%s'", s);
    xmlrpc_value *search = abrt_xmlrpc_call(ax, "Bug.search", "{s:s,s:(s)}", "quicksearch", s, "include_fields", "id");

    xmlrpc_value *bugs = rhbz_get_member("bugs", search);
    xmlrpc_DECREF(search);

    if (!bugs)
        error_msg_and_die(_("Bug.search(quicksearch) return value did not contain member 'bugs'"));

    return bugs;
}
