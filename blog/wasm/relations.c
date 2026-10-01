#include "blog.h"

#include <cwist/core/mem/alloc.h>
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
