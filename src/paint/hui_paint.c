#include "hui_paint.h"

#include <string.h>
#include <math.h>
#include <limits.h>
#include "../../include/hui/hui.h"

static size_t hui_paint_utf8_next(const char *text, size_t len, size_t offset) {
    if (!text || offset >= len) return len;
    offset++;
    while (offset < len && ((unsigned char) text[offset] & 0xC0u) == 0x80u) offset++;
    if (offset > len) offset = len;
    return offset;
}

static size_t hui_paint_utf8_count_range(const char *text, size_t start, size_t end) {
    if (!text || start >= end) return 0;
    size_t count = 0;
    size_t pos = start;
    while (pos < end) {
        size_t next = hui_paint_utf8_next(text, end, pos);
        if (next == pos) break;
        pos = next;
        count++;
    }
    return count;
}

static size_t hui_paint_utf8_count_total(const char *text, size_t len) {
    return hui_paint_utf8_count_range(text, 0, len);
}

static size_t hui_paint_utf8_offset_for_index(const char *text, size_t len, size_t index) {
    size_t pos = 0;
    while (index > 0 && pos < len) {
        pos = hui_paint_utf8_next(text, len, pos);
        index--;
    }
    if (pos > len) pos = len;
    return pos;
}

static size_t hui_paint_count_lines(const char *text, size_t len) {
    size_t lines = 1;
    size_t pos = 0;
    while (pos < len) {
        unsigned char ch = (unsigned char) text[pos];
        if (ch == '\r') {
            pos++;
            continue;
        }
        if (ch == '\n') {
            lines++;
            pos++;
            continue;
        }
        pos = hui_paint_utf8_next(text, len, pos);
    }
    return lines;
}

static size_t hui_paint_line_start_cp(const char *text, size_t len, size_t target_line) {
    size_t line = 0;
    size_t cp = 0;
    size_t pos = 0;
    while (pos < len && line < target_line) {
        unsigned char ch = (unsigned char) text[pos];
        size_t next = hui_paint_utf8_next(text, len, pos);
        cp++;
        if (ch == '\n') line++;
        pos = next;
    }
    return cp;
}

static size_t hui_paint_line_length_from_cp(const char *text, size_t len, size_t line_start_cp) {
    size_t byte_start = hui_paint_utf8_offset_for_index(text, len, line_start_cp);
    size_t pos = byte_start;
    size_t count = 0;
    while (pos < len) {
        unsigned char ch = (unsigned char) text[pos];
        if (ch == '\n') break;
        pos = hui_paint_utf8_next(text, len, pos);
        count++;
    }
    return count;
}

static void hui_paint_cp_to_line_col(const char *text, size_t len, size_t target_cp,
                                     size_t *out_line, size_t *out_col) {
    size_t cp_total = hui_paint_utf8_count_total(text, len);
    if (target_cp > cp_total) target_cp = cp_total;
    size_t line = 0;
    size_t cp = 0;
    size_t line_start_cp = 0;
    size_t pos = 0;
    while (pos < len && cp < target_cp) {
        unsigned char ch = (unsigned char) text[pos];
        size_t next = hui_paint_utf8_next(text, len, pos);
        cp++;
        if (ch == '\n') {
            line++;
            line_start_cp = cp;
        }
        pos = next;
    }
    size_t column = target_cp - line_start_cp;
    if (out_line) *out_line = line;
    if (out_col) *out_col = column;
}

#define HUI_TEXT_SELECTION_COLOR 0x663A7AFEu

static int hui_node_visible(const hui_dom_node *node, float viewport_w, float viewport_h);

static void hui_paint_rect_batch_reset(hui_draw_list *list) {
    if (!list) return;
    list->rect_batch_active = 0;
    list->rect_batch_color = 0;
    list->rect_batch_start = 0;
    list->rect_batch_count = 0;
}

