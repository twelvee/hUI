#include "hui_ir.h"

#include <stdio.h>
#include <stdlib.h>

int hui_ir_write_draw_only(const hui_draw_list *list, const char *path) {
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
    entry.version = 1;
    entry.offset = sizeof(hui_ir_header) + sizeof(hui_ir_toc_entry);
    entry.length = (uint64_t)(list->cmds.len * sizeof(hui_draw));
    fwrite(&entry, 1, sizeof(entry), f);
    if (list->cmds.len)
        fwrite(list->cmds.data, sizeof(hui_draw), list->cmds.len, f);
    fclose(f);
    return 0;
}

int hui_ir_dump_text_draw_only(const hui_draw_list *list, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "HUIR dump (DRAW only), commands: %zu\n", list->cmds.len);
    for (size_t i = 0; i < list->cmds.len; i++) {
        const hui_draw *draw = &list->cmds.data[i];
        if (draw->op == HUI_DRAW_OP_RECT) {
            fprintf(f, "%04zu: RECT  x=%.1f y=%.1f w=%.1f h=%.1f color=#%08X\n",
                    i, draw->f[0], draw->f[1], draw->f[2], draw->f[3], draw->u0);
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
