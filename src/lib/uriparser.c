/*
    Copyright (C) 2015  ABRT team
    Copyright (C) 2015  RedHat Inc

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

#include <regex.h>

int libreport_uri_userinfo_remove(const char *uri, char **result, char **scheme, char **hostname, char **username, char **password, char **location)
{
    /* https://www.ietf.org/rfc/rfc3986.txt
     * Appendix B.  Parsing a URI Reference with a Regular Expression
     *
     * scheme    = $2
     * authority = $4
     * location  = $5 <- introduced by jfilak
     * path      = $6
     * query     = $8
     * fragment  = $10
     *                         12            3  4          56       7   8        9 10 */
    const char *rfc3986_rx = "^(([^:/?#]+):)?(//([^/?#]*))?(([^?#]*)(\\?([^#]*))?(#(.*))?)$";
    regex_t re;
    int r = regcomp(&re, rfc3986_rx, REG_EXTENDED);
    assert(r == 0 || !"BUG: invalid regular expression");

    regmatch_t matchptr[10];
    r = regexec(&re, uri, ARRAY_SIZE(matchptr), matchptr, 0);
    if (r != 0)
    {
        log_debug("URI does not match RFC3986 regular expression.");
        return -EINVAL;
    }

    char *ptr = libreport_xzalloc((strlen(uri) + 1) * sizeof(char));
    *result = ptr;
    if (scheme != NULL)
        *scheme = NULL;
    if (hostname != NULL)
        *hostname = NULL;
    if (username != NULL)
        *username = NULL;
    if (password != NULL)
        *password = NULL;
    if (location != NULL)
        *location= NULL;

    /* https://www.ietf.org/rfc/rfc3986.txt
     * 5.3.  Component Recomposition
     *
      result = ""

      if defined(scheme) then
         append scheme to result;
         append ":" to result;
      endif;

      if defined(authority) then
         append "//" to result;
         append authority to result;
      endif;

      append path to result;

      if defined(query) then
         append "?" to result;
         append query to result;
      endif;

      if defined(fragment) then
         append "#" to result;
         append fragment to result;
      endif;

      return result;
    */

#define APPEND_MATCH(i, output) \
    if (matchptr[(i)].rm_so != -1) \
    { \
        size_t len = 0; \
        len = matchptr[(i)].rm_eo - matchptr[(i)].rm_so; \
        if (output) *output = g_strndup(uri + matchptr[(i)].rm_so, len); \
        strncpy(ptr, uri + matchptr[(i)].rm_so, len); \
        ptr += len; \
    }

    /* Append "scheme:" if defined */
    APPEND_MATCH(1, scheme);

    /* If authority is defined, append "//" */
    regmatch_t *match_authority = matchptr + 3;
    if (match_authority->rm_so != -1)
    {
        strcat(ptr, "//");
        ptr += 2;
    }

    ++match_authority;
    /* If authority has address part, remove userinfo and add the address */
    if (match_authority->rm_so != -1)
    {
        size_t len = match_authority->rm_eo - match_authority->rm_so;
        const char *authority = uri + match_authority->rm_so;

        /* Find the last '@'. Just for the case some used @ in username or
         * password */
        size_t at = len;
        while (at != 0)
        {
            if (authority[--at] != '@')
                continue;

            /* Find the first ':' before @. There should not be more ':' but this
             * is the most secure way -> avoid leaking an excerpt of a password
             * containing ':'.*/
            size_t colon = 0;
            while (colon < at)
            {
                if (authority[colon] != ':')
                {
                    ++colon;
                    continue;
                }

                if (password != NULL)
                    *password = g_strndup(authority + colon + 1, at - colon - 1);

                break;
            }

            if (username != NULL)
                *username = g_strndup(authority, colon);

            ++at;
            break;
        }

        len -= at;

        if (hostname != NULL)
            *hostname = g_strndup(authority + at, len);

        strncpy(ptr, authority + at, len);
        ptr += len;
    }

    /* Append path, query and fragment or "" */
    APPEND_MATCH(5, location);

    return 0;
}
