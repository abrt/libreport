/*
    Copyright (C) 2010  Denys Vlasenko (dvlasenk@redhat.com)
    Copyright (C) 2010  RedHat inc.

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

static void free_problem_item(void *ptr)
{
    if (ptr)
    {
        struct problem_item *item = (struct problem_item *)ptr;
        free(item->content);
        free(item);
    }
}

char *problem_item_format(struct problem_item *item)
{
    if (!item)
        return xstrdup("(nullitem)");

    if (item->flags & CD_FLAG_UNIXTIME)
    {
        errno = 0;
        char *end;
        time_t time = strtol(item->content, &end, 10);
        if (!errno && !*end && end != item->content)
        {
            char timeloc[256];
            int success = strftime(timeloc, sizeof(timeloc), "%c", localtime(&time));
            if (success)
                return xstrdup(timeloc);
        }
    }
    return NULL;
}

/* problem_data["name"] = { "content", CD_FLAG_foo_bits } */

problem_data_t *problem_data_new(void)
{
    return g_hash_table_new_full(g_str_hash, g_str_equal,
                 free, free_problem_item);
}

void problem_data_add_basics(problem_data_t *pd)
{
    const char *analyzer = problem_data_get_content_or_NULL(pd, FILENAME_ANALYZER);
    const char *type = problem_data_get_content_or_NULL(pd, FILENAME_TYPE);
    if (analyzer == NULL)
    {
        analyzer = type ? type : "libreport";
        problem_data_add_text_noteditable(pd, FILENAME_ANALYZER, analyzer);
    }

    if (type == NULL)
        problem_data_add_text_noteditable(pd, FILENAME_TYPE, analyzer);

    /* If application didn't provide dupe hash, we generate it
     * from all components, so we at least eliminate the exact same
     * reports
     *
     * We don't want to generate DUPHASH file because it is usually generated
     * later in some "analyze_*" event. DUPHASH was originally designed as
     * global problem identifier and generating of global identifier requires
     * more space and data. On the contrary UUID was originally designed as
     * local problem identifier. It means that this identifier is weaker (e.g.
     * a hash generated from a coredump without debuginfo - there can be many
     * similar backtraces without line numbers and function names).
     */
    if (problem_data_get_content_or_NULL(pd, FILENAME_UUID) == NULL)
    {
        /* If application provided DUPHASH, we should use it in UUID as well.
         * Otherwise we compute hash from all problem's data.
         */
        const char *const duphash = problem_data_get_content_or_NULL(pd, FILENAME_DUPHASH);
        if (duphash != NULL)
            problem_data_add_text_noteditable(pd, FILENAME_UUID, duphash);
        else
        {
            /* start hash */
            sha1_ctx_t sha1ctx;
            sha1_begin(&sha1ctx);

            /*
             * To avoid spurious hash differences, sort keys so that elements are
             * always processed in the same order:
             */
            GList *list = g_hash_table_get_keys(pd);
            list = g_list_sort(list, (GCompareFunc)strcmp);
            GList *l = list;
            while (l)
            {
                const char *key = l->data;
                l = l->next;
                struct problem_item *item = g_hash_table_lookup(pd, key);
                /* do not hash items which are binary (item->flags & CD_FLAG_BIN).
                 * Their ->content is full file name, with path. Path is always
                 * different and will make hash differ even if files are the same.
                 */
                if (item->flags & CD_FLAG_BIN)
                    continue;
                sha1_hash(&sha1ctx, item->content, strlen(item->content));
            }
            g_list_free(list);

            /* end hash */
            char hash_bytes[SHA1_RESULT_LEN];
            sha1_end(&sha1ctx, hash_bytes);
            char hash_str[SHA1_RESULT_LEN*2 + 1];
            bin2hex(hash_str, hash_bytes, SHA1_RESULT_LEN)[0] = '\0';

            problem_data_add_text_noteditable(pd, FILENAME_UUID, hash_str);
        }
    }
}

void problem_data_add_current_process_data(problem_data_t *pd)
{
    const char *executable = problem_data_get_content_or_NULL(pd, FILENAME_EXECUTABLE);
    if (executable == NULL)
    {
        char buf[PATH_MAX + 1];
        char exe[sizeof("/proc/%u/exe") + sizeof(int)*3];
        sprintf(exe, "/proc/%u/exe", (int)getpid());
        ssize_t read = readlink(exe, buf, PATH_MAX);
        if (read > 0)
        {
            buf[read] = '\0';
            VERB2 log("reporting initiated from: %s", buf);
            problem_data_add_text_noteditable(pd, FILENAME_EXECUTABLE, buf);
        }

//#ifdef WITH_RPM
        /* FIXME: component should be taken from rpm using librpm
         * which means we need to link against it :(
         * or run rpm -qf executable ??
         */
        /* Fedora/RHEL rpm specific piece of code */
        const char *component = problem_data_get_content_or_NULL(pd, FILENAME_COMPONENT);
        //FIXME: this REALLY needs to go away, or every report will be assigned to abrt
        if (component == NULL) // application didn't specify component
            problem_data_add_text_noteditable(pd, FILENAME_COMPONENT, "abrt");
//#endif
    }
}

