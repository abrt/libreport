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

typedef struct
{
    int page; //which tab in notepad
    GtkTextBuffer *buffer;
    GtkTextView *tev;
    GtkTextIter start;
    GtkTextIter end;
} search_item_t;

int compare_search_item(gconstpointer a, gconstpointer b)
{
    const search_item_t *lhs = a;
    const search_item_t *rhs = b;
    return gtk_text_iter_compare(&(lhs->start), &(rhs->start));
}

int sitem_get_start(search_item_t *item)
{
	if (item)
		return gtk_text_iter_get_offset(item->start);
}

int sitem_get_end(search_item_t *item)
{
	if (item)
		return gtk_text_iter_get_offset(item->end);
}

bool sitem_contains(search_item_t *parent, search_item_t *subitem)