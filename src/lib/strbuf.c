/*
    String buffer implementation

    Copyright (C) 2009  RedHat inc.

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

int libreport_prefixcmp(const char *str, const char *prefix)
{
    for (; ; str++, prefix++)
        if (!*prefix)
            return 0;
        else if (*str != *prefix)
            return (unsigned char)*prefix - (unsigned char)*str;
}

int libreport_suffixcmp(const char *str, const char *suffix)
{
    int len_minus_suflen = strlen(str) - strlen(suffix);
    if (len_minus_suflen < 0)
        return len_minus_suflen;
    else
        return strcmp(str + len_minus_suflen, suffix);
}

char *libreport_trim_all_whitespace(const char *str)
{
    char *trim = libreport_xzalloc(sizeof(char) * strlen(str) + 1);
    int i = 0;
    while (*str)
    {
        if (!isspace(*str))
            trim[i++] = *str;
        str++;
    }

    return trim;
}

/* If str is longer than max allowed length then
 * try to find first ' ' from the end of acceptable long str string
 *
 * If ' ' is found replace string after that by "..."
 *
 * If ' ' is NOT found in maximal allowed range, cut str string on
 * lenght (MAX_SUMMARY_LENGTH - strlen("...")) and append "..."
 *
 * If MAX_LENGTH is 15 and max allowed cut is 5:
 *
 *   0123456789ABCDEF -> 0123456789AB...
 *   0123456789 BCDEF -> 0123456789 ...
 *   012345 789ABCDEF -> 012345 789AB...
 */
char *libreport_shorten_string_to_length(const char *str, unsigned length)
{
    char *dup_str = g_strdup(str);
    if (strlen(str) > length)
    {
        char *max_end = dup_str + (length - strlen("..."));

        /* maximal number of characters to cut due to attempt cut dup_str
         * string after last ' '
         */
        int max_cut = 16;

        /* start looking for ' ' one char before the last possible character */
        char *buf = max_end - 1;
        while (buf[0] != ' ' && max_cut--)
            --buf;

        if (buf[0] != ' ')
            buf = max_end;
        else
            ++buf;

        buf[0] = '.';
        buf[1] = '.';
        buf[2] = '.';
        buf[3] = '\0';
    }

    return dup_str;
}

/*
 * Trims characters both from left and right side of a string.
 * Modifies the string in-place. Returns the trimmed string.
 */
char *libreport_strtrimch(char *str, int ch)
{
    if (!str)
        return NULL;

    /* Remove leading characters */
    char *tmp = str;
    while (*tmp == ch)
        ++tmp;
    libreport_overlapping_strcpy(str, tmp);

    /* Remove trailing characters */
    int i = strlen(str);
    while (--i >= 0)
    {
        if (str[i] != ch)
            break;
    }
    str[++i] = '\0';
    return str;
}

/*
 * Removes character from a string.
 * Modifies the string in-place. Returns the updated string.
 */
char *libreport_strremovech(char *str, int ch)
{
    char *ret = str;
    char *res = str;
    for ( ; *str != '\0'; ++str)
        if (*str != ch)
            *(res++) = *str;

    *res = '\0';
    return ret;
}


struct strbuf *libreport_strbuf_new(void)
{
    struct strbuf *buf = libreport_xzalloc(sizeof(*buf));
    /*buf->len = 0; - done by libreport_xzalloc */
    buf->alloc = 8;
    buf->buf = libreport_xzalloc(8);
    return buf;
}

void libreport_strbuf_free(struct strbuf *strbuf)
{
    if (!strbuf)
        return;
    free(strbuf->buf);
    free(strbuf);
}

char *libreport_strbuf_free_nobuf(struct strbuf *strbuf)
{
    char *ret = strbuf->buf;
    free(strbuf);
    return ret;
}


void libreport_strbuf_clear(struct strbuf *strbuf)
{
    assert(strbuf->alloc > 0);
    strbuf->len = 0;
    strbuf->buf[0] = '\0';
}

/* Ensures that the buffer can be extended by N+1 characters
 * without touching malloc/realloc.
 * Returns pointer where appended chars can be stored by the caller;
 * increments ->len by N (therefore callers don't need to do it).
 */
static char *strbuf_grow(struct strbuf *strbuf, unsigned increment)
{
    unsigned len = strbuf->len;
    unsigned need = strbuf->len = len + increment;
    unsigned cur_size = strbuf->alloc;
    if (cur_size <= need)
    {
        while (cur_size <= need)
            cur_size += 64 + cur_size / 8;
        strbuf->alloc = cur_size;
        strbuf->buf = libreport_xrealloc(strbuf->buf, cur_size);
    }
    char *p = strbuf->buf + len;
    return p;
}

struct strbuf *libreport_strbuf_append_char(struct strbuf *strbuf, char c)
{
    char *p = strbuf_grow(strbuf, 1);
    *p++ = c;
    *p = '\0';
    return strbuf;
}

struct strbuf *libreport_strbuf_append_str(struct strbuf *strbuf, const char *str)
{
    unsigned len = strlen(str);
    char *p = strbuf_grow(strbuf, len);
    assert(strbuf->len < strbuf->alloc);
    strcpy(p, str);
    return strbuf;
}

struct strbuf *libreport_strbuf_prepend_str(struct strbuf *strbuf, const char *str)
{
    unsigned cur_len = strbuf->len;
    unsigned inc_len = strlen(str);
    strbuf_grow(strbuf, inc_len);
    assert(strbuf->len < strbuf->alloc);
    memmove(strbuf->buf + inc_len, strbuf->buf, cur_len + 1);
    memcpy(strbuf->buf, str, inc_len);
    return strbuf;
}

struct strbuf *libreport_strbuf_append_strfv(struct strbuf *strbuf, const char *format, va_list p)
{
    char *string_ptr = libreport_xvasprintf(format, p);
    libreport_strbuf_append_str(strbuf, string_ptr);
    free(string_ptr);
    return strbuf;
}

struct strbuf *libreport_strbuf_append_strf(struct strbuf *strbuf, const char *format, ...)
{
    va_list p;

    va_start(p, format);
    libreport_strbuf_append_strfv(strbuf, format, p);
    va_end(p);

    return strbuf;
}

struct strbuf *libreport_strbuf_prepend_strfv(struct strbuf *strbuf, const char *format, va_list p)
{
    char *string_ptr = libreport_xvasprintf(format, p);
    libreport_strbuf_prepend_str(strbuf, string_ptr);
    free(string_ptr);
    return strbuf;
}

struct strbuf *libreport_strbuf_prepend_strf(struct strbuf *strbuf, const char *format, ...)
{
    va_list p;

    va_start(p, format);
    libreport_strbuf_prepend_strfv(strbuf, format, p);
    va_end(p);

    return strbuf;
}