void problem_data_add(problem_data_t *problem_data,
                const char *name,
                const char *content,
                unsigned flags)
{
    if (!(flags & CD_FLAG_BIN))
        flags |= CD_FLAG_TXT;
    if (!(flags & CD_FLAG_ISEDITABLE))
        flags |= CD_FLAG_ISNOTEDITABLE;

    struct problem_item *item = (struct problem_item *)xzalloc(sizeof(*item));
    item->content = xstrdup(content);
    item->flags = flags;
    g_hash_table_replace(problem_data, xstrdup(name), item);
}

void problem_data_add_text_noteditable(problem_data_t *problem_data,
                const char *name,
                const char *content)
{
    problem_data_add(problem_data, name, content, CD_FLAG_TXT + CD_FLAG_ISNOTEDITABLE);
}

void problem_data_add_text_editable(problem_data_t *problem_data,
                const char *name,
                const char *content)
{
    problem_data_add(problem_data, name, content, CD_FLAG_TXT + CD_FLAG_ISEDITABLE);
}

static const char *get_filename(const char *path)
{
    const char *filename = strrchr(path, '/');
    if (filename) /* +1 => strip the '/' */
        return filename + 1;
    return path;
}
void problem_data_add_file(problem_data_t *pd, const char *name, const char *path)
{
    problem_data_add(pd, name ? name : get_filename(path), path, CD_FLAG_BIN);
}


char *problem_data_get_content_or_die(problem_data_t *problem_data, const char *key)
{
    struct problem_item *item = problem_data_get_item_or_NULL(problem_data, key);
    if (!item)
        error_msg_and_die(_("Essential element '%s' is missing, can't continue"), key);
    return item->content;
}

char *problem_data_get_content_or_NULL(problem_data_t *problem_data, const char *key)
{
    struct problem_item *item = problem_data_get_item_or_NULL(problem_data, key);
    if (!item)
        return NULL;
    return item->content;
}


/* Miscellaneous helpers */

static const char *const editable_files[] = {
    FILENAME_COMMENT    ,
    FILENAME_BACKTRACE  ,
    FILENAME_REASON     ,
    //FILENAME_UID        ,
    //FILENAME_TIME       ,
    //FILENAME_ANALYZER   ,
    //FILENAME_EXECUTABLE ,
    //FILENAME_BINARY     ,
    FILENAME_OPEN_FDS   , /* user might want to hide sensitive file names */
    //FILENAME_LIMITS     ,
    FILENAME_CMDLINE    ,
    //FILENAME_CGROUP     ,
    //FILENAME_COREDUMP   ,
    FILENAME_BACKTRACE  ,
    FILENAME_MAPS       ,
    FILENAME_SMAPS      ,
    FILENAME_ENVIRON    ,
    //FILENAME_DUPHASH    ,
    //FILENAME_CRASH_FUNCTION,
    //FILENAME_ARCHITECTURE,
    //FILENAME_KERNEL     ,
    //FILENAME_OS_RELEASE ,
    //FILENAME_PACKAGE    ,
    //FILENAME_COMPONENT  ,
    //FILENAME_RATING     ,
    FILENAME_HOSTNAME   ,
    FILENAME_REMOTE     ,
    //FILENAME_TAINTED    ,
    //FILENAME_TAINTED_SHORT,
    //FILENAME_TAINTED_LONG,
    //FILENAME_UUID       ,
    //FILENAME_COUNT      ,
    //FILENAME_REPORTED_TO,
    //FILENAME_EVENT_LOG  ,
    NULL
};
static bool is_editable_file(const char *file_name)
{
    return is_in_string_list(file_name, (char**)editable_files);
}

