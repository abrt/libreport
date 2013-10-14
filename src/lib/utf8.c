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

char *sanitize_utf8(const char *src, uint32_t control_chars_to_sanitize)
{
    const char *initial_src = src;
    char *sanitized = NULL;
    unsigned sanitized_pos = 0;

    while (*src)
    {
        int bytes = 0;

        unsigned c = (unsigned char) *src;
        if (c <= 0x7f)
        {
            if (c < 32 && (((uint32_t)1 << c) & control_chars_to_sanitize))
                goto bad_byte;
            bytes = 1;
            goto good_byte;
        }

        /* Unicode -> utf8: */
        /* 80-7FF -> 110yyyxx 10xxxxxx */
        /* 800-FFFF -> 1110yyyy 10yyyyxx 10xxxxxx */
        /* 10000-1FFFFF -> 11110zzz 10zzyyyy 10yyyyxx 10xxxxxx */
        /* 200000-3FFFFFF -> 111110tt 10zzzzzz 10zzyyyy 10yyyyxx 10xxxxxx */
        /* 4000000-FFFFFFFF -> 111111tt 10tttttt 10zzzzzz 10zzyyyy 10yyyyxx 10xxxxxx */
        do {
            c <<= 1;
            bytes++;
        } while ((c & 0x80) && bytes < 6);
        if (bytes == 1)
        {
            /* A bare "continuation" byte. Say, 80 */
            goto bad_byte;
        }

        c = (uint8_t)(c) >> bytes;
        {
            const char *pp = src;
            int cnt = bytes;
            while (--cnt)
            {
                unsigned ch = (unsigned char) *++pp;
                if ((ch & 0xc0) != 0x80) /* Missing "continuation" byte. Example: e0 80 */
                {
                    goto bad_byte;
                }
                c = (c << 6) + (ch & 0x3f);
            }
        }
        /* TODO */
        /* Need to check that c isn't produced by overlong encoding */
        /* Example: 11000000 10000000 converts to NUL */
        /* 11110000 10000000 10000100 10000000 converts to 0x100 */
        /* correct encoding: 11000100 10000000 */
        if (c <= 0x7f) /* crude check: only catches bad encodings which map to chars <= 7f */
        {
            goto bad_byte;
        }

 good_byte:
        while (--bytes >= 0)
        {
            c = (unsigned char) *src++;
            if (sanitized)
            {
                sanitized = (char*) xrealloc(sanitized, sanitized_pos + 2);
                sanitized[sanitized_pos++] = c;
                sanitized[sanitized_pos] = '\0';
            }
        }
        continue;

 bad_byte:
        if (!sanitized)
        {
            sanitized_pos = src - initial_src;
            sanitized = xstrndup(initial_src, sanitized_pos);
        }
        sanitized = (char*) xrealloc(sanitized, sanitized_pos + 5);
        sanitized[sanitized_pos++] = '[';
        c = (unsigned char) *src++;
        sanitized[sanitized_pos++] = "0123456789ABCDEF"[c >> 4];
        sanitized[sanitized_pos++] = "0123456789ABCDEF"[c & 0xf];
        sanitized[sanitized_pos++] = ']';
        sanitized[sanitized_pos] = '\0';
    }

    if (sanitized)
        log_info("note: bad utf8, converted '%s' -> '%s'", initial_src, sanitized);

    return sanitized; /* usually NULL: the whole string is ok */
}
