#include "hui/hui_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hui_err.h"

static int hui_util_read_file(const char *path, uint8_t **out_data, size_t *out_len) {
    if (out_data) *out_data = NULL;
    if (out_len) *out_len = 0;
    if (!path || !out_data || !out_len) return HUI_EINVAL;
    FILE *file = fopen(path, "rb");
    if (!file) return HUI_EINVAL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return HUI_EINVAL;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return HUI_EINVAL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return HUI_EINVAL;
    }
    size_t length = (size_t) size;
    uint8_t *buffer = NULL;
    if (length > 0) {
        buffer = (uint8_t *) malloc(length);
        if (!buffer) {
            fclose(file);
            return HUI_ENOMEM;
        }
        size_t read_total = fread(buffer, 1, length, file);
        fclose(file);
        if (read_total != length) {
            free(buffer);
            return HUI_EINVAL;
        }
    } else {
        buffer = NULL;
        fclose(file);
    }
    *out_data = buffer;
    *out_len = length;
    return HUI_OK;
}

static int hui_feed_file_data(hui_ctx *ctx, const char *path, int is_css) {
    if (!ctx || !path) return HUI_EINVAL;
    uint8_t *buffer = NULL;
    size_t length = 0;
    int rc = hui_util_read_file(path, &buffer, &length);
    if (rc != HUI_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "failed to read file '%s'", path);
        hui_set_error_message(ctx, msg);
        return rc;
    }
    hui_bytes chunk = {buffer, length};
    if (is_css) rc = hui_feed_css(ctx, chunk, 1);
    else rc = hui_feed_html(ctx, chunk, 1);
    free(buffer);
    if (rc != HUI_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "failed to feed %s '%s'",
                 is_css ? "CSS" : "HTML", path);
        hui_set_error_message(ctx, msg);
    }
    return rc;
}

int hui_feed_html_file(hui_ctx *ctx, const char *path_utf8) {
    return hui_feed_file_data(ctx, path_utf8, 0);
}

int hui_feed_css_file(hui_ctx *ctx, const char *path_utf8) {
    return hui_feed_file_data(ctx, path_utf8, 1);
}

void hui_input_pointer_move(hui_ctx *ctx, float x, float y) {
    if (!ctx) return;
    hui_input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = HUI_INPUT_EVENT_POINTER_MOVE;
    ev.data.pointer_move.x = x;
    ev.data.pointer_move.y = y;
    hui_push_input(ctx, &ev);
}

void hui_input_pointer_button(hui_ctx *ctx, float x, float y, uint32_t buttons) {
    if (!ctx) return;
    hui_input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = HUI_INPUT_EVENT_POINTER_BUTTON;
    ev.data.pointer_button.x = x;
    ev.data.pointer_button.y = y;
    ev.data.pointer_button.buttons = buttons;
    hui_push_input(ctx, &ev);
}

void hui_input_pointer_leave(hui_ctx *ctx) {
    if (!ctx) return;
    hui_input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = HUI_INPUT_EVENT_POINTER_LEAVE;
    hui_push_input(ctx, &ev);
}

void hui_input_key_down_with_mods(hui_ctx *ctx, uint32_t keycode, uint32_t modifiers) {
    if (!ctx) return;
    hui_input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = HUI_INPUT_EVENT_KEY_DOWN;
    ev.data.key.keycode = keycode;
    ev.data.key.modifiers = modifiers;
    hui_push_input(ctx, &ev);
}

void hui_input_key_up_with_mods(hui_ctx *ctx, uint32_t keycode, uint32_t modifiers) {
    if (!ctx) return;
    hui_input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = HUI_INPUT_EVENT_KEY_UP;
    ev.data.key.keycode = keycode;
    ev.data.key.modifiers = modifiers;
    hui_push_input(ctx, &ev);
}

void hui_input_text_utf32(hui_ctx *ctx, uint32_t codepoint) {
    if (!ctx) return;
    hui_input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = HUI_INPUT_EVENT_TEXT_INPUT;
    ev.data.text.codepoint = codepoint;
    hui_push_input(ctx, &ev);
}
