#ifndef HUI_PAINT_H
#define HUI_PAINT_H

#include <stdint.h>
#include "../style/hui_style.h"
#include "hui/hui_draw.h"

typedef struct {
    HUI_VEC(hui_draw) cmds;
} hui_draw_list;

void hui_draw_list_init(hui_draw_list *list);

void hui_draw_list_reset(hui_draw_list *list);

void hui_draw_list_release(hui_draw_list *list);

void hui_paint_build(hui_draw_list *list, const hui_dom *dom, const hui_style_store *styles,
                     float viewport_w, float viewport_h);

#endif
