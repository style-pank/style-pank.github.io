/*
 * cwist search module — compiled to WebAssembly via Emscripten.
 *
 * Exports a single function:
 *
 *   float cwist_score(query, title, tags, summary, body)
 *
 * Returns a relevance score >= 0.0.  Higher means a better match.
 * Score == 0.0 means no match at all.
 *
 * Weights:  title 3 pts · tags 2 pts · summary 1 pt · body 1 pt
 *
 * Algorithm:
 *   Case-insensitive substring search (ASCII).
 *   Multi-byte UTF-8 sequences (Korean etc.) are compared byte-exact,
 *   which is correct because those scripts do not have case.
 */

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#  define WASM_EXPORT
#endif

#include <cwist/core/mem/alloc.h>
#include <cwist/core/sstring/sstring.h>
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Forward declaration for renderer in search_ui.c */
char *cwist_render_search_result(const char *title, const char *url, const char *summary, const char *tags, const char *query);
void cwist_free_html(char *html);
float cwist_score(const char *query, const char *title, const char *tags, const char *summary, const char *body);

typedef struct {
    cJSON *item;
    float score;
} scored_item_t;

static int compare_scored_items(const void *a, const void *b) {
    float diff = ((scored_item_t*)b)->score - ((scored_item_t*)a)->score;
    if (diff > 0) return 1;
    if (diff < 0) return -1;
    return 0;
}

WASM_EXPORT
char* cwist_search_and_render(const char* json_index, const char* query) {
    if (!json_index || !query || !*query) return NULL;

    cJSON *root = cJSON_Parse(json_index);
    if (!root) return NULL;

    int size = cJSON_GetArraySize(root);
    scored_item_t *scored = malloc(sizeof(scored_item_t) * size);
    int count = 0;

    for (int i = 0; i < size; ++i) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        const char *title = cJSON_GetObjectItemCaseSensitive(item, "title")->valuestring;
        const char *url   = cJSON_GetObjectItemCaseSensitive(item, "url")->valuestring;
        const char *summary = cJSON_GetObjectItemCaseSensitive(item, "summary")->valuestring;
        const char *body  = cJSON_GetObjectItemCaseSensitive(item, "body")->valuestring;
        
        cJSON *tags_arr = cJSON_GetObjectItemCaseSensitive(item, "tags");
        cwist_sstring *tags_ss = cwist_sstring_create();
        int tags_size = cJSON_GetArraySize(tags_arr);
        for (int j = 0; j < tags_size; ++j) {
            cwist_sstring_append(tags_ss, cJSON_GetArrayItem(tags_arr, j)->valuestring);
            if (j < tags_size - 1) cwist_sstring_append(tags_ss, " ");
        }

        float s = cwist_score(query, title, tags_ss->data, summary, body);
        if (s > 0) {
            scored[count].item = item;
            scored[count].score = s;
            count++;
        }
        cwist_sstring_destroy(tags_ss);
    }

    qsort(scored, count, sizeof(scored_item_t), compare_scored_items);

    cwist_sstring *out = cwist_sstring_create();
    cwist_sstring_assign(out, "");

    int limit = count > 10 ? 10 : count;
    for (int i = 0; i < limit; ++i) {
        cJSON *item = scored[i].item;
        const char *title = cJSON_GetObjectItemCaseSensitive(item, "title")->valuestring;
        const char *url   = cJSON_GetObjectItemCaseSensitive(item, "url")->valuestring;
        const char *summary = cJSON_GetObjectItemCaseSensitive(item, "summary")->valuestring;
        
        cJSON *tags_arr = cJSON_GetObjectItemCaseSensitive(item, "tags");
        cwist_sstring *tags_ss = cwist_sstring_create();
        int tags_size = cJSON_GetArraySize(tags_arr);
        for (int j = 0; j < tags_size; ++j) {
            cwist_sstring_append(tags_ss, cJSON_GetArrayItem(tags_arr, j)->valuestring);
            if (j < tags_size - 1) cwist_sstring_append(tags_ss, ", ");
        }

        char *html = cwist_render_search_result(title, url, summary, tags_ss->data, query);
        if (html) {
            cwist_sstring_append(out, html);
            cwist_free_html(html);
        }
        cwist_sstring_destroy(tags_ss);
    }

    char *result = cwist_strdup(out->data);
    cwist_sstring_destroy(out);
    free(scored);
    cJSON_Delete(root);
    return result;
}

/*
 * icontains — case-insensitive substring search.
 *
 * For each byte:
 *   - ASCII (< 0x80): fold to lower-case before comparing.
 *   - Non-ASCII (>= 0x80): compare byte-exact (UTF-8 continuation bytes
 *     cannot be confused with ASCII, so this is safe).
 *
 * Returns 1 if needle is found inside haystack, 0 otherwise.
 */
static int icontains(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) return 0;
    size_t nlen = strlen(needle);
    for (; *haystack; ++haystack) {
        size_t i;
        for (i = 0; i < nlen; ++i) {
            if (!haystack[i]) break;
            unsigned char h = (unsigned char)haystack[i];
            unsigned char n = (unsigned char)needle[i];
            if (h < 0x80 && n < 0x80) {
                if ((unsigned char)tolower(h) != (unsigned char)tolower(n)) break;
            } else {
                if (h != n) break;
            }
        }
        if (i == nlen) return 1;
    }
    return 0;
}

/*
 * cwist_score — score a post against a search query.
 *
 * All parameters are UTF-8 C strings.  NULL is treated as empty.
 * tags should be the tag list joined with spaces before calling.
 */
WASM_EXPORT
float cwist_score(const char *query,
                  const char *title,
                  const char *tags,
                  const char *summary,
                  const char *body) {
    if (!query || !*query) return 0.0f;

    float score = 0.0f;
    if (title   && icontains(title,   query)) score += 3.0f;
    if (tags    && icontains(tags,    query)) score += 2.0f;
    if (summary && icontains(summary, query)) score += 1.0f;
    if (body    && icontains(body,    query)) score += 1.0f;
    return score;
}