static void hui_paint_rect_batch_flush(hui_draw_list *list) {
    if (!list) return;
    if (!list->rect_batch_active || list->rect_batch_count == 0) {
        hui_paint_rect_batch_reset(list);
        return;
    }

    size_t start = list->rect_batch_start;
    size_t remaining = list->rect_batch_count;
    if (remaining == 1) {
        if (start < list->rects.len) {
            hui_draw_rect rect = list->rects.data[start];
            list->rects.len = start;
            hui_draw draw;
            memset(&draw, 0, sizeof(draw));
            draw.op = HUI_DRAW_OP_RECT;
            draw.u0 = list->rect_batch_color;
            draw.u1 = rect.node_index;
            draw.f[0] = rect.x;
            draw.f[1] = rect.y;
            draw.f[2] = rect.w;
            draw.f[3] = rect.h;
            hui_vec_push(&list->cmds, draw);
        }
        hui_paint_rect_batch_reset(list);
        return;
    }

    if (start > UINT32_MAX) {
        if (start < list->rects.len) {
            const hui_draw_rect *rects = &list->rects.data[start];
            for (size_t i = 0; i < remaining; i++) {
                hui_draw draw;
                memset(&draw, 0, sizeof(draw));
                draw.op = HUI_DRAW_OP_RECT;
                draw.u0 = list->rect_batch_color;
                draw.u1 = rects[i].node_index;
                draw.f[0] = rects[i].x;
                draw.f[1] = rects[i].y;
                draw.f[2] = rects[i].w;
                draw.f[3] = rects[i].h;
                hui_vec_push(&list->cmds, draw);
            }
        }
        list->rects.len = start;
        hui_paint_rect_batch_reset(list);
        return;
    }

    size_t offset = start;
    while (remaining > 0) {
        size_t chunk = remaining;
        if (chunk > UINT32_MAX) chunk = UINT32_MAX;
        hui_draw draw;
        memset(&draw, 0, sizeof(draw));
        draw.op = HUI_DRAW_OP_RECT_BATCH;
        draw.u0 = list->rect_batch_color;
        draw.u1 = (uint32_t) offset;
        draw.u2 = (uint32_t) chunk;
        hui_vec_push(&list->cmds, draw);
        offset += chunk;
        remaining -= chunk;
    }

    hui_paint_rect_batch_reset(list);
}

static void hui_paint_rect_batch_start(hui_draw_list *list, uint32_t color) {
    if (!list) return;
    list->rect_batch_active = 1;
    list->rect_batch_color = color;
    list->rect_batch_start = list->rects.len;
    list->rect_batch_count = 0;
}

static void hui_paint_emit_rect(hui_draw_list *list, uint32_t color, uint32_t node_index,
                                float x, float y, float w, float h) {
    if (!list) return;
    if (!list->rect_batch_active || list->rect_batch_color != color ||
        list->rect_batch_count >= UINT32_MAX) {
        hui_paint_rect_batch_flush(list);
        hui_paint_rect_batch_start(list, color);
    }
    hui_draw_rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    rect.node_index = node_index;
    hui_vec_push(&list->rects, rect);
    list->rect_batch_count++;
}

