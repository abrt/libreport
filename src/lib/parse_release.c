/*
    Copyright (C) 2010  ABRT team
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

/* Houston, we have a problem.
 *
 * Mapping from OS release string to (product,version) string tuple
 * in different bug databases is non-trivial, and differs from DB to DB.
 * OTOH, it does have a lot of similarities too, making it worthwhile
 * to use a common function for parsing.
 *
 * To accomodate this, parse_release() takes a flag parameter
 * with two types of bits:
 *
 * - some bits indicate a specific tweak to parsing algorithm (such as
 *   "remove everything after numeric version").
 *
 * - if there is a bug DB which requires a quirk not easily describable
 *   by a combination of aforementioned bits, it gets its own bit
 *   "parse OS release for bug database XYZ" (so far we have none).
 *
 * In order to not torture API users with the need to know these bits,
 * we have convenience functions parse_release_for_XYZ()
 * which call parse_release() with correct bits.
 */
enum {
    APPEND_MAJOR_VER_TO_RHEL_PRODUCT = 1 << 0,
    RETAIN_ALPHA_BETA_TAIL_IN_VER    = 1 << 1,
};

// caller is reposible for freeing *product* and *version*
static void parse_release(const char *release, char** product, char** version, int flags)
{
    /* Fedora has a single non-numeric release - Rawhide */
    if (strstr(release, "Rawhide"))
    {
        *product = xstrdup("Fedora");
        *version = xstrdup("rawhide");
        VERB3 log("%s: version:'%s' product:'%s'", __func__, *version, *product);
        return;
    }

    /* openSUSE has two non-numeric releases - Factory and Tumbleweed
       None of them is unfortunately identified in any of /etc/SuSE-brand,
       /etc/SuSE-release or /etc/os-release. Keep this piece of code commented
       just not to forget about that. */

    /*
    if (strstr(release, "Factory"))
    {
        *product = xstrdup("openSUSE");
        *version = xstrdup("Factory");
        VERB3 log("%s: version:'%s' product:'%s'", __func__, *version, *product);
        return;
    }

    if (strstr(release, "Tumbleweed"))
    {
        *product = xstrdup("openSUSE");
        *version = xstrdup("Tumbleweed");
        VERB3 log("%s: version:'%s' product:'%s'", __func__, *version, *product);
        return;
    }
    */

    bool it_is_rhel = false;

    struct strbuf *buf_product = strbuf_new();
    if (strstr(release, "Fedora"))
    {
        strbuf_append_str(buf_product, "Fedora");
    }
    else if (strstr(release, "Red Hat Enterprise Linux"))
    {
        strbuf_append_str(buf_product, "Red Hat Enterprise Linux");
        it_is_rhel = true;
    }
    else if (strstr(release, "openSUSE"))
    {
        strbuf_append_str(buf_product, "openSUSE");
    }
    else
    {
        /* TODO: add logic for parsing other distros' names here */
        strbuf_append_str(buf_product, release);
    }

    /* Examples of release strings:
     * installed system: "Red Hat Enterprise Linux Server release 6.2 Beta (Santiago)"
     * anaconda: "Red Hat Enterprise Linux 6.2"
     */
    struct strbuf *buf_version = strbuf_new();
    const char *r = strstr(release, "release");
    const char *space = r ? strchr(r, ' ') : NULL;
    if (!space)
    {
        /* Try to find "<space><digit>" sequence */
        space = release;
        while ((space = strchr(space, ' ')) != NULL)
        {
            if (space[1] >= '0' && space[1] <= '9')
                break;
            space++;
        }
    }
    if (space)
    {
        space++;
        /* Observed also: "Fedora 16-Alpha" rhbz#730887 */
        while ((*space >= '0' && *space <= '9') || *space == '.')
        {
            /* Eat string like "5.2" */
            strbuf_append_char(buf_version, *space);
            space++;
        }

        if (flags & RETAIN_ALPHA_BETA_TAIL_IN_VER)
        {
            /* Example: "... 6.2 [Beta ](Santiago)".
             * 'space' variable points to non-digit char after "2".
             * We assume that non-parenthesized text is "Alpha"/"Beta"/etc.
             * If this text is only whitespace, we won't append it.
             */
            const char *to_append = space;
            while (*space && *space != '(') /* go to '(' */
                space++;
            while (space > to_append && space[-1] == ' ') /* back to 1st non-space */
                space--;
            strbuf_append_strf(buf_version, "%.*s", (int)(space - to_append), to_append);
        }
    }

    if ((flags & APPEND_MAJOR_VER_TO_RHEL_PRODUCT) && it_is_rhel)
    {
        char *v = buf_version->buf;

        /* Append "integer part" of version to product:
         * "10.2<anything>" -> append " 10"
         * "10 <anything>"  -> append " 10"
         * "10"             -> append " 10"
         * "10abcde"        -> append ?????
         */
        unsigned idx_dot = strchrnul(v, '.') - v;
        unsigned idx_space = strchrnul(v, ' ') - v;
        strbuf_append_strf(buf_product, " %.*s",
                        (idx_dot < idx_space ? idx_dot : idx_space), v
        );
    }

    *version = strbuf_free_nobuf(buf_version);
    *product = strbuf_free_nobuf(buf_product);

    VERB3 log("%s: version:'%s' product:'%s'", __func__, *version, *product);
}

void parse_release_for_bz(const char *release, char** product, char** version)
{
    /* Fedora/RH bugzilla uses "Red Hat Enterprise Linux N" product for RHEL */
    parse_release(release, product, version, 0
        | APPEND_MAJOR_VER_TO_RHEL_PRODUCT
    );
}

/*
 * Note to developers: you can use
 * curl --silent -u $USER:$PASS "https://api.access.redhat.com/rs/products/Red%20Hat%20Enterprise%20Linux/versions" | xmllint --format -
 * to query RHTS for valid set of versions. Currently (2013-01) I get:
 * <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
 * <versions xmlns="http://www.redhat.com/gss/strata">
 *   <version>3 - ELS</version>
 *   <version>4</version>
 *   <version>4 - ELS</version>
 *   <version>5.0</version>
 *   <version>5.1</version>
 *   <version>5.2</version>
 *   <version>5.3</version>
 *   <version>5.4</version>
 *   <version>5.5</version>
 *   <version>5.6</version>
 *   <version>5.7</version>
 *   <version>5.8</version>
 *   <version>5.9</version>
 *   <version>6.0</version>
 *   <version>6.1</version>
 *   <version>6.2</version>
 *   <version>6.3</version>
 *   <version>6.4 Beta</version>
 *   <version>7.0 Alpha-2</version>
 *   <version>Unknown</version>
 * </versions>
 */
void parse_release_for_rhts(const char *release, char** product, char** version)
{
    parse_release(release, product, version, 0
        | RETAIN_ALPHA_BETA_TAIL_IN_VER
    );
}
