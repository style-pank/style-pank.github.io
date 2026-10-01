#include "blog.h"

#include <cwist/core/html/builder.h>
#include <cwist/core/mem/alloc.h>
#include <cwist/core/sstring/sstring.h>
#include <string.h>

static void append_rendered(cwist_sstring *out, cwist_html_element_t *el) {
    cwist_sstring *rendered = cwist_html_render(el);
    if (rendered && rendered->data) cwist_sstring_append(out, rendered->data);
    if (rendered) cwist_sstring_destroy(rendered);
}

char *blog_render_comment(const char *author, const char *date, const char *body_md) {
    cwist_html_element_t *meta = cwist_html_element_create("div");
    if (!meta) return NULL;
    cwist_html_element_add_class(meta, "comment-meta");

    cwist_html_element_t *author_el = cwist_html_element_create("span");
    if (author_el) {
        cwist_html_element_add_class(author_el, "comment-author");
        cwist_html_element_set_text(author_el, author && *author ? author : "Anonymous");
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

    /* The builder escapes text content, so the rendered markdown body is
     * spliced in around the built meta block instead of via set_text. */
    const char *md = body_md ? body_md : "";
    char *body_html = blog_render_markdown(md, strlen(md), false);

    cwist_sstring *out = cwist_sstring_create();
    cwist_sstring_assign(out, "<div class=\"comment\">");
    append_rendered(out, meta);
    cwist_sstring_append(out, "<div class=\"comment-body\">");
    if (body_html) cwist_sstring_append(out, body_html);
    cwist_sstring_append(out, "</div></div>");

    char *result = cwist_strdup(out->data ? out->data : "");
    cwist_free(body_html);
    cwist_sstring_destroy(out);
    cwist_html_element_destroy(meta);
    return result;
}
