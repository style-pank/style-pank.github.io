#ifndef CJSON_STUB_H
#define CJSON_STUB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cJSON {
    char *text;
} cJSON;

cJSON *cJSON_CreateObject(void);
void cJSON_AddStringToObject(cJSON *object, const char *name, const char *value);
void cJSON_Delete(cJSON *object);

#ifdef __cplusplus
}
#endif

#endif
