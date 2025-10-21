#ifndef HUI_IR_PUBLIC_H
#define HUI_IR_PUBLIC_H

#include <stdint.h>

#define HUIR_MAGIC 0x52495548u

typedef struct {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    uint32_t flags;
    uint64_t toc_offset;
    uint64_t reserved;
} HUIR_header;

typedef struct {
    uint32_t fourcc;
    uint32_t version;
    uint64_t offset;
    uint64_t length;
} HUIR_toc_entry;

enum {
    HUIR_OP_RECT = 10,
    HUIR_OP_GLYPH_RUN = 30
};

#endif
