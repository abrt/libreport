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
        /* On x32 arch, time_t is wider than long. Must use strtoll */
        long long ll = strtoll(item->content, &end, 10);
        time_t time = ll;
        if (!errno && *end == '\0' && end != item->content
         && ll == time /* there was no truncation in long long -> time_t conv */
        ) {
            char timeloc[256];
            int success = strftime(timeloc, sizeof(timeloc), "%c", localtime(&time));
            if (success)
                return xstrdup(timeloc);
        }
    }
    return NULL;
}

int problem_item_get_size(struct problem_item *item, unsigned long *size)
{
    if (item->size != PROBLEM_ITEM_UNINITIALIZED_SIZE)
    {
        *size = item->size;
        return 0;
    }

    if (item->flags & CD_FLAG_TXT)
    {
        *size = item->size = strlen(item->content);
        return 0;
    }

    /* else if (item->flags & CD_FLAG_BIN) */

    struct stat statbuf;
    statbuf.st_size = 0;

    if (stat(item->content, &statbuf) != 0)
        return -errno;

    *size = item->size = statbuf.st_size;
    return 0;
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
            log_info("reporting initiated from: %s", buf);
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

struct problem_item *problem_data_add_ext(problem_data_t *problem_data,
                const char *name,
                const char *content,
                unsigned flags,
                unsigned long size)
{
    if (!(flags & CD_FLAG_BIN))
        flags |= CD_FLAG_TXT;
    if (!(flags & CD_FLAG_ISEDITABLE))
        flags |= CD_FLAG_ISNOTEDITABLE;

    struct problem_item *item = (struct problem_item *)xzalloc(sizeof(*item));
    item->content = xstrdup(content);
    item->flags = flags;
    item->size = size;
    g_hash_table_replace(problem_data, xstrdup(name), item);

    return item;
}

void problem_data_add(problem_data_t *problem_data,
                const char *name,
                const char *content,
                unsigned flags)
{
    problem_data_add_ext(problem_data, name, content, flags, PROBLEM_ITEM_UNINITIALIZED_SIZE);
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
    INITIALIZE_LIBREPORT();

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
    FILENAME_MOUNTINFO  , /* user might want to hide sensitive file paths */
    //FILENAME_LIMITS     ,
    FILENAME_CMDLINE    ,
    FILENAME_CONTAINER_CMDLINE,
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
    FILENAME_KICKSTART_CFG,
    FILENAME_ANACONDA_TB,
    NULL
};
static bool is_editable_file(const char *file_name)
{
    return is_in_string_list(file_name, editable_files);
}

static const char *const always_text_files[] = {
    FILENAME_CMDLINE  ,
    FILENAME_BACKTRACE,
    FILENAME_OS_RELEASE,
    NULL
};
static int is_text_file_at(int dir_fd, const char *name, char **content, ssize_t *sz, int *file_fd)
{
    /* We were using magic.h API to check for file being text, but it thinks
     * that file containing just "0" is not text (!!)
     * So, we do it ourself.
     */

    int fd = secure_openat_read(dir_fd, name);
    if (fd < 0)
        return fd; /* it's not text (because it does not exist! :) */

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0)
    {
        close(fd);
        return -EIO; /* it's not text (because there is an I/O error) */
    }
    lseek(fd, 0, SEEK_SET);

    unsigned char *buf = xmalloc(*sz);
    ssize_t r = full_read(fd, buf, *sz);

    if (r < 0)
    {
        close(fd);
        free(buf);
        return -EIO; /* it's not text (because we can't read it) */
    }

    if (file_fd == NULL)
        close(fd);
    else
        *file_fd = fd;

    if (r < *sz)
        buf[r] = '\0';
    *sz = r;

    /* Some files in our dump directories are known to always be textual */
    const char *base = strrchr(name, '/');
    if (base)
    {
        base++;
        if (is_in_string_list(base, always_text_files))
            goto text;
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
            return CD_FLAG_BIN;
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
        goto text; /* looks like text to me */

    free(buf);
    return CD_FLAG_BIN; /* it's binary */

 text:
    if (size > CD_MAX_TEXT_SIZE)
    {
        free(buf);
        return CD_FLAG_BIN | CD_FLAG_BIGTXT;
    }

    *content = /* cast from (unsigned char *) to */(char *)buf;
    return CD_FLAG_TXT;
}


static int _problem_data_load_dump_dir_element(struct dump_dir *dd, const char *name, char **content, int *type_flags, int *fd)
{
    int file_fd = -1;
    int *file_fd_ptr = fd == NULL ? &file_fd : fd;

#define IS_TEXT_FILE_AT_PROBE_SIZE 4*1024

    ssize_t sz = IS_TEXT_FILE_AT_PROBE_SIZE;
    char *text = NULL;
    int r = is_text_file_at(dd->dd_fd, name, &text, &sz, file_fd_ptr);

    if (r < 0)
        return r;

    *type_flags = r;

    if ((r == CD_FLAG_BIN) || (r == (CD_FLAG_BIN | CD_FLAG_BIGTXT)))
        goto finito;

    if (r != CD_FLAG_TXT)
    {
        error_msg("Unrecognized is_text_file_at() return value");
        abort();
    }

    if (sz >= IS_TEXT_FILE_AT_PROBE_SIZE) /* did is_text_file() read entire file? */
    {
        /* no, it didn't, we need to read it all */
        free(text);
        lseek(*file_fd_ptr, 0, SEEK_SET);
        text = xmalloc_read(*file_fd_ptr, NULL);
    }

#undef IS_TEXT_FILE_AT_PROBE_SIZE

    /* Strip '\n' from one-line elements: */
    char *nl = strchr(text, '\n');
    if (nl && nl[1] == '\0')
        *nl = '\0';

    /* Sanitize possibly corrupted utf8.
     * Of control chars, allow only tab and newline.
     */
    char *sanitized = sanitize_utf8(text,
            (SANITIZE_ALL & ~SANITIZE_LF & ~SANITIZE_TAB)
    );

    if (sanitized != NULL)
    {
        free(text);
        text = sanitized;
    }

finito:
    if (file_fd >= 0)
        close(file_fd);

    *content = text;
    return 0;
}

int problem_data_load_dump_dir_element(struct dump_dir *dd, const char *name, char **content, int *type_flags, int *fd)
{
    if (!str_is_correct_filename(name))
        return -EINVAL;

    return _problem_data_load_dump_dir_element(dd, name, content, type_flags, fd);
}

void problem_data_load_from_dump_dir(problem_data_t *problem_data, struct dump_dir *dd, char **excluding)
{
    char *short_name;
    char *full_name;

    dd_init_next_file(dd);
    while (dd_get_next_file(dd, &short_name, &full_name))
    {
        if (excluding && is_in_string_list(short_name, (const char *const *)excluding))
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

        char *content = NULL;
        int flags = 0;
        int r = _problem_data_load_dump_dir_element(dd, short_name, &content, &flags, /*fd*/NULL);
        if (r < 0)
        {
            error_msg("Failed to load element %s: %s", short_name, strerror(-r));
            goto next;
        }

        if (flags & CD_FLAG_TXT)
        {
            if (is_editable_file(short_name))
                flags |= CD_FLAG_ISEDITABLE;
            else
                flags |= CD_FLAG_ISNOTEDITABLE;

            static const char *const list_files[] = {
                FILENAME_UID       ,
                FILENAME_PACKAGE   ,
                FILENAME_CMDLINE   ,
                FILENAME_TIME      ,
                FILENAME_COUNT     ,
                FILENAME_REASON    ,
                NULL
            };
            if (is_in_string_list(short_name, list_files))
                flags |= CD_FLAG_LIST;

            if (strcmp(short_name, FILENAME_TIME) == 0)
                flags |= CD_FLAG_UNIXTIME;
        }
        else
        {
            content = full_name;
            full_name = NULL;
        }

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

problem_data_t *create_problem_data_for_reporting(const char *dump_dir_name)
{
    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        return NULL; /* dd_opendir already emitted error msg */
    string_vector_ptr_t exclude_items = get_global_always_excluded_elements();
    problem_data_t *problem_data = problem_data_new();
    problem_data_load_from_dump_dir(problem_data, dd, exclude_items);
    dd_close(dd);
    string_vector_free(exclude_items);
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
    unsigned long a_time = a_time_str ? strtoul(a_time_str, NULL, 10) : 0;

    problem_data_t *b_data = *(problem_data_t **) b;
    const char *b_time_str = problem_data_get_content_or_NULL(b_data, filename);
    unsigned long b_time= b_time_str ? strtoul(b_time_str, NULL, 10) : 0;

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

static const gchar const* reproducible_names[_PROBLEM_REPRODUCIBLE_MAX_] =
{
    "Not sure how to reproduce the problem",
    "The problem is reproducible",
    "The problem occurs regularly",
};

int get_problem_data_reproducible(problem_data_t *problem_data)
{
    const char *reproducible_str = problem_data_get_content_or_NULL(problem_data, FILENAME_REPRODUCIBLE);
    if (reproducible_str == NULL)
    {
        log_info("Cannot return Reproducible type: missing "FILENAME_REPRODUCIBLE);
        return -1;
    }

    for (int i = 0; i < _PROBLEM_REPRODUCIBLE_MAX_; ++i)
        if (strcmp(reproducible_str, reproducible_names[i]) == 0)
            return i;

    error_msg("Cannot return Reproducible type: invalid format of '%s'", FILENAME_REPRODUCIBLE);
    return -1;
}

const char *get_problem_data_reproducible_name(int reproducible)
{
    if (reproducible < 0 || reproducible >= _PROBLEM_REPRODUCIBLE_MAX_)
    {
        error_msg("Cannot return Reproducible name: invalid code: %d", reproducible);
        return NULL;
    }

    return reproducible_names[reproducible];
}
