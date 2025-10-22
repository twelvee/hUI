#include "../include/hui/hui.h"

#include "hui_err.h"
#include "html/hui_html_builder.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#define HUI_TEXT_FIELD_DEFAULT_INITIAL_DELAY 0.35f
#define HUI_TEXT_FIELD_DEFAULT_REPEAT_DELAY 0.05f
#define HUI_TEXT_FIELD_CARET_BLINK_PERIOD 0.5f

static int hui_view_contains(const hui_u32_view *view, uint32_t value);

static size_t hui_utf8_next(const char *text, size_t len, size_t offset) {
    if (!text || offset >= len) return len;
    offset++;
    while (offset < len && ((unsigned char) text[offset] & 0xC0u) == 0x80u)
        offset++;
    if (offset > len) offset = len;
    return offset;
}

static size_t hui_utf8_prev(const char *text, size_t len, size_t offset) {
    if (!text || len == 0) return 0;
    if (offset > len) offset = len;
    if (offset == 0) return 0;
    offset--;
    while (offset > 0 && ((unsigned char) text[offset] & 0xC0u) == 0x80u)
        offset--;
    return offset;
}

static size_t hui_utf8_offset_for_index(const char *text, size_t len, size_t index) {
    size_t pos = 0;
    while (index > 0 && pos < len) {
        pos = hui_utf8_next(text, len, pos);
        index--;
    }
    return pos;
}

static size_t hui_utf8_count_range(const char *text, size_t start, size_t end) {
    if (!text || start >= end) return 0;
    size_t count = 0;
    size_t pos = start;
    while (pos < end) {
        size_t next = hui_utf8_next(text, end, pos);
        if (next == pos) break;
        pos = next;
        count++;
    }
    return count;
}

static size_t hui_utf8_count_total(const char *text, size_t len) {
    return hui_utf8_count_range(text, 0, len);
}

static int hui_handles_equal(hui_node_handle a, hui_node_handle b) {
    return a.index == b.index && a.gen == b.gen;
}

static size_t hui_text_field_selection_start(const hui_text_field *field) {
    if (!field) return 0;
    return field->caret < field->sel_anchor ? field->caret : field->sel_anchor;
}

static size_t hui_text_field_selection_end(const hui_text_field *field) {
    if (!field) return 0;
    return field->caret < field->sel_anchor ? field->sel_anchor : field->caret;
}

static int hui_text_field_has_selection(const hui_text_field *field) {
    return field && hui_text_field_selection_end(field) > hui_text_field_selection_start(field);
}

static void hui_text_field_reset_blink(hui_text_field *field) {
    if (!field) return;
    field->caret_timer = 0.0f;
    field->caret_visible = 1;
}

static void hui_text_field_cancel_nav(hui_text_field *field) {
    if (!field) return;
    field->nav_active_key = 0;
    field->nav_timer = 0.0f;
}

static int hui_text_field_nav_triggered(hui_ctx *ctx, hui_text_field *field,
                                        const hui_input_state *state,
                                        uint32_t keycode, float dt) {
    if (!ctx || !field || !state || keycode == 0) return 0;
    int pressed = hui_view_contains(&state->keys_pressed, keycode);
    int released = hui_view_contains(&state->keys_released, keycode);
    int held = hui_input_key_down(ctx, keycode);

    if (pressed) {
        field->nav_active_key = keycode;
        field->nav_timer = field->backspace_initial_delay;
        return 1;
    }

    if (field->nav_active_key != keycode) return 0;

    if (!held || released) {
        hui_text_field_cancel_nav(field);
        return 0;
    }

    if (field->nav_timer > 0.0f)
        field->nav_timer -= dt;
    if (field->nav_timer <= 0.0f) {
        field->nav_timer += field->backspace_repeat_delay;
        if (field->nav_timer < 0.0f) field->nav_timer = field->backspace_repeat_delay;
        return 1;
    }
    return 0;
}

static size_t hui_text_field_count_lines(const char *text, size_t len) {
    if (!text || len == 0) return 1;
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
        pos = hui_utf8_next(text, len, pos);
    }
    return lines;
}

static size_t hui_text_field_line_start_cp(const char *text, size_t len, size_t target_line) {
    size_t line = 0;
    size_t cp = 0;
    size_t pos = 0;
    while (pos < len && line < target_line) {
        unsigned char ch = (unsigned char) text[pos];
        size_t next = hui_utf8_next(text, len, pos);
        cp++;
        if (ch == '\n') line++;
        pos = next;
    }
    return cp;
}

static size_t hui_text_field_line_length_from_cp(const char *text, size_t len, size_t line_start_cp) {
    size_t byte_start = hui_utf8_offset_for_index(text, len, line_start_cp);
    size_t pos = byte_start;
    size_t cols = 0;
    while (pos < len) {
        unsigned char ch = (unsigned char) text[pos];
        if (ch == '\n') break;
        pos = hui_utf8_next(text, len, pos);
        cols++;
    }
    return cols;
}

