#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <pthread.h>
#include <math.h>

#include <cwist/core/sstring/sstring.h>
#include <cwist/core/mem/alloc.h>
#include <cJSON.h>
#include <md4c-html.h>

#include "blog.h"
#include "scheduler.h"

#define PATH_MAX_LEN    4096
#define MAX_EXCERPT_LEN 200
#define ACCENT_COLOR_BUFFER_SIZE 24
#define SITE_HOST       "style-pank.github.io"
#define COMMENT_REPO    "style-pank/style-pank.github.io"

/* Accent of the search page (no categories.cfg section of its own). */
#define SEARCH_ACCENT_PRIMARY   "#7a5cff"
#define SEARCH_ACCENT_SECONDARY "#9a84ff"

/* ── Data model ─────────────────────────────────────────────────────────── */

typedef struct blog_post_t {
    char *slug;
    char *title;
    char *date;
    char *excerpt;
    char *body;
    char *raw_markdown;
    char **tags;
    size_t tag_count;
    int reading_minutes;
    char *source_path;
} blog_post_t;

typedef struct blog_category_t {
    char *id;
    char *title;
    char *description;
    char *accent_primary;
    char *accent_secondary;
    int order;
    blog_post_t *posts;
    size_t post_count;
} blog_category_t;

typedef struct blog_catalog_t {
    blog_category_t *items;
    size_t count;
} blog_catalog_t;

/* ── Utilities ──────────────────────────────────────────────────────────── */

static char *strdup_safe(const char *src) {
    return src ? strdup(src) : NULL;
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len < 0) { fclose(fp); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t n = fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static bool ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode);
    if (mkdir(path, 0755) == 0) return true;
    return errno == EEXIST;
}

static bool ensure_parents(const char *filepath) {
    char tmp[PATH_MAX_LEN];
    snprintf(tmp, sizeof(tmp), "%s", filepath);
    char *slash = strrchr(tmp, '/');
    if (!slash) return true;
    *slash = '\0';
    if (tmp[0] == '\0') return true;
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (!ensure_dir(tmp)) { *p = '/'; return false; }
            *p = '/';
        }
    }
    return ensure_dir(tmp);
}

static bool join_path_checked(char *out, size_t out_size, const char *left, const char *right) {
    if (!out || out_size == 0 || !left || !right) return false;
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    bool needs_sep = left_len > 0 && left[left_len - 1] != '/';
    size_t total = left_len + (needs_sep ? 1 : 0) + right_len;
    if (total >= out_size) return false;
    memcpy(out, left, left_len);
    size_t pos = left_len;
    if (needs_sep) out[pos++] = '/';
    memcpy(out + pos, right, right_len);
    out[pos + right_len] = '\0';
    return true;
}

static bool write_file(const char *path, const char *content) {
    static pthread_mutex_t fs_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&fs_lock);
    bool ok = false;
    if (!ensure_parents(path)) {
        fprintf(stderr, "[bloggen] cannot create directories for %s\n", path);
        goto out;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[bloggen] cannot write %s\n", path);
        goto out;
    }
    size_t len = content ? strlen(content) : 0;
    if (len > 0) fwrite(content, 1, len, fp);
    fclose(fp);
    ok = true;
out:
    pthread_mutex_unlock(&fs_lock);
    return ok;
}

static bool copy_file(const char *src_path, const char *dst_path) {
    FILE *src = fopen(src_path, "rb");
    if (!src) return false;
    if (!ensure_parents(dst_path)) { fclose(src); return false; }
    FILE *dst = fopen(dst_path, "wb");
    if (!dst) { fclose(src); return false; }
    char buf[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) { ok = false; break; }
    }
    fclose(src);
    fclose(dst);
    return ok;
}

static char *trim(char *str) {
    if (!str) return str;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) *end-- = '\0';
    return str;
}

/* ── Catalog management ─────────────────────────────────────────────────── */

static void free_catalog(blog_catalog_t *catalog) {
    if (!catalog || !catalog->items) return;
    for (size_t i = 0; i < catalog->count; ++i) {
        blog_category_t *cat = &catalog->items[i];
        free(cat->id); free(cat->title); free(cat->description);
        free(cat->accent_primary); free(cat->accent_secondary);
        for (size_t j = 0; j < cat->post_count; ++j) {
            blog_post_t *post = &cat->posts[j];
            free(post->slug); free(post->title); free(post->date);
            free(post->excerpt); free(post->body); free(post->raw_markdown); free(post->source_path);
            for (size_t t = 0; t < post->tag_count; ++t) free(post->tags[t]);
            free(post->tags);
        }
        free(cat->posts);
    }
    free(catalog->items);
    catalog->items = NULL;
    catalog->count = 0;
}

static void free_post(blog_post_t *post) {
    if (!post) return;
    free(post->slug);
    free(post->title);
    free(post->date);
    free(post->excerpt);
    free(post->body);
    free(post->raw_markdown);
    free(post->source_path);
    for (size_t t = 0; t < post->tag_count; ++t) free(post->tags[t]);
    free(post->tags);
}

static void move_post(blog_post_t *dst, blog_post_t *src) {
    *dst = *src;
    memset(src, 0, sizeof(*src));
}

static blog_category_t *add_category(blog_catalog_t *catalog) {
    size_t new_count = catalog->count + 1;
    blog_category_t *tmp = realloc(catalog->items, new_count * sizeof(blog_category_t));
    if (!tmp) return NULL;
    catalog->items = tmp;
    blog_category_t *cat = &catalog->items[catalog->count];
    memset(cat, 0, sizeof(*cat));
    cat->accent_primary   = strdup_safe("#ff6b2b");
    cat->accent_secondary = strdup_safe("#ff9b6b");
    cat->order = (int)new_count;
    catalog->count = new_count;
    return cat;
}

static bool load_categories_cfg(const char *path, blog_catalog_t *catalog) {
    FILE *fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "[bloggen] cannot open %s\n", path); return false; }
    char line[1024];
    blog_category_t *current = NULL;
    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim(line);
        if (*trimmed == '#' || *trimmed == ';' || *trimmed == '\0') continue;
        if (trimmed[0] == '[') {
            char *end = strchr(trimmed, ']');
            if (!end) continue;
            *end = '\0';
            current = add_category(catalog);
            if (!current) break;
            free(current->id);
            current->id = strdup_safe(trim(trimmed + 1));
            continue;
        }
        char *eq = strchr(trimmed, '=');
        if (!eq || !current) continue;
        *eq = '\0';
        char *key = trim(trimmed);
        char *val = trim(eq + 1);
        if      (strcmp(key, "title")           == 0) { free(current->title);           current->title           = strdup_safe(val); }
        else if (strcmp(key, "description")     == 0) { free(current->description);     current->description     = strdup_safe(val); }
        else if (strcmp(key, "accent_primary")  == 0) { free(current->accent_primary);  current->accent_primary  = strdup_safe(val); }
        else if (strcmp(key, "accent_secondary")== 0) { free(current->accent_secondary);current->accent_secondary= strdup_safe(val); }
        else if (strcmp(key, "order")           == 0) { current->order = atoi(val); }
    }
    fclose(fp);
    return catalog->count > 0;
}

static void add_tag(blog_post_t *post, const char *value) {
    size_t new_count = post->tag_count + 1;
    char **tmp = realloc(post->tags, new_count * sizeof(char *));
    if (!tmp) return;
    post->tags = tmp;
    post->tags[post->tag_count] = strdup_safe(value);
    post->tag_count = new_count;
}

static bool has_tag(const blog_post_t *post, const char *tag) {
    if (!post || !tag) return false;
    for (size_t i = 0; i < post->tag_count; ++i) {
        if (post->tags[i] && strcmp(post->tags[i], tag) == 0) return true;
    }
    return false;
}

static void parse_tags(blog_post_t *post, const char *csv) {
    char *copy = strdup_safe(csv);
    if (!copy) return;
    char *token = strtok(copy, ",");
    while (token) {
        char *t = trim(token);
        if (*t) add_tag(post, t);
        token = strtok(NULL, ",");
    }
    free(copy);
}

