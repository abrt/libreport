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

#ifdef HAVE_PROXY
#include <proxy.h>

static pxProxyFactory *px_factory;

GList *get_proxy_list(const char *url)
{
    int i;
    GList *l;
    g_autofree char **proxies = NULL;

    if (!px_factory)
    {
        px_factory = px_proxy_factory_new();
        if (!px_factory)
            return NULL;
    }

    /* Cast to char * is needed with libproxy versions before 0.4.0 */
    proxies = px_proxy_factory_get_proxies(px_factory, (char *)url);
    if (!proxies)
        return NULL;

    for (i = 0, l = NULL; proxies[i]; i++)
        l = g_list_append(l, proxies[i]);

    /* Don't set proxy if the list contains just "direct://" */
    if (l && !g_list_next(l) && !strcmp(l->data, "direct://"))
    {
        g_free(l->data);
        g_list_free(l);
        l = NULL;
    }

    return l;
}

#else

GList *get_proxy_list(const char *url)
{
    /* Without libproxy just return an empty list */
    return NULL;
}

#endif
