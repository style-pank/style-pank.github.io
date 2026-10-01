#include "blog.h"

#include <cwist/core/mem/alloc.h>
#include <cwist/core/sstring/sstring.h>
#include <cJSON.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

float blog_pair_score(const char *tags_a, const char *tags_b) {
    if (!tags_a || !tags_b || !*tags_a || !*tags_b) return 0.0f;

    char *a_copy = cwist_strdup(tags_a);
    char *b_copy = cwist_strdup(tags_b);
    if (!a_copy || !b_copy) {
        cwist_free(a_copy);
        cwist_free(b_copy);
        return 0.0f;
    }

    float score = 0.0f;
    float common = 0.0f;
    char *save = NULL;
    for (char *tag = strtok_r(a_copy, " ", &save); tag; tag = strtok_r(NULL, " ", &save)) {
        if (strstr(tags_b, tag)) common += 1.0f;
        score += blog_score(tag, "", tags_b, "", "");
    }
    for (char *tag = strtok_r(b_copy, " ", &save); tag; tag = strtok_r(NULL, " ", &save)) {
        score += blog_score(tag, "", tags_a, "", "") * 0.7f;
    }
    score += common * 2.2f;

    cwist_free(a_copy);
    cwist_free(b_copy);
    return score;
}

/* Lower-cased, space-joined tag list of one index entry. */
static char *tag_string(const cJSON *item) {
    cwist_sstring *ss = cwist_sstring_create();
    cwist_sstring_assign(ss, "");
    const cJSON *tag;
    cJSON_ArrayForEach(tag, cJSON_GetObjectItemCaseSensitive(item, "tags")) {
        if (!cJSON_IsString(tag) || !tag->valuestring || !*tag->valuestring) continue;
        if (ss->size) cwist_sstring_append(ss, " ");
        for (const char *p = tag->valuestring; *p; ++p) {
            char c = (char)tolower((unsigned char)*p);
            cwist_sstring_append_len(ss, &c, 1);
        }
    }
    char *out = cwist_strdup(ss->data ? ss->data : "");
    cwist_sstring_destroy(ss);
    return out;
}

char *blog_relations_json(const void *index) {
    const cJSON *root = index;
    int n = cJSON_IsArray(root) ? cJSON_GetArraySize(root) : 0;

    char **tags = calloc((size_t)(n > 0 ? n : 1), sizeof(*tags));
    if (!tags) return NULL;
    for (int i = 0; i < n; ++i) tags[i] = tag_string(cJSON_GetArrayItem(root, i));

    cJSON *out = cJSON_CreateObject();
    cJSON *edges = cJSON_AddArrayToObject(out, "edges");
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            float w = blog_pair_score(tags[a], tags[b]);
            if (w <= 0) continue;
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "a", a);
            cJSON_AddNumberToObject(e, "b", b);
            cJSON_AddNumberToObject(e, "w", w);
            cJSON_AddItemToArray(edges, e);
        }
    }

    char *printed = cJSON_PrintUnformatted(out);
    char *result = cwist_strdup(printed ? printed : "{\"edges\":[]}");
    cJSON_free(printed);
    cJSON_Delete(out);
    for (int i = 0; i < n; ++i) cwist_free(tags[i]);
    free(tags);
    return result;
}