static void merge_post(blog_post_t *dst, blog_post_t *src) {
    if (!dst || !src) return;
    if ((!dst->title || !*dst->title) && src->title && *src->title) {
        free(dst->title);
        dst->title = strdup_safe(src->title);
    }
    if ((!dst->date || !*dst->date) && src->date && *src->date) {
        free(dst->date);
        dst->date = strdup_safe(src->date);
    }
    if ((!dst->excerpt || !*dst->excerpt) && src->excerpt && *src->excerpt) {
        free(dst->excerpt);
        dst->excerpt = strdup_safe(src->excerpt);
    }
    if ((!dst->body || !*dst->body) && src->body && *src->body) {
        free(dst->body);
        dst->body = strdup_safe(src->body);
    }
    if ((!dst->raw_markdown || !*dst->raw_markdown) && src->raw_markdown && *src->raw_markdown) {
        free(dst->raw_markdown);
        dst->raw_markdown = strdup_safe(src->raw_markdown);
    }
    if ((!dst->source_path || !*dst->source_path) && src->source_path && *src->source_path) {
        free(dst->source_path);
        dst->source_path = strdup_safe(src->source_path);
    }
    if (src->reading_minutes > dst->reading_minutes) {
        dst->reading_minutes = src->reading_minutes;
    }
    for (size_t i = 0; i < src->tag_count; ++i) {
        if (!src->tags[i] || !*src->tags[i]) continue;
        if (!has_tag(dst, src->tags[i])) add_tag(dst, src->tags[i]);
    }
}

static bool strings_equal_nonempty(const char *a, const char *b) {
    return a && b && *a && *b && strcmp(a, b) == 0;
}

static bool posts_have_same_content(const blog_post_t *a, const blog_post_t *b) {
    if (!a || !b) return false;
    if (!strings_equal_nonempty(a->title, b->title)) return false;
    if ((a->date && *a->date) || (b->date && *b->date)) {
        if (!strings_equal_nonempty(a->date, b->date)) return false;
    }
    return strings_equal_nonempty(a->body, b->body) ||
           strings_equal_nonempty(a->excerpt, b->excerpt);
}

static bool parse_front_matter(const char *content, size_t len,
                               size_t *body_offset, blog_post_t *post) {
    const char *ptr = content;
    const char *end = content + len;
    if (len >= 3 &&
        (unsigned char)ptr[0] == 0xEF &&
        (unsigned char)ptr[1] == 0xBB &&
        (unsigned char)ptr[2] == 0xBF) {
        ptr += 3; /* UTF-8 BOM */
    }
    if ((size_t)(end - ptr) < 3 || strncmp(ptr, "---", 3) != 0) {
        *body_offset = (size_t)(ptr - content);
        return true;
    }
    ptr = strchr(ptr, '\n');
    if (!ptr) return false;
    ptr++;
    while (ptr < end) {
        const char *line_end = strchr(ptr, '\n');
        size_t line_len = line_end ? (size_t)(line_end - ptr) : (size_t)(end - ptr);
        if (line_len >= 3 && strncmp(ptr, "---", 3) == 0) {
            *body_offset = (size_t)((line_end ? line_end + 1 : end) - content);
            return true;
        }
        char line[1024];
        size_t copy_len = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
        memcpy(line, ptr, copy_len);
        line[copy_len] = '\0';
        char *eq = strchr(line, ':');
        if (eq) {
            *eq = '\0';
            char *key = trim(line);
            char *val = trim(eq + 1);
            if      (strcmp(key, "title")           == 0) { free(post->title);   post->title   = strdup_safe(val); }
            else if (strcmp(key, "date")            == 0) { free(post->date);    post->date    = strdup_safe(val); }
            else if (strcmp(key, "excerpt")         == 0) { free(post->excerpt); post->excerpt = strdup_safe(val); }
            else if (strcmp(key, "tags")            == 0) { parse_tags(post, val); }
            else if (strcmp(key, "reading_minutes") == 0) { post->reading_minutes = atoi(val); }
        }
        if (!line_end) break;
        ptr = line_end + 1;
    }
    return false;
}

static int compare_posts(const void *a, const void *b) {
    const blog_post_t *pa = (const blog_post_t *)a;
    const blog_post_t *pb = (const blog_post_t *)b;
    if (!pa->slug && !pb->slug) return 0;
    if (!pa->slug) return 1;
    if (!pb->slug) return -1;
    int by_slug = strcmp(pa->slug, pb->slug);
    if (by_slug != 0) return by_slug;
    if (!pa->source_path && !pb->source_path) return 0;
    if (!pa->source_path) return 1;
    if (!pb->source_path) return -1;
    return strcmp(pa->source_path, pb->source_path);
}

static bool collect_posts_for_category(blog_category_t *cat, const char *posts_root) {
    char dir_path[PATH_MAX_LEN];
    if (!join_path_checked(dir_path, sizeof(dir_path), posts_root, cat->id ? cat->id : "")) {
        fprintf(stderr, "[bloggen] path too long for category %s\n", cat->id ? cat->id : "(null)");
        return false;
    }
    DIR *dir = opendir(dir_path);
    if (!dir) { fprintf(stderr, "[bloggen] missing directory %s\n", dir_path); return false; }
    struct dirent *entry;
    size_t count = 0;
    blog_post_t *posts = NULL;
    while ((entry = readdir(dir))) {
        // TODO: Parallelise per-file parsing to avoid serial disk I/O on huge post directories.
        if (entry->d_name[0] == '.') continue;
        const char *dot = strrchr(entry->d_name, '.');
        if (!dot || strcmp(dot, ".md") != 0) continue;
        size_t new_count = count + 1;
        blog_post_t *tmp = realloc(posts, new_count * sizeof(blog_post_t));
        if (!tmp) continue;
        posts = tmp;
        blog_post_t *post = &posts[count];
        memset(post, 0, sizeof(*post));
        size_t slug_len = (size_t)(dot - entry->d_name);
        post->slug = malloc(slug_len + 1);
        memcpy(post->slug, entry->d_name, slug_len);
        post->slug[slug_len] = '\0';
        char file_path[PATH_MAX_LEN];
        if (!join_path_checked(file_path, sizeof(file_path), dir_path, entry->d_name)) {
            fprintf(stderr, "[bloggen] path too long for post %s in %s\n", entry->d_name, dir_path);
            free_post(post);
            continue;
        }
        size_t file_len = 0;
        char *content = read_file(file_path, &file_len);
        if (!content) { fprintf(stderr, "[bloggen] failed to read %s\n", file_path); continue; }
        size_t body_offset = 0;
        if (!parse_front_matter(content, file_len, &body_offset, post)) {
            fprintf(stderr, "[bloggen] invalid front matter in %s\n", file_path);
            free(content); continue;
        }
        if (!post->title) post->title = strdup_safe(post->slug);
        const char *body_ptr = content + body_offset;
        if (!post->excerpt) {
            size_t elen = strlen(body_ptr);
            if (elen > MAX_EXCERPT_LEN) elen = MAX_EXCERPT_LEN;
            char *exc = malloc(elen + 1);
            memcpy(exc, body_ptr, elen);
            exc[elen] = '\0';
            post->excerpt = exc;
        }
        // TODO: Avoid duplicating every post body in memory; stream from disk when building indexes.
        post->body = strdup_safe(body_ptr);
        post->raw_markdown = strdup_safe(content);
        post->source_path = strdup_safe(file_path);
        free(content);
        count = new_count;
    }
    closedir(dir);
    if (posts && count > 1) {
        qsort(posts, count, sizeof(blog_post_t), compare_posts);
        size_t unique = 1;
        for (size_t i = 1; i < count; ++i) {
            blog_post_t *curr = &posts[i];
            bool merged = false;
            for (size_t j = 0; j < unique; ++j) {
                blog_post_t *existing = &posts[j];
                if (existing->slug && curr->slug && strcmp(existing->slug, curr->slug) == 0) {
                    merge_post(existing, curr);
                    fprintf(stderr, "[bloggen] duplicate slug '%s' in category '%s', merged %s into %s\n",
                            curr->slug, cat->id ? cat->id : "(unknown)",
                            curr->source_path ? curr->source_path : "(unknown)",
                            existing->source_path ? existing->source_path : "(unknown)");
                    free_post(curr);
                    memset(curr, 0, sizeof(*curr));
                    merged = true;
                    break;
                }
                if (posts_have_same_content(existing, curr)) {
                    merge_post(existing, curr);
                    fprintf(stderr, "[bloggen] duplicate content '%s' in category '%s', merged %s into %s\n",
                            curr->title ? curr->title : "(untitled)",
                            cat->id ? cat->id : "(unknown)",
                            curr->source_path ? curr->source_path : "(unknown)",
                            existing->source_path ? existing->source_path : "(unknown)");
                    free_post(curr);
                    memset(curr, 0, sizeof(*curr));
                    merged = true;
                    break;
                }
            }
            if (merged) continue;
            if (unique != i) {
                move_post(&posts[unique], curr);
            }
            unique++;
        }
        count = unique;
    }
    cat->posts = posts;
    cat->post_count = count;
    return true;
}

