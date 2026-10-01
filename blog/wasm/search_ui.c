#include "blog.h"

#include <cwist/core/html/builder.h>
#include <cwist/core/mem/alloc.h>
#include <cwist/core/sstring/sstring.h>
#include <string.h>

static const char *safe_href(const char *url) {
    if (!url || !*url) return "/";
    if (url[0] == '/') return url;
    return "#";
}

static char *render_and_destroy(cwist_html_element_t *el) {
    cwist_sstring *rendered = cwist_html_render(el);
    char *result = cwist_strdup(rendered && rendered->data ? rendered->data : "");
    if (rendered) cwist_sstring_destroy(rendered);
    cwist_html_element_destroy(el);
    return result;
}

static void add_text_child(cwist_html_element_t *parent, const char *tag,
                           const char *cls, const char *text) {
    cwist_html_element_t *el = cwist_html_element_create(tag);
    if (!el) return;
    cwist_html_element_add_class(el, cls);
    cwist_html_element_set_text(el, text ? text : "");
    cwist_html_element_add_child(parent, el);
}

char *blog_render_search_result(const char *title, const char *url,
                                const char *summary, const char *tags) {
    cwist_html_element_t *card = cwist_html_element_create("a");
    if (!card) return NULL;
    cwist_html_element_add_class(card, "search-result");
    cwist_html_element_add_attr(card, "href", safe_href(url));

    add_text_child(card, "div", "sr-title", title);
    if (summary && *summary) add_text_child(card, "div", "sr-summary", summary);
    if (tags && *tags) add_text_child(card, "div", "sr-tags", tags);

    return render_and_destroy(card);
}

char *blog_render_home(const char *title, const char *description, const char *chips_csv) {
    cwist_html_element_t *wrap = cwist_html_element_create("section");
    if (!wrap) return NULL;
    cwist_html_element_add_class(wrap, "han-eye");

    cwist_html_element_t *header = cwist_html_element_create("div");
    if (header) {
        cwist_html_element_add_class(header, "han-eye-header");
        cwist_html_element_add_child(wrap, header);
        add_text_child(header, "h2", "han-eye-title", title);
        add_text_child(header, "p", "han-eye-desc", description);
    }

    cwist_html_element_t *palette = cwist_html_element_create("div");
    if (palette) {
        cwist_html_element_add_class(palette, "han-palette");
        cwist_html_element_add_child(wrap, palette);

        char *copy = cwist_strdup(chips_csv ? chips_csv : "");
        char *save = NULL;
        for (char *tok = copy ? strtok_r(copy, ",", &save) : NULL; tok;
             tok = strtok_r(NULL, ",", &save)) {
            add_text_child(palette, "span", "han-chip", tok);
        }
        cwist_free(copy);
    }

    return render_and_destroy(wrap);
}
