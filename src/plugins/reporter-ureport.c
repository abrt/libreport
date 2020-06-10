/*
    Copyright (C) 2012  ABRT Team
    Copyright (C) 2012  RedHat inc.

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

#include <json.h>
#include "internal_libreport.h"
#include "ureport.h"
#include "libreport_curl.h"

#define DEFAULT_WEB_SERVICE_URL "https://retrace.fedoraproject.org/faf"

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
#if ENABLE_NLS
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif

    abrt_init(argv);

    struct ureport_server_config config;
    libreport_ureport_server_config_init(&config);

    enum {
        OPT_v = 1 << 0,
        OPT_d = 1 << 1,
        OPT_u = 1 << 2,
        OPT_k = 1 << 3,
        OPT_t = 1 << 4,
        OPT_h = 1 << 5,
        OPT_i = 1 << 6,
    };

    int ret = 1; /* "failure" (for now) */
    int insecure = !config.ur_ssl_verify;
    const char *conf_file = UREPORT_CONF_FILE_PATH;
    const char *arg_server_url = NULL;
    const char *client_auth = NULL;
    const char *http_auth = NULL;
    GList *auth_items = NULL;
    const char *dump_dir_path = ".";
    const char *ureport_hash = NULL;
    int ureport_hash_from_rt = 0;
    int rhbz_bug = -1;
    int rhbz_bug_from_rt = 0;
    const char *email_address = NULL;
    int email_address_from_env = 0;
    char *comment = NULL;
    int comment_file = 0;
    char *attach_value = NULL;
    char *attach_value_from_rt = NULL;
    g_autofree char *attach_value_from_rt_data = NULL;
    char *report_result_type = NULL;
    char *attach_type = NULL;
    struct dump_dir *dd = NULL;
    struct options program_options[] = {
        OPT__VERBOSE(&libreport_g_verbose),
        OPT__DUMP_DIR(&dump_dir_path),
        OPT_STRING('u', "url", &arg_server_url, "URL", _("Specify server URL")),
        OPT_BOOL('k', "insecure", &insecure,
                          _("Allow insecure connection to ureport server")),
        OPT_STRING('t', "auth", &client_auth, "SOURCE", _("Use client authentication")),
        OPT_STRING('h', "http-auth", &http_auth, "CREDENTIALS", _("Use HTTP Authentication")),
        OPT_LIST('i', "auth_items", &auth_items, "AUTH_ITEMS", _("Additional files included in 'auth' key")),
        OPT_STRING('c', NULL, &conf_file, "FILE", _("Configuration file")),
        OPT_STRING('a', "attach", &ureport_hash, "BTHASH",
                          _("bthash of uReport to attach (conflicts with -A)")),
        OPT_BOOL('A', "attach-rt", &ureport_hash_from_rt,
                          _("attach to a bthash from reported_to (conflicts with -a)")),
        OPT_STRING('e', "email", &email_address, "EMAIL",
                          _("contact e-mail address (requires -a|-A, conflicts with -E)")),
        OPT_BOOL('E', "email-env", &email_address_from_env,
                          _("contact e-mail address from environment or configuration file (requires -a|-A, conflicts with -e)")),
        OPT_INTEGER('b', "bug-id", &rhbz_bug,
                          _("attach RHBZ bug (requires -a|-A, conflicts with -B)")),
        OPT_BOOL('B', "bug-id-rt", &rhbz_bug_from_rt,
                          _("attach last RHBZ bug from reported_to (requires -a|-A, conflicts with -b)")),
        OPT_STRING('o', "comment", &comment, "DESCRIPTION",
                          _("attach short text (requires -a|-A, conflicts with -D)")),
        OPT_BOOL('O', "comment-file", &comment_file,
                          _("attach short text from comment (requires -a|-A, conflicts with -d)")),

        /* va l ue */
        OPT_STRING('l', "value", &attach_value, "DATA",
                          _("attach value (requires -a|-A and -T, conflicts with -L)")),
        OPT_STRING('L', "value-rt", &attach_value_from_rt, "FIELD",
                          _("attach data of FIELD [URL] of the last report result (requires -a|-A, -r and -T, conflicts with -l)")),

        OPT_STRING('r', "report-result-type", &report_result_type, "REPORT_RESULT_TYPE",
                          _("use REPORT_RESULT_TYPE when looking for FIELD in reported_to (used only with -L)")),
        OPT_STRING('T', "type", &attach_type, "ATTACHMENT_TYPE",
                          _("attach DATA as ureport attachment ATTACHMENT_TYPE (used only with -l|-L)")),
        OPT_END(),
    };

    const char *program_usage_string = _(
        "& [-v] [-c FILE] [-u URL] [-k] [-t SOURCE] [-h CREDENTIALS]\n"
        "  [-A -a bthash -B -b bug-id -E -e email -O -o comment] [-d DIR]\n"
        "  [-A -a bthash -T ATTACHMENT_TYPE -r REPORT_RESULT_TYPE -L RESULT_FIELD] [-d DIR]\n"
        "  [-A -a bthash -T ATTACHMENT_TYPE -l DATA] [-d DIR]\n"
        "& [-v] [-c FILE] [-u URL] [-k] [-t SOURCE] [-h CREDENTIALS] [-i AUTH_ITEMS] [-d DIR]\n"
        "\n"
        "Upload micro report or add an attachment to a micro report\n"
        "\n"
        "Reads the default configuration from "UREPORT_CONF_FILE_PATH
    );

    unsigned opts = libreport_parse_opts(argc, argv, program_options, program_usage_string);

    GHashTable *settings = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
    libreport_load_conf_file(conf_file, settings, /*skip key w/o values:*/ false);

    libreport_ureport_server_config_load(&config, settings);

    if (opts & OPT_u)
        libreport_ureport_server_config_set_url(&config, g_strdup(arg_server_url));
    if (opts & OPT_k)
        config.ur_ssl_verify = !insecure;
    if (opts & OPT_t)
        libreport_ureport_server_config_set_client_auth(&config, client_auth);
    if (opts & OPT_h)
        ureport_server_config_load_basic_auth(&config, http_auth);
    if (opts & OPT_i)
    {
        g_list_free_full(config.ur_prefs.urp_auth_items, free);
        config.ur_prefs.urp_auth_items = auth_items;
    }

    if (!config.ur_url)
        libreport_ureport_server_config_set_url(&config, g_strdup(DEFAULT_WEB_SERVICE_URL));

    if (ureport_hash && ureport_hash_from_rt)
        error_msg_and_die("You need to pass either -a bthash or -A");

    if (rhbz_bug >= 0 && rhbz_bug_from_rt)
        error_msg_and_die("You need to pass either -b bug-id or -B");

    if (email_address && email_address_from_env)
        error_msg_and_die("You need to pass either -e bthash or -E");

    if (comment && comment_file)
        error_msg_and_die("You need to pass either -o comment or -O");

    if (attach_value && attach_value_from_rt)
        error_msg_and_die("You need to pass either -l url or -L");

    if ((attach_value || attach_value_from_rt) && attach_type == NULL)
        error_msg_and_die("You need to pass -T together with -l and -L");

    if (attach_value_from_rt)
    {
        if (report_result_type == NULL)
            error_msg_and_die("You need to pass -r together with -L");

        /* If you introduce a new recognized value, don't forget to update
         * the documentation and the conditions below. */
        if (strcmp(attach_value_from_rt, "URL") != 0)
            error_msg_and_die("-L accepts only 'URL'");
    }

    if (ureport_hash_from_rt || rhbz_bug_from_rt || comment_file || attach_value_from_rt)
    {
        dd = dd_opendir(dump_dir_path, DD_OPEN_READONLY);
        if (!dd)
            libreport_xfunc_die();

        if (ureport_hash_from_rt)
        {
            g_autoptr(report_result_t) result = NULL;
            char *bthash;

            result = libreport_find_in_reported_to(dd, "uReport");
            if (NULL == result)
            {
                error_msg_and_die(_("This problem does not have an uReport assigned."));
            }
            bthash = report_result_get_bthash(result);

            /* This is leaked, because string options parsed by getopt() and friends
             * return pointers to argv elements as values, so trying to free this
             * could result in universe collapsing.
             *
             * TODO: Fix this, because it’s just sloppy.
             */
            ureport_hash = bthash;
        }

        if (rhbz_bug_from_rt)
        {
            g_autoptr(report_result_t) result = NULL;
            g_autofree char *url = NULL;

            result = libreport_find_in_reported_to(dd, "Bugzilla");
            if (NULL == result)
            {
                error_msg_and_die(_("This problem has not been reported to Bugzilla."));
            }
            url = report_result_get_url(result);

            char *bugid_ptr = strstr(url, "show_bug.cgi?id=");
            if (!bugid_ptr)
                error_msg_and_die(_("Unable to find bug ID in bugzilla URL '%s'"), url);
            bugid_ptr += strlen("show_bug.cgi?id=");

            /* we're just reading int, sscanf works fine */
            if (sscanf(bugid_ptr, "%d", &rhbz_bug) != 1)
                error_msg_and_die(_("Unable to parse bug ID from bugzilla URL '%s'"), url);
        }

        if (comment_file)
        {
            comment = dd_load_text(dd, FILENAME_COMMENT);
            if (comment == NULL)
                error_msg_and_die(_("Cannot attach comment from 'comment' file"));
            if (comment[0] == '\0')
                error_msg_and_die(_("'comment' file is empty"));
        }

        if (attach_value_from_rt)
        {
            g_autoptr(report_result_t) result = NULL;
            char *url;

            result = libreport_find_in_reported_to(dd, report_result_type);
            if (NULL == result)
            {
                error_msg_and_die(_("This problem has not been reported to '%s'."), report_result_type);
            }

            url = report_result_get_url(result);
            /* If you introduce a new attach_value_from_rt recognized value,
             * this condition will become invalid. */
            if (NULL == url)
            {
                error_msg_and_die(_("The report result '%s' is missing URL."), report_result_type);
            }

            /* Avoid the need to duplicate the string. */
            attach_value = url;
            attach_value_from_rt_data = url;
        }

        dd_close(dd);
    }

    if (email_address_from_env)
    {
        UREPORT_OPTION_VALUE_FROM_CONF(settings, "ContactEmail", email_address, (const char *));

        if (!email_address)
            error_msg_and_die(_("Neither environment variable 'uReport_ContactEmail' nor configuration option 'ContactEmail' is set"));
    }

    if (ureport_hash)
    {
        if (rhbz_bug < 0 && !email_address && !comment && !attach_value)
            error_msg_and_die(_("You need to specify bug ID, contact email, comment or all of them"));

        if (rhbz_bug >= 0)
        {
            if (ureport_attach(&config, ureport_hash, "RHBZ", "%d", rhbz_bug))
                goto finalize;
        }

        if (email_address)
        {
            if (ureport_attach(&config, ureport_hash, "email", "%s", email_address))
                goto finalize;
        }

        if (comment)
        {
            if (ureport_attach(&config, ureport_hash, "comment", "%s", comment))
                goto finalize;
        }

        if (attach_value)
        {
            if (ureport_attach(&config, ureport_hash, attach_type, "%s", attach_value))
                goto finalize;
        }

        ret = 0;
        goto finalize;
    }
    if (!ureport_hash && (rhbz_bug >= 0 || email_address))
        error_msg_and_die(_("You need to specify bthash of the uReport to attach."));

    struct ureport_preferences *prefs = &(config.ur_prefs);
    prefs->urp_flags |= UREPORT_PREF_FLAG_RETURN_ON_FAILURE;

    char *json_ureport = libreport_ureport_from_dump_dir_ext(dump_dir_path, prefs);
    if (!json_ureport)
    {
        error_msg(_("Failed to generate microreport from the problem data"));
        goto finalize;
    }

    struct ureport_server_response *response = libreport_ureport_submit(json_ureport, &config);
    free(json_ureport);

    if (!response)
        goto finalize;

    if (!response->urr_is_error)
    {
        log_notice("is known: %s", response->urr_value);
        ret = 0; /* "success" */

        if (!libreport_ureport_server_response_save_in_dump_dir(response, dump_dir_path, &config))
            libreport_xfunc_die();

        /* If a reported problem is not known then emit NEEDMORE */
        if (strcmp("true", response->urr_value) == 0)
        {
            log_warning(_("This problem has already been reported."));
            if (response->urr_message)
                log_warning("%s", response->urr_message);

            ret = EXIT_STOP_EVENT_RUN;
        }
    }
    else
        error_msg(_("Server responded with an error: '%s'"), response->urr_value);

    libreport_ureport_server_response_free(response);

finalize:
    if (config.ur_prefs.urp_auth_items == auth_items)
        config.ur_prefs.urp_auth_items = NULL;

    if (settings)
        g_hash_table_destroy(settings);
    libreport_ureport_server_config_destroy(&config);

    return ret;
}
