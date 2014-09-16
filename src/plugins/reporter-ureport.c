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
#include "abrt_curl.h"

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
#if ENABLE_NLS
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif

    abrt_init(argv);

    struct ureport_server_config config;
    ureport_server_config_init(&config);

    enum {
        OPT_v = 1 << 0,
        OPT_d = 1 << 1,
        OPT_u = 1 << 2,
        OPT_k = 1 << 3,
        OPT_t = 1 << 4,
        OPT_i = 1 << 5,
    };

    int ret = 1; /* "failure" (for now) */
    bool insecure = !config.ur_ssl_verify;
    const char *conf_file = UREPORT_CONF_FILE_PATH;
    const char *arg_server_url = NULL;
    const char *client_auth = NULL;
    GList *auth_items = NULL;
    const char *dump_dir_path = ".";
    const char *ureport_hash = NULL;
    bool ureport_hash_from_rt = false;
    int rhbz_bug = -1;
    bool rhbz_bug_from_rt = false;
    const char *email_address = NULL;
    bool email_address_from_env = false;
    struct dump_dir *dd = NULL;
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT__DUMP_DIR(&dump_dir_path),
        OPT_STRING('u', "url", &arg_server_url, "URL", _("Specify server URL")),
        OPT_BOOL('k', "insecure", &insecure,
                          _("Allow insecure connection to ureport server")),
        OPT_STRING('t', "auth", &client_auth, "SOURCE", _("Use client authentication")),
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
        OPT_END(),
    };

    const char *program_usage_string = _(
        "& [-v] [-c FILE] [-u URL] [-k] [-t SOURCE] [-A -a bthash -B -b bug-id -E -e email] [-d DIR]\n"
        "& [-v] [-c FILE] [-u URL] [-k] [-t SOURCE] [-i AUTH_ITEMS]\\\n"
        "  [-A -a bthash -B -b bug-id -E -e email] [-d DIR]\n"
        "\n"
        "Upload micro report or add an attachment to a micro report\n"
        "\n"
        "Reads the default configuration from "UREPORT_CONF_FILE_PATH
    );

    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);

    map_string_t *settings = new_map_string();
    load_conf_file(conf_file, settings, /*skip key w/o values:*/ false);

    ureport_server_config_load(&config, settings);

    if (opts & OPT_u)
        ureport_server_config_set_url(&config, xstrdup(arg_server_url));
    if (opts & OPT_k)
        config.ur_ssl_verify = !insecure;
    if (opts & OPT_t)
        ureport_server_config_set_client_auth(&config, client_auth);
    if (opts & OPT_i)
    {
        g_list_free_full(config.ur_prefs.urp_auth_items, free);
        config.ur_prefs.urp_auth_items = auth_items;
    }

    if (!config.ur_url)
        error_msg_and_die("You need to specify server URL");

    if (ureport_hash && ureport_hash_from_rt)
        error_msg_and_die("You need to pass either -a bthash or -A");

    if (rhbz_bug >= 0 && rhbz_bug_from_rt)
        error_msg_and_die("You need to pass either -b bug-id or -B");

    if (email_address && email_address_from_env)
        error_msg_and_die("You need to pass either -e bthash or -E");

    if (ureport_hash_from_rt || rhbz_bug_from_rt)
    {
        dd = dd_opendir(dump_dir_path, DD_OPEN_READONLY);
        if (!dd)
            xfunc_die();

        if (ureport_hash_from_rt)
        {
            report_result_t *ureport_result = find_in_reported_to(dd, "uReport");

            if (!ureport_result || !ureport_result->bthash)
                error_msg_and_die(_("This problem does not have an uReport assigned."));

            /* sorry, this will be leaked */
            ureport_hash = xstrdup(ureport_result->bthash);

            free_report_result(ureport_result);
        }

        if (rhbz_bug_from_rt)
        {
            report_result_t *bz_result = find_in_reported_to(dd, "Bugzilla");

            if (!bz_result || !bz_result->url)
                error_msg_and_die(_("This problem has not been reported to Bugzilla."));

            char *bugid_ptr = strstr(bz_result->url, "show_bug.cgi?id=");
            if (!bugid_ptr)
                error_msg_and_die(_("Unable to find bug ID in bugzilla URL '%s'"), bz_result->url);
            bugid_ptr += strlen("show_bug.cgi?id=");

            /* we're just reading int, sscanf works fine */
            if (sscanf(bugid_ptr, "%d", &rhbz_bug) != 1)
                error_msg_and_die(_("Unable to parse bug ID from bugzilla URL '%s'"), bz_result->url);

            free_report_result(bz_result);
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
        if (rhbz_bug < 0 && !email_address)
            error_msg_and_die(_("You need to specify bug ID, contact email or both"));

        if (rhbz_bug >= 0)
        {
            if (ureport_attach_int(ureport_hash, "RHBZ", rhbz_bug, &config))
                goto finalize;
        }

        if (email_address)
        {
            if (ureport_attach_string(ureport_hash, "email", email_address, &config))
                goto finalize;
        }

        ret = 0;
        goto finalize;
    }
    if (!ureport_hash && (rhbz_bug >= 0 || email_address))
        error_msg_and_die(_("You need to specify bthash of the uReport to attach."));

    char *json_ureport = ureport_from_dump_dir_ext(dump_dir_path, &(config.ur_prefs));
    if (!json_ureport)
    {
        error_msg(_("Not uploading an empty uReport"));
        goto finalize;
    }

    struct ureport_server_response *response = ureport_submit(json_ureport, &config);
    free(json_ureport);

    if (!response)
        goto finalize;

    if (!response->urr_is_error)
    {
        VERB1 log("is known: %s", response->urr_value);
        ret = 0; /* "success" */

        if (!ureport_server_response_save_in_dump_dir(response, dump_dir_path, &config))
            xfunc_die();

        /* If a reported problem is not known then emit NEEDMORE */
        if (strcmp("true", response->urr_value) == 0)
        {
            log(_("This problem has already been reported."));
            if (response->urr_message)
                log(response->urr_message);

            ret = EXIT_STOP_EVENT_RUN;
        }
    }
    else
        error_msg(_("Server responded with an error: '%s'"), response->urr_value);

    ureport_server_response_free(response);

finalize:
    if (config.ur_prefs.urp_auth_items == auth_items)
        config.ur_prefs.urp_auth_items = NULL;

    free_map_string(settings);
    ureport_server_config_destroy(&config);

    return ret;
}
