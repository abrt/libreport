#include "internal_libreport_gtk.h"

void libreport_reload_text_to_text_view(GtkTextView *tv, const char *text)
{
    GtkTextBuffer *tb = gtk_text_view_get_buffer(tv);
    GtkTextIter beg_iter, end_iter;
    gtk_text_buffer_get_iter_at_offset(tb, &beg_iter, 0);
    gtk_text_buffer_get_iter_at_offset(tb, &end_iter, -1);
    gtk_text_buffer_delete(tb, &beg_iter, &end_iter);

    if (!text)
        return;

    const gchar *end;
    while (!g_utf8_validate(text, -1, &end))
    {
        gtk_text_buffer_insert_at_cursor(tb, text, end - text);
        char buf[8];
        unsigned len = snprintf(buf, sizeof(buf), "<%02X>", (unsigned char)*end);
        gtk_text_buffer_insert_at_cursor(tb, buf, len);
        text = end + 1;
    }

    gtk_text_buffer_insert_at_cursor(tb, text, strlen(text));
}
