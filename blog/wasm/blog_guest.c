/*
 * Style and Grace render guest: a CWIST app compiled for wasm32-wasip2 and
 * exported through the cwist-guest world (lib/cwist/wit/cwist.wit).
 *
 * The browser talks to it the same way a client talks to a CWIST server:
 * serialized HTTP/1.1 requests in, serialized responses out. The requests
 * are built and parsed in C by the Emscripten page host (blog/ui), which
 * hands the bytes to the jco-transpiled component's dispatch export.
 *
 *   PUT  /search/index      body: search-index.json   -> 204
 *   POST /search            body: query               -> text/html
 *
 * Everything else (post bodies, comments, home card, relations, accents) is
 * rendered at build time by tools/generate_static.c with the same kernels.
 *
 * Generated bindings (cwist_guest.h / cwist_guest.c) come from wit-bindgen
 * at build time and are never committed.
 */
#include <cwist/sys/app/app.h>
#include <cwist/wasm/wasm_component.h>
#include <cwist/core/mem/alloc.h>
#include <cJSON.h>

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

static cwist_app *app(void) {
    if (g_app) return g_app;
    g_app = cwist_app_create();
    if (!g_app) return NULL;
    cwist_app_put(g_app, "/search/index", search_index_handler);
    cwist_app_post(g_app, "/search", search_handler);
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
