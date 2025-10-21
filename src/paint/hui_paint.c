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

static int hui_node_visible(const hui_dom_node *node, float viewport_w, float viewport_h) {
    if (!node) return 0;
    if (viewport_w <= 0.0f || viewport_h <= 0.0f) return 1;
    float x0 = node->x;
    float y0 = node->y;
    float x1 = x0 + node->w;
    float y1 = y0 + node->h;
    if (node->w <= 0.0f && node->h <= 0.0f) {
        return x0 >= 0.0f && x0 <= viewport_w && y0 >= 0.0f && y0 <= viewport_h;
    }
    if (x1 <= 0.0f || y1 <= 0.0f) return 0;
    if (x0 >= viewport_w || y0 >= viewport_h) return 0;
    return 1;
}

void hui_paint_build(hui_draw_list *list, const hui_dom *dom, const hui_style_store *styles,
                     float viewport_w, float viewport_h) {
    if (!dom || !styles) return;
    if (dom->nodes.len == 0 || dom->root == 0xFFFFFFFFu) return;
    if (styles->styles.len < dom->nodes.len) return;
    for (size_t i = 0; i < dom->nodes.len; i++) {
        const hui_dom_node *node = &dom->nodes.data[i];
        const hui_computed_style *cs = &styles->styles.data[i];
        if (!hui_node_visible(node, viewport_w, viewport_h)) continue;
        if (cs->display == 0) continue;
        if (node->type == HUI_NODE_ELEM && (cs->bg_color >> 24) != 0) {
            hui_draw draw;
            memset(&draw, 0, sizeof(draw));
            draw.op = HUI_DRAW_OP_RECT;
            draw.u0 = cs->bg_color;
            draw.u1 = (uint32_t) i;
            draw.f[0] = node->x;
            draw.f[1] = node->y;
            draw.f[2] = node->w;
            draw.f[3] = node->h;
            hui_vec_push(&list->cmds, draw);
        }
        if (node->type == HUI_NODE_TEXT && node->text && node->text_len) {
            hui_draw draw;
            memset(&draw, 0, sizeof(draw));
            draw.op = HUI_DRAW_OP_GLYPH_RUN;
            draw.u0 = cs->color;
            draw.u1 = (uint32_t) i;
            draw.f[0] = node->x;
            draw.f[1] = node->y;
            draw.f[2] = node->w;
            draw.f[3] = node->h;
            draw.f[4] = cs->font_size;
            hui_vec_push(&list->cmds, draw);
        }
    }
}
