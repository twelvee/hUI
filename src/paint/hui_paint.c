#include "hui_paint.h"

#include <string.h>

void hui_draw_list_init(hui_draw_list *list) {
    hui_vec_init(&list->cmds);
}

void hui_draw_list_reset(hui_draw_list *list) {
    free(list->cmds.data);
    list->cmds.data = NULL;
    list->cmds.len = list->cmds.cap = 0;
}

void hui_paint_build(hui_draw_list *list, const hui_dom *dom, const hui_style_store *styles) {
    if (dom->nodes.len == 0 || dom->root == 0xFFFFFFFFu) return;
    for (size_t i = 0; i < dom->nodes.len; i++) {
        const hui_dom_node *node = &dom->nodes.data[i];
        const hui_computed_style *cs = &styles->styles.data[i];
        if (node->type == HUI_NODE_ELEM && (cs->bg_color >> 24) != 0) {
            hui_draw draw;
            memset(&draw, 0, sizeof(draw));
            draw.op = HUI_OP_RECT;
            draw.u0 = cs->bg_color;
            draw.f[0] = node->x;
            draw.f[1] = node->y;
            draw.f[2] = node->w;
            draw.f[3] = node->h;
            hui_vec_push(&list->cmds, draw);
        }
        if (node->type == HUI_NODE_TEXT && node->text && node->text_len) {
            hui_draw draw;
            memset(&draw, 0, sizeof(draw));
            draw.op = HUI_OP_GLYPH_RUN;
            draw.u0 = cs->color;
            draw.f[0] = node->x;
            draw.f[1] = node->y;
            draw.f[2] = node->w;
            draw.f[3] = node->h;
            hui_vec_push(&list->cmds, draw);
        }
    }
}
