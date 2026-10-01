/*
 * Style and Grace render guest: a CWIST app compiled for wasm32-wasip2 and
 * exported through the cwist-guest world (lib/cwist/wit/cwist.wit).
 *
 * The browser talks to it the same way a client talks to a CWIST server:
 * serialized HTTP/1.1 requests in, serialized responses out, through the
 * jco-transpiled component and the cwist-wasm adapter (assets/cwist-blog.js).
 *
 *   POST /render/markdown   body: markdown            -> text/html
 *   POST /render/comments   body: [{author,created_at,body}] -> text/html
 *   POST /render/home       body: {title,description,chips}  -> text/html
 *   PUT  /search/index      body: search-index.json   -> 204
 *   POST /search            body: query               -> text/html
 *   POST /relations         body: search-index.json   -> application/json
 *   POST /theme             body: location.pathname   -> application/json
 *
 * Generated bindings (cwist_guest.h / cwist_guest.c) come from wit-bindgen
 * at build time and are never committed.
 */
#include <cwist/sys/app/app.h>
#include <cwist/wasm/wasm_component.h>
#include <cwist/core/mem/alloc.h>
#include <cJSON.h>
#include <string.h>

#include "blog.h"
#include "cwist_guest.h"

static cwist_app *g_app;
static cJSON *g_search_index;

static const char *body_of(cwist_http_request *req, size_t *len) {
    if (req->body && req->body->data) {
        if (len) *len = req->body->size;
        return req->body->data;
    }
    if (len) *len = 0;
    return "";
}

/* Hands an owned heap string to the response body. */
static void reply(cwist_http_response *res, char *owned, const char *content_type) {
    if (!owned) {
        res->status_code = CWIST_HTTP_INTERNAL_ERROR;
        cwist_sstring_assign(res->body, "render failed");
        return;
    }
    cwist_sstring_assign(res->body, owned);
    cwist_free(owned);
    cwist_http_header_add(&res->headers, "Content-Type", content_type);
}

static void bad_request(cwist_http_response *res, const char *why) {
    res->status_code = CWIST_HTTP_BAD_REQUEST;
    cwist_sstring_assign(res->body, why);
    cwist_http_header_add(&res->headers, "Content-Type", "text/plain; charset=utf-8");
}

static const char *json_str(const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

static void render_markdown_handler(cwist_http_request *req, cwist_http_response *res) {
    size_t len = 0;
    const char *md = body_of(req, &len);
    reply(res, blog_render_markdown(md, len, true), "text/html; charset=utf-8");
}

static void render_comments_handler(cwist_http_request *req, cwist_http_response *res) {
    cJSON *list = cJSON_Parse(body_of(req, NULL));
    if (!cJSON_IsArray(list)) {
        cJSON_Delete(list);
        bad_request(res, "expected a JSON array of comments");
        return;
    }

    cwist_sstring *out = cwist_sstring_create();
    cwist_sstring_assign(out, "");
    const cJSON *c;
    cJSON_ArrayForEach(c, list) {
        char date[11] = {0};
        strncpy(date, json_str(c, "created_at"), 10);
        char *html = blog_render_comment(json_str(c, "author"), date, json_str(c, "body"));
        if (html) {
            cwist_sstring_append(out, html);
            cwist_free(html);
        }
    }
    cJSON_Delete(list);
    reply(res, cwist_strdup(out->data ? out->data : ""), "text/html; charset=utf-8");
    cwist_sstring_destroy(out);
}

static void render_home_handler(cwist_http_request *req, cwist_http_response *res) {
    cJSON *in = cJSON_Parse(body_of(req, NULL));
    if (!cJSON_IsObject(in)) {
        cJSON_Delete(in);
        bad_request(res, "expected {title, description, chips}");
        return;
    }
    reply(res, blog_render_home(json_str(in, "title"), json_str(in, "description"),
                                json_str(in, "chips")),
          "text/html; charset=utf-8");
    cJSON_Delete(in);
}

static void search_index_handler(cwist_http_request *req, cwist_http_response *res) {
    cJSON *index = cJSON_Parse(body_of(req, NULL));
    if (!cJSON_IsArray(index)) {
        cJSON_Delete(index);
        bad_request(res, "expected the search-index.json array");
        return;
    }
    cJSON_Delete(g_search_index);
    g_search_index = index;
    res->status_code = CWIST_HTTP_NO_CONTENT;
}

static void search_handler(cwist_http_request *req, cwist_http_response *res) {
    if (!g_search_index) {
        res->status_code = CWIST_HTTP_CONFLICT;
        cwist_sstring_assign(res->body, "search index not loaded");
        return;
    }
    reply(res, blog_search_and_render(g_search_index, body_of(req, NULL)),
          "text/html; charset=utf-8");
}

static void relations_handler(cwist_http_request *req, cwist_http_response *res) {
    cJSON *index = cJSON_Parse(body_of(req, NULL));
    if (!cJSON_IsArray(index)) {
        cJSON_Delete(index);
        bad_request(res, "expected the search-index.json array");
        return;
    }
    reply(res, blog_relations_json(index), "application/json");
    cJSON_Delete(index);
}

static void theme_handler(cwist_http_request *req, cwist_http_response *res) {
    reply(res, blog_theme_json(body_of(req, NULL)), "application/json");
}

static cwist_app *app(void) {
    if (g_app) return g_app;
    g_app = cwist_app_create();
    if (!g_app) return NULL;
    cwist_app_post(g_app, "/render/markdown", render_markdown_handler);
    cwist_app_post(g_app, "/render/comments", render_comments_handler);
    cwist_app_post(g_app, "/render/home", render_home_handler);
    cwist_app_put(g_app, "/search/index", search_index_handler);
    cwist_app_post(g_app, "/search", search_handler);
    cwist_app_post(g_app, "/relations", relations_handler);
    cwist_app_post(g_app, "/theme", theme_handler);
    return g_app;
}

/* The canonical ABI copies the returned list out of guest memory and frees
 * it through cabi_post, so the response must be a libc allocation, which
 * cwist_alloc is under __wasi__. */
bool exports_c4punks_cwist_guest_dispatch(cwist_guest_list_u8_t *request,
                                          cwist_guest_list_u8_t *ret,
                                          exports_c4punks_cwist_guest_dispatch_error_t *err) {
    cwist_app *a = app();
    if (!a) {
        err->tag = EXPORTS_C4PUNKS_CWIST_GUEST_DISPATCH_ERROR_NOT_INITIALIZED;
        return false;
    }
    uint8_t *res_buf = NULL;
    size_t res_len = 0;
    if (cwist_wasm_component_dispatch(a, request->ptr, request->len, &res_buf, &res_len) != 0) {
        err->tag = EXPORTS_C4PUNKS_CWIST_GUEST_DISPATCH_ERROR_INVALID_REQUEST;
        return false;
    }
    ret->ptr = res_buf;
    ret->len = res_len;
    return true;
}

/* The blog keeps no sessions; accept the host's secret so the world's
 * contract holds. */
bool exports_c4punks_cwist_guest_use_session(cwist_guest_string_t *maybe_secret,
                                             exports_c4punks_cwist_guest_session_error_t *err) {
    (void)maybe_secret;
    (void)err;
    return true;
}

int main(void) {
    return app() ? 0 : 1;
}
