#include "hui_intern.h"

#include <string.h>
#include <stdlib.h>

static uint64_t hui_hash64(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *) data;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void hui_intern_resize(hui_intern *intern, size_t need) {
    size_t capacity = intern->table.cap ? intern->table.cap : 8;
    while (capacity < need * 2) capacity *= 2;
    int32_t *table_data = (int32_t *) malloc(capacity * sizeof(int32_t));
    if (!table_data) return;
    for (size_t i = 0; i < capacity; i++) table_data[i] = -1;
    for (size_t i = 0; i < intern->entries.len; i++) {
        const hui_atom_entry *entry = &intern->entries.data[i];
        uint64_t h = hui_hash64(entry->str, entry->len);
        size_t idx = (size_t) h & (capacity - 1);
        while (table_data[idx] != -1) idx = (idx + 1) & (capacity - 1);
        table_data[idx] = (int32_t) i;
    }
    free(intern->table.data);
    intern->table.data = table_data;
    intern->table.len = capacity;
    intern->table.cap = capacity;
}

void hui_intern_init(hui_intern *intern) {
    hui_arena_init(&intern->arena, 64 * 1024);
    hui_vec_init(&intern->entries);
    hui_vec_init(&intern->table);
}

void hui_intern_reset(hui_intern *intern) {
    hui_arena_reset(&intern->arena);
    hui_vec_free(&intern->entries);
    hui_vec_free(&intern->table);
}

hui_atom hui_intern_put(hui_intern *intern, const char *str, size_t len) {
    if (!intern->table.cap) hui_intern_resize(intern, 8);

    uint64_t h = hui_hash64(str, len);
    size_t mask = intern->table.cap - 1;
    size_t idx = (size_t) h & mask;
    while (1) {
        int32_t slot = intern->table.data[idx];
        if (slot == -1) break;
        const hui_atom_entry *entry = &intern->entries.data[slot];
        if (entry->len == len && memcmp(entry->str, str, len) == 0) {
            return entry->atom;
        }
        idx = (idx + 1) & mask;
    }

    if (intern->entries.len * 2 >= intern->table.cap) {
        hui_intern_resize(intern, intern->entries.len * 2 + 1);
        mask = intern->table.cap - 1;
        idx = (size_t) h & mask;
        while (intern->table.data[idx] != -1) idx = (idx + 1) & mask;
    }

    hui_atom_entry entry;
    entry.str = hui_arena_strndup(&intern->arena, str, len);
    entry.len = (uint32_t) len;
    entry.atom = (hui_atom) intern->entries.len + 1;
    hui_vec_push(&intern->entries, entry);
    intern->table.data[idx] = (int32_t)(intern->entries.len - 1);
    return entry.atom;
}

const char *hui_intern_str(const hui_intern *intern, hui_atom atom, uint32_t *len_out) {
    if (atom == 0 || atom > intern->entries.len) {
        if (len_out) *len_out = 0;
        return "";
    }
    const hui_atom_entry *entry = &intern->entries.data[atom - 1];
    if (len_out) *len_out = entry->len;
    return entry->str;
}