static void hui_paint_node(hui_draw_list *list, const hui_dom *dom, const hui_style_store *styles,
                           uint32_t idx, float viewport_w, float viewport_h) {
    if (!list || !dom || !styles) return;
    if (idx >= dom->nodes.len) return;
    if (idx >= styles->styles.len) return;
    const hui_dom_node *node = &dom->nodes.data[idx];
    const hui_computed_style *cs = &styles->styles.data[idx];
    if (cs->display == 0) return;
    if (!hui_node_visible(node, viewport_w, viewport_h)) return;

    if (node->type == HUI_NODE_ELEM) {
        if ((cs->present_mask & HUI_STYLE_PRESENT_BG_COLOR) && (cs->bg_color >> 24) != 0) {
            hui_paint_emit_rect(list, cs->bg_color, idx, node->x, node->y, node->w, node->h);
        }
    }

    if (node->type == HUI_NODE_TEXT) {
        int is_text_field = (node->tf_flags & HUI_NODE_TF_VALUE) != 0;
        const char *text = node->text ? node->text : "";
        size_t len = node->text ? node->text_len : 0;
        int has_text = (text && len > 0);

        float font_size = cs->font_size > 0.0f ? cs->font_size : 16.0f;
        float char_width = font_size * HUI_TEXT_APPROX_CHAR_ADVANCE;
        float line_height = cs->line_height > 0.0f ? cs->line_height : (font_size * HUI_TEXT_APPROX_LINE_HEIGHT);
        if (cs->line_height > 0.0f && cs->line_height <= 4.0f) line_height = cs->line_height * font_size;
        size_t cp_total = 0;
        size_t line_count = 1;
        if (has_text) {
            hui_dom_node *mutable_node = (hui_dom_node *) node;
            hui_dom_text_cache_refresh(mutable_node);
            cp_total = mutable_node->text_cache_cp;
            line_count = mutable_node->text_cache_lines ? mutable_node->text_cache_lines : 1;
            text = mutable_node->text ? mutable_node->text : "";
            len = mutable_node->text_len;
        }

        float scroll_x = is_text_field ? node->tf_scroll_x : 0.0f;
        int is_multiline_field = is_text_field && ((node->tf_flags & HUI_NODE_TF_MULTILINE) != 0);

        float text_height = font_size > 0.0f ? font_size : line_height;
        if (is_text_field) {
            if ((node->tf_flags & HUI_NODE_TF_HAS_SELECTION) != 0 &&
                (node->tf_flags & HUI_NODE_TF_PLACEHOLDER) == 0 &&
                node->tf_sel_end > node->tf_sel_start) {
                size_t sel_start_cp = node->tf_sel_start;
                size_t sel_end_cp = node->tf_sel_end;
                if (sel_start_cp > cp_total) sel_start_cp = cp_total;
                if (sel_end_cp > cp_total) sel_end_cp = cp_total;
                if (sel_end_cp > sel_start_cp) {
                    size_t start_line = 0, start_col = 0;
                    size_t end_line = 0, end_col = 0;
                    hui_paint_cp_to_line_col(text, len, sel_start_cp, &start_line, &start_col);
                    hui_paint_cp_to_line_col(text, len, sel_end_cp, &end_line, &end_col);
                    if (end_line >= start_line) {
                        for (size_t line = start_line; line <= end_line; line++) {
                            size_t line_start_cp = hui_paint_line_start_cp(text, len, line);
                            size_t line_len = hui_paint_line_length_from_cp(text, len, line_start_cp);
                            size_t line_start_col = (line == start_line) ? start_col : 0;
                            size_t line_end_col = (line == end_line) ? end_col : line_len;
                            if (line_start_col > line_len) line_start_col = line_len;
                            if (line_end_col > line_len) line_end_col = line_len;
                            if (line_end_col <= line_start_col) continue;
                            float highlight_x = node->x - scroll_x + char_width * (float) line_start_col;
                            float highlight_w = char_width * (float) (line_end_col - line_start_col);
                            float highlight_y = node->y + line_height * (float) line;
                            float line_bottom = highlight_y + line_height;
                            if (highlight_y + text_height > line_bottom)
                                highlight_y = line_bottom - text_height;
                            if (highlight_y < node->y) highlight_y = node->y;
                            if (highlight_y + text_height > node->y + node->h)
                                highlight_y = (node->y + node->h) - text_height;
                            if (node->w > 0.0f) {
                                float max_x = node->x + node->w;
                                if (highlight_x < node->x) {
                                    float delta = node->x - highlight_x;
                                    highlight_x = node->x;
                                    highlight_w = fmaxf(0.0f, highlight_w - delta);
                                }
                                if (highlight_x + highlight_w > max_x)
                                    highlight_w = max_x - highlight_x;
                            }
                            if (highlight_w <= 0.0f) continue;
                            hui_paint_emit_rect(list, HUI_TEXT_SELECTION_COLOR, idx,
                                                highlight_x, highlight_y, highlight_w, text_height);
                        }
                    }
                }
            }

            if ((node->tf_flags & (HUI_NODE_TF_FOCUSED | HUI_NODE_TF_CARET_VISIBLE)) ==
                (HUI_NODE_TF_FOCUSED | HUI_NODE_TF_CARET_VISIBLE) &&
                (node->tf_flags & HUI_NODE_TF_PLACEHOLDER) == 0) {
                size_t caret_cp = node->tf_caret;
                size_t caret_line = 0;
                size_t caret_col = 0;
                hui_paint_cp_to_line_col(text, len, caret_cp, &caret_line, &caret_col);
                float caret_x = node->x - scroll_x + char_width * (float) caret_col;
                float caret_h = text_height;
                float caret_y = node->y + line_height * (float) caret_line;
                float line_bottom = caret_y + line_height;
                if (caret_y + caret_h > line_bottom)
                    caret_y = line_bottom - caret_h;
                if (node->w > 0.0f) {
                    float min_x = node->x;
                    float max_x = node->x + node->w;
                    if (caret_x < min_x) caret_x = min_x;
                    if (caret_x > max_x) caret_x = max_x;
                }
                if (caret_y < node->y) caret_y = node->y;
                if (caret_y + caret_h > node->y + node->h)
                    caret_y = (node->y + node->h) - caret_h;
                float caret_w = fmaxf(char_width * 0.1f, 1.0f);
                hui_paint_emit_rect(list, (cs->color & 0x00FFFFFFu) | 0xFF000000u,
                                    idx, caret_x, caret_y, caret_w, caret_h);
            }
        }

        if (has_text) {
            hui_paint_rect_batch_flush(list);
            hui_draw draw;
            memset(&draw, 0, sizeof(draw));
            draw.f[5] = -1.0f;
            draw.op = HUI_DRAW_OP_GLYPH_RUN;
            draw.u0 = cs->color;
            draw.u1 = idx;
            draw.f[0] = node->x;
            draw.f[1] = node->y;
            draw.f[2] = node->w;
            draw.f[3] = node->h;
            draw.f[4] = cs->font_size;
            draw.f[5] = scroll_x;
            draw.f[6] = line_height;
            draw.u2 = cs->font_id;
            hui_vec_push(&list->cmds, draw);
        }
    }

    if (node->type == HUI_NODE_ELEM) {
        uint32_t child = node->first_child;
        while (child != 0xFFFFFFFFu) {
            if (child >= dom->nodes.len) break;
            hui_paint_node(list, dom, styles, child, viewport_w, viewport_h);
            child = dom->nodes.data[child].next_sibling;
        }
    }
}


