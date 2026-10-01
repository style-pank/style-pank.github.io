#include "blog.h"

#include <cwist/core/mem/alloc.h>
#include <cJSON.h>
#include <stdio.h>
#include <string.h>

/* Mirrors categories.cfg (accent_primary / accent_secondary). */
typedef struct {
    const char *section;
    const char *accent;
    const char *hover;
} section_accent_t;

static const section_accent_t SECTION_ACCENTS[] = {
    {"posts",        "#a84a43", "#cf8a7e"},
    {"culture-news", "#b8afa1", "#ddd5ca"},
    {"korean",       "#4a6fa3", "#8fa9c9"},
    {"russian",      "#2f3a4a", "#6f7b89"},
    {"english",      "#a3833f", "#d1bb7d"},
    {"search",       "#7a5cff", "#9a84ff"},
};

static const section_accent_t *section_from_path(const char *path) {
    if (!path) return NULL;
    if (strstr(path, "/search/")) return &SECTION_ACCENTS[5];
    for (size_t i = 0; i < sizeof(SECTION_ACCENTS) / sizeof(SECTION_ACCENTS[0]); ++i) {
        char cat[96], post[96];
        snprintf(cat, sizeof(cat), "/category/%s/", SECTION_ACCENTS[i].section);
        snprintf(post, sizeof(post), "/post/%s/", SECTION_ACCENTS[i].section);
        if (strstr(path, cat) || strstr(path, post)) return &SECTION_ACCENTS[i];
    }
    return NULL;
}

char *blog_theme_json(const char *path) {
    const section_accent_t *s = section_from_path(path);
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "section", s ? s->section : "");
    if (s) {
        unsigned r = 0, g = 0, b = 0;
        sscanf(s->accent + 1, "%02x%02x%02x", &r, &g, &b);
        cJSON_AddStringToObject(out, "accent", s->accent);
        cJSON_AddStringToObject(out, "hover", s->hover);
        int rgb[3] = {(int)r, (int)g, (int)b};
        cJSON_AddItemToObject(out, "rgb", cJSON_CreateIntArray(rgb, 3));
    }
    char *printed = cJSON_PrintUnformatted(out);
    char *result = cwist_strdup(printed ? printed : "{\"section\":\"\"}");
    cJSON_free(printed);
    cJSON_Delete(out);
    return result;
}
