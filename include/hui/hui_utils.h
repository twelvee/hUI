#ifndef HUI_HUI_UTILS_H
#define HUI_HUI_UTILS_H

#include <stdint.h>

#include "hui.h"

#ifdef __cplusplus
extern "C" {
#endif

int hui_feed_html_file(hui_ctx *ctx, const char *path_utf8);

int hui_feed_css_file(hui_ctx *ctx, const char *path_utf8);

void hui_input_pointer_move(hui_ctx *ctx, float x, float y);

void hui_input_pointer_button(hui_ctx *ctx, float x, float y, uint32_t buttons);

void hui_input_pointer_leave(hui_ctx *ctx);

void hui_input_key_down_with_mods(hui_ctx *ctx, uint32_t keycode, uint32_t modifiers);
void hui_input_key_up_with_mods(hui_ctx *ctx, uint32_t keycode, uint32_t modifiers);
void hui_input_text_utf32(hui_ctx *ctx, uint32_t codepoint);

#ifdef __cplusplus
}
#endif

#endif
