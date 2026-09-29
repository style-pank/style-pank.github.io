#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#  define WASM_EXPORT
#endif

#include <string.h>
#include <stdio.h>

typedef struct {
    const char *section;
    const char *accent;
    const char *hover;
} section_accent_t;

static const section_accent_t SECTION_ACCENTS[] = {
    {"software", "#4a6fa3", "#8fa9c9"},
    {"post",     "#a84a43", "#cf8a7e"},
    {"general",  "#a3833f", "#d1bb7d"},
    {"music",    "#2f3a4a", "#6f7b89"},
    {"culture",  "#b8afa1", "#ddd5ca"},
    {"search",   "#7a5cff", "#9a84ff"}
};

WASM_EXPORT
const char* cwist_get_section_from_path(const char* path) {
    if (!path) return "";
    if (strstr(path, "/category/software/") || strstr(path, "/post/software/")) return "software";
    if (strstr(path, "/category/post/")     || strstr(path, "/post/post/"))     return "post";
    if (strstr(path, "/category/general/")  || strstr(path, "/post/general/"))  return "general";
    if (strstr(path, "/category/music/")    || strstr(path, "/post/music/"))    return "music";
    if (strstr(path, "/category/culture/")  || strstr(path, "/post/culture/"))  return "culture";
    if (strstr(path, "/search/")) return "search";
    return "";
}

WASM_EXPORT
const char* cwist_get_accent_for_section(const char* section) {
    if (!section) return "";
    for (size_t i = 0; i < sizeof(SECTION_ACCENTS)/sizeof(SECTION_ACCENTS[0]); ++i) {
        if (strcmp(SECTION_ACCENTS[i].section, section) == 0) {
            return SECTION_ACCENTS[i].accent;
        }
    }
    return "";
}

WASM_EXPORT
const char* cwist_get_hover_for_section(const char* section) {
    if (!section) return "";
    for (size_t i = 0; i < sizeof(SECTION_ACCENTS)/sizeof(SECTION_ACCENTS[0]); ++i) {
        if (strcmp(SECTION_ACCENTS[i].section, section) == 0) {
            return SECTION_ACCENTS[i].hover;
        }
    }
    return "";
}

WASM_EXPORT
void cwist_hex_to_rgb(const char* hex, int* r, int* g, int* b) {
    if (!hex || hex[0] != '#' || strlen(hex) != 7) {
        *r = *g = *b = 0;
        return;
    }
    sscanf(hex + 1, "%02x%02x%02x", r, g, b);
}
