/*
    Copyright (C) ABRT Team
    Copyright (C) RedHat inc.

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

#include <gtk/gtk.h>
#include "internal_libreport_gtk.h"
#include "search_item.h"

search_item_t *sitem_new(int page,
                         GtkTextBuffer *buffer,
                         GtkTextView *tev,
                         GtkTextIter start,
                         GtkTextIter end
                         )
{
    search_item_t *word = g_new(search_item_t, 1);
    word->start = start;
    word->end = end;
    word->buffer = buffer;
    word->tev = tev;
    word->page = page;

    return word;
}

int sitem_compare(const search_item_t *item1, const search_item_t *item2)
{
    return gtk_text_iter_compare(&(item1->start), &(item2->start));
}

int sitem_get_start_offset(const search_item_t *item)
{
    if (item)
        return gtk_text_iter_get_offset(&(item->start));

    return -1;
}

int sitem_get_end_offset(const search_item_t *item)
{
    if (item)
        return gtk_text_iter_get_offset(&(item->end));

    return -1;
}

GtkTextIter *sitem_get_start_iter(search_item_t *item)
{
    if (item)
        return &(item->start);

    return NULL;
}

GtkTextIter *sitem_get_end_iter(search_item_t *item)
{
    if (item)
        return &(item->end);

    return NULL;
}

/* checks whether parent contains subitem
 * this is not part
 * return 0 if parent contains subitem
*/
gint sitem_contains(const search_item_t *parent, const search_item_t *subitem)
{
    return !(sitem_get_start_offset(subitem) >= sitem_get_start_offset(parent)
             && sitem_get_end_offset(subitem) <= sitem_get_end_offset(parent));
}
