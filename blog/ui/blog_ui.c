/*
 * Style and Grace page host, compiled with Emscripten (assets/blog-ui.js +
 * blog-ui.wasm) and loaded in every page's <head>.
 *
 * WASI 0.2 gives the CWIST guest component (blog/wasm) no DOM, events or
 * storage, so this module owns that side of the page in C:
 *
 *   - theme: saved choice (localStorage) or prefers-color-scheme, the
 *     [data-theme-toggle] buttons
 *   - search page: loads the component (assets/cwist-blog.mjs), pushes
 *     search-index.json into it, debounces input, keyboard navigation
 *   - post pages: hands code blocks to highlight.js and loads MathJax when
 *     the generator marked the body with data-math
 *
 * Talking to the component works the way a client talks to a CWIST server:
 * this file serializes the HTTP/1.1 request, the jco-lowered dispatch export
 * runs the guest's cwist_app, and the serialized response is parsed back here.
 *
 * The EM_JS bodies are the only JavaScript left, and each one is a single
 * browser API call the C side cannot make itself.
 */
#define _GNU_SOURCE
#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/html5.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Browser bindings ───────────────────────────────────────────────────── */
/* clang-format off - EM_JS bodies are JavaScript */

/* Resolves rel against this module's own script URL (pages live at
 * different depths; assets/ is the fixed point). */
EM_JS(char *, js_asset_url, (const char *rel), {
    var base = location.href;
    for (var i = 0; i < document.scripts.length; i++) {
        if (document.scripts[i].src.indexOf("blog-ui.js") !== -1) base = document.scripts[i].src;
    }
    return stringToNewUTF8(new URL(UTF8ToString(rel), base).href);
});

EM_JS(void, js_when_dom_ready, (void), {
    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", function () { _blog_ui_dom_ready(); });
    } else {
        _blog_ui_dom_ready();
    }
});

/* Calls blog_ui_event(id) whenever `type` fires on any element matching sel. */
EM_JS(void, js_listen, (const char *sel, const char *type, int id), {
    var els = document.querySelectorAll(UTF8ToString(sel));
    var t = UTF8ToString(type);
    for (var i = 0; i < els.length; i++) {
        els[i].addEventListener(t, function () { _blog_ui_event(id); });
    }
});

EM_JS(int, js_exists, (const char *sel), {
    return document.querySelector(UTF8ToString(sel)) ? 1 : 0;
});

EM_JS(int, js_count, (const char *sel), {
    return document.querySelectorAll(UTF8ToString(sel)).length;
});

EM_JS(void, js_set_html, (const char *sel, const char *html), {
    var el = document.querySelector(UTF8ToString(sel));
    if (el) el.innerHTML = UTF8ToString(html);
});

EM_JS(void, js_set_text_all, (const char *sel, const char *text), {
    var els = document.querySelectorAll(UTF8ToString(sel));
    for (var i = 0; i < els.length; i++) els[i].textContent = UTF8ToString(text);
});

EM_JS(void, js_set_attr_all, (const char *sel, const char *name, const char *value), {
    var els = document.querySelectorAll(UTF8ToString(sel));
    for (var i = 0; i < els.length; i++) els[i].setAttribute(UTF8ToString(name), UTF8ToString(value));
});

EM_JS(void, js_set_flag, (const char *sel, const char *prop, int on), {
    var el = document.querySelector(UTF8ToString(sel));
    if (el) el[UTF8ToString(prop)] = !!on;
});

EM_JS(void, js_focus, (const char *sel), {
    var el = document.querySelector(UTF8ToString(sel));
    if (el) el.focus();
});

EM_JS(char *, js_value, (const char *sel), {
    var el = document.querySelector(UTF8ToString(sel));
    return stringToNewUTF8(el ? el.value : "");
});

/* Toggles cls so only the idx-th match carries it, and scrolls it into view. */
EM_JS(void, js_mark_nth, (const char *sel, const char *cls, int idx), {
    var els = document.querySelectorAll(UTF8ToString(sel));
    var c = UTF8ToString(cls);
    for (var i = 0; i < els.length; i++) {
        els[i].classList.toggle(c, i === idx);
        if (i === idx) els[i].scrollIntoView({ block: "nearest" });
    }
});