static size_t hui_text_field_caret_from_point(hui_ctx *ctx, hui_text_field *field, float px, float py) {
    (void) ctx;
    if (!field) return 0;
    if (field->placeholder_visible) return 0;
    hui_rect rect = (hui_rect){0, 0, 0, 0};
    if (hui_node_get_layout(ctx, field->text, &rect) != HUI_OK) {
        if (hui_node_get_layout(ctx, field->container, &rect) != HUI_OK)
            return field->caret;
    }
    const char *text = field->buffer;
    size_t len = field->length;
    size_t cp_total = hui_utf8_count_total(text, len);
    if (!field->multiline) {
        float width = rect.w;
        float font_size = rect.h > 0.0f ? rect.h / HUI_TEXT_APPROX_LINE_HEIGHT : 16.0f;
        if (font_size <= 0.0f) font_size = 16.0f;
        float char_width = font_size * HUI_TEXT_APPROX_CHAR_ADVANCE;
        if (char_width <= 0.1f) char_width = 1.0f;
        float relative = px - rect.x;
        if (relative < 0.0f) relative = 0.0f;
        if (width > 0.0f && relative > width) relative = width;
        float content_x = relative + field->scroll_x;
        if (content_x < 0.0f) content_x = 0.0f;
        float cp_pos = content_x / char_width;
        if (cp_pos < 0.0f) cp_pos = 0.0f;
        size_t cp_index = (size_t) (cp_pos + 0.5f);
        if (cp_total > 0 && cp_index > cp_total) cp_index = cp_total;
        return hui_utf8_offset_for_index(field->buffer, field->length, cp_index);
    }

    size_t line_count = hui_text_field_count_lines(text, len);
    if (line_count == 0) line_count = 1;
    float total_height = rect.h;
    float line_height = (total_height > 0.0f) ? (total_height / (float) line_count) : 0.0f;
    float font_size = (line_height > 0.0f) ? (line_height / HUI_TEXT_APPROX_LINE_HEIGHT) : 16.0f;
    if (font_size <= 0.0f) font_size = 16.0f;
    if (line_height <= 0.0f) line_height = font_size * HUI_TEXT_APPROX_LINE_HEIGHT;
    float char_width = font_size * HUI_TEXT_APPROX_CHAR_ADVANCE;
    if (char_width <= 0.1f) char_width = 1.0f;

    float relative_y = py - rect.y;
    if (relative_y < 0.0f) relative_y = 0.0f;
    float max_height = line_height * (float) line_count;
    if (relative_y > max_height) relative_y = max_height;
    size_t line_index = (line_height > 0.0f) ? (size_t) (relative_y / line_height) : 0;
    if (line_index >= line_count) line_index = line_count - 1;

    size_t line_start_cp = hui_text_field_line_start_cp(text, len, line_index);
    size_t line_len = hui_text_field_line_length_from_cp(text, len, line_start_cp);

    float relative_x = px - rect.x + field->scroll_x;
    if (relative_x < 0.0f) relative_x = 0.0f;
    if (rect.w > 0.0f && relative_x > rect.w) relative_x = rect.w;
    size_t column = (size_t) (relative_x / char_width + 0.5f);
    if (column > line_len) column = line_len;

    size_t cp_index = line_start_cp + column;
    if (cp_index > cp_total) cp_index = cp_total;
    return hui_utf8_offset_for_index(field->buffer, field->length, cp_index);
}
static float hui_text_field_font_size(hui_ctx *ctx, const hui_text_field *field) {
    if (!ctx || !field) return 16.0f;
    hui_rect rect;
    if (!hui_node_is_null(field->text) &&
        hui_node_get_layout(ctx, field->text, &rect) == HUI_OK) {
        if (rect.h > 0.0f) {
            float fs = rect.h / HUI_TEXT_APPROX_LINE_HEIGHT;
            if (fs > 0.0f) return fs;
        }
    }
    if (!hui_node_is_null(field->value) &&
        hui_node_get_layout(ctx, field->value, &rect) == HUI_OK) {
        if (rect.h > 0.0f) {
            float fs = rect.h / HUI_TEXT_APPROX_LINE_HEIGHT;
            if (fs > 0.0f) return fs;
        }
    }
    if (!hui_node_is_null(field->container) &&
        hui_node_get_layout(ctx, field->container, &rect) == HUI_OK) {
        if (rect.h > 0.0f) {
            float fs = rect.h / HUI_TEXT_APPROX_LINE_HEIGHT;
            if (fs > 0.0f) return fs;
        }
    }
    return 16.0f;
}

static float hui_text_field_inner_width(hui_ctx *ctx, const hui_text_field *field) {
    if (!ctx || !field) return 0.0f;
    hui_rect rect;
    if (hui_node_get_layout(ctx, field->container, &rect) != HUI_OK)
        return 0.0f;
    float padding_left = 0.0f;
    float padding_right = 0.0f;
    hui_rect value_rect;
    if (!hui_node_is_null(field->value) &&
        hui_node_get_layout(ctx, field->value, &value_rect) == HUI_OK) {
        padding_left = value_rect.x - rect.x;
        float value_right = value_rect.x + value_rect.w;
        float container_right = rect.x + rect.w;
        padding_right = container_right - value_right;
        if (padding_left < 0.0f) padding_left = 0.0f;
        if (padding_right < 0.0f) padding_right = 0.0f;
    }
    float inner = rect.w - padding_left - padding_right;
    if (inner < 0.0f) inner = 0.0f;
    return inner;
}

