#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#  define WASM_EXPORT
#endif

#include <cwist/core/html/builder.h>
#include <cwist/core/mem/alloc.h>
#include <cwist/core/sstring/sstring.h>
#include <md4c-html.h>
#include <string.h>

static void md_process_output(const MD_CHAR* text, MD_SIZE size, void* userdata) {
    cwist_sstring *ss = (cwist_sstring*)userdata;
    cwist_sstring_append_len(ss, text, size);
}

static const char *safe_text(const char *s) {
    return s ? s : "";
}

WASM_EXPORT
char *cwist_render_comment(const char *author, const char *date, const char *body_md) {
    cwist_html_element_t *comment = cwist_html_element_create("div");
    if (!comment) return NULL;
    cwist_html_element_add_class(comment, "comment");

    cwist_html_element_t *meta = cwist_html_element_create("div");
    if (meta) {
        cwist_html_element_add_class(meta, "comment-meta");
        cwist_html_element_add_child(comment, meta);

        cwist_html_element_t *author_el = cwist_html_element_create("span");
        if (author_el) {
            cwist_html_element_add_class(author_el, "comment-author");
            cwist_html_element_set_text(author_el, safe_text(author));
            cwist_html_element_add_child(meta, author_el);
        }

        if (date && *date) {
            cwist_html_element_t *date_el = cwist_html_element_create("span");
            if (date_el) {
                cwist_html_element_add_class(date_el, "comment-date");
                cwist_html_element_set_text(date_el, date);
                cwist_html_element_add_child(meta, date_el);
            }
        }
    }

    cwist_sstring *body_ss = cwist_sstring_create();
    cwist_sstring_assign(body_ss, "");
    md_html(body_md, (MD_SIZE)strlen(body_md), md_process_output, body_ss, 
            MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS, MD_HTML_FLAG_SKIP_UTF8_BOM);

    cwist_html_element_t *body = cwist_html_element_create("div");
    if (body) {
        cwist_html_element_add_class(body, "comment-body");
        cwist_html_element_set_text(body, body_ss->data);
        cwist_html_element_add_child(comment, body);
    }
    cwist_sstring_destroy(body_ss);

    cwist_sstring *rendered = cwist_html_render(comment);
    char *result = NULL;
    if (rendered && rendered->data) {
        result = cwist_strdup(rendered->data);
    } else {
        result = cwist_strdup("");
    }

    if (rendered) cwist_sstring_destroy(rendered);
    cwist_html_element_destroy(comment);
    return result;
}
