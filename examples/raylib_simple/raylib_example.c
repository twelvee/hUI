#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "hui_err.h"
#include "raylib.h"

#include "hui/hui.h"
#include "hui/hui_draw.h"
#include "hui/hui_html_tags.h"

typedef struct {
    const hui_font_resource *resource;
    Font font;
} loaded_font_entry;

static loaded_font_entry g_loaded_fonts[16];
static size_t g_loaded_font_count = 0;

static Color hui_color_from_argb(uint32_t argb) {
    Color color = {
        .r = (unsigned char) ((argb >> 16) & 0xFFu),
        .g = (unsigned char) ((argb >> 8) & 0xFFu),
        .b = (unsigned char) (argb & 0xFFu),
        .a = (unsigned char) ((argb >> 24) & 0xFFu)
    };
    return color;
}

static Font example_get_font(hui_ctx *ctx, const hui_draw *cmd) {
    const hui_font_resource *res = hui_draw_font(ctx, cmd);
    if (!res) return GetFontDefault();
    for (size_t i = 0; i < g_loaded_font_count; i++) {
        if (g_loaded_fonts[i].resource == res) return g_loaded_fonts[i].font;
    }
    if (g_loaded_font_count >= (sizeof(g_loaded_fonts) / sizeof(g_loaded_fonts[0]))) {
        TraceLog(LOG_WARNING, "Font cache capacity reached, falling back to default font");
        return GetFontDefault();
    }
    Font font = LoadFontFromMemory(".ttf", res->data, (int) res->size, 32, NULL, 0);
    if (font.texture.id == 0) {
        TraceLog(LOG_WARNING, "Failed to load font '%s', using default", res->family ? res->family : "(unknown)");
        return GetFontDefault();
    }
    TraceLog(LOG_INFO, "Loaded font '%s' (%u glyphs texture %dx%d)", res->family ? res->family : "(unknown)", font.glyphCount, font.texture.width, font.texture.height);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    g_loaded_fonts[g_loaded_font_count].resource = res;
    g_loaded_fonts[g_loaded_font_count].font = font;
    g_loaded_font_count++;
    return font;
}

static void draw_text_utf8(hui_ctx *ctx, const hui_draw *cmd) {
    size_t text_len = 0;
    const char *text = hui_draw_text_utf8(ctx, cmd, &text_len);
    if (!text || text_len == 0) return;

    char stack_buf[256];
    char *buffer = stack_buf;
    if (text_len >= sizeof(stack_buf)) {
        buffer = MemAlloc(text_len + 1);
        if (!buffer) return;
    }
    memcpy(buffer, text, text_len);
    buffer[text_len] = '\0';

    float font_size = cmd->f[4] > 0.0f ? cmd->f[4] : 16.0f;
    float scroll_x = cmd->f[5];
    float line_height = (cmd->f[6] > 0.0f) ? cmd->f[6] : (font_size * HUI_TEXT_APPROX_LINE_HEIGHT);
    int clip_scissor = (scroll_x >= 0.0f) && (cmd->f[2] > 0.0f) && (cmd->f[3] > 0.0f);
    if (clip_scissor) {
        int sx = (int) floorf(cmd->f[0]);
        int sy = (int) floorf(cmd->f[1]);
        int sw = (int) ceilf(cmd->f[2]);
        int sh = (int) ceilf(cmd->f[3]);
        if (sw > 0 && sh > 0) BeginScissorMode(sx, sy, sw, sh);
        else clip_scissor = 0;
    }
    Font font = example_get_font(ctx, cmd);
    if (font.texture.id == 0) {
        TraceLog(LOG_WARNING, "Using default font fallback for draw cmd");
    }
    static int debug_count = 0;
    if (debug_count < 4) {
        debug_count++;
        TraceLog(LOG_INFO, "draw run fontId=%u size=%.1f texture=%d text=\"%.*s\"", cmd->u2, font_size, font.texture.id,
                 (int) ((text_len < 12) ? text_len : 12), text);
    }
    float draw_x = cmd->f[0] - (scroll_x >= 0.0f ? scroll_x : 0.0f);
    Vector2 line_pos = {draw_x, cmd->f[1]};
    size_t start = 0;
    while (start < text_len) {
        size_t end = start;
        while (end < text_len && buffer[end] != '\n') end++;
        size_t line_len = end - start;
        if (line_len > 0) {
            char saved = buffer[end];
            buffer[end] = '\0';
            DrawTextEx(font, buffer + start, line_pos, font_size, 0.0f, hui_color_from_argb(cmd->u0));
            buffer[end] = saved;
        }
        if (end >= text_len) break;
        start = end + 1;
        line_pos.x = draw_x;
        line_pos.y += line_height;
    }
    if (clip_scissor) EndScissorMode();

    if (buffer != stack_buf) MemFree(buffer);
}

