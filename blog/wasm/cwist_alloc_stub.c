#include <cwist/core/mem/alloc.h>
#include <stdlib.h>
#include <string.h>

ttak_owner_t *cwist_create_owner(void) {
    return NULL;
}

static size_t normalized_size(size_t size) {
    return size > 0 ? size : 1;
}

void *cwist_malloc(size_t size) {
    size_t actual = normalized_size(size);
    void *ptr = calloc(1, actual);
    return ptr;
}

void *cwist_alloc(size_t size) {
    return cwist_malloc(size);
}

void *cwist_alloc_array(size_t count, size_t elem_size) {
    size_t total = count * elem_size;
    return cwist_malloc(total);
}

void *cwist_realloc(void *ptr, size_t new_size) {
    size_t actual = normalized_size(new_size);
    return realloc(ptr, actual);
}

static size_t stub_strnlen(const char *src, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && src[n] != '\0') {
        n++;
    }
    return n;
}

static char *dup_buffer(const char *src, size_t len) {
    if (!src) return NULL;
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    memcpy(buf, src, len);
    buf[len] = '\0';
    return buf;
}

char *cwist_strdup(const char *src) {
    return src ? dup_buffer(src, strlen(src)) : NULL;
}

char *cwist_strndup(const char *src, size_t n) {
    if (!src) return NULL;
    size_t len = stub_strnlen(src, n);
    return dup_buffer(src, len);
}

void cwist_free(void *ptr) {
    free(ptr);
}
