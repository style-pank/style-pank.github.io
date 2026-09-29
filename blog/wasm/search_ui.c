#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#  define WASM_EXPORT
#endif

#include <cwist/core/html/builder.h>
#include <cwist/core/mem/alloc.h>
#include <cwist/core/mem/gc.h>
#include <cwist/core/sstring/sstring.h>
#include <md4c-html.h>

#include <stdbool.h>
#include <string.h>

static void md_process_output(const MD_CHAR* text, MD_SIZE size, void* userdata) {
    cwist_sstring *ss = (cwist_sstring*)userdata;
    cwist_sstring_append_len(ss, text, size);
}

static const char *safe_text(const char *s) {
    return s ? s : "";
}

static const char *safe_href(const char *url) {
    if (!url || !*url) return "/";
    if (url[0] == '/') return url;
    return "#";
}

WASM_EXPORT
char *cwist_render_search_result(const char *title,
                                 const char *url,
                                 const char *summary,
                                 const char *tags,
                                 const char *query) {
    (void)query;

    cwist_html_element_t *card = cwist_html_element_create("a");
    if (!card) return NULL;
    cwist_html_element_add_class(card, "search-result");
    cwist_html_element_add_attr(card, "href", safe_href(url));

    cwist_html_element_t *title_el = cwist_html_element_create("div");
    if (!title_el) {
        cwist_html_element_destroy(card);
        return NULL;
    }
    cwist_html_element_add_class(title_el, "sr-title");
    cwist_html_element_set_text(title_el, safe_text(title));
    cwist_html_element_add_child(card, title_el);

    if (summary && *summary) {
        cwist_sstring *body_ss = cwist_sstring_create();
        cwist_sstring_assign(body_ss, "");
        md_html(summary, (MD_SIZE)strlen(summary), md_process_output, body_ss, 
                MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS, MD_HTML_FLAG_SKIP_UTF8_BOM);

        cwist_html_element_t *summary_el = cwist_html_element_create("div");
        if (summary_el) {
            cwist_html_element_add_class(summary_el, "sr-summary");
            cwist_html_element_set_text(summary_el, body_ss->data);
            cwist_html_element_add_child(card, summary_el);
        }
        cwist_sstring_destroy(body_ss);
    }

    if (tags && *tags) {
        cwist_html_element_t *tags_el = cwist_html_element_create("div");
        if (tags_el) {
            cwist_html_element_add_class(tags_el, "sr-tags");
            cwist_html_element_set_text(tags_el, tags);
            cwist_html_element_add_child(card, tags_el);
        }
    }

    cwist_sstring *rendered = cwist_html_render(card);
    char *result = NULL;
    if (rendered && rendered->data) {
        result = cwist_strdup(rendered->data);
    } else {
        result = cwist_strdup("");
    }

    if (rendered) cwist_sstring_destroy(rendered);
    cwist_html_element_destroy(card);
    return result;
}

WASM_EXPORT
char *cwist_render_home_eye_candy(const char *title,
                                  const char *description,
                                  const char *palette_csv) {
    cwist_gc_t gc = {0};
    cwist_gc(&gc, true);

    cwist_html_element_t *wrap = cwist_html_element_create("section");
    if (!wrap) {
        cwist_gc_shutdown(&gc);
        return NULL;
    }
    cwist_html_element_add_class(wrap, "han-eye");

    cwist_html_element_t *header = cwist_html_element_create("div");
    if (header) {
        cwist_html_element_add_class(header, "han-eye-header");
        cwist_html_element_add_child(wrap, header);

        cwist_html_element_t *title_el = cwist_html_element_create("h2");
        if (title_el) {
            cwist_html_element_add_class(title_el, "han-eye-title");
            cwist_html_element_set_text(title_el, safe_text(title));
            cwist_html_element_add_child(header, title_el);
        }

        cwist_html_element_t *desc_el = cwist_html_element_create("p");
        if (desc_el) {
            cwist_html_element_add_class(desc_el, "han-eye-desc");
            cwist_html_element_set_text(desc_el, safe_text(description));
            cwist_html_element_add_child(header, desc_el);
        }
    }

    cwist_html_element_t *palette = cwist_html_element_create("div");
    if (palette) {
        cwist_html_element_add_class(palette, "han-palette");
        cwist_html_element_add_child(wrap, palette);

        char *copy = cwist_strdup(safe_text(palette_csv));
        cwist_reg_ptr(&gc, copy);
        
        if (copy) {
            char *token = strtok(copy, ",");
            while (token) {
                cwist_html_element_t *chip = cwist_html_element_create("span");
                if (chip) {
                    cwist_html_element_add_class(chip, "han-chip");
                    cwist_html_element_set_text(chip, token);
                    cwist_html_element_add_child(palette, chip);
                }
                token = strtok(NULL, ",");
            }
        }
    }

    cwist_sstring *rendered = cwist_html_render(wrap);
    char *result = NULL;
    if (rendered && rendered->data) {
        result = cwist_strdup(rendered->data);
    } else {
        result = cwist_strdup("");
    }

    if (rendered) cwist_sstring_destroy(rendered);
    cwist_html_element_destroy(wrap);
    cwist_gc_shutdown(&gc);
    return result;
}

WASM_EXPORT
void cwist_free_html(char *html) {
    cwist_free(html);
}