void hui_draw_list_init(hui_draw_list *list) {
    if (!list) return;
    hui_vec_init(&list->cmds);
    hui_vec_init(&list->rects);
    hui_paint_rect_batch_reset(list);
}

void hui_draw_list_reset(hui_draw_list *list) {
    if (!list) return;
    list->cmds.len = 0;
    list->rects.len = 0;
    hui_paint_rect_batch_reset(list);
}

void hui_draw_list_release(hui_draw_list *list) {
    if (!list) return;
    hui_vec_free(&list->cmds);
    hui_vec_free(&list->rects);
    hui_paint_rect_batch_reset(list);
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
    if (!list || !dom || !styles) return;
    if (dom->nodes.len == 0 || styles->styles.len == 0) return;
    if (dom->root != 0xFFFFFFFFu) {
        uint32_t node = dom->root;
        while (node != 0xFFFFFFFFu) {
            if (node >= dom->nodes.len) break;
            hui_paint_node(list, dom, styles, node, viewport_w, viewport_h);
            uint32_t next = dom->nodes.data[node].next_sibling;
            node = next;
        }
    } else {
        size_t limit = styles->styles.len;
        if (limit > dom->nodes.len) limit = dom->nodes.len;
        for (size_t i = 0; i < limit; i++) {
            if (dom->nodes.data[i].parent != 0xFFFFFFFFu) continue;
            hui_paint_node(list, dom, styles, (uint32_t) i, viewport_w, viewport_h);
        }
    }
    hui_paint_rect_batch_flush(list);
}
