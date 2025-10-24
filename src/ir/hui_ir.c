#include "hui_ir.h"

#include <stdio.h>
#include <stdlib.h>

int hui_ir_write_draw_only(const hui_draw_list *list, const char *path) {
    if (!list || !path) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    hui_ir_header header;
    header.magic = 0x48554952u;
    header.major = 1;
    header.minor = 0;
    header.flags = 0;
    header.toc_offset = sizeof(hui_ir_header);
    header.reserved = 0;
    fwrite(&header, 1, sizeof(header), f);
    hui_ir_toc_entry entry;
    entry.fourcc = 0x44415257u;
    entry.version = 2;
    entry.offset = sizeof(hui_ir_header) + sizeof(hui_ir_toc_entry);
    struct {
        uint64_t command_count;
        uint64_t rect_count;
    } draw_header;
    draw_header.command_count = list->cmds.len;
    draw_header.rect_count = list->rects.len;
    uint64_t cmd_bytes = draw_header.command_count * (uint64_t) sizeof(hui_draw);
    uint64_t rect_bytes = draw_header.rect_count * (uint64_t) sizeof(hui_draw_rect);
    entry.length = sizeof(draw_header) + cmd_bytes + rect_bytes;
    fwrite(&entry, 1, sizeof(entry), f);
    fwrite(&draw_header, 1, sizeof(draw_header), f);
    if (draw_header.command_count) {
        fwrite(list->cmds.data, sizeof(hui_draw), (size_t) draw_header.command_count, f);
    }
    if (draw_header.rect_count) {
        fwrite(list->rects.data, sizeof(hui_draw_rect), (size_t) draw_header.rect_count, f);
    }
    fclose(f);
    return 0;
}

int hui_ir_dump_text_draw_only(const hui_draw_list *list, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "HUIR dump (DRAW only), commands: %zu rects: %zu\n",
            list->cmds.len, list->rects.len);
    for (size_t i = 0; i < list->cmds.len; i++) {
        const hui_draw *draw = &list->cmds.data[i];
        if (draw->op == HUI_DRAW_OP_RECT) {
            fprintf(f, "%04zu: RECT  x=%.1f y=%.1f w=%.1f h=%.1f color=#%08X\n",
                    i, draw->f[0], draw->f[1], draw->f[2], draw->f[3], draw->u0);
        } else if (draw->op == HUI_DRAW_OP_RECT_BATCH) {
            size_t start = draw->u1;
            size_t count = draw->u2;
            fprintf(f, "%04zu: RECT_BATCH color=#%08X count=%zu\n", i, draw->u0, count);
            if (start + count <= list->rects.len) {
                const hui_draw_rect *rect = &list->rects.data[start];
                fprintf(f, "      first: x=%.1f y=%.1f w=%.1f h=%.1f node=%u\n",
                        rect->x, rect->y, rect->w, rect->h, rect->node_index);
            }
        } else if (draw->op == HUI_DRAW_OP_GLYPH_RUN) {
            fprintf(f, "%04zu: TEXT  x=%.1f y=%.1f w=%.1f h=%.1f color=#%08X\n",
                    i, draw->f[0], draw->f[1], draw->f[2], draw->f[3], draw->u0);
        } else {
            fprintf(f, "%04zu: OP %u\n", i, draw->op);
        }
    }
    fclose(f);
    return 0;
}
