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

typedef struct
{
    int page; //which tab in notepad
    GtkTextBuffer *buffer;
    GtkTextView *tev;
    GtkTextIter start;
    GtkTextIter end;
} search_item_t;

search_item_t *sitem_new(int page,
                         GtkTextBuffer *buffer,
                         GtkTextView *tev,
                         GtkTextIter start,
                         GtkTextIter end
                         );
void sitem_free(search_item_t *item);
int sitem_compare(const search_item_t *item1, const search_item_t *item2);
GtkTextIter *sitem_get_start_iter(search_item_t *item);
GtkTextIter *sitem_get_end_iter(search_item_t *item);
bool sitem_is_in_sitemlist(const search_item_t *item, GList *item_list);
