#include "blog.h"

#include <md4c-html.h>
#include <cwist/core/mem/alloc.h>
#include <cwist/core/sstring/sstring.h>

static void md_process_output(const MD_CHAR *text, MD_SIZE size, void *userdata) {
    cwist_sstring_append_len((cwist_sstring *)userdata, text, size);
}

char *blog_render_markdown(const char *markdown, size_t len, bool allow_html) {
    if (!markdown) return NULL;

    cwist_sstring *ss = cwist_sstring_create();
    if (!ss) return NULL;
    cwist_sstring_assign(ss, "");

    unsigned flags = MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS | MD_FLAG_LATEXMATHSPANS;
    if (!allow_html) flags |= MD_FLAG_NOHTML;

    int rc = md_html(markdown, (MD_SIZE)len, md_process_output, ss, flags,
                     MD_HTML_FLAG_SKIP_UTF8_BOM);

    char *out = rc == 0 ? cwist_strdup(ss->data ? ss->data : "") : NULL;
    cwist_sstring_destroy(ss);
    return out;
}
