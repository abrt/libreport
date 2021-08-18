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
#include "internal_libreport.h"
#include "proxies.h"

#include <gio/gio.h>

GList *get_proxy_list(const char *url)
{
    int i;
    GList *l = NULL;
    GProxyResolver *resolver;
    g_auto(GStrv) proxies = NULL;
    g_autoptr(GError) error = NULL;

    resolver = g_proxy_resolver_get_default();

    proxies = g_proxy_resolver_lookup(resolver, url, NULL, &error);
    if (!proxies)
    {
        log_warning("Failed to perform proxy lookup for %s: %s", url, error->message);
        return NULL;
    }

    for (i = 0, l = NULL; proxies[i]; i++)
        l = g_list_append(l, g_steal_pointer(&proxies[i]));

    /* Don't set proxy if the list contains just "direct://" */
    if (l && !g_list_next(l) && !strcmp(l->data, "direct://"))
    {
        g_list_free(l);
        l = NULL;
    }

    return l;
}