static hui_filter_decision only_ui(const hui_tag_probe *probe, void *user) {
    (void) user;
    static const hui_html_tag_entry keep[] = {
        {HUI_HTML_TAG_HEADER, sizeof(HUI_HTML_TAG_HEADER) - 1},
        {HUI_HTML_TAG_MAIN, sizeof(HUI_HTML_TAG_MAIN) - 1},
        {HUI_HTML_TAG_SECTION, sizeof(HUI_HTML_TAG_SECTION) - 1},
        {HUI_HTML_TAG_FOOTER, sizeof(HUI_HTML_TAG_FOOTER) - 1},
        {HUI_HTML_TAG_FORM, sizeof(HUI_HTML_TAG_FORM) - 1},
        {HUI_HTML_TAG_LABEL, sizeof(HUI_HTML_TAG_LABEL) - 1},
        {HUI_HTML_TAG_H2, sizeof(HUI_HTML_TAG_H2) - 1},
        {HUI_HTML_TAG_H1, sizeof(HUI_HTML_TAG_H1) - 1},
        {HUI_HTML_TAG_P, sizeof(HUI_HTML_TAG_P) - 1},
        {HUI_HTML_TAG_BUTTON, sizeof(HUI_HTML_TAG_BUTTON) - 1},
        {HUI_HTML_TAG_DIV, sizeof(HUI_HTML_TAG_DIV) - 1},
        {HUI_HTML_TAG_SPAN, sizeof(HUI_HTML_TAG_SPAN) - 1},
        {HUI_HTML_TAG_INPUT, sizeof(HUI_HTML_TAG_INPUT) - 1},
        {HUI_HTML_TAG_TEXTAREA, sizeof(HUI_HTML_TAG_TEXTAREA) - 1},
        {HUI_HTML_TAG_SELECT, sizeof(HUI_HTML_TAG_SELECT) - 1},
        {HUI_HTML_TAG_OPTION, sizeof(HUI_HTML_TAG_OPTION) - 1},
    };
    for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++) {
        const hui_html_tag_entry *tag = &keep[i];
        if (probe->tag_len == tag->length && memcmp(probe->tag, tag->name, tag->length) == 0)
            return HUI_FILTER_TAKE;
    }
    return HUI_FILTER_SKIP_DESCEND;
}

static const char *raylib_clipboard_get(void *user) {
    (void) user;
    return GetClipboardText();
}

static void raylib_clipboard_set(void *user, const char *text_utf8) {
    (void) user;
    SetClipboardText(text_utf8 ? text_utf8 : "");
}

static void render_hui_draw_list(hui_ctx *ctx, hui_draw_list_view draw_view) {
    if (!ctx || !draw_view.items || draw_view.count == 0) return;
    for (size_t i = 0; i < draw_view.count; i++) {
        const hui_draw *cmd = &draw_view.items[i];
        if (cmd->op == HUI_DRAW_OP_RECT) {
            Color color = hui_color_from_argb(cmd->u0);
            if (color.a == 0) continue;
            DrawRectangleV((Vector2){cmd->f[0], cmd->f[1]}, (Vector2){cmd->f[2], cmd->f[3]}, color);
        } else if (cmd->op == HUI_DRAW_OP_GLYPH_RUN) {
            if (((cmd->u0 >> 24) & 0xFFu) == 0) continue;
            draw_text_utf8(ctx, cmd);
        }
    }
}

typedef struct {
    Vector2 pos;
    uint32_t buttons;
    int inside;
} pointer_state;

static uint32_t read_pointer_buttons(void) {
    uint32_t buttons = 0;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) buttons |= HUI_POINTER_BUTTON_PRIMARY;
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) buttons |= HUI_POINTER_BUTTON_SECONDARY;
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) buttons |= HUI_POINTER_BUTTON_MIDDLE;
    return buttons;
}

static void process_pointer_input(hui_ctx *ctx, pointer_state *state) {
    int inside = IsCursorOnScreen();
    if (inside) {
        Vector2 pos = GetMousePosition();
        uint32_t buttons = read_pointer_buttons();
        float dx = pos.x - state->pos.x;
        float dy = pos.y - state->pos.y;
        int moved = !state->inside || (fabsf(dx) + fabsf(dy)) > 0.5f;
        if (moved) {
            hui_input_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = HUI_INPUT_EVENT_POINTER_MOVE;
            ev.data.pointer_move.x = pos.x;
            ev.data.pointer_move.y = pos.y;
            hui_push_input(ctx, &ev);
        }
        if (buttons != state->buttons) {
            hui_input_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = HUI_INPUT_EVENT_POINTER_BUTTON;
            ev.data.pointer_button.x = pos.x;
            ev.data.pointer_button.y = pos.y;
            ev.data.pointer_button.buttons = buttons;
            hui_push_input(ctx, &ev);
        }
        state->pos = pos;
        state->buttons = buttons;
    } else if (state->inside) {
        hui_input_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = HUI_INPUT_EVENT_POINTER_LEAVE;
        hui_push_input(ctx, &ev);
        state->pos = (Vector2){-1.0f, -1.0f};
        state->buttons = 0u;
    }
    state->inside = inside;
}

