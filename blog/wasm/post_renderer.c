#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#  define WASM_EXPORT
#endif

#include <md4c-html.h>
#include <cwist/core/mem/alloc.h>
#include <cwist/core/sstring/sstring.h>
#include <string.h>

static void md_process_output(const MD_CHAR* text, MD_SIZE size, void* userdata) {
    cwist_sstring *ss = (cwist_sstring*)userdata;
    cwist_sstring_append_len(ss, text, size);
}

WASM_EXPORT
char* cwist_render_markdown(const char* markdown) {
    if (!markdown) return NULL;

    cwist_sstring *ss = cwist_sstring_create();
    if (!ss) return NULL;
    cwist_sstring_assign(ss, "");

    int result = md_html(markdown, (MD_SIZE)strlen(markdown), md_process_output, ss, 
                         MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS, MD_HTML_FLAG_SKIP_UTF8_BOM);
    
    char *out = NULL;
    if (result == 0) {
        out = cwist_strdup(ss->data);
    }

    cwist_sstring_destroy(ss);
    return out;
}
