#ifndef HUI_VEC_H
#define HUI_VEC_H
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define HUI_VEC(T) struct { T* data; size_t len; size_t cap; }

static inline int hui_vec_grow(void **data, size_t elem, size_t *cap, size_t need) {
    size_t c = *cap;
    if (need <= c) return 0;
    size_t nc = c ? c * 2 : 4;
    if (nc < need) nc = need;
    void *nd = realloc(*data, nc * elem);
    if (!nd) return -1;
    *data = nd;
    *cap = nc;
    return 0;
}

#define hui_vec_init(v) do { (v)->data=NULL; (v)->len=0; (v)->cap=0; } while(0)
#define hui_vec_reserve(v, n) hui_vec_grow((void**)&(v)->data, sizeof(*(v)->data), &(v)->cap, (n))
#define hui_vec_push(v, val) do { if (hui_vec_grow((void**)&(v)->data, sizeof(*(v)->data), &(v)->cap, (v)->len+1)) abort(); (v)->data[(v)->len++]=(val); } while(0)
#define hui_vec_free(v) do { free((v)->data); (v)->data=NULL; (v)->len=0; (v)->cap=0; } while(0)

#endif