EM_JS(void, js_click_nth, (const char *sel, int idx), {
    var el = document.querySelectorAll(UTF8ToString(sel))[idx];
    if (el) el.click();
});

EM_JS(void, js_set_root_theme, (const char *theme), {
    document.documentElement.setAttribute("data-theme", UTF8ToString(theme));
});

EM_JS(char *, js_storage_get, (const char *key), {
    try {
        var v = localStorage.getItem(UTF8ToString(key));
        return v === null ? 0 : stringToNewUTF8(v);
    } catch (_) {
        return 0;
    }
});

EM_JS(void, js_storage_set, (const char *key, const char *value), {
    try { localStorage.setItem(UTF8ToString(key), UTF8ToString(value)); } catch (_) {}
});

EM_JS(int, js_prefers_light, (void), {
    return window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches ? 1 : 0;
});

EM_JS(void, js_highlight_code, (void), {
    if (window.hljs && typeof window.hljs.highlightAll === "function") window.hljs.highlightAll();
});

/* MathJax reads its configuration from window.MathJax before it loads.
 * Backslashes are doubled once more because EM_JS bodies are unescaped
 * one extra time on their way into the glue. */
EM_JS(void, js_load_mathjax, (const char *src), {
    window.MathJax = {
        tex: { inlineMath: [["$", "$"], ["\\\\(", "\\\\)"]], displayMath: [["$$", "$$"], ["\\\\[", "\\\\]"]] },
        svg: { fontCache: "global" }
    };
    var s = document.createElement("script");
    s.src = UTF8ToString(src);
    s.async = true;
    document.head.appendChild(s);
});

/* Fetches url; calls blog_ui_fetched(tag, bytes, len) with a NUL-terminated
 * malloc'd buffer the C side owns, or (tag, 0, 0) on failure. */
EM_JS(void, js_fetch, (const char *url, int tag), {
    fetch(UTF8ToString(url), { cache: "no-store" })
        .then(function (r) {
            if (!r.ok) throw new Error("HTTP " + r.status);
            return r.arrayBuffer();
        })
        .then(function (buf) {
            var b = new Uint8Array(buf);
            var p = _malloc(b.length + 1);
            HEAPU8.set(b, p);
            HEAPU8[p + b.length] = 0;
            _blog_ui_fetched(tag, p, b.length);
        })
        .catch(function (e) {
            console.error("[blog-ui] fetch failed", e);
            _blog_ui_fetched(tag, 0, 0);
        });
});

/* Imports the jco-transpiled CWIST component and waits for instantiation. */
EM_JS(void, js_load_component, (const char *url), {
    import(UTF8ToString(url))
        .then(function (m) {
            return m.$init.then(function () {
                Module.cwistGuest = m.guest;
                _blog_ui_component_ready(1);
            });
        })
        .catch(function (e) {
            console.error("[blog-ui] CWIST component failed to load", e);
            _blog_ui_component_ready(0);
        });
});

/* guest.dispatch: request bytes in, response bytes out (malloc'd, length in
 * *out_len), NULL when the guest returns a dispatch-error. */
EM_JS(uint8_t *, js_component_dispatch, (const uint8_t *req, size_t len, size_t *out_len), {
    try {
        var res = Module.cwistGuest.dispatch(HEAPU8.slice(req, req + len));
        var p = _malloc(res.length + 1);
        HEAPU8.set(res, p);
        HEAPU8[p + res.length] = 0;
        HEAPU32[out_len >> 2] = res.length;
        return p;
    } catch (e) {
        console.error("[blog-ui] CWIST dispatch failed", e);
        return 0;
    }
});

/* clang-format on */

/* ── CWIST client: serialized HTTP/1.1 over the component boundary ─────── */

typedef struct {
    int status;   /* 0 when the dispatch itself failed */
    char *body;   /* NUL-terminated, owned; never NULL when status != 0 */
} cwist_reply_t;

