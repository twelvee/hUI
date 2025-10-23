#include "hui_layout.h"

#include <string.h>
#include "../../include/hui/hui.h"

static void layout_node(hui_dom *dom, const hui_style_store *styles, uint32_t idx, float x, float y, float width) {
    hui_dom_node *node = &dom->nodes.data[idx];
    const hui_computed_style *cs = &styles->styles.data[idx];
    if (cs->display == 0) {
        node->x = x;
        node->y = y;
        node->w = 0.0f;
        node->h = 0.0f;
        return;
    }
    node->x = x;
    node->y = y;
    float inner_width = width - (cs->padding[1] + cs->padding[3]);
    if (inner_width < 0.0f) inner_width = 0.0f;
    node->w = (cs->width > 0.0f) ? cs->width : width;
    if (node->type == HUI_NODE_TEXT) {
        float fs = cs->font_size > 0.0f ? cs->font_size : 16.0f;
        float line_h = cs->line_height > 0.0f ? cs->line_height : (fs * HUI_TEXT_APPROX_LINE_HEIGHT);
        if (cs->line_height > 0.0f && cs->line_height <= 4.0f) line_h = cs->line_height * fs;
        size_t lines = 1;
        size_t max_cols = 0;
        if (node->text && node->text_len > 0) {
            hui_dom_text_cache_refresh(node);
            lines = node->text_cache_lines ? node->text_cache_lines : 1;
            max_cols = node->text_cache_max_cols;
        }
        float char_w = HUI_TEXT_APPROX_CHAR_ADVANCE * fs;
        float text_w = char_w * (float) max_cols;
        if (text_w > inner_width) text_w = inner_width;
        float text_h = line_h * (float) lines;
        int is_text_field = (node->tf_flags & HUI_NODE_TF_VALUE) != 0;
        if (is_text_field) {
            node->w = inner_width;
            float enforced_height = 0.0f;
            if (node->parent != 0xFFFFFFFFu && node->parent < styles->styles.len) {
                const hui_computed_style *parent_cs = &styles->styles.data[node->parent];
                if (parent_cs->height >= 0.0f) enforced_height = parent_cs->height;
                else if (parent_cs->min_height > 0.0f) enforced_height = parent_cs->min_height;
            }
            if (enforced_height > text_h) text_h = enforced_height;
            if (text_h < line_h) text_h = line_h;
        } else {
            node->w = text_w;
        }
        node->h = text_h;
        return;
    }
    float cursor_y = y + cs->padding[0];
    uint32_t child = node->first_child;
    while (child != 0xFFFFFFFFu) {
        layout_node(dom, styles, child, x + cs->padding[3], cursor_y, inner_width);
        cursor_y = dom->nodes.data[child].y + dom->nodes.data[child].h;
        child = dom->nodes.data[child].next_sibling;
    }
    node->h = (cursor_y - y) + cs->padding[2];
    float content_height = node->h - (cs->padding[0] + cs->padding[2]);
    if (content_height < 0.0f) content_height = 0.0f;
    if (cs->height >= 0.0f) {
        float delta = cs->height - content_height;
        node->h += delta;
        content_height = cs->height;
    }
    if (cs->min_height > 0.0f && content_height < cs->min_height) {
        float delta = cs->min_height - content_height;
        node->h += delta;
    }
}

void hui_layout_run(hui_dom *dom, const hui_style_store *styles, const hui_layout_opts *opts) {
    if (dom->root == 0xFFFFFFFFu || dom->nodes.len == 0) return;
    float viewport_w = (opts && opts->viewport_w > 0.0f) ? opts->viewport_w : 800.0f;
    float cursor_y = 0.0f;
    uint32_t node = dom->root;
    while (node != 0xFFFFFFFFu) {
        layout_node(dom, styles, node, 0.0f, cursor_y, viewport_w);
        cursor_y = dom->nodes.data[node].y + dom->nodes.data[node].h;
        node = dom->nodes.data[node].next_sibling;
    }
}