static void hui_text_field_update_scroll(hui_ctx *ctx, hui_text_field *field,
                                         size_t caret_cp, size_t sel_start_cp,
                                         size_t sel_end_cp, size_t cp_total) {
    if (!ctx || !field) return;
    if (field->multiline || field->placeholder_visible) {
        field->scroll_x = 0.0f;
        return;
    }
    float inner_width = hui_text_field_inner_width(ctx, field);
    if (inner_width <= 0.0f) {
        field->scroll_x = 0.0f;
        return;
    }
    float font_size = hui_text_field_font_size(ctx, field);
    if (font_size <= 0.0f) font_size = 16.0f;
    float char_width = font_size * HUI_TEXT_APPROX_CHAR_ADVANCE;
    if (char_width <= 0.0f) {
        field->scroll_x = 0.0f;
        return;
    }
    float text_px = char_width * (float) cp_total;
    float max_scroll = text_px - inner_width;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    float desired = field->scroll_x;
    float caret_px = char_width * (float) caret_cp;
    float left_margin = char_width * 0.5f;
    float right_margin = char_width * 0.75f;
    float visible_start = desired;
    float visible_end = visible_start + inner_width;

    if (caret_px < visible_start + left_margin)
        desired = caret_px - left_margin;
    if (caret_px > visible_end - right_margin)
        desired = caret_px - (inner_width - right_margin);

    if (sel_start_cp < sel_end_cp) {
        float sel_start_px = char_width * (float) sel_start_cp;
        float sel_end_px = char_width * (float) sel_end_cp;
        if (sel_start_px < desired)
            desired = sel_start_px;
        if (sel_end_px > desired + inner_width)
            desired = sel_end_px - inner_width;
    }

    if (desired < 0.0f) desired = 0.0f;
    if (desired > max_scroll) desired = max_scroll;
    field->scroll_x = desired;
}

static void hui_text_field_update_dom_state(hui_ctx *ctx, hui_text_field *field) {
    if (!ctx || !field) return;
    uint32_t flags = HUI_NODE_TF_VALUE;
    size_t caret_cp = 0;
    size_t sel_start_cp = 0;
    size_t sel_end_cp = 0;
    size_t cp_total = 0;

    if (field->placeholder_visible) {
        flags |= HUI_NODE_TF_PLACEHOLDER;
        if (field->focused) flags |= HUI_NODE_TF_FOCUSED;
        field->scroll_x = 0.0f;
        field->scroll_y = 0.0f;
    } else {
        if (field->focused) flags |= HUI_NODE_TF_FOCUSED;
        if (field->caret_visible && field->focused)
            flags |= HUI_NODE_TF_CARET_VISIBLE;
        size_t sel_start = hui_text_field_selection_start(field);
        size_t sel_end = hui_text_field_selection_end(field);
        caret_cp = hui_utf8_count_range(field->buffer, 0, field->caret);
        sel_start_cp = hui_utf8_count_range(field->buffer, 0, sel_start);
        sel_end_cp = hui_utf8_count_range(field->buffer, 0, sel_end);
        cp_total = hui_utf8_count_total(field->buffer, field->length);
        if (sel_end_cp > sel_start_cp)
            flags |= HUI_NODE_TF_HAS_SELECTION;
        hui_text_field_update_scroll(ctx, field, caret_cp, sel_start_cp, sel_end_cp, cp_total);
        field->scroll_y = 0.0f;
    }

    hui_dom_set_text_field_state(ctx, field->text, flags,
                                 (uint32_t) caret_cp,
                                 (uint32_t) sel_start_cp,
                                 (uint32_t) sel_end_cp,
                                 field->scroll_x,
                                 field->scroll_y);
}

static void hui_text_field_set_selection_internal(hui_ctx *ctx, hui_text_field *field,
                                                  size_t anchor, size_t caret, int reset_blink) {
    if (!field) return;
    if (anchor > field->length) anchor = field->length;
    if (caret > field->length) caret = field->length;
    field->sel_anchor = anchor;
    field->caret = caret;
    field->select_all = (field->length > 0 && hui_text_field_selection_start(field) == 0 &&
                         hui_text_field_selection_end(field) == field->length);
    if (reset_blink) hui_text_field_reset_blink(field);
    hui_text_field_update_dom_state(ctx, field);
}
static uint32_t hui_text_field_apply_text(hui_ctx *ctx, hui_text_field *field) {
    if (field->placeholder_visible && field->length == 0) {
        field->placeholder_visible = 0;
        hui_text_field_update_dom_state(ctx, field);
        return 0;
    }
    if (hui_dom_set_text(ctx, field->text, field->buffer) != HUI_OK) return 0;
    hui_text_field_update_dom_state(ctx, field);
    return HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
}

