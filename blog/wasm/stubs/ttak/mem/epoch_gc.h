#ifndef __TTAK_MEM_EPOCH_GC_H__
#define __TTAK_MEM_EPOCH_GC_H__

#include <stddef.h>
#include <stdlib.h>

typedef void (*ttak_epoch_gc_free_fn)(void *);

typedef struct ttak_epoch_gc {
    void *nodes[128];
    int count;
} ttak_epoch_gc_t;

static inline void ttak_epoch_gc_init(ttak_epoch_gc_t *gc, ttak_epoch_gc_free_fn free_fn, void *ctx) {
    if(gc) gc->count = 0;
}
static inline void ttak_epoch_gc_shutdown(ttak_epoch_gc_t *gc) {
    if(!gc) return;
    for(int i=0; i<gc->count; i++) {
        if(gc->nodes[i]) free(gc->nodes[i]);
    }
    gc->count = 0;
}
static inline void ttak_epoch_gc_rotate(ttak_epoch_gc_t *gc) {
    // No-op for stub
}
static inline void ttak_epoch_gc_retire(ttak_epoch_gc_t *gc, void *ptr) {
    if(gc && gc->count < 128) {
        gc->nodes[gc->count++] = ptr;
    } else {
        free(ptr);
    }
}

#endif
