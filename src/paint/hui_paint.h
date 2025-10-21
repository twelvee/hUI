#ifndef HUI_PAINT_H
#define HUI_PAINT_H

#include <stdint.h>
#include "../style/hui_style.h"

typedef enum {
    HUI_OP_RECT = 1,
    HUI_OP_GLYPH_RUN = 2
} hui_draw_op;

typedef struct {
    hui_draw_op op;
    uint32_t u0;
    uint32_t u1;
    float f[6];
} hui_draw;

typedef struct {
    HUI_VEC(hui_draw) cmds;
} hui_draw_list;

void hui_draw_list_init(hui_draw_list *list);

void hui_draw_list_reset(hui_draw_list *list);

void hui_paint_build(hui_draw_list *list, const hui_dom *dom, const hui_style_store *styles);

#endif