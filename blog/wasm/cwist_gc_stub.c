#include <cwist/core/mem/gc.h>
#include <cwist/core/mem/alloc.h>
#include <stdlib.h>

void ttak_epoch_gc_init(ttak_epoch_gc_t *gc, ttak_epoch_gc_free_fn free_fn, void *ctx) {
    // Stub
}

void ttak_epoch_gc_shutdown(ttak_epoch_gc_t *gc) {
    // Stub
}

void ttak_epoch_gc_rotate(ttak_epoch_gc_t *gc) {
    // Stub
}

void ttak_epoch_gc_retire(ttak_epoch_gc_t *gc, void *ptr) {
    if (ptr) free(ptr);
}

static void free_ptr(void *ptr) {
    if (ptr) free(ptr);
}

void cwist_gc(cwist_gc_t *gc, bool manual_rotation) {
    if (!gc) return;
    if (gc->initialized) return;
    ttak_epoch_gc_init(&gc->impl, free_ptr, NULL);
    gc->initialized = true;
}

void cwist_gc_shutdown(cwist_gc_t *gc) {
    if (!gc) return;
    if (!gc->initialized) return;
    ttak_epoch_gc_shutdown(&gc->impl);
    gc->initialized = false;
}

void cwist_gc_rotate(cwist_gc_t *gc) {
    if (!gc || !gc->initialized) return;
    ttak_epoch_gc_rotate(&gc->impl);
}

void cwist_reg_ptr(cwist_gc_t *gc, void *ptr) {
    if (!gc || !gc->initialized || !ptr) return;
    ttak_epoch_gc_retire(&gc->impl, ptr);
}

void cwist_reg_ptr_sized(cwist_gc_t *gc, void *ptr, size_t size) {
    cwist_reg_ptr(gc, ptr);
}

ttak_epoch_gc_t *cwist_gc_raw(cwist_gc_t *gc) {
    return gc ? &gc->impl : NULL;
}
