#include "hui_layout.h"

#include <string.h>
#include "../../include/hui/hui.h"

static size_t layout_utf8_next(const char *text, size_t len, size_t offset) {
    if (!text || offset >= len) return len;
    offset++;
    while (offset < len && ((unsigned char) text[offset] & 0xC0u) == 0x80u) offset++;
    if (offset > len) offset = len;
    return offset;
}

static void layout_measure_multiline(const char *text, uint32_t len, size_t *out_lines, size_t *out_max_cols) {
    size_t lines = 1;
    size_t max_cols = 0;
    size_t cols = 0;
    size_t pos = 0;
    while (pos < len) {
        unsigned char ch = (unsigned char) text[pos];
        if (ch == '\r') {
            pos++;
            continue;
        }
        if (ch == '\n') {
            if (cols > max_cols) max_cols = cols;
            cols = 0;
            lines++;
            pos++;
            continue;
        }
        cols++;
        pos = layout_utf8_next(text, len, pos);
    }
    if (cols > max_cols) max_cols = cols;
    if (out_lines) *out_lines = lines;
    if (out_max_cols) *out_max_cols = max_cols;
}

static void layout_node(hui_dom *dom, const hui_style_store *styles, uint32_t idx, float x, float y, float width) {
    hui_dom_node *node = &dom->nodes.data[idx];
    const hui_computed_style *cs = &styles->styles.data[idx];
    node->x = x;
    node->y = y;
    float inner_width = width - (cs->padding[1] + cs->padding[3]);
    if (inner_width < 0.0f) inner_width = 0.0f;
    node->w = (cs->width > 0.0f) ? cs->width : width;
    if (node->type == HUI_NODE_TEXT) {
        float fs = cs->font_size > 0.0f ? cs->font_size : 16.0f;
        size_t lines = 1;
        size_t max_cols = node->text_len;
        if (node->text && node->text_len > 0) {
            layout_measure_multiline(node->text, node->text_len, &lines, &max_cols);
        } else {
            max_cols = 0;
        }
        float char_w = HUI_TEXT_APPROX_CHAR_ADVANCE * fs;
        float text_w = char_w * (float) max_cols;
        if (text_w > inner_width) text_w = inner_width;
        node->w = text_w;
        node->h = HUI_TEXT_APPROX_LINE_HEIGHT * fs * (float) lines;
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
