#include "hui_layout.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "../../include/hui/hui.h"

typedef struct {
    uint32_t index;
    float base_main;
    float target_main;
    float flex_grow;
    float flex_shrink;
    float shrink_weight;
    float cross_size;
} hui_flex_item;

static void layout_node(hui_dom *dom, const hui_style_store *styles, uint32_t idx, float x, float y, float width);

static void translate_subtree(hui_dom *dom, uint32_t idx, float dx, float dy) {
    if (!dom || idx == 0xFFFFFFFFu) return;
    hui_dom_node *node = &dom->nodes.data[idx];
    node->x += dx;
    node->y += dy;
    uint32_t child = node->first_child;
    while (child != 0xFFFFFFFFu) {
        translate_subtree(dom, child, dx, dy);
        child = dom->nodes.data[child].next_sibling;
    }
}

static void layout_flex_container(hui_dom *dom, const hui_style_store *styles, uint32_t idx,
                                  hui_dom_node *node, const hui_computed_style *cs,
                                  float x, float y, float width) {
    (void) idx;
    float container_width = (cs->width > 0.0f) ? cs->width : width;
    float inner_width = container_width - (cs->padding[1] + cs->padding[3]);
    if (inner_width < 0.0f) inner_width = 0.0f;
    uint32_t child_iter = node->first_child;
    size_t item_count = 0;
    while (child_iter != 0xFFFFFFFFu) {
        const hui_computed_style *child_cs = &styles->styles.data[child_iter];
        if (child_cs->display != HUI_DISPLAY_NONE) item_count++;
        child_iter = dom->nodes.data[child_iter].next_sibling;
    }
    if (item_count == 0) {
        node->h = cs->padding[0] + cs->padding[2];
        return;
    }
    hui_flex_item *items = (hui_flex_item *) malloc(sizeof(hui_flex_item) * item_count);
    if (!items) {
        node->h = cs->padding[0] + cs->padding[2];
        return;
    }

    float base_sum = 0.0f;
    float total_grow = 0.0f;
    float total_shrink_weight = 0.0f;
    size_t item_idx = 0;
    child_iter = node->first_child;
    while (child_iter != 0xFFFFFFFFu && item_idx < item_count) {
        const hui_computed_style *child_cs = &styles->styles.data[child_iter];
        if (child_cs->display == HUI_DISPLAY_NONE) {
            child_iter = dom->nodes.data[child_iter].next_sibling;
            continue;
        }
        hui_flex_item *item = &items[item_idx++];
        item->index = child_iter;
        item->flex_grow = (child_cs->flex_grow > 0.0f) ? child_cs->flex_grow : 0.0f;
        item->flex_shrink = (child_cs->flex_shrink >= 0.0f) ? child_cs->flex_shrink : 1.0f;
        float measure_width = 0.0f;
        if (cs->flex_direction == HUI_FLEX_DIRECTION_ROW) {
            if (child_cs->flex_basis >= 0.0f) measure_width = child_cs->flex_basis;
            else if (child_cs->width >= 0.0f) measure_width = child_cs->width;
            else measure_width = 0.0f;
        } else {
            if (child_cs->width >= 0.0f) measure_width = child_cs->width;
            else measure_width = inner_width;
        }
        if (measure_width < 0.0f) measure_width = 0.0f;
        layout_node(dom, styles, child_iter, 0.0f, 0.0f, measure_width);
        hui_dom_node *child_node = &dom->nodes.data[child_iter];
        if (cs->flex_direction == HUI_FLEX_DIRECTION_ROW) {
            if (child_cs->flex_basis >= 0.0f) item->base_main = child_cs->flex_basis;
            else if (child_cs->width >= 0.0f) item->base_main = child_cs->width;
            else item->base_main = child_node->w;
            item->cross_size = child_node->h;
        } else {
            if (child_cs->flex_basis >= 0.0f) item->base_main = child_cs->flex_basis;
            else if (child_cs->height >= 0.0f) item->base_main = child_cs->height;
            else item->base_main = child_node->h;
            item->cross_size = child_node->w;
        }
        if (item->base_main < 0.0f) item->base_main = 0.0f;
        item->target_main = item->base_main;
        item->shrink_weight = item->flex_shrink * item->base_main;
        base_sum += item->base_main;
        total_grow += item->flex_grow;
        total_shrink_weight += item->shrink_weight;
        child_iter = child_node->next_sibling;
    }

    float available_main = (cs->flex_direction == HUI_FLEX_DIRECTION_ROW)
                           ? inner_width
                           : ((cs->height >= 0.0f) ? cs->height : base_sum);
    float leftover = available_main - base_sum;
    if (cs->flex_direction == HUI_FLEX_DIRECTION_COLUMN && cs->height < 0.0f) {
        leftover = 0.0f;
    }
    if (leftover > 0.0f && total_grow > 0.0f) {
        for (size_t i = 0; i < item_count; i++) {
            float share = items[i].flex_grow / total_grow;
            items[i].target_main = items[i].base_main + leftover * share;
        }
    } else if (leftover < 0.0f && total_shrink_weight > 0.0f) {
        float deficit = -leftover;
        for (size_t i = 0; i < item_count; i++) {
            float weight = items[i].shrink_weight;
            float delta = (weight / total_shrink_weight) * deficit;
            float new_main = items[i].base_main - delta;
            if (new_main < 0.0f) new_main = 0.0f;
            items[i].target_main = new_main;
        }
    }

    float main_total = 0.0f;
    float max_cross = 0.0f;
    for (size_t i = 0; i < item_count; i++) {
        const hui_computed_style *child_cs = &styles->styles.data[items[i].index];
        float width_param = 0.0f;
        if (cs->flex_direction == HUI_FLEX_DIRECTION_ROW) {
            width_param = items[i].target_main;
        } else {
            width_param = (child_cs->width >= 0.0f) ? child_cs->width : inner_width;
        }
        if (width_param < 0.0f) width_param = 0.0f;
        layout_node(dom, styles, items[i].index, 0.0f, 0.0f, width_param);
        hui_dom_node *child_node = &dom->nodes.data[items[i].index];
        if (cs->flex_direction == HUI_FLEX_DIRECTION_ROW) {
            items[i].target_main = child_node->w;
            items[i].cross_size = child_node->h;
        } else {
            items[i].target_main = child_node->h;
            items[i].cross_size = child_node->w;
        }
        main_total += items[i].target_main;
        if (items[i].cross_size > max_cross) max_cross = items[i].cross_size;
    }

    if (cs->flex_direction == HUI_FLEX_DIRECTION_ROW)
        available_main = inner_width;
    else
        available_main = (cs->height >= 0.0f) ? cs->height : main_total;
    float free_space = available_main - main_total;
    float distributable = (free_space > 0.0f) ? free_space : 0.0f;

    float gap = 0.0f;
    float start_offset = 0.0f;
    size_t visible = item_count;
    switch (cs->justify_content) {
        case HUI_FLEX_JUSTIFY_FLEX_START: break;
        case HUI_FLEX_JUSTIFY_CENTER:
            start_offset = distributable * 0.5f;
            break;
        case HUI_FLEX_JUSTIFY_FLEX_END:
            start_offset = (free_space > 0.0f) ? free_space : 0.0f;
            break;
        case HUI_FLEX_JUSTIFY_SPACE_BETWEEN:
            if (visible > 1) gap = distributable / (float) (visible - 1);
            else start_offset = distributable * 0.5f;
            break;
        case HUI_FLEX_JUSTIFY_SPACE_AROUND:
            if (visible > 0) {
                gap = distributable / (float) visible;
                start_offset = gap * 0.5f;
            }
            break;
        case HUI_FLEX_JUSTIFY_SPACE_EVENLY:
            if (visible > 0) {
                gap = distributable / (float) (visible + 1);
                start_offset = gap;
            }
            break;
        default: break;
    }

    float main_start = (cs->flex_direction == HUI_FLEX_DIRECTION_ROW) ? (x + cs->padding[3])
                                                                      : (y + cs->padding[0]);
    float main_cursor = start_offset;
    float main_end_max = 0.0f;
    float cross_available;
    if (cs->flex_direction == HUI_FLEX_DIRECTION_ROW) {
        cross_available = (cs->height >= 0.0f) ? cs->height : max_cross;
    } else {
        float inner_cross = (cs->width > 0.0f) ? cs->width : container_width;
        inner_cross -= (cs->padding[1] + cs->padding[3]);
        if (inner_cross < 0.0f) inner_cross = 0.0f;
        cross_available = inner_cross;
    }

    for (size_t i = 0; i < item_count; i++) {
        hui_dom_node *child_node = &dom->nodes.data[items[i].index];
        const hui_computed_style *child_cs = &styles->styles.data[items[i].index];
        uint32_t align = child_cs->align_self;
        if (align == HUI_FLEX_ALIGN_AUTO) align = cs->align_items;
        float child_main = items[i].target_main;
        float child_cross = items[i].cross_size;
        float cross_offset = 0.0f;
        if (align == HUI_FLEX_ALIGN_CENTER)
            cross_offset = (cross_available - child_cross) * 0.5f;
        else if (align == HUI_FLEX_ALIGN_FLEX_END)
            cross_offset = cross_available - child_cross;
        if (cross_offset < 0.0f) cross_offset = 0.0f;

        float final_x = x + cs->padding[3];
        float final_y = y + cs->padding[0];
        if (cs->flex_direction == HUI_FLEX_DIRECTION_ROW) {
            final_x += main_cursor;
            final_y += cross_offset;
            translate_subtree(dom, items[i].index, final_x - child_node->x, final_y - child_node->y);
            if (align == HUI_FLEX_ALIGN_STRETCH && cross_available > 0.0f)
                child_node->h = cross_available;
            main_cursor += child_main + gap;
            float end_pos = (final_x - main_start) + child_main;
            if (end_pos > main_end_max) main_end_max = end_pos;
        } else {
            final_x += cross_offset;
            final_y += main_cursor;
            translate_subtree(dom, items[i].index, final_x - child_node->x, final_y - child_node->y);
            if (align == HUI_FLEX_ALIGN_STRETCH && cross_available > 0.0f)
                child_node->w = cross_available;
            main_cursor += child_main + gap;
            float end_pos = (final_y - main_start) + child_main;
            if (end_pos > main_end_max) main_end_max = end_pos;
        }
    }

    if (cs->flex_direction == HUI_FLEX_DIRECTION_ROW) {
        float content_cross = (cs->height >= 0.0f) ? cs->height : max_cross;
        if (content_cross < 0.0f) content_cross = 0.0f;
        node->h = content_cross + cs->padding[0] + cs->padding[2];
        if (cs->height >= 0.0f && node->h < cs->height + cs->padding[0] + cs->padding[2])
            node->h = cs->height + cs->padding[0] + cs->padding[2];
        if (cs->width <= 0.0f) {
            float used_main = main_end_max + cs->padding[1] + cs->padding[3];
            if (used_main > node->w) node->w = used_main;
        }
    } else {
        float content_main = (cs->height >= 0.0f) ? cs->height : main_end_max;
        if (content_main < 0.0f) content_main = 0.0f;
        node->h = content_main + cs->padding[0] + cs->padding[2];
        if (cs->width <= 0.0f) {
            float used_cross = cross_available + cs->padding[1] + cs->padding[3];
            if (used_cross > node->w) node->w = used_cross;
        }
    }

    free(items);
}

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
    if (node->type == HUI_NODE_ELEM && cs->display == HUI_DISPLAY_FLEX) {
        layout_flex_container(dom, styles, idx, node, cs, x, y, node->w);
        return;
    }
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