static const char *const always_text_files[] = {
    FILENAME_CMDLINE  ,
    FILENAME_BACKTRACE,
    FILENAME_OS_RELEASE,
    NULL
};
static char* is_text_file(const char *name, ssize_t *sz)
{
    /* We were using magic.h API to check for file being text, but it thinks
     * that file containing just "0" is not text (!!)
     * So, we do it ourself.
     */

    int fd = open(name, O_RDONLY);
    if (fd < 0)
        return NULL; /* it's not text (because it does not exist! :) */

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0 || size > CD_MAX_TEXT_SIZE)
    {
        close(fd);
        return NULL; /* it's not a SMALL text */
    }
    lseek(fd, 0, SEEK_SET);

    unsigned char *buf = xmalloc(*sz);
    ssize_t r = full_read(fd, buf, *sz);
    close(fd);
    if (r < 0)
    {
        free(buf);
        return NULL; /* it's not text (because we can't read it) */
    }
    if (r < *sz)
        buf[r] = '\0';
    *sz = r;

    /* Some files in our dump directories are known to always be textual */
    const char *base = strrchr(name, '/');
    if (base)
    {
        base++;
        if (is_in_string_list(base, (char**)always_text_files))
            return (char*)buf;
    }

    /* Every once in a while, even a text file contains a few garbled
     * or unexpected non-ASCII chars. We should not declare it "binary".
     *
     * Used to have RATIO = 50 (2%), but then came Fedora 19 with
     * os_release = "Schrödinger's Cat". Bumped to 10%.
     * Alternatives: add os_release to always_text_files[]
     * or add "if it is valid Unicode, then it's text" check here.
     *
     * Replaced crude "buf[r] > 0x7e is bad" logic with
     * "if it is a broken Unicode, then it's bad".
     */
    const unsigned RATIO = 10;
    unsigned total_chars = r + RATIO;
    unsigned bad_chars = 1; /* 1 prevents division by 0 later */
    bool prev_was_unicode = 0;
    ssize_t i = -1;
    while (++i < r)
    {
        /* Among control chars, only '\t','\n' etc are allowed */
        if (buf[i] < ' ' && !isspace(buf[i]))
        {
            /* We don't like NULs and other control chars very much.
             * Not text for sure!
             */
            free(buf);
            return NULL;
        }
        if (buf[i] == 0x7f)
            bad_chars++;
        else if (buf[i] > 0x7f)
        {
            /* We test two possible bad cases with one comparison:
             * (1) prev byte was unicode AND cur byte is 11xxxxxx:
             * BAD - unicode start byte can't be in the middle of unicode char
             * (2) prev byte wasnt unicode AND cur byte is 10xxxxxx:
             * BAD - unicode continuation byte can't start unicode char
             */
            if (prev_was_unicode == ((buf[i] & 0x40) == 0x40))
                bad_chars++;
        }
        prev_was_unicode = (buf[i] > 0x7f);
    }

    if ((total_chars / bad_chars) >= RATIO)
        return (char*)buf; /* looks like text to me */

    free(buf);
    return NULL; /* it's binary */
}

void problem_data_load_from_dump_dir(problem_data_t *problem_data, struct dump_dir *dd, char **excluding)
{
    char *short_name;
    char *full_name;

    dd_init_next_file(dd);
    while (dd_get_next_file(dd, &short_name, &full_name))
    {
        if (excluding && is_in_string_list(short_name, excluding))
        {
            //log("Excluded:'%s'", short_name);
            goto next;
        }

        if (short_name[0] == '#'
         || (short_name[0] && short_name[strlen(short_name) - 1] == '~')
        ) {
            //log("Excluded (editor backup file):'%s'", short_name);
            goto next;
        }

        ssize_t sz = 4*1024;
        char *text = NULL;
        bool editable = is_editable_file(short_name);

        if (!editable)
        {
            text = is_text_file(full_name, &sz);
            if (!text)
            {
                problem_data_add(problem_data,
                        short_name,
                        full_name,
                        CD_FLAG_BIN + CD_FLAG_ISNOTEDITABLE
                );
                goto next;
            }
        }

        char *content;
        if (sz < 4*1024) /* did is_text_file read entire file? */
        {
            /* yes */
            content = text;
        }
        else
        {
            /* no, need to read it all */
            free(text);
            content = dd_load_text(dd, short_name);
        }
        /* Strip '\n' from one-line elements: */
        char *nl = strchr(content, '\n');
        if (nl && nl[1] == '\0')
            *nl = '\0';

        /* Sanitize possibly corrupted utf8.
         * Of control chars, allow only tab and newline.
         */
        char *sanitized = sanitize_utf8(content,
                (SANITIZE_ALL & ~SANITIZE_LF & ~SANITIZE_TAB)
        );
        if (sanitized)
        {
            free(content);
            content = sanitized;
        }

        int flags = 0;

        if (editable)
            flags |= CD_FLAG_TXT | CD_FLAG_ISEDITABLE;
        else
            flags |= CD_FLAG_TXT | CD_FLAG_ISNOTEDITABLE;

        static const char *const list_files[] = {
            FILENAME_UID       ,
            FILENAME_PACKAGE   ,
            FILENAME_EXECUTABLE,
            FILENAME_TIME      ,
            FILENAME_COUNT     ,
            NULL
        };
        if (is_in_string_list(short_name, (char**)list_files))
            flags |= CD_FLAG_LIST;

        if (strcmp(short_name, FILENAME_TIME) == 0)
            flags |= CD_FLAG_UNIXTIME;

        problem_data_add(problem_data,
                short_name,
                content,
                flags
        );
        free(content);
 next:
        free(short_name);
        free(full_name);
    }
}

