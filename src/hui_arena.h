#ifndef HUI_ARENA_H
#define HUI_ARENA_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct hui_arena_block {
    struct hui_arena_block *next;
    size_t used;
    size_t cap;
    uint8_t data[];
} hui_arena_block;

typedef struct {
    hui_arena_block *head;
    size_t block_size;
} hui_arena;

static inline void hui_arena_init(hui_arena *arena, size_t block_size) {
    arena->head = NULL;
    arena->block_size = block_size ? block_size : (size_t) 64 * 1024;
}

static inline void hui_arena_reset(hui_arena *arena) {
    hui_arena_block *blk = arena->head;
    while (blk) {
        hui_arena_block *next = blk->next;
        free(blk);
        blk = next;
    }
    arena->head = NULL;
}

static inline void *hui_arena_alloc(hui_arena *arena, size_t size, size_t align) {
    if (align == 0) align = 1;
    size_t mask = align - 1;
    hui_arena_block *blk = arena->head;
    if (!blk || ((blk->used + mask) & ~mask) + size > blk->cap) {
        size_t cap = arena->block_size;
        if (cap < size + 64) cap = size + 64;
        blk = (hui_arena_block *) malloc(sizeof(hui_arena_block) + cap);
        if (!blk) return NULL;
        blk->next = arena->head;
        blk->used = 0;
        blk->cap = cap;
        arena->head = blk;
    }
    size_t offset = (blk->used + mask) & ~mask;
    blk->used = offset + size;
    return blk->data + offset;
}

static inline char *hui_arena_strndup(hui_arena *arena, const char *s, size_t len) {
    char *dst = (char *) hui_arena_alloc(arena, len + 1, 1);
    if (!dst) return NULL;
    memcpy(dst, s, len);
    dst[len] = '\0';
    return dst;
}

#endif
