#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <internal_libreport_gtk.h>


static char *find_end(const char* url_start)
{
    static const char *known_url_ends = ",'\"\t\n ";

    char *url_end = strchrnul(url_start, *known_url_ends);
    const char *curr_end = known_url_ends;
    while(*(++curr_end) != '\0')
    {
        char *tmp = strchrnul(url_start, *curr_end);
        if (tmp < url_end)
            url_end = tmp;
    }
    return url_end;
}

/* Returns a list of pointers to url token begining and it's lenght
 * This implementation parses the following line:
 *   https://partner-bugzilla.redhat.com/ftp://ftp.kernel.org/http://bugzilla.redhat.com/http://google.com/https://gmail.com/
 * to the following tokens:
 *   https://partner-bugzilla.redhat.com/
 *   ftp://ftp.kernel.org/
 *   http://bugzilla.redhat.com/
 *   http://google.com/
 *   https://gmail.com/
 */
GList *libreport_find_url_tokens(const char *line)
{
    static const char *const known_url_prefixes[] = {"http://", "https://", "ftp://", "file://", NULL};
    const char *const *pfx = known_url_prefixes;
    GList *tokens = NULL;
    while (*pfx != NULL)
    {
        const char *cur_pos = line;
        const char *url_start;
        while ((url_start = strstr(cur_pos, *pfx)) != NULL)
        {
            char *url_end = find_end(url_start);
            int len = url_end - url_start;

            GList *anc = tokens;
            for (; anc; anc = g_list_next(anc))
            {
                const struct libreport_url_token *const t = (struct libreport_url_token *)anc->data;
                if (t->start >= url_start)
                    break;
            }

            /* need it for overlap correction */
            GList *prev = NULL;
            /* initialize it after overlap correction */
            struct libreport_url_token *tok = xmalloc(sizeof(*tok));
            if (anc)
            {   /* insert a new token before token following in the str*/
                prev = g_list_previous(anc);

                struct libreport_url_token *following = anc->data;
                if (url_end > following->start)
                    /* correct ovrelaps with following token */
                    len -= url_end - following->start;

                GList *new = g_list_prepend(anc, tok);
                /* a new token is to become head of the list*/
                if (anc == tokens)
                    tokens = new;
            }
            else
            {   /* append a new token to the end of list */
                prev = g_list_last(tokens);
                tokens = g_list_append(tokens, tok);
            }

            if (prev)
            {   /* correct overlaps with previous token */
                struct libreport_url_token *previous = prev->data;
                const char *prev_end = previous->start + previous->len;

                if (prev_end > url_start)
                    previous->len -= prev_end - url_start;
            }

            tok->start = url_start;
            tok->len = len;

            /* move right behind the current prefix */
            cur_pos = url_start + strlen(*pfx);
        }
        pfx++;
    }

    return tokens;
}

char *tag_url(const char *line, const char *prefix)
{
    struct strbuf *result = strbuf_new();
    const char *last = line;
    GList *urls = libreport_find_url_tokens(line);
    for (GList *u = urls; u; u = g_list_next(u))
    {
        const struct libreport_url_token *const t = (struct libreport_url_token *)u->data;

        /* add text between hyperlinks */
        if (last < t->start)
            /* TODO : add strbuf_append_strn() */
            strbuf_append_strf(result, "%.*s", t->start - last, last);

        strbuf_append_strf(result, "%s<a href=\"%.*s\">%.*s</a>",
                                   prefix,
                                   t->len, t->start,
                                   t->len, t->start);

        last = t->start + t->len;
    }

    g_list_free_full(urls, g_free);

    /* add a text following the last link */
    if (last[0] != '\0')
        strbuf_append_str(result, last);

    return strbuf_free_nobuf(result);
}
