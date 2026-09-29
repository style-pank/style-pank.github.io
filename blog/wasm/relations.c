#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#  define WASM_EXPORT
#endif

#include <string.h>
#include <cwist/core/mem/alloc.h>
#include <cwist/core/mem/gc.h>

extern float cwist_score(const char *query, const char *title, const char *tags, const char *summary, const char *body);

WASM_EXPORT
float cwist_pair_score(const char *tags_a_csv, const char *tags_b_csv) {
    if (!tags_a_csv || !tags_b_csv || !*tags_a_csv || !*tags_b_csv) return 0.0f;

    cwist_gc_t gc = {0};
    cwist_gc(&gc, true);

    float score = 0.0f;
    float common_count = 0.0f;

    char *a_copy = cwist_strdup(tags_a_csv);
    char *b_copy = cwist_strdup(tags_b_csv);
    
    cwist_reg_ptr(&gc, a_copy);
    cwist_reg_ptr(&gc, b_copy);

    if (!a_copy || !b_copy) {
        cwist_gc_shutdown(&gc);
        return 0.0f;
    }

    char *saveptr_a;
    char *tag_a = strtok_r(a_copy, " ", &saveptr_a);
    while (tag_a) {
        if (strstr(tags_b_csv, tag_a)) {
            common_count += 1.0f;
        }
        score += cwist_score(tag_a, "", tags_b_csv, "", "");
        tag_a = strtok_r(NULL, " ", &saveptr_a);
    }

    char *saveptr_b;
    char *tag_b = strtok_r(b_copy, " ", &saveptr_b);
    while (tag_b) {
        score += cwist_score(tag_b, "", tags_a_csv, "", "") * 0.7f;
        tag_b = strtok_r(NULL, " ", &saveptr_b);
    }

    score += common_count * 2.2f;

    cwist_gc_shutdown(&gc);
    return score;
}