static cwist_reply_t cwist_call(const char *method, const char *path,
                                const char *body, size_t body_len) {
    cwist_reply_t reply = {0, NULL};
    char head[256];
    int head_len = snprintf(head, sizeof(head),
                            "%s %s HTTP/1.1\r\n"
                            "Host: wasm\r\n"
                            "Content-Type: text/plain; charset=utf-8\r\n"
                            "Content-Length: %zu\r\n"
                            "Connection: close\r\n\r\n",
                            method, path, body_len);
    if (head_len < 0 || (size_t)head_len >= sizeof(head)) return reply;

    uint8_t *req = malloc((size_t)head_len + body_len);
    if (!req) return reply;
    memcpy(req, head, (size_t)head_len);
    if (body_len) memcpy(req + head_len, body, body_len);

    size_t res_len = 0;
    uint8_t *res = js_component_dispatch(req, (size_t)head_len + body_len, &res_len);
    free(req);
    if (!res) return reply;

    /* "HTTP/1.1 200 OK\r\n...\r\n\r\n<body>" */
    const char *text = (const char *)res;
    const char *sep = strstr(text, "\r\n\r\n");
    int status = 0;
    if (sep && sscanf(text, "HTTP/%*d.%*d %d", &status) == 1) {
        reply.status = status;
        reply.body = strdup(sep + 4);
    }
    free(res);
    if (reply.status && !reply.body) reply.status = 0;
    return reply;
}

/* ── Theme ──────────────────────────────────────────────────────────────── */

#define THEME_STORAGE_KEY "cwist-theme"

static bool g_light;

static void update_theme_buttons(void) {
    const char *next = g_light ? "dark mode" : "light mode";
    js_set_attr_all("[data-theme-toggle]", "aria-label", next);
    js_set_attr_all("[data-theme-toggle]", "title", next);
    js_set_text_all("[data-theme-toggle]", g_light ? "🌙 Dark" : "☀️ Light");
}

static void apply_theme(bool light) {
    g_light = light;
    js_set_root_theme(light ? "light" : "dark");
    update_theme_buttons();
}

/* Saved choice first, then the OS preference, dark by default. */
static bool resolve_light_theme(void) {
    char *saved = js_storage_get(THEME_STORAGE_KEY);
    bool light;
    if (saved && strcmp(saved, "light") == 0) light = true;
    else if (saved && strcmp(saved, "dark") == 0) light = false;
    else light = js_prefers_light();
    free(saved);
    return light;
}

static void toggle_theme(void) {
    apply_theme(!g_light);
    js_storage_set(THEME_STORAGE_KEY, g_light ? "light" : "dark");
}

/* ── Search page ────────────────────────────────────────────────────────── */

#define SEARCH_INPUT     "#search-input"
#define SEARCH_RESULTS   "#search-results"
#define SEARCH_ITEMS     "#search-results .search-result"
#define SEARCH_NO_RESULT "#search-no-result"
#define SEARCH_DEBOUNCE_MS 120

enum { EVENT_THEME_TOGGLE = 1, EVENT_SEARCH_INPUT = 2 };
enum { FETCH_SEARCH_INDEX = 1 };

static int g_component_state;  /* 0 loading, 1 ready, -1 failed */
static char *g_index_json;     /* until it has been pushed into the component */
static size_t g_index_len;
static bool g_index_fetched;
static bool g_index_loaded;
static int g_cursor = -1;
static long g_debounce_timer;

static void show_results(const char *html, bool searched) {
    js_set_html(SEARCH_RESULTS, html);
    js_set_flag(SEARCH_NO_RESULT, "hidden", !searched || *html != '\0');
    g_cursor = -1;
}

static void render_search(void) {
    char *raw = js_value(SEARCH_INPUT);
    char *q = raw ? raw : "";
    while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
    size_t len = strlen(q);
    while (len && (q[len - 1] == ' ' || q[len - 1] == '\t' || q[len - 1] == '\n' ||
                   q[len - 1] == '\r'))
        len--;

    if (!len || !g_index_loaded) {
        show_results("", false);
    } else {
        cwist_reply_t r = cwist_call("POST", "/search", q, len);
        show_results(r.status >= 200 && r.status < 400 ? r.body : "", true);
        free(r.body);
    }
    free(raw);
}

