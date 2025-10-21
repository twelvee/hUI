#ifndef HUI_IR_H
#define HUI_IR_H

#include <stdint.h>
#include "../paint/hui_paint.h"

typedef struct {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    uint32_t flags;
    uint64_t toc_offset;
    uint64_t reserved;
} hui_ir_header;

typedef struct {
    uint32_t fourcc;
    uint32_t version;
    uint64_t offset;
    uint64_t length;
} hui_ir_toc_entry;

int hui_ir_write_draw_only(const hui_draw_list *list, const char *path);

int hui_ir_dump_text_draw_only(const hui_draw_list *list, const char *path);

#endif