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
            char *url_end = find_end(url_start);
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