static uint32_t hui_text_field_show_placeholder(hui_ctx *ctx, hui_text_field *field) {
    if (!field->placeholder) return 0;
    int was_visible = field->placeholder_visible;
    field->caret = 0;
    field->sel_anchor = 0;
    field->caret_visible = 0;
    field->select_all = 0;
    hui_text_field_cancel_nav(field);
    uint32_t dirty = 0;
    field->placeholder_visible = 1;
    if (!was_visible) {
        if (hui_dom_set_text(ctx, field->text, field->placeholder) == HUI_OK)
            dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
        if (hui_dom_add_class(ctx, field->container, "placeholder") == HUI_OK)
            dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
    }
    field->scroll_x = 0.0f;
    field->scroll_y = 0.0f;
    hui_text_field_update_dom_state(ctx, field);
    return dirty;
}

static uint32_t hui_text_field_hide_placeholder(hui_ctx *ctx, hui_text_field *field) {
    if (!field->placeholder_visible) return 0;
    field->placeholder_visible = 0;
    uint32_t dirty = 0;
    dirty |= hui_text_field_apply_text(ctx, field);
    if (hui_dom_remove_class(ctx, field->container, "placeholder") == HUI_OK)
        dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
    hui_text_field_update_dom_state(ctx, field);
    return dirty;
}

static uint32_t hui_text_field_refresh_placeholder(hui_ctx *ctx, hui_text_field *field) {
    if (!field->placeholder) return 0;
    if (field->length == 0 && !field->focused)
        return hui_text_field_show_placeholder(ctx, field);
    return hui_text_field_hide_placeholder(ctx, field);
}

static uint32_t hui_text_field_set_selected_state(hui_ctx *ctx, hui_text_field *field, int selected) {
    uint32_t dirty = 0;
    if (selected) {
        size_t start = 0;
        size_t end = field->length;
        field->select_all = (end > 0);
        hui_text_field_set_selection_internal(ctx, field, start, end, 1);
        if (field->select_all &&
            hui_dom_add_class(ctx, field->container, "selected") == HUI_OK)
            dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
    } else {
        if (!field->select_all && !hui_text_field_has_selection(field)) return 0;
        field->select_all = 0;
        hui_text_field_set_selection_internal(ctx, field, field->caret, field->caret, 0);
        if (hui_dom_remove_class(ctx, field->container, "selected") == HUI_OK)
            dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
    }
    return dirty;
}

static uint32_t hui_text_field_clear_selection(hui_ctx *ctx, hui_text_field *field) {
    return hui_text_field_set_selected_state(ctx, field, 0);
}
static uint32_t hui_text_field_replace_range(hui_ctx *ctx, hui_text_field *field,
                                             size_t start, size_t end,
                                             const char *insert_data, size_t insert_len) {
    if (!ctx || !field) return 0;
    if (start > end) {
        size_t tmp = start;
        start = end;
        end = tmp;
    }
    if (end > field->length) end = field->length;
    if (start > field->length) start = field->length;

    const char *data_ptr = insert_data;
    size_t data_len = insert_len;
    char stack_buf[256];
    char *heap_buf = NULL;

    if (insert_data && insert_len > 0) {
        int needs_filter = 0;
        for (size_t i = 0; i < insert_len; i++) {
            unsigned char ch = (unsigned char) insert_data[i];
            if (!field->multiline && (ch == '\n' || ch == '\r')) {
                needs_filter = 1;
                break;
            }
            if (field->multiline && ch == '\r') {
                needs_filter = 1;
                break;
            }
        }
        if (needs_filter) {
            size_t cap = insert_len + 1;
            char *dst = (cap <= sizeof(stack_buf)) ? stack_buf : (char *) malloc(cap);
            if (!dst) return 0;
            size_t w = 0;
            for (size_t i = 0; i < insert_len; i++) {
                unsigned char ch = (unsigned char) insert_data[i];
                if (!field->multiline) {
                    if (ch == '\r' || ch == '\n') continue;
                    dst[w++] = (char) ch;
                } else {
                    if (ch == '\r') continue;
                    dst[w++] = (char) ch;
                }
            }
            data_ptr = dst;
            data_len = w;
            if (dst != stack_buf) heap_buf = dst;
        }
    }

    size_t available = field->capacity ? field->capacity - 1 : 0;
    if (data_len > available) data_len = available;
    if (start > available) start = available;
    if (end > available) end = available;

    size_t current_len = field->buffer ? strlen(field->buffer) : 0;
    if (current_len > available) current_len = available;
    if (field->length != current_len) field->length = current_len;

    size_t tail_len = (end < field->length) ? (field->length - end) : 0;
    size_t desired_len = start + data_len + tail_len;
    if (desired_len > available) {
        size_t overflow = desired_len - available;
        if (overflow >= data_len) {
            data_len = 0;
        } else {
            data_len -= overflow;
        }
        desired_len = start + data_len + tail_len;
    }

    if (data_len > 0 && data_ptr) {
        memmove(field->buffer + start + data_len, field->buffer + end, tail_len + 1);
        memcpy(field->buffer + start, data_ptr, data_len);
    } else {
        memmove(field->buffer + start, field->buffer + end, tail_len + 1);
    }

    field->length = desired_len;
    field->buffer[field->length] = '\0';
    uint32_t dirty = hui_text_field_apply_text(ctx, field);
    if (heap_buf) free(heap_buf);
    return dirty;
}

