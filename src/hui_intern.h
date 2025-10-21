#ifndef HUI_INTERN_H
#define HUI_INTERN_H

#include <stdint.h>
#include "hui_vec.h"
#include "hui_arena.h"

typedef uint32_t hui_atom;

typedef struct {
    hui_atom atom;
    const char *str;
    uint32_t len;
} hui_atom_entry;

typedef struct {
    hui_arena arena;

    HUI_VEC(hui_atom_entry) entries;

    HUI_VEC (int32_t) table;
} hui_intern;

void hui_intern_init(hui_intern *intern);

void hui_intern_reset(hui_intern *intern);

hui_atom hui_intern_put(hui_intern *intern, const char *str, size_t len);

const char *hui_intern_str(const hui_intern *intern, hui_atom atom, uint32_t *len_out);

#endif