#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <internal_libreport_gtk.h>

char *tag_url(const char* line, const char* prefix)
{
    static const char *const known_url_prefixes[] = {"http://", "https://", "ftp://", "file://", NULL};

    char *result = xstrdup(line);

    const char *const *pfx = known_url_prefixes;
    while (*pfx != NULL)
    {
        char *cur_pos = result;
        char *url_start;
        while ((url_start = strstr(cur_pos, *pfx)) != NULL)
        {
            char *url_end = strchrnul(url_start, ' '); //TODO: also '.', ',', '\t', '\n'...
            int len = url_end - url_start;
            char *hyperlink = xasprintf("%s<a href=\"%.*s\">%.*s</a>",
                            prefix,
                            len, url_start,
                            len, url_start
            );
            len = url_start - result;
            char *old = result;
            result = xasprintf("%.*s%s%s",
                            len, result,
                            hyperlink,
                            url_end
            );
            cur_pos = result + len + strlen(hyperlink);
            free(old);
            free(hyperlink);
        }
        pfx++;
    }
    return result;
}
