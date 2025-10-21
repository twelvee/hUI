#include <stdint.h>
#include <string.h>

#include "hui_err.h"
#include "raylib.h"

#include "hui/hui.h"
#include "hui/hui_draw.h"
#include "hui/hui_html_tags.h"

static Color hui_color_from_argb(uint32_t argb) {
    Color color = {
        .r = (unsigned char) ((argb >> 16) & 0xFFu),
        .g = (unsigned char) ((argb >> 8) & 0xFFu),
        .b = (unsigned char) (argb & 0xFFu),
        .a = (unsigned char) ((argb >> 24) & 0xFFu)
    };
    return color;
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
    DrawTextEx(GetFontDefault(), buffer, (Vector2){cmd->f[0], cmd->f[1]}, font_size, 0.0f,
               hui_color_from_argb(cmd->u0));

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

    const char *html =
            "<!doctype html><html><body>"
            "<header class='bar'><h1 id='title'>Hello, hUI!</h1></header>"
            "<main class='content'>"
            "<section class='panel'>"
            "<h2>Stay in the loop</h2>"
            "<p class='lead'>Fill the form and press play to experience the flow.</p>"
            "<form class='form-card'>"
            "<div class='field'>"
            "<label for='name-value'>Name</label>"
            "<div id='name-input' class='input'><span id='name-value'></span></div>"
            "</div>"
            "<div class='field'>"
            "<label for='email-value'>Email</label>"
            "<div id='email-input' class='input'><span id='email-value'></span></div>"
            "</div>"
            "<div class='actions'>"
            "<button id='play'>Play</button>"
            "<button id='quit'>Quit</button>"
            "</div>"
            "</form>"
            "</section>"
            "</main>"
            "<footer><span class='muted'>Powered by hUI + raylib</span></footer>"
            "</body></html>";

    const char *css =
            "body { background-color: #1e1e1e; color: #f0f0f0; font-size: 20px; }"
            "header.bar { background-color: #007acc; color: #ffffff; padding: 24px; }"
            "main.content { padding: 32px; }"
            "section.panel { background-color: #252526; padding: 24px; border-radius: 12px; }"
            "section.panel h2 { font-size: 28px; margin-bottom: 12px; }"
            "p.lead { margin: 0 0 20px 0; color: #cccccc; font-size: 18px; }"
            "form.form-card { margin-top: 12px; }"
            "div.field { margin-bottom: 18px; }"
            "div.field label { display: block; font-size: 16px; color: #bbbbbb; margin-bottom: 6px; }"
            "div.input { background-color: #2d2d30; padding: 12px 16px; border-radius: 8px; border: 2px solid #2d2d30; }"
            "div.input span { display: block; font-size: 20px; color: #f0f0f0; }"
            "div.input.placeholder span { color: #666666; }"
            "div.input.active { border-color: #3b8ad9; background-color: #313135; }"
            "div.input.selected span { background-color: #3b8ad9; color: #101418; padding: 0 4px; border-radius: 4px; }"
            "div.actions { margin-top: 8px; }"
            "div.actions button { background-color: #2d2d30; color: #ffffff; padding: 12px 24px; margin-right: 12px; font-size: 24px; border-radius: 8px; }"
            "div.actions button:hover { background-color: #3b8ad9; color: #ffffff; }"
            "footer { background-color: #2d2d30; color: #bbbbbb; padding: 16px; margin-top: 24px; }";

    if (hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) != HUI_OK) {
        TraceLog(LOG_ERROR, "Failed to feed HTML: %s", hui_last_error(ctx));
        hui_destroy(ctx);
        CloseWindow();
        return 1;
    }
    if (hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1) != HUI_OK) {
        TraceLog(LOG_ERROR, "Failed to feed CSS: %s", hui_last_error(ctx));
        hui_destroy(ctx);
        CloseWindow();
        return 1;
    }
    if (hui_parse(ctx) != HUI_OK) {
        TraceLog(LOG_ERROR, "Parse failed: %s", hui_last_error(ctx));
        hui_destroy(ctx);
        CloseWindow();
        return 1;
    }

    char name_buffer[128] = {0};
    char email_buffer[128] = {0};
    hui_text_field fields[2];
    size_t field_count = 0;

    const hui_clipboard_iface clipboard = {
        .get_text = raylib_clipboard_get,
        .set_text = raylib_clipboard_set,
        .user = NULL
    };
    const hui_text_field_keymap keymap = {
        .backspace = KEY_BACKSPACE,
        .select_all = KEY_A,
        .copy = KEY_C,
        .paste = KEY_V
    };

    hui_text_field_desc name_desc = {
        .container_id = "name-input",
        .value_id = "name-value",
        .placeholder = "Enter your name",
        .buffer = name_buffer,
        .buffer_capacity = sizeof(name_buffer),
        .clipboard = &clipboard,
        .keymap = &keymap
    };
    if (field_count < sizeof(fields) / sizeof(fields[0]) &&
        hui_text_field_init(ctx, &fields[field_count], &name_desc) == HUI_OK) {
        field_count++;
    } else {
        TraceLog(LOG_WARNING, "Failed to initialise name input field");
    }

    hui_text_field_desc email_desc = {
        .container_id = "email-input",
        .value_id = "email-value",
        .placeholder = "email@example.com",
        .buffer = email_buffer,
        .buffer_capacity = sizeof(email_buffer),
        .clipboard = &clipboard,
        .keymap = &keymap
    };
    if (field_count < sizeof(fields) / sizeof(fields[0]) &&
        hui_text_field_init(ctx, &fields[field_count], &email_desc) == HUI_OK) {
        field_count++;
    } else {
        TraceLog(LOG_WARNING, "Failed to initialise email input field");
    }

    hui_build_opts opts = {(float) GetRenderWidth(), (float) GetRenderHeight(), 96.0f, 0};
    if (hui_build_ir(ctx, &opts) != HUI_OK) {
        TraceLog(LOG_ERROR, "Initial build failed: %s", hui_last_error(ctx));
        hui_destroy(ctx);
        CloseWindow();
        return 1;
    }

    RenderTexture2D ui_layer = LoadRenderTexture((int) opts.viewport_w, (int) opts.viewport_h);
    if (ui_layer.id == 0) {
        TraceLog(LOG_WARNING, "Failed to create UI render texture, falling back to direct rendering");
    }
    int ui_layer_dirty = 1;

    Vector2 prev_mouse = {-1.0f, -1.0f};
    uint32_t prev_buttons = 0u;
    int cursor_was_on_screen = 0;

    while (!WindowShouldClose()) {
        uint32_t dirty = 0;
        if (IsWindowResized()) {
            opts.viewport_w = (float) GetRenderWidth();
            opts.viewport_h = (float) GetRenderHeight();
            dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
            if (ui_layer.id != 0) UnloadRenderTexture(ui_layer);
            ui_layer = LoadRenderTexture((int) opts.viewport_w, (int) opts.viewport_h);
            if (ui_layer.id == 0) {
                TraceLog(LOG_WARNING, "Failed to recreate UI render texture after resize");
            }
            ui_layer_dirty = 1;
        }

        float frame_dt = GetFrameTime();
        int cursor_on_screen = IsCursorOnScreen();
        if (cursor_on_screen) {
            Vector2 mouse = GetMousePosition();
            uint32_t buttons = 0;
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) buttons |= HUI_POINTER_BUTTON_PRIMARY;
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) buttons |= HUI_POINTER_BUTTON_SECONDARY;
            if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) buttons |= HUI_POINTER_BUTTON_MIDDLE;
            if (!cursor_was_on_screen || mouse.x != prev_mouse.x || mouse.y != prev_mouse.y) {
                hui_input_event move_event;
                memset(&move_event, 0, sizeof(move_event));
                move_event.type = HUI_INPUT_EVENT_POINTER_MOVE;
                move_event.data.pointer_move.x = mouse.x;
                move_event.data.pointer_move.y = mouse.y;
                hui_push_input(ctx, &move_event);
                prev_mouse = mouse;
            }
            if (buttons != prev_buttons) {
                hui_input_event button_event;
                memset(&button_event, 0, sizeof(button_event));
                button_event.type = HUI_INPUT_EVENT_POINTER_BUTTON;
                button_event.data.pointer_button.x = mouse.x;
                button_event.data.pointer_button.y = mouse.y;
                button_event.data.pointer_button.buttons = buttons;
                hui_push_input(ctx, &button_event);
                prev_buttons = buttons;
            }
            prev_mouse = mouse;
        } else {
            if (cursor_was_on_screen) {
                hui_input_event leave_event;
                memset(&leave_event, 0, sizeof(leave_event));
                leave_event.type = HUI_INPUT_EVENT_POINTER_LEAVE;
                hui_push_input(ctx, &leave_event);
            }
            prev_mouse = (Vector2){-1.0f, -1.0f};
            prev_buttons = 0u;
        }
        cursor_was_on_screen = cursor_on_screen;

        const int tracked_keys[] = {KEY_BACKSPACE, KEY_A, KEY_C, KEY_V};
        uint32_t modifiers = 0;
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) modifiers |= HUI_KEY_MOD_SHIFT;
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) modifiers |= HUI_KEY_MOD_CTRL;
        if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) modifiers |= HUI_KEY_MOD_ALT;
        if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) modifiers |= HUI_KEY_MOD_SUPER;
        for (size_t i = 0; i < sizeof(tracked_keys) / sizeof(tracked_keys[0]); i++) {
            int key = tracked_keys[i];
            if (IsKeyPressed(key)) {
                hui_input_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = HUI_INPUT_EVENT_KEY_DOWN;
                ev.data.key.keycode = (uint32_t) key;
                ev.data.key.modifiers = modifiers;
                hui_push_input(ctx, &ev);
            }
            if (IsKeyReleased(key)) {
                hui_input_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = HUI_INPUT_EVENT_KEY_UP;
                ev.data.key.keycode = (uint32_t) key;
                uint32_t mods_now = 0;
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) mods_now |= HUI_KEY_MOD_SHIFT;
                if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) mods_now |= HUI_KEY_MOD_CTRL;
                if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) mods_now |= HUI_KEY_MOD_ALT;
                if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) mods_now |= HUI_KEY_MOD_SUPER;
                ev.data.key.modifiers = mods_now;
                hui_push_input(ctx, &ev);
            }
        }

        int codepoint;
        while ((codepoint = GetCharPressed()) > 0) {
            hui_input_event text_event;
            memset(&text_event, 0, sizeof(text_event));
            text_event.type = HUI_INPUT_EVENT_TEXT_INPUT;
            text_event.data.text.codepoint = (uint32_t) codepoint;
            hui_push_input(ctx, &text_event);
        }

        dirty |= hui_process_input(ctx);
        for (size_t i = 0; i < field_count; i++) {
            dirty |= hui_text_field_step(ctx, &fields[i], frame_dt);
        }
        if (dirty) {
            if (hui_build_ir(ctx, &opts) != HUI_OK) {
                TraceLog(LOG_WARNING, "Rebuild failed: %s", hui_last_error(ctx));
            } else {
                ui_layer_dirty = 1;
            }
        }

        hui_draw_list_view draw_view = hui_get_draw_list(ctx);

        if (ui_layer.id != 0 && ui_layer_dirty) {
            BeginTextureMode(ui_layer);
            ClearBackground((Color){0, 0, 0, 0});
            render_hui_draw_list(ctx, draw_view);
            EndTextureMode();
            ui_layer_dirty = 0;
        }

        BeginDrawing();
        ClearBackground((Color){30, 30, 30, 255});

        if (ui_layer.id != 0) {
            Rectangle src = {0.0f, 0.0f, (float) ui_layer.texture.width, -(float) ui_layer.texture.height};
            Rectangle dst = {0.0f, 0.0f, opts.viewport_w, opts.viewport_h};
            DrawTexturePro(ui_layer.texture, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
        } else {
            render_hui_draw_list(ctx, draw_view);
        }

        DrawFPS(10, 10);
        EndDrawing();
    }

    if (ui_layer.id != 0) {
        UnloadRenderTexture(ui_layer);
    }
    hui_destroy(ctx);
    CloseWindow();
    return 0;
}