static uint32_t read_modifiers(void) {
    uint32_t mods = 0;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) mods |= HUI_KEY_MOD_SHIFT;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) mods |= HUI_KEY_MOD_CTRL;
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) mods |= HUI_KEY_MOD_ALT;
    if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) mods |= HUI_KEY_MOD_SUPER;
    return mods;
}

static void process_key_input(hui_ctx *ctx) {
    static const int tracked_keys[] = {
        KEY_BACKSPACE,
        KEY_DELETE,
        KEY_A,
        KEY_C,
        KEY_V,
        KEY_X,
        KEY_LEFT,
        KEY_RIGHT,
        KEY_UP,
        KEY_DOWN,
        KEY_HOME,
        KEY_END,
        KEY_ENTER
    };
    for (size_t i = 0; i < sizeof(tracked_keys) / sizeof(tracked_keys[0]); i++) {
        int key = tracked_keys[i];
        if (IsKeyPressed(key)) {
            hui_input_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = HUI_INPUT_EVENT_KEY_DOWN;
            ev.data.key.keycode = (uint32_t) key;
            ev.data.key.modifiers = read_modifiers();
            hui_push_input(ctx, &ev);
            if (key == KEY_ENTER) {
                hui_input_event text_ev;
                memset(&text_ev, 0, sizeof(text_ev));
                text_ev.type = HUI_INPUT_EVENT_TEXT_INPUT;
                text_ev.data.text.codepoint = '\n';
                hui_push_input(ctx, &text_ev);
            }
        }
        if (IsKeyReleased(key)) {
            hui_input_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = HUI_INPUT_EVENT_KEY_UP;
            ev.data.key.keycode = (uint32_t) key;
            ev.data.key.modifiers = read_modifiers();
            hui_push_input(ctx, &ev);
        }
    }
}

static void process_text_input(hui_ctx *ctx) {
    int codepoint;
    while ((codepoint = GetCharPressed()) > 0) {
        hui_input_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = HUI_INPUT_EVENT_TEXT_INPUT;
        ev.data.text.codepoint = (uint32_t) codepoint;
        hui_push_input(ctx, &ev);
    }
}