static uint32_t hui_text_field_delete_range(hui_ctx *ctx, hui_text_field *field, size_t start, size_t end) {
    return hui_text_field_replace_range(ctx, field, start, end, NULL, 0);
}

static uint32_t hui_text_field_insert_utf8(hui_ctx *ctx, hui_text_field *field, size_t offset,
                                           const char *text_utf8, size_t len) {
    if (!text_utf8 || len == 0) return 0;
    return hui_text_field_replace_range(ctx, field, offset, offset, text_utf8, len);
}

static uint32_t hui_text_field_insert_codepoint(hui_ctx *ctx, hui_text_field *field,
                                                size_t offset, uint32_t codepoint, size_t *advance) {
    if (advance) *advance = 0;
    if (!field->multiline && (codepoint == '\n' || codepoint == '\r')) return 0;
    if (field->multiline && codepoint == '\r') return 0;
    char utf8[5] = {0};
    int bytes = 0;
    if (codepoint <= 0x7F) {
        utf8[0] = (char) codepoint;
        bytes = 1;
    } else if (codepoint <= 0x7FF) {
        utf8[0] = (char) (0xC0 | ((codepoint >> 6) & 0x1F));
        utf8[1] = (char) (0x80 | (codepoint & 0x3F));
        bytes = 2;
    } else if (codepoint <= 0xFFFF) {
        utf8[0] = (char) (0xE0 | ((codepoint >> 12) & 0x0F));
        utf8[1] = (char) (0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = (char) (0x80 | (codepoint & 0x3F));
        bytes = 3;
    } else if (codepoint <= 0x10FFFF) {
        utf8[0] = (char) (0xF0 | ((codepoint >> 18) & 0x07));
        utf8[1] = (char) (0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = (char) (0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = (char) (0x80 | (codepoint & 0x3F));
        bytes = 4;
    }
    if (bytes <= 0) return 0;
    uint32_t dirty = hui_text_field_insert_utf8(ctx, field, offset, utf8, (size_t) bytes);
    if (advance) *advance = (size_t) bytes;
    return dirty;
}

static uint32_t hui_text_field_replace_with_utf8(hui_ctx *ctx, hui_text_field *field, const char *text_utf8) {
    if (!text_utf8) text_utf8 = "";
    size_t src_len = strlen(text_utf8);
    size_t copy_len = src_len;
    if (field->capacity > 0 && copy_len >= field->capacity)
        copy_len = field->capacity - 1;
    uint32_t dirty = hui_text_field_replace_range(ctx, field, 0, field->length, text_utf8, copy_len);
    hui_text_field_set_selection_internal(ctx, field, field->length, field->length, 1);
    return dirty;
}

static int hui_view_contains(const hui_u32_view *view, uint32_t value) {
    if (!view || !view->data) return 0;
    for (size_t i = 0; i < view->count; i++) {
        if (view->data[i] == value) return 1;
    }
    return 0;
}

int hui_text_field_init(hui_ctx *ctx, hui_text_field *field, const hui_text_field_desc *desc) {
    if (!ctx || !field || !desc || !desc->buffer || desc->buffer_capacity == 0) return HUI_EINVAL;
    memset(field, 0, sizeof(*field));
    hui_node_handle container = desc->container;
    if (container.index == 0 && container.gen == 0) container = HUI_NODE_NULL;
    if (hui_node_is_null(container) && desc->container_id)
        container = hui_dom_query_id(ctx, desc->container_id);
    if (hui_node_is_null(container) || !hui_node_is_element(ctx, container)) return HUI_EINVAL;

    hui_node_handle value = desc->value;
    if (value.index == 0 && value.gen == 0) value = HUI_NODE_NULL;
    if (hui_node_is_null(value) && desc->value_id)
        value = hui_dom_query_id(ctx, desc->value_id);
    if (hui_node_is_null(value) || !hui_node_is_element(ctx, value)) return HUI_EINVAL;

    hui_node_handle text = hui_node_first_child(ctx, value);
    if (hui_node_is_null(text) || !hui_node_is_text(ctx, text)) {
        text = hui_dom_create_text(ctx, "");
        if (hui_node_is_null(text)) return HUI_ENOMEM;
        if (hui_dom_append_child(ctx, value, text) != HUI_OK) return HUI_EINVAL;
    }

    field->container = container;
    field->value = value;
    field->text = text;
    field->buffer = desc->buffer;
    field->capacity = desc->buffer_capacity;
    field->length = 0;
    field->buffer[0] = '\0';
    field->flags = desc->flags;
    field->multiline = (desc->flags & HUI_TEXT_FIELD_FLAG_MULTI_LINE) != 0;
    field->placeholder = desc->placeholder;
    field->placeholder_visible = 0;
    field->focused = 0;
    field->select_all = 0;
    field->caret_visible = 0;
    field->selecting = 0;
    field->caret = 0;
    field->sel_anchor = 0;
    field->caret_timer = 0.0f;
    field->backspace_timer = 0.0f;
    field->backspace_initial_delay = desc->backspace_initial_delay > 0.0f
                                     ? desc->backspace_initial_delay
                                     : HUI_TEXT_FIELD_DEFAULT_INITIAL_DELAY;
    field->backspace_repeat_delay = desc->backspace_repeat_delay > 0.0f
                                    ? desc->backspace_repeat_delay
                                    : HUI_TEXT_FIELD_DEFAULT_REPEAT_DELAY;
    if (desc->clipboard) field->clipboard = *desc->clipboard;
    else {
        field->clipboard.get_text = NULL;
        field->clipboard.set_text = NULL;
        field->clipboard.user = NULL;
    }
    if (desc->keymap) field->keymap = *desc->keymap;
    else {
        memset(&field->keymap, 0, sizeof(field->keymap));
    }

    if (desc->initial_text) {
        size_t len = strlen(desc->initial_text);
        if (len >= field->capacity) len = field->capacity - 1;
        memcpy(field->buffer, desc->initial_text, len);
        field->buffer[len] = '\0';
        field->length = len;
        hui_text_field_apply_text(ctx, field);
    }
    hui_text_field_refresh_placeholder(ctx, field);
    field->caret = field->length;
    field->sel_anchor = field->caret;
    hui_text_field_update_dom_state(ctx, field);
    return HUI_OK;
}

static uint32_t hui_text_field_handle_focus(hui_ctx *ctx, hui_text_field *field, hui_node_handle focus_handle) {
    uint32_t dirty = 0;
    int is_focused = !hui_node_is_null(focus_handle) && hui_handles_equal(focus_handle, field->container);
    if (is_focused != field->focused) {
        field->focused = is_focused;
        hui_text_field_cancel_nav(field);
        if (field->focused) {
            if (hui_dom_add_class(ctx, field->container, "active") == HUI_OK)
                dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
            dirty |= hui_text_field_hide_placeholder(ctx, field);
            hui_text_field_set_selection_internal(ctx, field, field->length, field->length, 1);
            field->selecting = 0;
        } else {
            if (hui_dom_remove_class(ctx, field->container, "active") == HUI_OK)
                dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
            dirty |= hui_text_field_clear_selection(ctx, field);
            dirty |= hui_text_field_refresh_placeholder(ctx, field);
            field->backspace_timer = 0.0f;
            field->caret_visible = 0;
            field->selecting = 0;
            hui_text_field_set_selection_internal(ctx, field, field->length, field->length, 0);
        }
    }
    return dirty;
}

const char *hui_text_field_text(const hui_text_field *field) {
    return field ? field->buffer : NULL;
}

size_t hui_text_field_length(const hui_text_field *field) {
    return field ? field->length : 0;
}

uint32_t hui_text_field_set_text(hui_ctx *ctx, hui_text_field *field, const char *text_utf8) {
    if (!ctx || !field) return 0;
    uint32_t dirty = 0;
    dirty |= hui_text_field_clear_selection(ctx, field);
    dirty |= hui_text_field_replace_with_utf8(ctx, field, text_utf8);
    dirty |= hui_text_field_refresh_placeholder(ctx, field);
    return dirty;
}
uint32_t hui_text_field_step(hui_ctx *ctx, hui_text_field *field, float dt) {
    if (!ctx || !field) return 0;
    const hui_input_state *state = hui_input_get_state(ctx);
    if (!state) return 0;

    uint32_t dirty = 0;

    hui_node_handle focus_handle = hui_input_get_focus(ctx);

    hui_rect rect = (hui_rect){0, 0, 0, 0};
    int has_rect = (hui_node_get_layout(ctx, field->container, &rect) == HUI_OK);
    int pointer_inside_rect = 0;
    if (has_rect && state->pointer_inside) {
        float px = state->pointer_x;
        float py = state->pointer_y;
        pointer_inside_rect = (px >= rect.x && px <= rect.x + rect.w &&
                               py >= rect.y && py <= rect.y + rect.h);
    }

    int primary_pressed = (state->pointer_pressed & HUI_POINTER_BUTTON_PRIMARY) != 0;
    int primary_down = (state->pointer_buttons & HUI_POINTER_BUTTON_PRIMARY) != 0;
    int primary_released = (state->pointer_released & HUI_POINTER_BUTTON_PRIMARY) != 0;

    if (primary_pressed) {
        if (pointer_inside_rect) {
            hui_input_set_focus(ctx, field->container);
            focus_handle = field->container;
        } else if (!hui_node_is_null(focus_handle) &&
                   hui_handles_equal(focus_handle, field->container)) {
            hui_input_set_focus(ctx, HUI_NODE_NULL);
            focus_handle = HUI_NODE_NULL;
        }
    }

    dirty |= hui_text_field_handle_focus(ctx, field, focus_handle);

    if (!field->focused) {
        dirty |= hui_text_field_refresh_placeholder(ctx, field);
        hui_text_field_cancel_nav(field);
        hui_text_field_update_dom_state(ctx, field);
        return dirty;
    }

    uint32_t mods = state->key_modifiers;
    int ctrl_down = (mods & HUI_KEY_MOD_CTRL) != 0;
    int shift_down = (mods & HUI_KEY_MOD_SHIFT) != 0;

    if (primary_pressed && pointer_inside_rect) {
        float px = state->pointer_x;
        float py = state->pointer_y;
        size_t caret_offset = hui_text_field_caret_from_point(ctx, field, px, py);
        size_t anchor = shift_down ? field->sel_anchor : caret_offset;
        hui_text_field_cancel_nav(field);
        hui_text_field_set_selection_internal(ctx, field, anchor, caret_offset, 1);
        field->selecting = 1;
        dirty |= HUI_DIRTY_PAINT;
    }

    int keyboard_activity = (state->text_input.count > 0) ||
                            (state->keys_pressed.count > 0) ||
                            field->nav_active_key != 0;

    if (field->selecting && primary_down && !keyboard_activity) {
        float px = state->pointer_x;
        float py = state->pointer_y;
        size_t caret_offset = hui_text_field_caret_from_point(ctx, field, px, py);
        hui_text_field_set_selection_internal(ctx, field, field->sel_anchor, caret_offset, 0);
        hui_text_field_reset_blink(field);
        dirty |= HUI_DIRTY_PAINT;
    }

    if (field->selecting && (!primary_down || primary_released)) {
        field->selecting = 0;
    }
    if (field->keymap.select_all && ctrl_down &&
        hui_view_contains(&state->keys_pressed, field->keymap.select_all)) {
        if (field->length > 0) {
            dirty |= hui_text_field_set_selected_state(ctx, field, 1);
            hui_text_field_reset_blink(field);
            dirty |= HUI_DIRTY_PAINT;
        }
    }

    size_t sel_start = hui_text_field_selection_start(field);
    size_t sel_end = hui_text_field_selection_end(field);
    int has_selection = (sel_end > sel_start);

    if (field->keymap.copy && ctrl_down &&
        hui_view_contains(&state->keys_pressed, field->keymap.copy)) {
        if (has_selection && field->clipboard.set_text) {
            char saved = field->buffer[sel_end];
            field->buffer[sel_end] = '\0';
            field->clipboard.set_text(field->clipboard.user, field->buffer + sel_start);
            field->buffer[sel_end] = saved;
        }
    }

    if (field->keymap.cut && ctrl_down &&
        hui_view_contains(&state->keys_pressed, field->keymap.cut)) {
        if (has_selection && field->clipboard.set_text) {
            char saved = field->buffer[sel_end];
            field->buffer[sel_end] = '\0';
            field->clipboard.set_text(field->clipboard.user, field->buffer + sel_start);
            field->buffer[sel_end] = saved;
            dirty |= hui_text_field_delete_range(ctx, field, sel_start, sel_end);
            hui_text_field_set_selection_internal(ctx, field, sel_start, sel_start, 1);
            dirty |= hui_text_field_refresh_placeholder(ctx, field);
            dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
            has_selection = 0;
        }
    }

    if (field->keymap.paste && ctrl_down &&
        hui_view_contains(&state->keys_pressed, field->keymap.paste)) {
        if (field->clipboard.get_text) {
            const char *clip = field->clipboard.get_text(field->clipboard.user);
            if (clip && clip[0]) {
                dirty |= hui_text_field_hide_placeholder(ctx, field);
                sel_start = hui_text_field_selection_start(field);
                sel_end = hui_text_field_selection_end(field);
                if (sel_end > sel_start) {
                    dirty |= hui_text_field_delete_range(ctx, field, sel_start, sel_end);
                }
                size_t insert_len = strlen(clip);
                if (field->capacity > 0 && insert_len >= field->capacity)
                    insert_len = field->capacity - 1;
                dirty |= hui_text_field_insert_utf8(ctx, field, sel_start, clip, insert_len);
                size_t new_caret = sel_start + insert_len;
                hui_text_field_set_selection_internal(ctx, field, new_caret, new_caret, 1);
                dirty |= hui_text_field_refresh_placeholder(ctx, field);
                dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
                has_selection = 0;
            }
        }
    }
    if (state->text_input.count > 0) {
        hui_text_field_cancel_nav(field);
        field->selecting = 0;
        dirty |= hui_text_field_hide_placeholder(ctx, field);
        size_t actual_len = strlen(field->buffer);
        if (field->length != actual_len)
            field->length = actual_len;
        size_t sel_start_bytes = field->caret < field->sel_anchor ? field->caret : field->sel_anchor;
        size_t sel_end_bytes = field->caret < field->sel_anchor ? field->sel_anchor : field->caret;
        size_t caret = sel_start_bytes;
        if (sel_end_bytes > sel_start_bytes) {
            dirty |= hui_text_field_delete_range(ctx, field, sel_start_bytes, sel_end_bytes);
        } else {
            caret = field->caret;
        }
        for (size_t i = 0; i < state->text_input.count; i++) {
            size_t advance = 0;
            dirty |= hui_text_field_insert_codepoint(ctx, field, caret, state->text_input.data[i], &advance);
            caret += advance;
        }
        hui_text_field_set_selection_internal(ctx, field, caret, caret, 1);
        dirty |= hui_text_field_refresh_placeholder(ctx, field);
        dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
    }

    if (field->keymap.move_left &&
        hui_text_field_nav_triggered(ctx, field, state, field->keymap.move_left, dt)) {
        size_t new_caret = hui_utf8_prev(field->buffer, field->length, field->caret);
        size_t anchor = shift_down ? field->sel_anchor : new_caret;
        field->selecting = 0;
        hui_text_field_set_selection_internal(ctx, field, anchor, new_caret, 1);
        dirty |= HUI_DIRTY_PAINT;
    }

    if (field->keymap.move_right &&
        hui_text_field_nav_triggered(ctx, field, state, field->keymap.move_right, dt)) {
        size_t new_caret = hui_utf8_next(field->buffer, field->length, field->caret);
        size_t anchor = shift_down ? field->sel_anchor : new_caret;
        field->selecting = 0;
        hui_text_field_set_selection_internal(ctx, field, anchor, new_caret, 1);
        dirty |= HUI_DIRTY_PAINT;
    }

    if (field->keymap.move_home &&
        hui_text_field_nav_triggered(ctx, field, state, field->keymap.move_home, dt)) {
        size_t anchor = shift_down ? field->sel_anchor : 0;
        field->selecting = 0;
        hui_text_field_set_selection_internal(ctx, field, anchor, 0, 1);
        dirty |= HUI_DIRTY_PAINT;
    }

    if (field->keymap.move_end &&
        hui_text_field_nav_triggered(ctx, field, state, field->keymap.move_end, dt)) {
        size_t end_pos = field->length;
        size_t anchor = shift_down ? field->sel_anchor : end_pos;
        field->selecting = 0;
        hui_text_field_set_selection_internal(ctx, field, anchor, end_pos, 1);
        dirty |= HUI_DIRTY_PAINT;
    }

    if (field->keymap.delete_forward &&
        hui_view_contains(&state->keys_pressed, field->keymap.delete_forward)) {
        sel_start = hui_text_field_selection_start(field);
        sel_end = hui_text_field_selection_end(field);
        dirty |= hui_text_field_hide_placeholder(ctx, field);
        if (sel_end > sel_start) {
            dirty |= hui_text_field_delete_range(ctx, field, sel_start, sel_end);
            hui_text_field_set_selection_internal(ctx, field, sel_start, sel_start, 1);
            dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
        } else if (field->caret < field->length) {
            size_t next = hui_utf8_next(field->buffer, field->length, field->caret);
            dirty |= hui_text_field_delete_range(ctx, field, field->caret, next);
            hui_text_field_set_selection_internal(ctx, field, field->caret, field->caret, 1);
            dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
        }
        dirty |= hui_text_field_refresh_placeholder(ctx, field);
    }

    int backspace_pressed = field->keymap.backspace &&
                            hui_view_contains(&state->keys_pressed, field->keymap.backspace);
    int backspace_down = field->keymap.backspace &&
                         hui_input_key_down(ctx, field->keymap.backspace);
    int backspace_repeats = 0;

    if (backspace_pressed) {
        backspace_repeats = 1;
        field->backspace_timer = field->backspace_initial_delay;
    } else if (backspace_down) {
        field->backspace_timer -= dt;
        if (field->backspace_repeat_delay > 0.0f) {
            while (field->backspace_timer <= 0.0f) {
                backspace_repeats++;
                field->backspace_timer += field->backspace_repeat_delay;
            }
        } else if (field->backspace_timer <= 0.0f) {
            backspace_repeats = 1;
            field->backspace_timer = 0.0f;
        }
    } else {
        field->backspace_timer = 0.0f;
    }

    if (backspace_repeats > 0) {
        dirty |= hui_text_field_hide_placeholder(ctx, field);
        for (int repeat = 0; repeat < backspace_repeats; repeat++) {
            sel_start = hui_text_field_selection_start(field);
            sel_end = hui_text_field_selection_end(field);
            if (sel_end > sel_start) {
                dirty |= hui_text_field_delete_range(ctx, field, sel_start, sel_end);
                hui_text_field_set_selection_internal(ctx, field, sel_start, sel_start, 1);
                dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
            } else if (field->caret > 0) {
                size_t prev = hui_utf8_prev(field->buffer, field->length, field->caret);
                if (prev == field->caret) break;
                dirty |= hui_text_field_delete_range(ctx, field, prev, field->caret);
                hui_text_field_set_selection_internal(ctx, field, prev, prev, 1);
                dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
            } else {
                break;
            }
        }
        dirty |= hui_text_field_refresh_placeholder(ctx, field);
    }

    if (!hui_text_field_has_selection(field) && !field->selecting) {
        field->caret_timer += dt;
        if (field->caret_timer >= HUI_TEXT_FIELD_CARET_BLINK_PERIOD) {
            field->caret_timer -= HUI_TEXT_FIELD_CARET_BLINK_PERIOD;
            field->caret_visible = !field->caret_visible;
            hui_text_field_update_dom_state(ctx, field);
            dirty |= HUI_DIRTY_PAINT;
        }
    } else {
        field->caret_visible = 1;
        field->caret_timer = 0.0f;
    }

    dirty |= hui_text_field_refresh_placeholder(ctx, field);
    hui_text_field_update_dom_state(ctx, field);
    return dirty;
}
