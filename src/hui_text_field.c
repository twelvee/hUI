#include "../include/hui/hui.h"

#include "hui_err.h"

#include <string.h>

#define HUI_TEXT_FIELD_DEFAULT_INITIAL_DELAY 0.35f
#define HUI_TEXT_FIELD_DEFAULT_REPEAT_DELAY 0.05f

static int hui_handles_equal(hui_node_handle a, hui_node_handle b) {
    return a.index == b.index && a.gen == b.gen;
}

static uint32_t hui_text_field_apply_text(hui_ctx *ctx, hui_text_field *field) {
    if (hui_dom_set_text(ctx, field->text, field->buffer) != HUI_OK) return 0;
    return HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
}

static uint32_t hui_text_field_show_placeholder(hui_ctx *ctx, hui_text_field *field) {
    if (!field->placeholder || field->placeholder_visible) return 0;
    field->placeholder_visible = 1;
    uint32_t dirty = 0;
    if (hui_dom_set_text(ctx, field->text, field->placeholder) == HUI_OK)
        dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
    if (hui_dom_add_class(ctx, field->container, "placeholder") == HUI_OK)
        dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
    return dirty;
}

static uint32_t hui_text_field_hide_placeholder(hui_ctx *ctx, hui_text_field *field) {
    if (!field->placeholder_visible) return 0;
    field->placeholder_visible = 0;
    uint32_t dirty = 0;
    dirty |= hui_text_field_apply_text(ctx, field);
    if (hui_dom_remove_class(ctx, field->container, "placeholder") == HUI_OK)
        dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
    return dirty;
}

static uint32_t hui_text_field_refresh_placeholder(hui_ctx *ctx, hui_text_field *field) {
    if (!field->placeholder) return 0;
    if (field->length == 0 && !field->focused)
        return hui_text_field_show_placeholder(ctx, field);
    return hui_text_field_hide_placeholder(ctx, field);
}

static uint32_t hui_text_field_set_selected_state(hui_ctx *ctx, hui_text_field *field, int selected) {
    if (selected) {
        if (field->select_all) return 0;
        field->select_all = 1;
        if (hui_dom_add_class(ctx, field->container, "selected") == HUI_OK)
            return HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
        return 0;
    }
    if (!field->select_all) return 0;
    field->select_all = 0;
    if (hui_dom_remove_class(ctx, field->container, "selected") == HUI_OK)
        return HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
    return 0;
}

static uint32_t hui_text_field_clear_selection(hui_ctx *ctx, hui_text_field *field) {
    return hui_text_field_set_selected_state(ctx, field, 0);
}

static int hui_text_field_trim_last_utf8(hui_text_field *field) {
    if (!field || field->length == 0) return 0;
    size_t new_len = field->length;
    do {
        if (new_len == 0) break;
        new_len--;
    } while (new_len > 0 && ((unsigned char) field->buffer[new_len] & 0xC0u) == 0x80u);
    if (new_len == field->length) return 0;
    field->buffer[new_len] = '\0';
    field->length = new_len;
    return 1;
}

static int hui_text_field_encode_codepoint(uint32_t codepoint, char out[5]) {
    if (!out) return 0;
    if (codepoint <= 0x7F) {
        out[0] = (char) codepoint;
        out[1] = '\0';
        return 1;
    } else if (codepoint <= 0x7FF) {
        out[0] = (char) (0xC0 | ((codepoint >> 6) & 0x1F));
        out[1] = (char) (0x80 | (codepoint & 0x3F));
        out[2] = '\0';
        return 2;
    } else if (codepoint <= 0xFFFF) {
        out[0] = (char) (0xE0 | ((codepoint >> 12) & 0x0F));
        out[1] = (char) (0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char) (0x80 | (codepoint & 0x3F));
        out[3] = '\0';
        return 3;
    } else if (codepoint <= 0x10FFFF) {
        out[0] = (char) (0xF0 | ((codepoint >> 18) & 0x07));
        out[1] = (char) (0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char) (0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char) (0x80 | (codepoint & 0x3F));
        out[4] = '\0';
        return 4;
    }
    return 0;
}