static int compare_categories(const void *a, const void *b) {
    return ((const blog_category_t *)a)->order - ((const blog_category_t *)b)->order;
}

/* ── cwist_sstring helpers ──────────────────────────────────────────────── */

/* Append a formatted string to an sstring (printf-style). */
static void ss_fmt(cwist_sstring *ss, const char *fmt, ...) {
    va_list ap, copy;
    va_start(ap, fmt);
    va_copy(copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed <= 0) { va_end(ap); return; }
    char *tmp = malloc((size_t)needed + 1);
    if (!tmp) { va_end(ap); return; }
    vsnprintf(tmp, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    cwist_sstring_append(ss, tmp);
    free(tmp);
}

/* Append plain text into markdown safely (no raw HTML execution). */
static void append_markdown_safe_text(cwist_sstring *out, const char *s, bool single_line) {
    if (!s) return;
    for (const char *p = s; *p; ++p) {
        char ch = *p;
        if (ch == '&') {
            cwist_sstring_append(out, "&amp;");
            continue;
        }
        if (ch == '<') {
            cwist_sstring_append(out, "&lt;");
            continue;
        }
        if (ch == '>') {
            cwist_sstring_append(out, "&gt;");
            continue;
        }
        if (single_line && (ch == '\r' || ch == '\n')) {
            cwist_sstring_append(out, " ");
            continue;
        }
        if (ch == '\\' || ch == '*' || ch == '_' || ch == '`' ||
            ch == '[' || ch == ']' || ch == '(' || ch == ')' ||
            ch == '#' || ch == '!' || ch == '|') {
            char esc[3] = { '\\', ch, '\0' };
            cwist_sstring_append(out, esc);
            continue;
        }
        char buf[2] = { ch, '\0' };
        cwist_sstring_append(out, buf);
    }
}

/* Appends s as a JSON-escaped string literal (including quotes). */
static void append_json_string(cwist_sstring *out, const char *s);

/* ── Markdown rendering ─────────────────────────────────────────────────── */

static void md_callback(const MD_CHAR *text, MD_SIZE size, void *userdata) {
    cwist_sstring_append_len((cwist_sstring *)userdata, (const char *)text, (size_t)size);
}

static bool render_markdown_text(const char *body, size_t body_len, cwist_sstring *out) {
    if (!body) {
        cwist_sstring_assign_len(out, "", 0);
        return true;
    }
    cwist_sstring_assign_len(out, "", 0);
    int rc = md_html((const MD_CHAR *)body, (MD_SIZE)body_len, md_callback, out,
                     MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS,
                     MD_HTML_FLAG_SKIP_UTF8_BOM);
    return rc == 0;
}

static bool render_markdown(const char *path, cwist_sstring *out) {
    size_t len = 0;
    // TODO: Stream very large markdown files instead of loading entire file into memory.
    char *content = read_file(path, &len);
    if (!content) { fprintf(stderr, "[bloggen] failed to read markdown %s\n", path); return false; }

    /* Skip YAML front matter (--- ... ---) before handing to md4c */
    const char *body = content;
    size_t body_len  = len;
    const char *scan = content;
    if (len >= 3 &&
        (unsigned char)scan[0] == 0xEF &&
        (unsigned char)scan[1] == 0xBB &&
        (unsigned char)scan[2] == 0xBF) {
        scan += 3;
    }
    if ((size_t)(content + len - scan) >= 3 && strncmp(scan, "---", 3) == 0) {
        const char *nl = strchr(scan + 3, '\n');
        if (nl) {
            const char *p = nl + 1;
            while (p < content + len) {
                const char *line_end = strchr(p, '\n');
                size_t line_len = line_end ? (size_t)(line_end - p) : (size_t)(content + len - p);
                if (line_len >= 3 && strncmp(p, "---", 3) == 0) {
                    body     = line_end ? line_end + 1 : content + len;
                    body_len = (size_t)(content + len - body);
                    break;
                }
                if (!line_end) break;
                p = line_end + 1;
            }
        }
    }

    bool ok = render_markdown_text(body, body_len, out);
    free(content);
    return ok;
}

/* ── HTML layout ────────────────────────────────────────────────────────── */

/*
 * render_nav — emits the sticky top navigation bar.
 * active_category is the id of the current category, or NULL for home.
 */
static void render_nav(blog_catalog_t *catalog, const char *active_category,
                       const char *root_prefix, cwist_sstring *out) {
    cwist_sstring_append(out,
        "<header class=\"site-header\">\n"
        "<nav class=\"nav\">\n"
        "<a class=\"brand\" href=\"");
    cwist_sstring_append(out, root_prefix);
    cwist_sstring_append(out, "\">"
        "<span class=\"brand-hex\"><img src=\"");
    cwist_sstring_append(out, root_prefix);
    cwist_sstring_append(out, "assets/favicon/favicon.ico\" alt=\"Style and Grace logo\"></span>"
        "<span>Style and Grace</span>"
        "</a>\n"
        "<ul class=\"nav-list\">\n");
    for (size_t i = 0; i < catalog->count; ++i) {
        blog_category_t *cat = &catalog->items[i];
        if (!cat->id || !cat->title) continue;
        bool active = active_category && strcmp(active_category, cat->id) == 0;
        cwist_sstring_append(out, "<li><a");
        if (active) cwist_sstring_append(out, " class=\"active\"");
        cwist_sstring_append(out, " href=\"");
        cwist_sstring_append(out, root_prefix);
        cwist_sstring_append(out, "category/");
        cwist_sstring_append(out, cat->id);
        cwist_sstring_append(out, "/\">");
        cwist_sstring_append_escaped(out, cat->title);
        cwist_sstring_append(out, "</a></li>\n");
    }
    /* search link */
    cwist_sstring_append(out, "<li><a");
    if (active_category && strcmp(active_category, "search") == 0)
        cwist_sstring_append(out, " class=\"active\"");
    cwist_sstring_append(out, " href=\"");
    cwist_sstring_append(out, root_prefix);
    cwist_sstring_append(out,
        "search/\">"
        "\xea\xb2\x80\xec\x83\x89"   /* 검색 */
        "</a></li>\n");
    cwist_sstring_append(out, "<li><a href=\"");
    cwist_sstring_append(out, root_prefix);
    cwist_sstring_append(out,
        "relations/\">"
        "Relations"
        "</a></li>\n");
    cwist_sstring_append(out,
        "</ul>\n"
        "<div class=\"nav-actions\">"
        "<button class=\"theme-toggle\" type=\"button\" data-theme-toggle"
        " aria-label=\"Light mode\" title=\"Light mode\">☀️ Light</button>"
        "</div>\n"
        "</nav>\n"
        "</header>\n");
}

/*
 * render_accent_style — section accent as CSS variables, including the
 * translucent --accent-dim / --accent-glow derived from a #rrggbb primary.
 */
static void render_accent_style(const char *primary, const char *secondary,
                                cwist_sstring *out) {
    ss_fmt(out, "<style>:root{--accent:%s;--accent-hover:%s;--section-tint:%s;",
           primary, secondary, primary);
    unsigned r, g, b;
    if (primary[0] == '#' && strlen(primary) == 7 &&
        sscanf(primary + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
        ss_fmt(out, "--accent-dim:rgba(%u, %u, %u, 0.12);--accent-glow:rgba(%u, %u, %u, 0.22);",
               r, g, b, r, g, b);
    }
    cwist_sstring_append(out, "}</style>\n");
}

/*
 * render_page — wraps content in the full HTML page shell.
 * accent_primary / accent_secondary: CSS variable overrides (may be NULL).
 * active_category: category id, "search", or NULL; marks the nav link.
 */
static void render_page(blog_catalog_t *catalog,
                        const char *page_title,
                        const char *accent_primary,
                        const char *accent_secondary,
                        const char *active_category,
                        cwist_sstring *main_content,
                        const char *root_prefix,
                        cwist_sstring *out) {
    cwist_sstring_assign_len(out, "", 0);
    cwist_sstring_append(out,
        "<!DOCTYPE html>\n"
        "<html lang=\"ko\">\n"
        "<head>\n"
        "<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "<title>");
    cwist_sstring_append_escaped(out, page_title ? page_title : "Style and Grace");
    cwist_sstring_append(out, "</title>\n<link rel=\"stylesheet\" href=\"");
    cwist_sstring_append(out, root_prefix);
    cwist_sstring_append(out, "assets/styles.css\">\n");
    /* Emscripten page host (blog/ui/blog_ui.c): theme, search UI, and the
     * CWIST component on pages that need it. In <head> so the saved theme
     * lands as early as possible. */
    cwist_sstring_append(out, "<script src=\"");
    cwist_sstring_append(out, root_prefix);
    cwist_sstring_append(out, "assets/blog-ui.js\"></script>\n");
    if (accent_primary && accent_secondary) {
        render_accent_style(accent_primary, accent_secondary, out);
    }
    cwist_sstring_append(out,
        "</head>\n"
        "<body>\n"
        "<div class=\"site-wrap\">\n");
    render_nav(catalog, active_category, root_prefix, out);
    cwist_sstring_append(out, "<main class=\"main\">\n");
    if (main_content && main_content->data) cwist_sstring_append(out, main_content->data);
    cwist_sstring_append(out,
        "\n</main>\n"
        "<footer class=\"site-footer\">\n"
        "<p>GitHub Pages</p>\n"
        "</footer>\n"
        "</div>\n"
        "</body>\n"
        "</html>\n");
}

/* ── Page builders ──────────────────────────────────────────────────────── */

static void build_home(blog_catalog_t *catalog, const char *out_dir) {
    cwist_sstring *content = cwist_sstring_create();
    cwist_sstring *page    = cwist_sstring_create();

    /* hero */
    cwist_sstring_append(content,
        "<section class=\"hero\">\n"
        "<p class=\"hero-label\"></p>\n"
        "<h1 class=\"hero-title\">Style and Grace</h1>\n"
        "<p class=\"hero-desc\">"
        "기록과 생각을 차분하게 풀어내는 개인 블로그입니다."
        "</p>\n"
        "</section>\n");

    /* overview card: chips are the first five category titles */
    cwist_sstring *chips = cwist_sstring_create();
    cwist_sstring_assign(chips, "");
    for (size_t i = 0, n = 0; i < catalog->count && n < 5; ++i) {
        const char *title = catalog->items[i].title;
        if (!catalog->items[i].id || !title || !*title) continue;
        if (n++) cwist_sstring_append(chips, ",");
        cwist_sstring_append(chips, title);
    }
    char *eye_candy = blog_render_home(
        "블로그 빠른 둘러보기",
        "카테고리 중심으로 최근 글 흐름을 바로 확인할 수 있습니다.",
        chips->size ? chips->data : "포스트,카테고리,검색");
    cwist_sstring_append(content, "<section id=\"home-eye-candy\" class=\"home-eye-candy\">");
    if (eye_candy) cwist_sstring_append(content, eye_candy);
    cwist_sstring_append(content, "</section>\n");
    cwist_free(eye_candy);
    cwist_sstring_destroy(chips);

    /* category grid */
    cwist_sstring_append(content,
        "<section>\n"
        "<h2 class=\"section-title\">\xEC\xB9\xB4\xED\x85\x8C\xEA\xB3\xA0\xEB\xA6\xAC</h2>\n"
        "<div class=\"cat-grid\">\n");

    for (size_t i = 0; i < catalog->count; ++i) {
        blog_category_t *cat = &catalog->items[i];
        if (!cat->id || !cat->title) continue;
        cwist_sstring_append(content, "<a class=\"cat-card\" href=\"category/");
        cwist_sstring_append(content, cat->id);
        cwist_sstring_append(content, "/\">\n");
        cwist_sstring_append(content, "<div class=\"cat-card-bar\" style=\"background:linear-gradient(90deg,");
        cwist_sstring_append(content, cat->accent_primary);
        cwist_sstring_append(content, ",");
        cwist_sstring_append(content, cat->accent_secondary);
        cwist_sstring_append(content, ");\"></div>\n");
        cwist_sstring_append(content, "<div class=\"cat-card-body\">\n");
        cwist_sstring_append(content, "<h3 class=\"cat-card-title\">");
        cwist_sstring_append_escaped(content, cat->title);
        cwist_sstring_append(content, "</h3>\n");
        cwist_sstring_append(content, "<p class=\"cat-card-desc\">");
        cwist_sstring_append_escaped(content, cat->description ? cat->description : "");
        cwist_sstring_append(content, "</p>\n");
        cwist_sstring_append(content, "<div class=\"cat-card-meta\">\n");
        cwist_sstring_append(content, "<span class=\"cat-card-count\">");
        ss_fmt(content, "%zu", cat->post_count);
        /* "posts" in Korean: 포스트 */
        cwist_sstring_append(content, " \xED\x8F\xAC\xEC\x8A\xA4\xED\x8A\xB8</span>\n");
        cwist_sstring_append(content, "<span class=\"cat-card-arrow\">&rarr;</span>\n");
        cwist_sstring_append(content, "</div>\n</div>\n</a>\n");
    }
    cwist_sstring_append(content, "</div>\n</section>\n");

    render_page(catalog, "Style and Grace", NULL, NULL, NULL, content, "", page);

    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/index.html", out_dir);
    write_file(path, page->data);

    cwist_sstring_destroy(content);
    cwist_sstring_destroy(page);
}

static void build_category_page(blog_catalog_t *catalog, blog_category_t *cat,
                                const char *out_dir) {
    cwist_sstring *content = cwist_sstring_create();
    cwist_sstring *page    = cwist_sstring_create();
    const char *root = "../../";

    /* page top: breadcrumb + header */
    cwist_sstring_append(content, "<div class=\"page-top\">\n");
    cwist_sstring_append(content,
        "<nav class=\"breadcrumb\">"
        "<a href=\"../../\">\xED\x99\x88</a>"
        "<span class=\"bc-sep\">/</span>"
        "<span>");
    cwist_sstring_append_escaped(content, cat->title ? cat->title : "");
    cwist_sstring_append(content, "</span></nav>\n");

    cwist_sstring_append(content, "<div style=\"display:flex;align-items:center;gap:.6rem\">\n");
    cwist_sstring_append(content,
        "<span class=\"page-title-accent\" style=\"background:linear-gradient(135deg,");
    cwist_sstring_append(content, cat->accent_primary);
    cwist_sstring_append(content, ",");
    cwist_sstring_append(content, cat->accent_secondary);
    cwist_sstring_append(content, ");\"></span>\n");
    cwist_sstring_append(content, "<h1 class=\"page-title\">");
    cwist_sstring_append_escaped(content, cat->title ? cat->title : "");
    cwist_sstring_append(content, "</h1>\n</div>\n");

    cwist_sstring_append(content, "<p class=\"page-desc\">");
    cwist_sstring_append_escaped(content, cat->description ? cat->description : "");
    cwist_sstring_append(content, "</p>\n");

    cwist_sstring_append(content, "<div class=\"page-meta\">\n");
    cwist_sstring_append(content, "<span class=\"post-count-badge\">");
    ss_fmt(content, "%zu", cat->post_count);
    cwist_sstring_append(content, " posts</span>\n</div>\n</div>\n");

    /* post list cards */
    char accent_soft[ACCENT_COLOR_BUFFER_SIZE] = "rgba(255,255,255,0.05)";
    char accent_border[ACCENT_COLOR_BUFFER_SIZE] = "rgba(255,255,255,0.16)";
    if (cat->accent_primary && cat->accent_primary[0] == '#' && strlen(cat->accent_primary) == 7) {
        snprintf(accent_soft, sizeof(accent_soft), "%s18", cat->accent_primary);
        snprintf(accent_border, sizeof(accent_border), "%s44", cat->accent_primary);
    }

    cwist_sstring_append(content, "<section class=\"post-grid\" style=\"--cat-accent:");
    cwist_sstring_append(content, cat->accent_primary ? cat->accent_primary : "#ff6b2b");
    cwist_sstring_append(content, ";--cat-accent-soft:");
    cwist_sstring_append(content, accent_soft);
    cwist_sstring_append(content, ";--cat-accent-border:");
    cwist_sstring_append(content, accent_border);
    cwist_sstring_append(content, ";\">\n");
    for (size_t i = 0; i < cat->post_count; ++i) {
        blog_post_t *post = &cat->posts[i];
        if (!post->slug || !post->title) continue;

        cwist_sstring_append(content, "<a class=\"post-card\" href=\"");
        cwist_sstring_append(content, root);
        cwist_sstring_append(content, "post/");
        cwist_sstring_append(content, cat->id ? cat->id : "");
        cwist_sstring_append(content, "/");
        cwist_sstring_append(content, post->slug);
        cwist_sstring_append(content, "/\">\n");

        if ((post->date && *post->date) || post->reading_minutes > 0) {
            cwist_sstring_append(content, "<div class=\"post-card-meta\">");
            if (post->date && *post->date) {
                cwist_sstring_append(content, "<time>");
                cwist_sstring_append_escaped(content, post->date);
                cwist_sstring_append(content, "</time>");
            }
            if (post->reading_minutes > 0) {
                if (post->date && *post->date) {
                    cwist_sstring_append(content, "<span class=\"dot\">&middot;</span>");
                }
                ss_fmt(content, "<span>%d min read</span>", post->reading_minutes);
            }
            cwist_sstring_append(content, "</div>\n");
        }

        cwist_sstring_append(content, "<h2 class=\"post-card-title\">");
        cwist_sstring_append_escaped(content, post->title);
        cwist_sstring_append(content, "</h2>\n");

        if (post->excerpt && *post->excerpt) {
            cwist_sstring *rendered_excerpt = cwist_sstring_create();
            render_markdown_text(post->excerpt, strlen(post->excerpt), rendered_excerpt);
            cwist_sstring_append(content, "<div class=\"post-card-excerpt\">");
            cwist_sstring_append(content, rendered_excerpt->data);
            cwist_sstring_append(content, "</div>\n");
            cwist_sstring_destroy(rendered_excerpt);
        }

        if (post->tag_count > 0) {
            cwist_sstring_append(content, "<div class=\"tags\">");
            for (size_t t = 0; t < post->tag_count; ++t) {
                cwist_sstring_append(content, "<span class=\"tag\">");
                cwist_sstring_append_escaped(content, post->tags[t]);
                cwist_sstring_append(content, "</span>");
            }
            cwist_sstring_append(content, "</div>\n");
        }

        cwist_sstring_append(content, "</a>\n");
    }
    cwist_sstring_append(content, "</section>\n");

    char page_title_buf[512];
    snprintf(page_title_buf, sizeof(page_title_buf), "%s – Style and Grace",
             cat->title ? cat->title : "");

    render_page(catalog, page_title_buf,
                cat->accent_primary, cat->accent_secondary,
                cat->id, content, root, page);

    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/category/%s/index.html", out_dir, cat->id);
    write_file(path, page->data);

    cwist_sstring_destroy(content);
    cwist_sstring_destroy(page);
}

/* data/comments.json, keyed "<category>/<slug>". Loaded once in main()
 * before the scheduler starts and only read afterwards. */
static cJSON *g_comments;

static bool starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* True when an absolute http(s) URL points at another host than this site. */
static bool is_external_url(const char *url, size_t len) {
    const char *host = NULL;
    if (len > 7 && strncmp(url, "http://", 7) == 0) host = url + 7;
    else if (len > 8 && strncmp(url, "https://", 8) == 0) host = url + 8;
    if (!host) return false;
    size_t host_len = 0;
    while (host + host_len < url + len && !strchr("/:?#", host[host_len])) host_len++;
    return !(host_len == strlen(SITE_HOST) && strncasecmp(host, SITE_HOST, host_len) == 0);
}

/*
 * finalize_article_html — md4c output fix-ups for the static page:
 *   - <x-equation> spans become \( \) / \[ \] TeX for MathJax
 *   - "/assets/..." src/href become relative to root (site lives under /docs/)
 *   - links to other hosts open in a new tab
 */
static void finalize_article_html(const char *html, const char *root, cwist_sstring *out) {
    bool display = false;
    const char *p = html;
    while (*p) {
        if (starts_with(p, "<x-equation type=\"display\">")) {
            cwist_sstring_append(out, "\\[");
            display = true;
            p += strlen("<x-equation type=\"display\">");
        } else if (starts_with(p, "<x-equation>")) {
            cwist_sstring_append(out, "\\(");
            display = false;
            p += strlen("<x-equation>");
        } else if (starts_with(p, "</x-equation>")) {
            cwist_sstring_append(out, display ? "\\]" : "\\)");
            p += strlen("</x-equation>");
        } else if (starts_with(p, "src=\"/assets/") || starts_with(p, "href=\"/assets/") ||
                   starts_with(p, "src='/assets/") || starts_with(p, "href='/assets/")) {
            const char *eq = strchr(p, '=');
            cwist_sstring_append_len(out, p, (size_t)(eq - p));
            cwist_sstring_append(out, "=\"");
            cwist_sstring_append(out, root);
            cwist_sstring_append(out, "assets/");
            p = eq + strlen("=\"/assets/");
        } else if (starts_with(p, "<a href=\"")) {
            const char *url = p + strlen("<a href=\"");
            const char *close = strchr(url, '"');
            if (!close) { cwist_sstring_append(out, p); break; }
            cwist_sstring_append_len(out, p, (size_t)(close + 1 - p));
            if (is_external_url(url, (size_t)(close - url)))
                cwist_sstring_append(out, " target=\"_blank\" rel=\"noopener noreferrer\"");
            p = close + 1;
        } else {
            const char *next = p + 1;
            while (*next && *next != '<' && *next != 's' && *next != 'h') next++;
            cwist_sstring_append_len(out, p, (size_t)(next - p));
            p = next;
        }
    }
}

static void render_article_body(const blog_post_t *post, const char *root, cwist_sstring *out) {
    const char *body = post->body ? post->body : "";
    while (isspace((unsigned char)*body)) body++;
    if (!*body) {
        cwist_sstring_append(out, "<p>이 게시물 본문이 비어 있습니다.</p>");
        return;
    }
    char *html = blog_render_markdown(body, strlen(body), true);
    if (!html) {
        fprintf(stderr, "[bloggen] markdown render failed for %s\n",
                post->source_path ? post->source_path : post->slug);
        return;
    }
    finalize_article_html(html, root, out);
    cwist_free(html);
}

/*
 * render_comment_section — the stored comments for <category>/<slug> plus
 * a plain GET form that opens a prefilled GitHub issue (no script needed).
 */
static void render_comment_section(const blog_category_t *cat, const blog_post_t *post,
                                   cwist_sstring *out) {
    char key[1024];
    snprintf(key, sizeof(key), "%s/%s", cat->id, post->slug ? post->slug : "");
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(g_comments, key);
    int count = cJSON_IsArray(list) ? cJSON_GetArraySize(list) : 0;

    cwist_sstring_append(out,
        "<section class=\"comment-section\" id=\"comment-section\">\n"
        "<h2 class=\"comment-section-title\">댓글</h2>\n"
        "<div class=\"comment-count\">");
    if (count > 0) ss_fmt(out, "%d개의 댓글", count);
    cwist_sstring_append(out, "</div>\n<div class=\"comment-list\">");
    if (count == 0) {
        cwist_sstring_append(out, "<p class=\"no-comments\">아직 댓글이 없습니다.</p>");
    }
    const cJSON *items = count > 0 ? list : NULL;
    const cJSON *c;
    cJSON_ArrayForEach(c, items) {
        const cJSON *author = cJSON_GetObjectItemCaseSensitive(c, "author");
        const cJSON *created = cJSON_GetObjectItemCaseSensitive(c, "created_at");
        const cJSON *body = cJSON_GetObjectItemCaseSensitive(c, "body");
        char date[11] = {0};
        if (cJSON_IsString(created) && created->valuestring)
            strncpy(date, created->valuestring, 10);
        char *html = blog_render_comment(
            cJSON_IsString(author) ? author->valuestring : "", date,
            cJSON_IsString(body) ? body->valuestring : "");
        if (html) cwist_sstring_append(out, html);
        cwist_free(html);
    }
    cwist_sstring_append(out,
        "</div>\n"
        "<form class=\"comment-form\" action=\"https://github.com/" COMMENT_REPO "/issues/new\""
        " method=\"get\" target=\"_blank\" rel=\"noopener noreferrer\">\n"
        "<h3 class=\"comment-form-title\">댓글 작성</h3>\n"
        "<p class=\"comment-form-note\">GitHub 계정으로 댓글을 작성할 수 있습니다.</p>\n"
        "<input type=\"hidden\" name=\"title\" value=\"[comment] ");
    cwist_sstring_append_escaped(out, key);
    cwist_sstring_append(out,
        "\">\n"
        "<input type=\"hidden\" name=\"labels\" value=\"comment\">\n"
        "<textarea name=\"body\" class=\"comment-textarea\" rows=\"4\""
        " placeholder=\"댓글을 입력하세요...\" required></textarea>\n"
        "<button class=\"comment-submit\" type=\"submit\">GitHub으로 댓글 달기</button>\n"
        "</form>\n"
        "</section>\n");
}

static void build_post_page(blog_catalog_t *catalog, blog_category_t *cat,
                            blog_post_t *post, const char *out_dir) {
    cwist_sstring *content = cwist_sstring_create();
    cwist_sstring *page    = cwist_sstring_create();
    const char *root = "../../../";

    /* article wrap */
    cwist_sstring_append(content, "<div class=\"article-wrap\">\n");

    /* breadcrumb */
    cwist_sstring_append(content,
        "<nav class=\"breadcrumb\">"
        "<a href=\"../../../\">\xED\x99\x88</a>"
        "<span class=\"bc-sep\">/</span>");
    cwist_sstring_append(content, "<a href=\"../../../category/");
    cwist_sstring_append(content, cat->id);
    cwist_sstring_append(content, "/\">");
    cwist_sstring_append_escaped(content, cat->title ? cat->title : "");
    cwist_sstring_append(content, "</a>"
        "<span class=\"bc-sep\">/</span>"
        "<span>");
    cwist_sstring_append_escaped(content, post->title ? post->title : "");
    cwist_sstring_append(content, "</span></nav>\n");

    /* article header */
    cwist_sstring_append(content, "<header class=\"article-header\">\n");
    if (post->tag_count > 0) {
        cwist_sstring_append(content, "<div class=\"tags\">\n");
        for (size_t t = 0; t < post->tag_count; ++t) {
            cwist_sstring_append(content, "<span class=\"tag\">");
            cwist_sstring_append_escaped(content, post->tags[t]);
            cwist_sstring_append(content, "</span>\n");
        }
        cwist_sstring_append(content, "</div>\n");
    }
    cwist_sstring_append(content, "<h1 class=\"article-title\">");
    cwist_sstring_append_escaped(content, post->title ? post->title : "");
    cwist_sstring_append(content, "</h1>\n");
    cwist_sstring_append(content, "<div class=\"article-meta\">\n");
    if (post->date && *post->date) {
        cwist_sstring_append(content, "<time>");
        cwist_sstring_append_escaped(content, post->date);
        cwist_sstring_append(content, "</time>\n");
    }
    if (post->reading_minutes > 0) {
        cwist_sstring_append(content, "<span class=\"dot\">&middot;</span>\n");
        ss_fmt(content, "<span>%d min read</span>\n", post->reading_minutes);
    }
    cwist_sstring_append(content, "</div>\n</header>\n");

    cwist_sstring_append(content, "<div class=\"divider\"></div>\n");

    /* article body, rendered here instead of in the browser */
    cwist_sstring *body_html = cwist_sstring_create();
    cwist_sstring_assign(body_html, "");
    render_article_body(post, root, body_html);
    const char *body = body_html->data ? body_html->data : "";
    /* blog-ui.js loads MathJax for bodies marked data-math and runs
     * highlight.js over code blocks. */
    bool has_math = strchr(body, '$') || strstr(body, "\\(") || strstr(body, "\\[");
    cwist_sstring_append(content, "<div class=\"article-body\" id=\"article-body\"");
    if (has_math) cwist_sstring_append(content, " data-math");
    cwist_sstring_append(content, ">");
    cwist_sstring_append(content, body);
    cwist_sstring_append(content, "</div>\n");
    if (strstr(body, "<pre><code")) {
        cwist_sstring_append(content, "<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/github-dark.min.css\">\n");
        cwist_sstring_append(content, "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js\"></script>\n");
    }
    cwist_sstring_destroy(body_html);

    /* article footer: back link */
    cwist_sstring_append(content, "<footer class=\"article-footer\">\n");
    cwist_sstring_append(content, "<a class=\"back-link\" href=\"../../../category/");
    cwist_sstring_append(content, cat->id);
    cwist_sstring_append(content, "/\">&larr; ");
    cwist_sstring_append_escaped(content, cat->title ? cat->title : "");
    cwist_sstring_append(content,
        "\xEB\xA1\x9C \xEB\x8F\x8C\xEC\x95\x84\xEA\xB0\x80\xEA\xB8\xB0"
        "</a>\n</footer>\n");

    render_comment_section(cat, post, content);
    cwist_sstring_append(content, "</div>\n");

    char page_title_buf[512];
    snprintf(page_title_buf, sizeof(page_title_buf), "%s – Style and Grace",
             post->title ? post->title : "");

    render_page(catalog, page_title_buf,
                cat->accent_primary, cat->accent_secondary,
                cat->id, content, root, page);

    {
        char path[PATH_MAX_LEN];
        snprintf(path, sizeof(path), "%s/post/%s/%s/index.html",
                 out_dir, cat->id, post->slug);
        write_file(path, page->data);
    }

    cwist_sstring_destroy(content);
    cwist_sstring_destroy(page);
}

/* ── Search index & search page ─────────────────────────────────────────── */

/*
 * append_json_string — write s as a JSON-encoded double-quoted string.
 * Escapes: " \ and ASCII control characters.
 * Non-ASCII UTF-8 bytes are passed through unchanged (valid in JSON).
 */
static void append_json_string(cwist_sstring *out, const char *s) {
    cwist_sstring_append(out, "\"");
    if (s) {
        for (const char *p = s; *p; ++p) {
            unsigned char c = (unsigned char)*p;
            if      (c == '"')  cwist_sstring_append(out, "\\\"");
            else if (c == '\\') cwist_sstring_append(out, "\\\\");
            else if (c == '\n') cwist_sstring_append(out, "\\n");
            else if (c == '\r') cwist_sstring_append(out, "\\r");
            else if (c == '\t') cwist_sstring_append(out, "\\t");
            else if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                cwist_sstring_append(out, buf);
            } else {
                char ch[2] = { *p, '\0' };
                cwist_sstring_append(out, ch);
            }
        }
    }
    cwist_sstring_append(out, "\"");
}

/*
 * build_search_index — write docs/search-index.json.
 * Each entry: { title, url, summary, tags, date, body }
 */
static void build_search_index(blog_catalog_t *catalog, const char *out_dir) {
    // TODO: Stream JSON directly to disk to sidestep building massive strings for large catalogs.
    cwist_sstring *json = cwist_sstring_create();
    cwist_sstring_append(json, "[\n");
    bool first = true;
    for (size_t i = 0; i < catalog->count; ++i) {
        blog_category_t *cat = &catalog->items[i];
        if (!cat->id) continue;
        for (size_t j = 0; j < cat->post_count; ++j) {
            blog_post_t *post = &cat->posts[j];
            if (!first) cwist_sstring_append(json, ",\n");
            first = false;
            cwist_sstring_append(json, "  {\"title\":");
            append_json_string(json, post->title ? post->title : "");
            cwist_sstring_append(json, ",\"url\":\"/post/");
            cwist_sstring_append(json, cat->id);
            cwist_sstring_append(json, "/");
            cwist_sstring_append(json, post->slug ? post->slug : "");
            cwist_sstring_append(json, "/\",\"summary\":");
            append_json_string(json, post->excerpt ? post->excerpt : "");
            cwist_sstring_append(json, ",\"tags\":[");
            for (size_t t = 0; t < post->tag_count; ++t) {
                if (t) cwist_sstring_append(json, ",");
                append_json_string(json, post->tags[t]);
            }
            cwist_sstring_append(json, "],\"date\":");
            append_json_string(json, post->date ? post->date : "");
            cwist_sstring_append(json, ",\"body\":");
            append_json_string(json, post->body ? post->body : "");
            cwist_sstring_append(json, "}");
        }
    }
    cwist_sstring_append(json, "\n]\n");

    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/search-index.json", out_dir);
    write_file(path, json->data);
    cwist_sstring_destroy(json);
}

/*
 * build_search_page — generate docs/search/index.html.
 * The page contains the search UI; JavaScript loads the WASM module
 * and search-index.json at runtime.
 */
static void build_search_page(blog_catalog_t *catalog, const char *out_dir) {
    cwist_sstring *content = cwist_sstring_create();
    cwist_sstring *page    = cwist_sstring_create();
    const char *root = "../";

    cwist_sstring_append(content,
        "<div class=\"search-wrap\">\n"
        "<h1 class=\"search-page-title\">"
        "\xed\x8f\xac\xec\x8a\xa4\xed\x8a\xb8 \xea\xb2\x80\xec\x83\x89"  /* 포스트 검색 */
        "</h1>\n"
        "<div class=\"search-box\">\n"
        "<input type=\"search\" id=\"search-input\" class=\"search-input\""
        " placeholder=\""
        "\xea\xb2\x80\xec\x83\x89\xec\x96\xb4\xeb\xa5\xbc"  /* 검색어를 */
        " "
        "\xec\x9e\x85\xeb\xa0\xa5\xed\x95\x98\xec\x84\xb8\xec\x9a\x94"  /* 입력하세요 */
        "...\""
        " disabled autocomplete=\"off\" spellcheck=\"false\">\n"
        "</div>\n"
        "<p id=\"search-no-result\" class=\"search-no-result\" hidden>"
        "\xea\xb2\xb0\xea\xb3\xbc \xec\x97\x86\xec\x9d\x8c"  /* 결과 없음 */
        "</p>\n"
        "<div id=\"search-results\" class=\"search-results\""
        " role=\"listbox\" aria-live=\"polite\"></div>\n"
        "</div>\n");
    /* blog-ui.js finds #search-input and loads the CWIST component, which
     * scores and renders results (PUT /search/index, POST /search). */

    render_page(catalog,
        "\xea\xb2\x80\xec\x83\x89 \xe2\x80\x93 Style and Grace",  /* 검색 – … */
        SEARCH_ACCENT_PRIMARY, SEARCH_ACCENT_SECONDARY, "search", content, root, page);

    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/search/index.html", out_dir);
    write_file(path, page->data);

    cwist_sstring_destroy(content);
    cwist_sstring_destroy(page);
}


/*
 * Relations mesh: every post pair is scored with the same tag affinity the
 * component kernel uses (blog_pair_score, blog/wasm/relations.c) and drawn
 * as a static SVG. Hover highlighting is CSS (:has), clicking is a link.
 */
#define REL_W        960.0
#define REL_H        600.0
#define REL_MARGIN   24.0
#define REL_LABEL_CP 18

typedef struct {
    const blog_category_t *cat;
    const blog_post_t *post;
    char *tags;     /* lower-cased, space-joined */
    double x, y;
} rel_node_t;

typedef struct {
    size_t a, b;
    double w;
} rel_edge_t;

static int compare_rel_edges(const void *lhs, const void *rhs) {
    const rel_edge_t *x = lhs, *y = rhs;
    if (x->w != y->w) return x->w < y->w ? 1 : -1;
    if (x->a != y->a) return x->a < y->a ? -1 : 1;
    return x->b < y->b ? -1 : (x->b > y->b);
}

static double clamp_d(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Appends at most max_cp UTF-8 code points of s, HTML-escaped. */
static void append_escaped_prefix(cwist_sstring *out, const char *s, size_t max_cp) {
    size_t bytes = 0, cp = 0;
    while (s[bytes] && cp < max_cp) {
        bytes++;
        while (((unsigned char)s[bytes] & 0xC0) == 0x80) bytes++;
        cp++;
    }
    char *prefix = strndup(s, bytes);
    if (!prefix) return;
    cwist_sstring_append_escaped(out, prefix);
    free(prefix);
}

static void build_relations_page(blog_catalog_t *catalog, const char *out_dir) {
    cwist_sstring *content = cwist_sstring_create();
    cwist_sstring *page    = cwist_sstring_create();
    const char *root = "../";

    size_t n = 0;
    for (size_t i = 0; i < catalog->count; ++i)
        if (catalog->items[i].id) n += catalog->items[i].post_count;
    rel_node_t *nodes = calloc(n ? n : 1, sizeof(*nodes));
    rel_edge_t *edges = calloc(n > 1 ? n * (n - 1) / 2 : 1, sizeof(*edges));
    if (!nodes || !edges) {
        free(nodes); free(edges);
        cwist_sstring_destroy(content); cwist_sstring_destroy(page);
        return;
    }

    /* concentric rings around the centre, clamped into the stage */
    double ring_gap = (REL_W < REL_H ? REL_W : REL_H) * 0.17;
    size_t k = 0, ring = 0, in_ring = 0, ring_slots = 1;
    for (size_t i = 0; i < catalog->count; ++i) {
        const blog_category_t *cat = &catalog->items[i];
        if (!cat->id) continue;
        for (size_t j = 0; j < cat->post_count; ++j, ++k) {
            const blog_post_t *post = &cat->posts[j];
            if (in_ring >= ring_slots) {
                ring++;
                in_ring = 0;
                ring_slots = ring * 8 > 6 ? ring * 8 : 6;
            }
            double angle = (2.0 * M_PI / (double)ring_slots) * (double)in_ring +
                           (double)ring * M_PI / 8.0;
            double radius = (double)ring * ring_gap;
            nodes[k].cat = cat;
            nodes[k].post = post;
            nodes[k].x = clamp_d(REL_W / 2 + cos(angle) * radius, REL_MARGIN, REL_W - REL_MARGIN);
            nodes[k].y = clamp_d(REL_H / 2 + sin(angle) * radius, REL_MARGIN, REL_H - REL_MARGIN);
            in_ring++;

            cwist_sstring *tags = cwist_sstring_create();
            cwist_sstring_assign(tags, "");
            for (size_t t = 0; t < post->tag_count; ++t) {
                if (!post->tags[t] || !*post->tags[t]) continue;
                if (tags->size) cwist_sstring_append(tags, " ");
                for (const char *c = post->tags[t]; *c; ++c) {
                    char lower = (char)tolower((unsigned char)*c);
                    cwist_sstring_append_len(tags, &lower, 1);
                }
            }
            nodes[k].tags = strdup(tags->data ? tags->data : "");
            cwist_sstring_destroy(tags);
        }
    }

    size_t edge_count = 0;
    for (size_t a = 0; a < n; ++a) {
        for (size_t b = a + 1; b < n; ++b) {
            double w = blog_pair_score(nodes[a].tags, nodes[b].tags);
            if (w <= 0) continue;
            edges[edge_count++] = (rel_edge_t){ a, b, w < 10.0 ? w : 10.0 };
        }
    }
    qsort(edges, edge_count, sizeof(*edges), compare_rel_edges);
    size_t edge_cap = n * 4 > 24 ? n * 4 : 24;
    if (edge_cap > 140) edge_cap = 140;
    if (edge_count > edge_cap) edge_count = edge_cap;

    cwist_sstring_append(content,
        "<section class=\"relations-wrap\">\n"
        "<h1 class=\"search-page-title\">Relations Mesh</h1>\n"
        "<p class=\"relations-desc\">CWIST 커널이 모든 게시물의 태그를 비교해 연결 구조를 그립니다.</p>\n"
        "<div class=\"relations-stage\">\n");
    ss_fmt(content,
        "<svg class=\"relations-svg\" viewBox=\"0 0 %.0f %.0f\" role=\"img\""
        " aria-label=\"post relations graph\">\n<g class=\"rel-edges\">\n", REL_W, REL_H);
    for (size_t i = 0; i < edge_count; ++i) {
        const rel_node_t *a = &nodes[edges[i].a];
        const rel_node_t *b = &nodes[edges[i].b];
        double dx = b->x - a->x, dy = b->y - a->y;
        double dist = sqrt(dx * dx + dy * dy);
        double cx = (a->x + b->x) / 2 + dy * 0.08;
        double cy = (a->y + b->y) / 2 -
                    (pow(dist > 1 ? dist : 1, 0.62) * 0.95 + edges[i].w * 0.8);
        ss_fmt(content,
            "<path class=\"rel-edge e%zu e%zu\" style=\"--w:%.2fpx\""
            " d=\"M%.1f %.1fQ%.1f %.1f %.1f %.1f\"/>\n",
            edges[i].a, edges[i].b, 0.45 + edges[i].w * 0.16,
            a->x, a->y, cx, cy, b->x, b->y);
    }
    cwist_sstring_append(content, "</g>\n<g class=\"rel-nodes\">\n");
    for (size_t i = 0; i < n; ++i) {
        const blog_post_t *post = nodes[i].post;
        const char *title = post->title ? post->title : "Untitled";
        ss_fmt(content, "<a class=\"rel-node n%zu\" href=\"%spost/%s/", i, root, nodes[i].cat->id);
        cwist_sstring_append_escaped(content, post->slug ? post->slug : "");
        ss_fmt(content, "/\" transform=\"translate(%.1f %.1f)\"><title>", nodes[i].x, nodes[i].y);
        cwist_sstring_append_escaped(content, title);
        ss_fmt(content, " · 태그 %zu개</title><rect x=\"-6\" y=\"-6\" width=\"12\" height=\"12\"/>"
                        "<text x=\"10\" y=\"-10\">", post->tag_count);
        append_escaped_prefix(content, title, REL_LABEL_CP);
        cwist_sstring_append(content, "</text></a>\n");
    }
    cwist_sstring_append(content, "</g>\n</svg>\n<style>\n");
    for (size_t i = 0; i < n; ++i) {
        ss_fmt(content, ".relations-svg:has(.n%zu:hover) .e%zu", i, i);
        cwist_sstring_append(content, i + 1 < n ? ",\n" : "");
    }
    if (n) cwist_sstring_append(content, "{stroke:rgba(238,179,88,.82);stroke-width:calc(var(--w) + 1.15px)}\n");
    cwist_sstring_append(content, "</style>\n</div>\n");
    ss_fmt(content, "<p class=\"relations-legend\">노드 %zu개 · 연결 %zu개</p>\n</section>\n",
           n, edge_count);

    render_page(catalog,
        "Relations – Style and Grace",
        NULL, NULL, NULL, content, root, page);

    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/relations/index.html", out_dir);
    write_file(path, page->data);

    for (size_t i = 0; i < n; ++i) free(nodes[i].tags);
    free(nodes);
    free(edges);
    cwist_sstring_destroy(content);
    cwist_sstring_destroy(page);
}

/* ── Asset copy ─────────────────────────────────────────────────────────── */

static void copy_assets(const char *src_css, const char *out_dir) {
    char dst_path[PATH_MAX_LEN];
    snprintf(dst_path, sizeof(dst_path), "%s/assets/styles.css", out_dir);
    if (!copy_file(src_css, dst_path)) {
        fprintf(stderr, "[bloggen] failed to copy %s to %s\n", src_css, dst_path);
    }

    const char *slash = strrchr(src_css, '/');
#ifdef _WIN32
    const char *bslash = strrchr(src_css, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    char asset_dir[PATH_MAX_LEN];
    if (slash) {
        size_t len = (size_t)(slash - src_css);
        if (len >= sizeof(asset_dir)) len = sizeof(asset_dir) - 1;
        memcpy(asset_dir, src_css, len);
        asset_dir[len] = '\0';
    } else {
        snprintf(asset_dir, sizeof(asset_dir), ".");
    }

    char favicon_dir[PATH_MAX_LEN];
    char favicon_src[PATH_MAX_LEN];
    if (!join_path_checked(favicon_dir, sizeof(favicon_dir), asset_dir, "favicon") ||
        !join_path_checked(favicon_src, sizeof(favicon_src), favicon_dir, "favicon.ico")) {
        fprintf(stderr, "[bloggen] favicon path is too long under %s\n", asset_dir);
        return;
    }
    struct stat st;
    if (stat(favicon_src, &st) == 0 && S_ISREG(st.st_mode)) {
        char favicon_dst[PATH_MAX_LEN];
        snprintf(favicon_dst, sizeof(favicon_dst), "%s/assets/favicon/favicon.ico", out_dir);
        if (!copy_file(favicon_src, favicon_dst)) {
            fprintf(stderr, "[bloggen] failed to copy %s to %s\n", favicon_src, favicon_dst);
        }
    }
}

/* ── Scheduler bindings ─────────────────────────────────────────────────── */

static size_t blog_contract_get_category_count(const blog_catalog_t *catalog) {
    return catalog ? catalog->count : 0;
}

static size_t blog_contract_get_post_count(const blog_catalog_t *catalog, size_t category_index) {
    if (!catalog || category_index >= catalog->count) return 0;
    return catalog->items[category_index].post_count;
}

static void blog_contract_build_category(blog_catalog_t *catalog, size_t category_index,
                                         const char *out_dir) {
    if (!catalog || category_index >= catalog->count) return;
    blog_category_t *cat = &catalog->items[category_index];
    if (!cat->id) return;
    build_category_page(catalog, cat, out_dir);
}

static void blog_contract_build_post(blog_catalog_t *catalog, size_t category_index,
                                     size_t post_index, const char *out_dir) {
    if (!catalog || category_index >= catalog->count) return;
    blog_category_t *cat = &catalog->items[category_index];
    if (post_index >= cat->post_count || !cat->id) return;
    build_post_page(catalog, cat, &cat->posts[post_index], out_dir);
}

static const blog_scheduler_contract_t BLOG_SCHEDULER_CONTRACT = {
    .get_category_count = blog_contract_get_category_count,
    .get_post_count = blog_contract_get_post_count,
    .build_home = build_home,
    .build_category = blog_contract_build_category,
    .build_post = blog_contract_build_post,
    .build_search_index = build_search_index,
    .build_search_page = build_search_page,
};

/* ── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <categories.cfg> <posts_dir> <assets_css> <out_dir>"
                        " [comments.json]\n", argv[0]);
        return 1;
    }
    const char *categories_cfg = argv[1];
    const char *posts_dir      = argv[2];
    const char *assets_css     = argv[3];
    const char *out_dir        = argv[4];
    const char *comments_json  = argc > 5 ? argv[5] : NULL;

    if (comments_json) {
        char *raw = read_file(comments_json, NULL);
        g_comments = raw ? cJSON_Parse(raw) : NULL;
        free(raw);
        if (!cJSON_IsObject(g_comments)) {
            fprintf(stderr, "[bloggen] ignoring unreadable comments file %s\n", comments_json);
        }
    }

    blog_catalog_t catalog = {0};
    if (!load_categories_cfg(categories_cfg, &catalog)) {
        fprintf(stderr, "[bloggen] no categories defined\n");
        free_catalog(&catalog);
        return 1;
    }

    qsort(catalog.items, catalog.count, sizeof(blog_category_t), compare_categories);
    for (size_t i = 0; i < catalog.count; ++i) {
        if (!catalog.items[i].id) continue;
        collect_posts_for_category(&catalog.items[i], posts_dir);
    }

    copy_assets(assets_css, out_dir);
    if (blog_scheduler_dispatch(&catalog, out_dir, &BLOG_SCHEDULER_CONTRACT) != 0) {
        fprintf(stderr, "[bloggen] scheduler dispatch failed\n");
        cJSON_Delete(g_comments);
        free_catalog(&catalog);
        return 1;
    }

    build_relations_page(&catalog, out_dir);

    cJSON_Delete(g_comments);
    free_catalog(&catalog);
    return 0;
}