static void debounced_render(void *user_data) {
    (void)user_data;
    g_debounce_timer = 0;
    render_search();
}

/* Runs once both the component and search-index.json are in. */
static void maybe_push_index(void) {
    if (g_component_state == 0 || !g_index_fetched) return;
    if (g_component_state == 1 && g_index_json) {
        cwist_reply_t r = cwist_call("PUT", "/search/index", g_index_json, g_index_len);
        g_index_loaded = r.status >= 200 && r.status < 300;
        if (!g_index_loaded)
            emscripten_console_errorf("[blog-ui] search index rejected (%d)", r.status);
        free(r.body);
    }
    free(g_index_json);
    g_index_json = NULL;

    js_set_flag(SEARCH_INPUT, "disabled", 0);
    if (g_index_loaded) {
        js_focus(SEARCH_INPUT);
        render_search();
    }
}

static bool on_search_keydown(int type, const EmscriptenKeyboardEvent *e, void *user_data) {
    (void)type;
    (void)user_data;
    int count = js_count(SEARCH_ITEMS);
    if (!count) return false;

    if (strcmp(e->key, "ArrowDown") == 0) {
        g_cursor = g_cursor + 1 < count ? g_cursor + 1 : count - 1;
    } else if (strcmp(e->key, "ArrowUp") == 0) {
        g_cursor = g_cursor - 1 > -1 ? g_cursor - 1 : -1;
    } else if (strcmp(e->key, "Enter") == 0 && g_cursor >= 0) {
        js_click_nth(SEARCH_ITEMS, g_cursor);
        return true;
    } else {
        return false;
    }
    js_mark_nth(SEARCH_ITEMS, "focused", g_cursor);
    return true; /* preventDefault */
}

static void init_search(void) {
    char *component = js_asset_url("cwist-blog.mjs");
    char *index = js_asset_url("../search-index.json");
    js_load_component(component);
    js_fetch(index, FETCH_SEARCH_INDEX);
    free(component);
    free(index);

    js_listen(SEARCH_INPUT, "input", EVENT_SEARCH_INPUT);
    emscripten_set_keydown_callback(SEARCH_INPUT, NULL, false, on_search_keydown);
}

/* ── Exports called back from the browser ───────────────────────────────── */

EMSCRIPTEN_KEEPALIVE void blog_ui_event(int id) {
    if (id == EVENT_THEME_TOGGLE) {
        toggle_theme();
    } else if (id == EVENT_SEARCH_INPUT) {
        if (g_debounce_timer) emscripten_clear_timeout(g_debounce_timer);
        g_debounce_timer = emscripten_set_timeout(debounced_render, SEARCH_DEBOUNCE_MS, NULL);
    }
}

EMSCRIPTEN_KEEPALIVE void blog_ui_component_ready(int ok) {
    g_component_state = ok ? 1 : -1;
    maybe_push_index();
}

EMSCRIPTEN_KEEPALIVE void blog_ui_fetched(int tag, char *bytes, size_t len) {
    if (tag != FETCH_SEARCH_INDEX) {
        free(bytes);
        return;
    }
    g_index_json = bytes;
    g_index_len = len;
    g_index_fetched = true;
    maybe_push_index();
}

EMSCRIPTEN_KEEPALIVE void blog_ui_dom_ready(void) {
    js_listen("[data-theme-toggle]", "click", EVENT_THEME_TOGGLE);
    update_theme_buttons();

    if (js_exists(SEARCH_INPUT)) init_search();

    if (js_exists("pre code")) js_highlight_code();
    if (js_exists("[data-math]")) js_load_mathjax("https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-svg.js");
}

int main(void) {
    /* <html> exists while <head> parses, so the theme lands before the body
     * paints whenever the module is ready in time. */
    apply_theme(resolve_light_theme());
    js_when_dom_ready();
    return 0;
}