static uint32_t hui_text_field_append_codepoint(hui_ctx *ctx, hui_text_field *field, uint32_t codepoint) {
    char utf8[5] = {0};
    int bytes = hui_text_field_encode_codepoint(codepoint, utf8);
    if (bytes <= 0) return 0;
    if (field->length + (size_t) bytes >= field->capacity) return 0;
    memcpy(field->buffer + field->length, utf8, (size_t) bytes);
    field->length += (size_t) bytes;
    field->buffer[field->length] = '\0';
    return hui_text_field_apply_text(ctx, field);
}

static int hui_view_contains(const hui_u32_view *view, uint32_t value) {
    if (!view || !view->data) return 0;
    for (size_t i = 0; i < view->count; i++) {
        if (view->data[i] == value) return 1;
    }
    return 0;
}

static uint32_t hui_text_field_replace_with_utf8(hui_ctx *ctx, hui_text_field *field, const char *text_utf8) {
    if (!text_utf8) text_utf8 = "";
    size_t src_len = strlen(text_utf8);
    if (src_len >= field->capacity) src_len = field->capacity - 1;
    memcpy(field->buffer, text_utf8, src_len);
    field->buffer[src_len] = '\0';
    field->length = src_len;
    return hui_text_field_apply_text(ctx, field);
}

static uint32_t hui_text_field_append_utf8(hui_ctx *ctx, hui_text_field *field, const char *text_utf8) {
    if (!text_utf8) return 0;
    uint32_t dirty = 0;
    size_t idx = 0;
    while (text_utf8[idx] != '\0') {
        unsigned char c = (unsigned char) text_utf8[idx];
        size_t cp_len = 1;
        if ((c & 0x80u) == 0x00u) cp_len = 1;
        else if ((c & 0xE0u) == 0xC0u) cp_len = 2;
        else if ((c & 0xF0u) == 0xE0u) cp_len = 3;
        else if ((c & 0xF8u) == 0xF0u) cp_len = 4;
        if (field->length + cp_len >= field->capacity) break;
        for (size_t k = 0; k < cp_len && text_utf8[idx + k] != '\0'; k++) {
            field->buffer[field->length++] = text_utf8[idx + k];
        }
        field->buffer[field->length] = '\0';
        idx += cp_len;
    }
    if (idx > 0) dirty |= hui_text_field_apply_text(ctx, field);
    return dirty;
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
    field->placeholder = desc->placeholder;
    field->placeholder_visible = 0;
    field->focused = 0;
    field->select_all = 0;
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
        field->keymap.backspace = 0;
        field->keymap.select_all = 0;
        field->keymap.copy = 0;
        field->keymap.paste = 0;
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
    return HUI_OK;
}