problem_data_t *create_problem_data_from_dump_dir(struct dump_dir *dd)
{
    problem_data_t *problem_data = problem_data_new();
    problem_data_load_from_dump_dir(problem_data, dd, NULL);
    return problem_data;
}

/*
 * Returns NULL-terminated char *vector[]. Result itself must be freed,
 * but do no free list elements. IOW: do free(result), but never free(result[i])!
 * If comma_separated_list is NULL or "", returns NULL.
 */
static char **build_exclude_vector(const char *comma_separated_list)
{
    char **exclude_items = NULL;
    if (comma_separated_list && comma_separated_list[0])
    {
        /* even w/o commas, we'll need two elements:
         * exclude_items[0] = "name"
         * exclude_items[1] = NULL
         */
        unsigned cnt = 2;

        const char *cp = comma_separated_list;
        while (*cp)
            if (*cp++ == ',')
                cnt++;

        /* We place the string directly after the char *vector[cnt]: */
        exclude_items = xzalloc(cnt * sizeof(exclude_items[0]) + (cp - comma_separated_list) + 1);
        char *p = strcpy((char*)&exclude_items[cnt], comma_separated_list);

        char **pp = exclude_items;
        *pp++ = p;
        while (*p)
        {
            if (*p++ == ',')
            {
                p[-1] = '\0';
                *pp++ = p;
            }
        }
    }

    return exclude_items;
}

problem_data_t *create_problem_data_for_reporting(const char *dump_dir_name)
{
    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        return NULL; /* dd_opendir already emitted error msg */
    char **exclude_items = build_exclude_vector(getenv("EXCLUDE_FROM_REPORT"));
    problem_data_t *problem_data = problem_data_new();
    problem_data_load_from_dump_dir(problem_data, dd, exclude_items);
    dd_close(dd);
    free(exclude_items);
    return problem_data;
}

void log_problem_data(problem_data_t *problem_data, const char *pfx)
{
    GHashTableIter iter;
    char *name;
    struct problem_item *value;
    g_hash_table_iter_init(&iter, problem_data);
    while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&value))
    {
        log("%s[%s]:'%s' 0x%x",
                pfx, name,
                value->content,
                value->flags
        );
    }
}

gint cmp_problem_data(gconstpointer a, gconstpointer b, gpointer filename)
{
    problem_data_t *a_data = *(problem_data_t **) a;
    const char *a_time_str = problem_data_get_content_or_NULL(a_data, filename);
    unsigned long a_time= strtoul(a_time_str, NULL, 10);

    problem_data_t *b_data = *(problem_data_t **) b;
    const char *b_time_str = problem_data_get_content_or_NULL(b_data, filename);
    unsigned long b_time= strtoul(b_time_str, NULL, 10);

    /* newer first */
    if (a_time > b_time)
        return -1;

    if (a_time == b_time)
        return 0;

    return 1;
}

static bool problem_data_get_osinfo_from_items(problem_data_t *problem_data,
        map_string_t *osinfo, const char *osinfo_name, const char *release_name)
{
    char *data = problem_data_get_content_or_NULL(problem_data, osinfo_name);
    if (data)
    {
        parse_osinfo(data, osinfo);
        return true;
    }

    data = problem_data_get_content_or_NULL(problem_data, release_name);
    if (!data)
        return false;

    insert_map_string(osinfo, xstrdup(OSINFO_PRETTY_NAME), xstrdup(data));
    return true;
}

void problem_data_get_osinfo(problem_data_t *problem_data, map_string_t *osinfo)
{
    char *rootdir = problem_data_get_content_or_NULL(problem_data, FILENAME_ROOTDIR);
    if (rootdir &&
        problem_data_get_osinfo_from_items(problem_data, osinfo,
                FILENAME_OS_INFO_IN_ROOTDIR, FILENAME_OS_RELEASE_IN_ROOTDIR))
        return;

    problem_data_get_osinfo_from_items(problem_data, osinfo,
                FILENAME_OS_INFO, FILENAME_OS_RELEASE);
}
