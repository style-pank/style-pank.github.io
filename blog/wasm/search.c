/*
 * Search kernel: scores every post of the JSON index against the query and
 * renders the top hits through search_ui.c.
 *
 * Weights: title 3 pts · tags 2 pts · summary 1 pt · body 1 pt
 *
 * Case-insensitive substring search (ASCII). Multi-byte UTF-8 sequences
 * (Korean etc.) are compared byte-exact, which is correct because those
 * scripts do not have case.
 */
#include "blog.h"

#include <cwist/core/mem/alloc.h>
#include <cwist/core/sstring/sstring.h>
#include <cJSON.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *blog_render_search_result(const char *title, const char *url,
                                const char *summary, const char *tags);

typedef struct {
    const cJSON *item;
    float score;
} scored_item_t;

static int compare_scored_items(const void *a, const void *b) {
    float diff = ((const scored_item_t *)b)->score - ((const scored_item_t *)a)->score;
    if (diff > 0) return 1;
    if (diff < 0) return -1;
    return 0;
}

static const char *field(const cJSON *item, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

static void join_tags(const cJSON *item, const char *sep, cwist_sstring *out) {
    cwist_sstring_assign(out, "");
    const cJSON *tags = cJSON_GetObjectItemCaseSensitive(item, "tags");
    const cJSON *tag;
    bool first = true;
    cJSON_ArrayForEach(tag, tags) {
        if (!cJSON_IsString(tag) || !tag->valuestring) continue;
        if (!first) cwist_sstring_append(out, sep);
        cwist_sstring_append(out, tag->valuestring);
        first = false;
    }
}

char *blog_search_and_render(const void *index, const char *query) {
    const cJSON *root = index;
    if (!cJSON_IsArray(root) || !query || !*query) return cwist_strdup("");

    int size = cJSON_GetArraySize(root);
    scored_item_t *scored = calloc((size_t)(size > 0 ? size : 1), sizeof(*scored));
    cwist_sstring *tags_ss = cwist_sstring_create();
    if (!scored || !tags_ss) {
        free(scored);
        if (tags_ss) cwist_sstring_destroy(tags_ss);
        return NULL;
    }

    int count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, root) {
        join_tags(item, " ", tags_ss);
        float s = blog_score(query, field(item, "title"), tags_ss->data,
                             field(item, "summary"), field(item, "body"));
        if (s > 0) {
            scored[count].item = item;
            scored[count].score = s;
            count++;
        }
    }
    qsort(scored, (size_t)count, sizeof(*scored), compare_scored_items);

    cwist_sstring *out = cwist_sstring_create();
    cwist_sstring_assign(out, "");
    int limit = count > 10 ? 10 : count;
    for (int i = 0; i < limit; ++i) {
        join_tags(scored[i].item, ", ", tags_ss);
        char *html = blog_render_search_result(field(scored[i].item, "title"),
                                               field(scored[i].item, "url"),
                                               field(scored[i].item, "summary"),
                                               tags_ss->data);
        if (html) {
            cwist_sstring_append(out, html);
            cwist_free(html);
        }
    }

    char *result = cwist_strdup(out->data ? out->data : "");
    cwist_sstring_destroy(out);
    cwist_sstring_destroy(tags_ss);
    free(scored);
    return result;
}

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
                if (tolower(h) != tolower(n)) break;
            } else if (h != n) {
                break;
            }
        }
        if (i == nlen) return 1;
    }
    return 0;
}

float blog_score(const char *query, const char *title, const char *tags,
                 const char *summary, const char *body) {
    if (!query || !*query) return 0.0f;

    float score = 0.0f;
    if (icontains(title, query))   score += 3.0f;
    if (icontains(tags, query))    score += 2.0f;
    if (icontains(summary, query)) score += 1.0f;
    if (icontains(body, query))    score += 1.0f;
    return score;
}
