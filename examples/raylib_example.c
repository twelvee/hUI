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
            {HUI_HTML_TAG_FOOTER, sizeof(HUI_HTML_TAG_FOOTER) - 1},
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
            "<main><p class='lead'>Raylib renderer</p><div class='cta'>"
            "<button id='play'>Play</button><button id='quit'>Quit</button>"
            "</div></main>"
            "<footer><span class='muted'>Powered by hUI + raylib</span></footer>"
            "</body></html>";

    const char *css =
            "body { background-color: #1e1e1e; color: #f0f0f0; font-size: 20px; }"
            "header.bar { background-color: #007acc; color: #ffffff; padding: 24px; }"
            "main { padding: 32px; }"
            "p.lead { margin-bottom: 16px; }"
            "div.cta { display: block; margin-top: 16px; }"
            "button { background-color: #2d2d30; color: #ffffff; padding: 12px 24px; margin-right: 12px; font-size: 28px; }"
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

    hui_build_opts opts = {800.0f, 600.0f, 96.0f, 0};
    if (hui_build_ir(ctx, &opts) != HUI_OK) {
        TraceLog(LOG_ERROR, "Initial build failed: %s", hui_last_error(ctx));
        hui_destroy(ctx);
        CloseWindow();
        return 1;
    }

    while (!WindowShouldClose()) {
        if (IsWindowResized()) {
            opts.viewport_w = (float) GetScreenWidth();
            opts.viewport_h = (float) GetScreenHeight();
            if (hui_build_ir(ctx, &opts) != HUI_OK) {
                TraceLog(LOG_WARNING, "Rebuild failed: %s", hui_last_error(ctx));
            }
        }

        const hui_draw_list_view draw_view = hui_get_draw_list(ctx);

        BeginDrawing();
        ClearBackground((Color){30, 30, 30, 255});

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

        DrawFPS(10, 10);
        EndDrawing();
    }

    hui_destroy(ctx);
    CloseWindow();
    return 0;
}