static uint32_t hui_text_field_handle_focus(hui_ctx *ctx, hui_text_field *field, hui_node_handle focus_handle) {
    uint32_t dirty = 0;
    int is_focused = !hui_node_is_null(focus_handle) && hui_handles_equal(focus_handle, field->container);
    if (is_focused != field->focused) {
        field->focused = is_focused;
        if (field->focused) {
            if (hui_dom_add_class(ctx, field->container, "active") == HUI_OK)
                dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
            dirty |= hui_text_field_hide_placeholder(ctx, field);
        } else {
            if (hui_dom_remove_class(ctx, field->container, "active") == HUI_OK)
                dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_PAINT;
            dirty |= hui_text_field_clear_selection(ctx, field);
            dirty |= hui_text_field_refresh_placeholder(ctx, field);
            field->backspace_timer = 0.0f;
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
    if (!field->placeholder_visible)
        dirty |= hui_text_field_apply_text(ctx, field);
    return dirty;
}

uint32_t hui_text_field_step(hui_ctx *ctx, hui_text_field *field, float dt) {
    if (!ctx || !field) return 0;
    const hui_input_state *state = hui_input_get_state(ctx);
    if (!state) return 0;

    uint32_t dirty = 0;

    hui_node_handle focus_handle = hui_input_get_focus(ctx);

    hui_rect rect = {0, 0, 0, 0};
    int has_rect = (hui_node_get_layout(ctx, field->container, &rect) == HUI_OK);
    int pointer_inside_rect = 0;
    if (has_rect && state->pointer_inside) {
        float px = state->pointer_x;
        float py = state->pointer_y;
        pointer_inside_rect = (px >= rect.x && px <= rect.x + rect.w && py >= rect.y && py <= rect.y + rect.h);
    }

    if ((state->pointer_pressed & HUI_POINTER_BUTTON_PRIMARY) != 0) {
        if (pointer_inside_rect) {
            hui_input_set_focus(ctx, field->container);
            focus_handle = field->container;
            dirty |= hui_text_field_clear_selection(ctx, field);
        } else if (!hui_node_is_null(focus_handle) && hui_handles_equal(focus_handle, field->container)) {
            hui_input_set_focus(ctx, HUI_NODE_NULL);
            focus_handle = HUI_NODE_NULL;
        }
    }

    dirty |= hui_text_field_handle_focus(ctx, field, focus_handle);

    if (!field->focused) return dirty;

    uint32_t ctrl_down = state->key_modifiers & HUI_KEY_MOD_CTRL;

    if (field->keymap.select_all && ctrl_down && hui_view_contains(&state->keys_pressed, field->keymap.select_all)) {
        if (field->length > 0)
            dirty |= hui_text_field_set_selected_state(ctx, field, 1);
    }

    if (field->keymap.copy && ctrl_down && hui_view_contains(&state->keys_pressed, field->keymap.copy)) {
        if (field->select_all && field->clipboard.set_text && field->length > 0)
            field->clipboard.set_text(field->clipboard.user, field->buffer);
    }

    if (field->keymap.paste && ctrl_down && hui_view_contains(&state->keys_pressed, field->keymap.paste)) {
        if (field->clipboard.get_text) {
            const char *clip = field->clipboard.get_text(field->clipboard.user);
            if (clip && clip[0]) {
                int had_selection = field->select_all && field->length > 0;
                if (had_selection) {
                    field->length = 0;
                    field->buffer[0] = '\0';
                }
                dirty |= hui_text_field_clear_selection(ctx, field);
                dirty |= hui_text_field_hide_placeholder(ctx, field);
                if (had_selection || field->length == 0)
                    dirty |= hui_text_field_replace_with_utf8(ctx, field, clip);
                else
                    dirty |= hui_text_field_append_utf8(ctx, field, clip);
                dirty |= hui_text_field_refresh_placeholder(ctx, field);
            }
        }
    }

    if (state->text_input.count > 0) {
        dirty |= hui_text_field_hide_placeholder(ctx, field);
        if (field->select_all && field->length > 0) {
            field->length = 0;
            field->buffer[0] = '\0';
            dirty |= hui_text_field_clear_selection(ctx, field);
        }
        for (size_t i = 0; i < state->text_input.count; i++) {
            dirty |= hui_text_field_append_codepoint(ctx, field, state->text_input.data[i]);
        }
        dirty |= hui_text_field_refresh_placeholder(ctx, field);
    }

    int backspace_pressed = field->keymap.backspace &&
                            hui_view_contains(&state->keys_pressed, field->keymap.backspace);
    int backspace_down = field->keymap.backspace && hui_input_key_down(ctx, field->keymap.backspace);
    if (backspace_pressed) {
        dirty |= hui_text_field_hide_placeholder(ctx, field);
        if (field->select_all && field->length > 0) {
            field->length = 0;
            field->buffer[0] = '\0';
            dirty |= hui_text_field_clear_selection(ctx, field);
            dirty |= hui_text_field_apply_text(ctx, field);
        } else if (hui_text_field_trim_last_utf8(field)) {
            dirty |= hui_text_field_apply_text(ctx, field);
        }
        dirty |= hui_text_field_refresh_placeholder(ctx, field);
        field->backspace_timer = field->backspace_initial_delay;
    } else if (backspace_down) {
        if (field->backspace_timer > 0.0f) {
            field->backspace_timer -= dt;
        }
        if (field->backspace_timer <= 0.0f) {
            dirty |= hui_text_field_hide_placeholder(ctx, field);
            if (field->select_all && field->length > 0) {
                field->length = 0;
                field->buffer[0] = '\0';
                dirty |= hui_text_field_clear_selection(ctx, field);
                dirty |= hui_text_field_apply_text(ctx, field);
            } else if (hui_text_field_trim_last_utf8(field)) {
                dirty |= hui_text_field_apply_text(ctx, field);
            }
            dirty |= hui_text_field_refresh_placeholder(ctx, field);
            field->backspace_timer = field->backspace_repeat_delay;
        }
    } else {
        field->backspace_timer = 0.0f;
    }

    return dirty;
}
