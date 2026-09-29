#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

static char *dup_text(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char *buf = (char *)malloc(len + 1);
    if (!buf) return NULL;
    memcpy(buf, src, len);
    buf[len] = '\0';
    return buf;
}

cJSON *cJSON_CreateObject(void) {
    cJSON *obj = (cJSON *)calloc(1, sizeof(cJSON));
    return obj;
}

void cJSON_AddStringToObject(cJSON *object, const char *name, const char *value) {
    if (!object) return;
    const char *source = value ? value : name;
    if (!source) return;
    free(object->text);
    object->text = dup_text(source);
}

void cJSON_Delete(cJSON *object) {
    if (!object) return;
    free(object->text);
    free(object);
}