static void blit_ui_to_texture(RenderTexture2D target, hui_ctx *ctx, hui_draw_list_view view) {
    if (target.id == 0) return;
    BeginTextureMode(target);
    ClearBackground((Color){0, 0, 0, 0});
    render_hui_draw_list(ctx, view);
    EndTextureMode();
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 600, "hUI + raylib");
    //SetTargetFPS(60);

    hui_ctx *ctx = hui_create(NULL, NULL);
    if (!ctx) {
        TraceLog(LOG_ERROR, "Failed to create hUI context");
        CloseWindow();
        return 1;
    }

    hui_set_dom_filter(ctx, only_ui, NULL);

    char name_value[128] = "Pat";
    char email_value[128] = "pat@example.com";
    char topic_value[32] = "General";
    char message_value[512] = "Hello there!\nI'd love to learn more about hUI.";
    hui_binding name_binding = {
        .type = HUI_BIND_STRING,
        .ptr = name_value,
        .string_capacity = sizeof(name_value)
    };
    hui_binding email_binding = {
        .type = HUI_BIND_STRING,
        .ptr = email_value,
        .string_capacity = sizeof(email_value)
    };
    hui_binding topic_binding = {
        .type = HUI_BIND_STRING,
        .ptr = topic_value,
        .string_capacity = sizeof(topic_value)
    };
    hui_binding message_binding = {
        .type = HUI_BIND_STRING,
        .ptr = message_value,
        .string_capacity = sizeof(message_value)
    };
    hui_bind_variable(ctx, "name_value", &name_binding);
    hui_bind_variable(ctx, "email_value", &email_binding);
    hui_bind_variable(ctx, "topic_value", &topic_binding);
    hui_bind_variable(ctx, "message_value", &message_binding);

    const hui_clipboard_iface clipboard = {
        .get_text = raylib_clipboard_get,
        .set_text = raylib_clipboard_set,
        .user = NULL
    };
    const hui_text_field_keymap keymap = {
        .backspace = KEY_BACKSPACE,
        .select_all = KEY_A,
        .copy = KEY_C,
        .paste = KEY_V,
        .cut = KEY_X,
        .move_left = KEY_LEFT,
        .move_right = KEY_RIGHT,
        .move_up = KEY_UP,
        .move_down = KEY_DOWN,
        .move_home = KEY_HOME,
        .move_end = KEY_END,
        .delete_forward = KEY_DELETE
    };
    hui_set_text_input_defaults(ctx, &clipboard, &keymap, 512);
    hui_set_text_input_repeat(ctx, 0.4f, 0.05f);
    hui_set_asset_base(ctx, "examples/raylib_simple");

    const char *html_path = "examples/raylib_simple/ui.html";
    char *html_text = LoadFileText(html_path);
    if (!html_text) {
        TraceLog(LOG_ERROR, "Failed to load %s", html_path);
        hui_destroy(ctx);
        CloseWindow();
        return 1;
    }
    hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html_text, strlen(html_text)}, 1);
    UnloadFileText(html_text);

    const char *css_path = "examples/raylib_simple/ui.css";
    char *css_text = LoadFileText(css_path);
    if (css_text) {
        hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css_text, strlen(css_text)}, 1);
        UnloadFileText(css_text);
    }

    if (hui_parse(ctx) != HUI_OK) {
        TraceLog(LOG_ERROR, "Parse failed: %s", hui_last_error(ctx));
        hui_destroy(ctx);
        CloseWindow();
        return 1;
    }

    pointer_state pointer = {{-1.0f, -1.0f}, 0u, 0};
    hui_build_opts opts = {(float) GetRenderWidth(), (float) GetRenderHeight(), 96.0f, 0};

    hui_render_output render_out = {0};
    if (hui_render(ctx, &opts, &render_out) != HUI_OK) {
        TraceLog(LOG_ERROR, "Initial build failed: %s", hui_last_error(ctx));
        hui_destroy(ctx);
        CloseWindow();
        return 1;
    }

    RenderTexture2D ui_layer = LoadRenderTexture((int) opts.viewport_w, (int) opts.viewport_h);
    if (ui_layer.id == 0) {
        TraceLog(LOG_WARNING, "Failed to create UI render texture, falling back to direct rendering");
    } else {
        blit_ui_to_texture(ui_layer, ctx, render_out.draw);
    }

    while (!WindowShouldClose()) {
        bool texture_reset = false;
        if (IsWindowResized()) {
            opts.viewport_w = (float) GetRenderWidth();
            opts.viewport_h = (float) GetRenderHeight();
            if (ui_layer.id != 0) UnloadRenderTexture(ui_layer);
            ui_layer = LoadRenderTexture((int) opts.viewport_w, (int) opts.viewport_h);
            if (ui_layer.id == 0) {
                TraceLog(LOG_WARNING, "Failed to recreate UI render texture after resize");
            } else {
                texture_reset = true;
            }
        }

        process_pointer_input(ctx, &pointer);
        process_key_input(ctx);
        process_text_input(ctx);

        float frame_dt = GetFrameTime();
        hui_step(ctx, frame_dt);

        int ctx_dirty = hui_has_dirty(ctx);
        if (texture_reset || ctx_dirty) {
            hui_render_output next_out = {0};
            if (hui_render(ctx, &opts, &next_out) != HUI_OK) {
                TraceLog(LOG_WARNING, "Render failed: %s", hui_last_error(ctx));
            } else {
                if (ui_layer.id != 0 && (next_out.changed || texture_reset)) {
                    blit_ui_to_texture(ui_layer, ctx, next_out.draw);
                }
                render_out = next_out;
            }
        }

        BeginDrawing();
        ClearBackground((Color){30, 30, 30, 255});

        if (ui_layer.id != 0) {
            Rectangle src = {0.0f, 0.0f, (float) ui_layer.texture.width, -(float) ui_layer.texture.height};
            Rectangle dst = {0.0f, 0.0f, opts.viewport_w, opts.viewport_h};
            DrawTexturePro(ui_layer.texture, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
        } else {
            render_hui_draw_list(ctx, render_out.draw);
        }

        DrawFPS(10, 10);
        EndDrawing();
    }

    if (ui_layer.id != 0) {
        UnloadRenderTexture(ui_layer);
    }
    for (size_t i = 0; i < g_loaded_font_count; i++) {
        if (g_loaded_fonts[i].font.texture.id != 0) {
            UnloadFont(g_loaded_fonts[i].font);
        }
    }
    hui_destroy(ctx);
    CloseWindow();
    return 0;
}
