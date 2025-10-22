#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <hui/hui.h>

#include "hui_err.h"
#include "hui_intern.h"
#include "html/hui_html_builder.h"
#include "css/hui_css_parser.h"
#include "style/hui_style.h"
#include "layout/hui_layout.h"
#include "paint/hui_paint.h"
#include "ir/hui_ir.h"

#define ASSERT(cond) do { if (!(cond)) { fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); current_failed = 1; } } while (0)

enum {
    TEST_KEY_LEFT = 1000,
    TEST_KEY_RIGHT = 1001,
    TEST_KEY_HOME = 1002,
    TEST_KEY_END = 1003,
    TEST_KEY_UP = 1004,
    TEST_KEY_DOWN = 1005,
    TEST_KEY_DELETE = 127,
    TEST_KEY_CUT = 'X'
};

typedef struct {
    char buffer[128];
} test_clipboard;

static const char *test_clipboard_get(void *user) {
    test_clipboard *clip = (test_clipboard *) user;
    return clip->buffer;
}

static void test_clipboard_set(void *user, const char *text) {
    test_clipboard *clip = (test_clipboard *) user;
    if (!text) text = "";
    size_t len = strlen(text);
    if (len >= sizeof(clip->buffer)) len = sizeof(clip->buffer) - 1;
    memcpy(clip->buffer, text, len);
    clip->buffer[len] = '\0';
}

typedef void (*test_fn)(void);

typedef struct {
    const char *name;
    test_fn fn;
} test_case;

static int current_failed = 0;

static void test_intern(void) {
    hui_intern intern;
    hui_intern_init(&intern);
    hui_atom a = hui_intern_put(&intern, "foo", 3);
    hui_atom b = hui_intern_put(&intern, "foo", 3);
    hui_atom c = hui_intern_put(&intern, "bar", 3);
    ASSERT(a == b);
    ASSERT(a != c);
    uint32_t len = 0;
    const char *s = hui_intern_str(&intern, a, &len);
    ASSERT(len == 3 && memcmp(s, "foo", 3) == 0);
    hui_intern_reset(&intern);
}

static void test_html_builder(void) {
    const char *html = "<div id='root' class='a b'><span>t</span></div>";
    hui_intern atoms;
    hui_dom dom;
    hui_builder builder;
    hui_intern_init(&atoms);
    hui_dom_init(&dom);
    memset(&builder, 0, sizeof(builder));
    builder.dom = &dom;
    builder.atoms = &atoms;
    ASSERT(hui_build_from_html(&builder, html, strlen(html)) == HUI_OK);
    ASSERT(dom.root != 0xFFFFFFFFu);
    hui_dom_node *root = &dom.nodes.data[dom.root];
    uint32_t len = 0;
    const char *tag = hui_intern_str(&atoms, root->tag, &len);
    ASSERT(len == 3 && memcmp(tag, "div", 3) == 0);
    ASSERT(root->first_child != 0xFFFFFFFFu);
    hui_dom_node *span = &dom.nodes.data[root->first_child];
    tag = hui_intern_str(&atoms, span->tag, &len);
    ASSERT(len == 4 && memcmp(tag, "span", 4) == 0);
    hui_dom_reset(&dom);
    hui_intern_reset(&atoms);
}

static void test_css_parser(void) {
    const char *css = ".foo { color: #010203; padding: 4px; }\nspan { font-size: 12px; }";
    hui_stylesheet sheet;
    hui_intern atoms;
    hui_css_init(&sheet);
    hui_intern_init(&atoms);
    ASSERT(hui_css_parse(&sheet, &atoms, css, strlen(css)) == 0);
    ASSERT(sheet.rules.len == 2);
    hui_css_reset(&sheet);
    hui_intern_reset(&atoms);
}

static void build_dom_and_style(hui_dom *dom, hui_intern *atoms, hui_stylesheet *sheet, const char *html,
                                const char *css) {
    hui_builder builder;
    hui_dom_init(dom);
    hui_intern_init(atoms);
    memset(&builder, 0, sizeof(builder));
    builder.dom = dom;
    builder.atoms = atoms;
    ASSERT(hui_build_from_html(&builder, html, strlen(html)) == HUI_OK);
    hui_css_init(sheet);
    ASSERT(hui_css_parse(sheet, atoms, css, strlen(css)) == 0);
}

static void test_style_layout_paint(void) {
    const char *html = "<header class='bar'><h1>Hello</h1></header>";
    const char *css = ".bar { background-color: #202020; color: #ffffff; padding: 8px; } h1 { font-size: 18px; }";
    hui_dom dom;
    hui_intern atoms;
    hui_stylesheet sheet;
    build_dom_and_style(&dom, &atoms, &sheet, html, css);
    hui_style_store store;
    hui_style_store_init(&store);
    hui_apply_styles(&store, &dom, &atoms, &sheet, HUI_PROP_ALL);
    ASSERT(store.styles.len == dom.nodes.len);
    hui_layout_opts opts = {800.0f, 600.0f, 96.0f};
    hui_layout_run(&dom, &store, &opts);
    hui_draw_list list;
    hui_draw_list_init(&list);
    hui_paint_build(&list, &dom, &store, 800.0f, 600.0f);
    ASSERT(list.cmds.len >= 2);
    hui_draw_list_reset(&list);
    hui_style_store_reset(&store);
    hui_css_reset(&sheet);
    hui_dom_reset(&dom);
    hui_intern_reset(&atoms);
}

static void test_filter_spec(void) {
    const char *html = "<root><span class='keep'>ok</span><p>drop</p></root>";
    const char *allow_tags[] = {"span"};
    hui_filter_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.allow_tags = allow_tags;
    spec.allow_tags_count = 1;
    spec.max_depth = -1;

    hui_dom dom;
    hui_intern atoms;
    hui_builder builder;
    hui_dom_init(&dom);
    hui_intern_init(&atoms);
    memset(&builder, 0, sizeof(builder));
    builder.dom = &dom;
    builder.atoms = &atoms;
    builder.filter_spec = &spec;

    ASSERT(hui_build_from_html(&builder, html, strlen(html)) == HUI_OK);
    ASSERT(dom.root != 0xFFFFFFFFu);
    hui_dom_node *root = &dom.nodes.data[dom.root];
    uint32_t len = 0;
    const char *tag = hui_intern_str(&atoms, root->tag, &len);
    ASSERT(len == 4 && memcmp(tag, "span", 4) == 0);
    ASSERT(root->first_child != 0xFFFFFFFFu);
    hui_dom_node *text = &dom.nodes.data[root->first_child];
    ASSERT(text->type == HUI_NODE_TEXT);
    ASSERT(strncmp(text->text, "ok", text->text_len) == 0);

    hui_dom_reset(&dom);
    hui_intern_reset(&atoms);
}

static void test_dom_mutations(void) {
    const char *html = "<div id='root'></div>";
    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *)html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);
    hui_node_handle root = hui_dom_root(ctx);
    ASSERT(!hui_node_is_null(root));
    ASSERT(hui_dom_add_class(ctx, root, "foo") == HUI_OK);
    ASSERT(hui_dom_remove_class(ctx, root, "foo") == HUI_OK);
    ASSERT(hui_dom_set_attr(ctx, root, "id", "newid") == HUI_OK);
    ASSERT(hui_dom_set_text(ctx, root, "foo") != HUI_OK);
    hui_destroy(ctx);
}

static void test_auto_text_input(void) {
    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);

    char name_storage[64] = {0};
    hui_binding name_binding = {
        .type = HUI_BIND_STRING,
        .ptr = name_storage,
        .string_capacity = sizeof(name_storage)
    };
    ASSERT(hui_bind_variable(ctx, "name", &name_binding) == HUI_OK);

    test_clipboard clip;
    memset(&clip, 0, sizeof(clip));
    hui_clipboard_iface clipboard = {
        .get_text = test_clipboard_get,
        .set_text = test_clipboard_set,
        .user = &clip
    };
    hui_text_field_keymap keymap = {
        .backspace = 8,
        .select_all = 'A',
        .copy = 'C',
        .paste = 'V',
        .cut = TEST_KEY_CUT,
        .move_left = TEST_KEY_LEFT,
        .move_right = TEST_KEY_RIGHT,
        .move_up = TEST_KEY_UP,
        .move_down = TEST_KEY_DOWN,
        .move_home = TEST_KEY_HOME,
        .move_end = TEST_KEY_END,
        .delete_forward = TEST_KEY_DELETE
    };
    hui_set_text_input_defaults(ctx, &clipboard, &keymap, 64);

    const char *html = "<main><input id='name' placeholder='Name' value=\"{{ name }}\"></main><p>{{ name }}</p>";
    const char *css =
            "input { padding: 11px 6px 13px 19px; font-size: 14px; }"
            "input.placeholder { color: #999999; }";
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);

    hui_build_opts opts = {220.0f, 140.0f, 96.0f, 0};
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);
    uint32_t initial_dirty = hui_step(ctx, 0.0f);
    if (initial_dirty) {
        ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);
    }

    hui_node_handle input = hui_dom_query_id(ctx, "name");
    ASSERT(!hui_node_is_null(input));

    hui_draw_list_view view = hui_get_draw_list(ctx);
    hui_rect rect;
    ASSERT(hui_node_get_layout(ctx, input, &rect) == HUI_OK);
    float px = rect.x + rect.w * 0.5f;
    float py = rect.y + rect.h * 0.5f;

    hui_input_event move = {0};
    move.type = HUI_INPUT_EVENT_POINTER_MOVE;
    move.data.pointer_move.x = px;
    move.data.pointer_move.y = py;
    ASSERT(hui_push_input(ctx, &move) == HUI_OK);

    hui_input_event press = {0};
    press.type = HUI_INPUT_EVENT_POINTER_BUTTON;
    press.data.pointer_button.x = px;
    press.data.pointer_button.y = py;
    press.data.pointer_button.buttons = HUI_POINTER_BUTTON_PRIMARY;
    ASSERT(hui_push_input(ctx, &press) == HUI_OK);

    uint32_t dirty = hui_step(ctx, 0.016f);
    ASSERT(dirty != 0);

    const hui_input_state *state = hui_input_get_state(ctx);
    ASSERT(!hui_node_is_null(state->focus));
    ASSERT(state->focus.index == input.index && state->focus.gen == input.gen);

    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    hui_input_event text_ev = {0};
    text_ev.type = HUI_INPUT_EVENT_TEXT_INPUT;
    text_ev.data.text.codepoint = 'H';
    ASSERT(hui_push_input(ctx, &text_ev) == HUI_OK);
    text_ev.data.text.codepoint = 'i';
    ASSERT(hui_push_input(ctx, &text_ev) == HUI_OK);

    dirty = hui_step(ctx, 0.016f);
    ASSERT(dirty != 0);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    hui_node_handle text = hui_node_first_child(ctx, input);
    ASSERT(!hui_node_is_null(text));
    hui_rect input_rect;
    ASSERT(hui_node_get_layout(ctx, input, &input_rect) == HUI_OK);
    hui_rect text_rect;
    ASSERT(hui_node_get_layout(ctx, text, &text_rect) == HUI_OK);
    ASSERT(fabsf(text_rect.x - (input_rect.x + 19.0f)) < 0.01f);
    ASSERT(fabsf(text_rect.y - (input_rect.y + 11.0f)) < 0.01f);

    view = hui_get_draw_list(ctx);
    int hi_found = 0;
    for (size_t i = 0; i < view.count; i++) {
        const hui_draw *cmd = &view.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *text = hui_draw_text_utf8(ctx, cmd, &len);
        if (text && len == strlen("Hi") && strncmp(text, "Hi", len) == 0) {
            hi_found = 1;
        }
    }
    ASSERT(hi_found);
    ASSERT(strcmp(name_storage, "Hi") == 0);

    hui_input_event backspace = {0};
    backspace.type = HUI_INPUT_EVENT_KEY_DOWN;
    backspace.data.key.keycode = 8;
    backspace.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &backspace) == HUI_OK);

    dirty = hui_step(ctx, 0.016f);
    ASSERT(dirty != 0);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    view = hui_get_draw_list(ctx);
    int h_found = 0;
    for (size_t i = 0; i < view.count; i++) {
        const hui_draw *cmd = &view.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *text = hui_draw_text_utf8(ctx, cmd, &len);
        if (text && len == 1 && text[0] == 'H') {
            h_found = 1;
        }
    }
    ASSERT(h_found);

    hui_destroy(ctx);
}

static void test_queries(void) {
    const char *html = "<header class='bar'><h1 id='title'>A</h1><p class='bar'>B</p></header>";
    hui_ctx *ctx = hui_create(NULL, NULL);
    hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1);
    hui_parse(ctx);
    hui_node_handle title = hui_dom_query_id(ctx, "title");
    ASSERT(!hui_node_is_null(title));
    hui_node_handle class_hits[4];
    size_t count = hui_dom_query_class(ctx, "bar", class_hits, 4);
    ASSERT(count == 2);
    hui_destroy(ctx);
}

static void test_ir_serialization(void) {
    hui_draw_list list;
    hui_draw_list_init(&list);
    hui_draw draw;
    memset(&draw, 0, sizeof(draw));
    draw.op = HUI_DRAW_OP_RECT;
    draw.u0 = 0xFF0000FFu;
    hui_vec_push(&list.cmds, draw);
    const char *path = "test.huir";
    ASSERT(hui_ir_write_draw_only(&list, path) == 0);
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    fclose(f);
    remove(path);
    hui_draw_list_reset(&list);
}

static uint32_t find_rect_color_for_node(hui_ctx *ctx, uint32_t node_index) {
    hui_draw_list_view view = hui_get_draw_list(ctx);
    for (size_t i = 0; i < view.count; i++) {
        const hui_draw *cmd = &view.items[i];
        if (cmd->op == HUI_DRAW_OP_RECT && cmd->u1 == node_index)
            return cmd->u0;
    }
    return 0;
}

static void test_button_text_color(void) {
    const char *html =
            "<!doctype html><html><body>"
            "<main><button id='play'>Play</button><p>Body</p></main>"
            "</body></html>";
    const char *css =
            "body { color: #A0A0A0; }"
            "button { color: #FFFFFF; background-color: #202020; }";
    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);
    hui_build_opts opts = {640.0f, 480.0f, 96.0f, 0};
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);
    hui_draw_list_view view = hui_get_draw_list(ctx);
    int play_found = 0;
    for (size_t i = 0; i < view.count; i++) {
        const hui_draw *cmd = &view.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *text = hui_draw_text_utf8(ctx, cmd, &len);
        if (!text || len == 0) continue;
        if (len == 4 && strncmp(text, "Play", 4) == 0) {
            ASSERT(cmd->u0 == 0xFFFFFFFFu);
            play_found = 1;
        }
    }
    ASSERT(play_found);
    hui_destroy(ctx);
}

static void test_input_hover_interaction(void) {
    const char *html = "<!doctype html><html><body><button id='btn'>Click</button></body></html>";
    const char *css =
            "button { background-color: #202020; width: 100px; height: 40px; }"
            "button:hover { background-color: #ff0000; }";
    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);
    hui_build_opts opts = {200.0f, 200.0f, 96.0f, 0};
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    hui_node_handle btn = hui_dom_query_id(ctx, "btn");
    ASSERT(!hui_node_is_null(btn));

    uint32_t base_color = find_rect_color_for_node(ctx, btn.index);
    ASSERT(base_color == 0xFF202020u);

    hui_input_event move;
    memset(&move, 0, sizeof(move));
    move.type = HUI_INPUT_EVENT_POINTER_MOVE;
    move.data.pointer_move.x = 10.0f;
    move.data.pointer_move.y = 10.0f;
    ASSERT(hui_push_input(ctx, &move) == HUI_OK);

    hui_input_event down;
    memset(&down, 0, sizeof(down));
    down.type = HUI_INPUT_EVENT_POINTER_BUTTON;
    down.data.pointer_button.x = 12.0f;
    down.data.pointer_button.y = 12.0f;
    down.data.pointer_button.buttons = HUI_POINTER_BUTTON_PRIMARY;
    ASSERT(hui_push_input(ctx, &down) == HUI_OK);

    uint32_t dirty = hui_process_input(ctx);
    ASSERT(dirty != 0);
    ASSERT((dirty & HUI_DIRTY_STYLE) != 0);
    ASSERT(hui_process_input(ctx) == 0);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    uint32_t hover_color = find_rect_color_for_node(ctx, btn.index);
    ASSERT(hover_color == 0xFFFF0000u);

    hui_input_event leave;
    memset(&leave, 0, sizeof(leave));
    leave.type = HUI_INPUT_EVENT_POINTER_LEAVE;
    ASSERT(hui_push_input(ctx, &leave) == HUI_OK);

    dirty = hui_process_input(ctx);
    ASSERT(dirty != 0);
    ASSERT((dirty & HUI_DIRTY_STYLE) != 0);
    ASSERT(hui_process_input(ctx) == 0);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    uint32_t reset_color = find_rect_color_for_node(ctx, btn.index);
    ASSERT(reset_color == 0xFF202020u);

    hui_destroy(ctx);
}

static void test_font_size_application(void) {
    const char *html =
            "<!doctype html><html><body>"
            "<main><button id='play'>Play</button><p class='muted'>Body</p></main>"
            "</body></html>";
    const char *css =
            "body { font-size: 18px; }"
            "button { font-size: 32px; }";
    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);
    hui_build_opts opts = {640.0f, 480.0f, 96.0f, 0};
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);
    hui_draw_list_view view = hui_get_draw_list(ctx);
    int play_found = 0;
    int body_found = 0;
    for (size_t i = 0; i < view.count; i++) {
        const hui_draw *cmd = &view.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *text = hui_draw_text_utf8(ctx, cmd, &len);
        if (!text || len == 0) continue;
        if (len == 4 && strncmp(text, "Play", 4) == 0) {
            ASSERT(fabsf(cmd->f[4] - 32.0f) < 0.1f);
            play_found = 1;
        } else if (len == 4 && strncmp(text, "Body", 4) == 0) {
            ASSERT(fabsf(cmd->f[4] - 18.0f) < 0.1f);
            body_found = 1;
        }
    }
    ASSERT(play_found && body_found);
    hui_destroy(ctx);
}

static void test_text_field_interaction(void) {
    const char *html = "<!doctype html><html><body>"
                       "<div id='name-input' class='input'><span id='name-value'></span></div>"
                       "</body></html>";
    const char *css = ".input { padding: 4px; border: 1px solid #202020; }";

    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);

    hui_build_opts opts = {200.0f, 120.0f, 96.0f, 0};
    char buffer[64] = {0};
    test_clipboard clip;
    memset(&clip, 0, sizeof(clip));

    hui_text_field_keymap keymap = {
        .backspace = 8,
        .select_all = 'A',
        .copy = 'C',
        .paste = 'V',
        .cut = TEST_KEY_CUT,
        .move_left = TEST_KEY_LEFT,
        .move_right = TEST_KEY_RIGHT,
        .move_up = TEST_KEY_UP,
        .move_down = TEST_KEY_DOWN,
        .move_home = TEST_KEY_HOME,
        .move_end = TEST_KEY_END,
        .delete_forward = TEST_KEY_DELETE
    };
    hui_clipboard_iface clipboard = {
        .get_text = test_clipboard_get,
        .set_text = test_clipboard_set,
        .user = &clip
    };
    hui_text_field field;
    hui_text_field_desc desc = {
        .container_id = "name-input",
        .value_id = "name-value",
        .placeholder = "Placeholder",
        .buffer = buffer,
        .buffer_capacity = sizeof(buffer),
        .clipboard = &clipboard,
        .keymap = &keymap
    };
    hui_node_handle test_container = hui_dom_query_id(ctx, "name-input");
    ASSERT(!hui_node_is_null(test_container));
    ASSERT(hui_node_is_element(ctx, test_container));
    hui_node_handle test_value = hui_dom_query_id(ctx, "name-value");
    ASSERT(!hui_node_is_null(test_value));
    ASSERT(hui_node_is_element(ctx, test_value));
    int init_res = hui_text_field_init(ctx, &field, &desc);
    ASSERT(init_res == HUI_OK);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    hui_draw_list_view view = hui_get_draw_list(ctx);
    int placeholder_found = 0;
    for (size_t i = 0; i < view.count; i++) {
        const hui_draw *cmd = &view.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *text = hui_draw_text_utf8(ctx, cmd, &len);
        if (text && len == strlen("Placeholder") && strncmp(text, "Placeholder", len) == 0)
            placeholder_found = 1;
    }
    ASSERT(placeholder_found);

    uint32_t placeholder_dirty = hui_text_field_set_text(ctx, &field, "");
    ASSERT(placeholder_dirty != 0);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    view = hui_get_draw_list(ctx);
    placeholder_found = 0;
    for (size_t i = 0; i < view.count; i++) {
        const hui_draw *cmd = &view.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *text = hui_draw_text_utf8(ctx, cmd, &len);
        if (text && len == strlen("Placeholder") && strncmp(text, "Placeholder", len) == 0)
            placeholder_found = 1;
    }
    ASSERT(placeholder_found);

    hui_node_handle container = hui_dom_query_id(ctx, "name-input");
    ASSERT(!hui_node_is_null(container));
    hui_rect rect;
    ASSERT(hui_node_get_layout(ctx, container, &rect) == HUI_OK);
    float px = rect.x + rect.w * 0.5f;
    float py = rect.y + rect.h * 0.5f;

    hui_input_event move = {0};
    move.type = HUI_INPUT_EVENT_POINTER_MOVE;
    move.data.pointer_move.x = px;
    move.data.pointer_move.y = py;
    ASSERT(hui_push_input(ctx, &move) == HUI_OK);

    hui_input_event down = {0};
    down.type = HUI_INPUT_EVENT_POINTER_BUTTON;
    down.data.pointer_button.x = px;
    down.data.pointer_button.y = py;
    down.data.pointer_button.buttons = HUI_POINTER_BUTTON_PRIMARY;
    ASSERT(hui_push_input(ctx, &down) == HUI_OK);

    uint32_t dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(dirty != 0);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    view = hui_get_draw_list(ctx);
    placeholder_found = 0;
    for (size_t i = 0; i < view.count; i++) {
        const hui_draw *cmd = &view.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *text = hui_draw_text_utf8(ctx, cmd, &len);
        if (text && len == strlen("Placeholder") && strncmp(text, "Placeholder", len) == 0)
            placeholder_found = 1;
    }
    ASSERT(!placeholder_found);

    hui_input_event text_ev = {0};
    text_ev.type = HUI_INPUT_EVENT_TEXT_INPUT;
    text_ev.data.text.codepoint = 'H';
    ASSERT(hui_push_input(ctx, &text_ev) == HUI_OK);
    text_ev.data.text.codepoint = 'i';
    ASSERT(hui_push_input(ctx, &text_ev) == HUI_OK);

    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(strcmp(hui_text_field_text(&field), "Hi") == 0);
    ASSERT(hui_text_field_length(&field) == 2);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    hui_input_event arrow = {0};
    arrow.type = HUI_INPUT_EVENT_KEY_DOWN;
    arrow.data.key.keycode = TEST_KEY_LEFT;
    arrow.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &arrow) == HUI_OK);
    arrow.type = HUI_INPUT_EVENT_KEY_UP;
    ASSERT(hui_push_input(ctx, &arrow) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);

    text_ev.data.text.codepoint = 'a';
    ASSERT(hui_push_input(ctx, &text_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(strcmp(hui_text_field_text(&field), "Hai") == 0);

    arrow.type = HUI_INPUT_EVENT_KEY_DOWN;
    arrow.data.key.keycode = TEST_KEY_LEFT;
    arrow.data.key.modifiers = HUI_KEY_MOD_SHIFT;
    ASSERT(hui_push_input(ctx, &arrow) == HUI_OK);
    arrow.type = HUI_INPUT_EVENT_KEY_UP;
    ASSERT(hui_push_input(ctx, &arrow) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);

    hui_input_event del = {0};
    del.type = HUI_INPUT_EVENT_KEY_DOWN;
    del.data.key.keycode = TEST_KEY_DELETE;
    del.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &del) == HUI_OK);
    del.type = HUI_INPUT_EVENT_KEY_UP;
    ASSERT(hui_push_input(ctx, &del) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(strcmp(hui_text_field_text(&field), "Hi") == 0);

    hui_input_event home = {0};
    home.type = HUI_INPUT_EVENT_KEY_DOWN;
    home.data.key.keycode = TEST_KEY_HOME;
    home.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &home) == HUI_OK);
    home.type = HUI_INPUT_EVENT_KEY_UP;
    ASSERT(hui_push_input(ctx, &home) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);

    hui_input_event end_key = {0};
    end_key.type = HUI_INPUT_EVENT_KEY_DOWN;
    end_key.data.key.keycode = TEST_KEY_END;
    end_key.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &end_key) == HUI_OK);
    end_key.type = HUI_INPUT_EVENT_KEY_UP;
    ASSERT(hui_push_input(ctx, &end_key) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(strcmp(hui_text_field_text(&field), "Hi") == 0);

    hui_input_event key_ev = {0};
    key_ev.type = HUI_INPUT_EVENT_KEY_DOWN;
    key_ev.data.key.keycode = 'A';
    key_ev.data.key.modifiers = HUI_KEY_MOD_CTRL;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(field.select_all);

    key_ev.data.key.keycode = 'C';
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(strcmp(clip.buffer, "Hi") == 0);

    memset(clip.buffer, 0, sizeof(clip.buffer));
    memcpy(clip.buffer, "HELLO", 5);
    key_ev.data.key.keycode = 'V';
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(strcmp(hui_text_field_text(&field), "HELLO") == 0);

    hui_input_event backspace_down = {0};
    backspace_down.type = HUI_INPUT_EVENT_KEY_DOWN;
    backspace_down.data.key.keycode = 8;
    backspace_down.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &backspace_down) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(strcmp(hui_text_field_text(&field), "HELL") == 0);

    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, field.backspace_initial_delay + 0.02f);
    ASSERT(strcmp(hui_text_field_text(&field), "HEL") == 0);

    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, field.backspace_repeat_delay + 0.02f);
    ASSERT(strcmp(hui_text_field_text(&field), "HE") == 0);

    hui_input_event backspace_up = {0};
    backspace_up.type = HUI_INPUT_EVENT_KEY_UP;
    backspace_up.data.key.keycode = 8;
    backspace_up.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &backspace_up) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(strcmp(hui_text_field_text(&field), "HE") == 0);

    text_ev.type = HUI_INPUT_EVENT_TEXT_INPUT;
    text_ev.data.text.codepoint = '\n';
    ASSERT(hui_push_input(ctx, &text_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(strcmp(hui_text_field_text(&field), "HE") == 0);

    hui_destroy(ctx);
}

static void test_textarea_multiline(void) {
    const char *html = "<!doctype html><html><body>"
                       "<div id='note-input' class='input'><span id='note-value'></span></div>"
                       "</body></html>";
    const char *css = ".input { padding: 4px; border: 1px solid #202020; }";

    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);

    hui_build_opts opts = {200.0f, 160.0f, 96.0f, 0};
    char buffer[128] = {0};
    test_clipboard clip;
    memset(&clip, 0, sizeof(clip));

    hui_text_field_keymap keymap = {
        .backspace = 8,
        .select_all = 'A',
        .copy = 'C',
        .paste = 'V',
        .cut = TEST_KEY_CUT,
        .move_left = TEST_KEY_LEFT,
        .move_right = TEST_KEY_RIGHT,
        .move_up = TEST_KEY_UP,
        .move_down = TEST_KEY_DOWN,
        .move_home = TEST_KEY_HOME,
        .move_end = TEST_KEY_END,
        .delete_forward = TEST_KEY_DELETE
    };
    hui_clipboard_iface clipboard = {
        .get_text = test_clipboard_get,
        .set_text = test_clipboard_set,
        .user = &clip
    };

    hui_text_field field;
    hui_text_field_desc desc = {
        .container_id = "note-input",
        .value_id = "note-value",
        .placeholder = "Write something",
        .buffer = buffer,
        .buffer_capacity = sizeof(buffer),
        .clipboard = &clipboard,
        .keymap = &keymap,
        .flags = HUI_TEXT_FIELD_FLAG_MULTI_LINE
    };
    ASSERT(hui_text_field_init(ctx, &field, &desc) == HUI_OK);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    ASSERT(hui_text_field_set_text(ctx, &field, "Hello\r\nWorld") != 0);
    ASSERT(strcmp(hui_text_field_text(&field), "Hello\nWorld") == 0);
    ASSERT(hui_text_field_length(&field) == strlen("Hello\nWorld"));

    ASSERT(hui_input_set_focus(ctx, field.container) == HUI_OK);

    hui_input_event newline = {0};
    newline.type = HUI_INPUT_EVENT_TEXT_INPUT;
    newline.data.text.codepoint = '\n';
    ASSERT(hui_push_input(ctx, &newline) == HUI_OK);
    uint32_t dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(strcmp(hui_text_field_text(&field), "Hello\nWorld\n") == 0);

    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    ASSERT(hui_text_field_set_text(ctx, &field, "One\nTwo\nThree") != 0);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);
    ASSERT(hui_input_set_focus(ctx, field.container) == HUI_OK);

    hui_input_event key_ev = {0};
    key_ev.type = HUI_INPUT_EVENT_KEY_DOWN;
    key_ev.data.key.keycode = TEST_KEY_HOME;
    key_ev.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    key_ev.type = HUI_INPUT_EVENT_KEY_UP;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(field.caret == strlen("One\nTwo\n"));

    key_ev.type = HUI_INPUT_EVENT_KEY_DOWN;
    key_ev.data.key.keycode = TEST_KEY_UP;
    key_ev.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    key_ev.type = HUI_INPUT_EVENT_KEY_UP;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(field.caret == strlen("One\n"));

    key_ev.type = HUI_INPUT_EVENT_KEY_DOWN;
    key_ev.data.key.keycode = TEST_KEY_RIGHT;
    key_ev.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    key_ev.type = HUI_INPUT_EVENT_KEY_UP;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);

    key_ev.type = HUI_INPUT_EVENT_KEY_DOWN;
    key_ev.data.key.keycode = TEST_KEY_RIGHT;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    key_ev.type = HUI_INPUT_EVENT_KEY_UP;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(field.caret == strlen("One\n") + 2);

    key_ev.type = HUI_INPUT_EVENT_KEY_DOWN;
    key_ev.data.key.keycode = TEST_KEY_DOWN;
    key_ev.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    key_ev.type = HUI_INPUT_EVENT_KEY_UP;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(field.caret == strlen("One\nTwo\n") + 2);

    key_ev.type = HUI_INPUT_EVENT_KEY_DOWN;
    key_ev.data.key.keycode = TEST_KEY_END;
    key_ev.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    key_ev.type = HUI_INPUT_EVENT_KEY_UP;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(field.caret == strlen("One\nTwo\nThree"));

    key_ev.type = HUI_INPUT_EVENT_KEY_DOWN;
    key_ev.data.key.keycode = TEST_KEY_HOME;
    key_ev.data.key.modifiers = HUI_KEY_MOD_SHIFT;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    key_ev.type = HUI_INPUT_EVENT_KEY_UP;
    key_ev.data.key.modifiers = 0;
    ASSERT(hui_push_input(ctx, &key_ev) == HUI_OK);
    dirty = hui_process_input(ctx);
    dirty |= hui_text_field_step(ctx, &field, 0.016f);
    ASSERT(field.caret == strlen("One\nTwo\n"));
    size_t sel_start = field.sel_anchor < field.caret ? field.sel_anchor : field.caret;
    size_t sel_end = field.sel_anchor > field.caret ? field.sel_anchor : field.caret;
    ASSERT(sel_start == strlen("One\nTwo\n"));
    ASSERT(sel_end == strlen("One\nTwo\nThree"));

    hui_destroy(ctx);
}

static void test_text_field_scroll(void) {
    const char *html = "<!doctype html><html><body>"
                       "<div id='scroll-input' class='input'><span id='scroll-value'></span></div>"
                       "</body></html>";
    const char *css = ".input { width: 120px; padding: 4px; border: 1px solid #202020; }";

    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);

    hui_build_opts opts = {200.0f, 120.0f, 96.0f, 0};
    char buffer[128] = {0};

    hui_text_field field;
    hui_text_field_desc desc = {
        .container_id = "scroll-input",
        .value_id = "scroll-value",
        .placeholder = "Hint",
        .buffer = buffer,
        .buffer_capacity = sizeof(buffer)
    };
    ASSERT(hui_text_field_init(ctx, &field, &desc) == HUI_OK);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    const char *long_text = "012345678901234567890123456789";
    ASSERT(hui_text_field_set_text(ctx, &field, long_text) != 0);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    ASSERT(field.scroll_x > 0.0f);

    hui_draw_list_view view = hui_get_draw_list(ctx);
    int glyph_found = 0;
    for (size_t i = 0; i < view.count; i++) {
        const hui_draw *cmd = &view.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *cmd_text = hui_draw_text_utf8(ctx, cmd, &len);
        if (!cmd_text) continue;
        if (len == strlen(long_text) && strncmp(cmd_text, long_text, len) == 0) {
            glyph_found = 1;
            ASSERT(cmd->f[5] >= 0.0f);
            float diff = cmd->f[5] - field.scroll_x;
            if (diff < 0.0f) diff = -diff;
            ASSERT(diff < 0.001f);
            break;
        }
    }
    ASSERT(glyph_found);

    hui_destroy(ctx);
}

static void test_render_caching(void) {
    const char *html = "<div id='root'><span id='label'>Hello</span></div>";
    const char *css = "div { background-color: #202020; color: #ffffff; padding: 8px; }";
    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);
    hui_build_opts opts = {320.0f, 180.0f, 96.0f, 0};

    hui_render_output first = {0};
    ASSERT(hui_render(ctx, &opts, &first) == HUI_OK);
    ASSERT(first.changed != 0);
    size_t initial_commands = first.draw.count;

    hui_render_output second = {0};
    ASSERT(hui_render(ctx, &opts, &second) == HUI_OK);
    ASSERT(second.changed == 0);
    ASSERT(second.draw.count == initial_commands);

    hui_node_handle root = hui_dom_root(ctx);
    ASSERT(!hui_node_is_null(root));
    hui_mark_dirty(ctx, root, HUI_DIRTY_PAINT);

    hui_render_output third = {0};
    ASSERT(hui_render(ctx, &opts, &third) == HUI_OK);
    ASSERT(third.changed != 0);

    hui_destroy(ctx);
}

static void test_text_template_bindings(void) {
    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);

    char name[32] = "Alice";
    char email[64] = "alice@example.com";

    hui_binding name_binding = {
        .type = HUI_BIND_STRING,
        .ptr = name,
        .string_capacity = sizeof(name)
    };
    hui_binding email_binding = {
        .type = HUI_BIND_STRING,
        .ptr = email,
        .string_capacity = sizeof(email)
    };
    ASSERT(hui_bind_variable(ctx, "name_value", &name_binding) == HUI_OK);
    ASSERT(hui_bind_variable(ctx, "email_value", &email_binding) == HUI_OK);

    const char *html = "<p id='msg'>Hi {{ name_value }}, contact us at {{ email_value }}.</p>";
    const char *css = "p { color: #ffffff; }";
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);

    hui_build_opts opts = {360.0f, 180.0f, 96.0f, 0};
    hui_render_output out = {0};
    ASSERT(hui_render(ctx, &opts, &out) == HUI_OK);

    const char *expected = "Hi Alice, contact us at alice@example.com.";
    int found = 0;
    for (size_t i = 0; i < out.draw.count; i++) {
        const hui_draw *cmd = &out.draw.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *text = hui_draw_text_utf8(ctx, cmd, &len);
        if (text && len == strlen(expected) && strncmp(text, expected, len) == 0) {
            found = 1;
            break;
        }
    }
    ASSERT(found);

    snprintf(name, sizeof(name), "%s", "Bob");
    snprintf(email, sizeof(email), "%s", "bob@example.org");

    uint32_t dirty = hui_step(ctx, 0.0f);
    ASSERT(dirty != 0);
    ASSERT(hui_render(ctx, &opts, &out) == HUI_OK);

    expected = "Hi Bob, contact us at bob@example.org.";
    found = 0;
    for (size_t i = 0; i < out.draw.count; i++) {
        const hui_draw *cmd = &out.draw.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *text = hui_draw_text_utf8(ctx, cmd, &len);
        if (text && len == strlen(expected) && strncmp(text, expected, len) == 0) {
            found = 1;
            break;
        }
    }
    ASSERT(found);

    hui_destroy(ctx);
}

static void test_text_binding_render(void) {
    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);

    int counter = 5;
    hui_binding binding = {
        .type = HUI_BIND_INT,
        .ptr = &counter,
        .string_capacity = 0
    };
    ASSERT(hui_bind_variable(ctx, "counter", &binding) == HUI_OK);

    const char *html = "<p id='counter'>{{ counter }}</p>";
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);

    hui_build_opts opts = {120.0f, 80.0f, 96.0f, 0};
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    hui_draw_list_view view = hui_get_draw_list(ctx);
    int five_found = 0;
    for (size_t i = 0; i < view.count; i++) {
        const hui_draw *cmd = &view.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *text = hui_draw_text_utf8(ctx, cmd, &len);
        if (text && len == strlen("5") && strncmp(text, "5", len) == 0) {
            five_found = 1;
            break;
        }
    }
    ASSERT(five_found);

    counter = 42;
    uint32_t dirty = hui_step(ctx, 0.0f);
    ASSERT(dirty != 0);
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    view = hui_get_draw_list(ctx);
    int forty_two_found = 0;
    for (size_t i = 0; i < view.count; i++) {
        const hui_draw *cmd = &view.items[i];
        if (cmd->op != HUI_DRAW_OP_GLYPH_RUN) continue;
        size_t len = 0;
        const char *text = hui_draw_text_utf8(ctx, cmd, &len);
        if (text && len == strlen("42") && strncmp(text, "42", len) == 0) {
            forty_two_found = 1;
            break;
        }
    }
    ASSERT(forty_two_found);

    hui_destroy(ctx);
}

static void test_input_dirty_flags(void) {
    const char *html = "<button id='btn'>Click</button>";
    const char *css = "button { background-color: #202020; color: #ffffff; padding: 8px; }";
    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);
    ASSERT(hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1) == HUI_OK);
    ASSERT(hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);
    hui_build_opts opts = {320.0f, 180.0f, 96.0f, 0};
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);

    hui_draw_list_view initial_view = hui_get_draw_list(ctx);
    ASSERT(initial_view.count > 0);

    uint32_t dirty = hui_process_input(ctx);
    ASSERT(dirty == 0);

    hui_input_event move = {0};
    move.type = HUI_INPUT_EVENT_POINTER_MOVE;
    move.data.pointer_move.x = 12.0f;
    move.data.pointer_move.y = 12.0f;
    ASSERT(hui_push_input(ctx, &move) == HUI_OK);

    dirty = hui_process_input(ctx);
    ASSERT(dirty != 0);

    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);
    hui_draw_list_view hovered_view = hui_get_draw_list(ctx);
    ASSERT(hovered_view.count == initial_view.count);

    dirty = hui_process_input(ctx);
    ASSERT(dirty == 0);

    hui_destroy(ctx);
}

static void test_ctx_pipeline(void) {
    const char *html = "<header id='top' class='bar'><h1 id='title'>Text</h1></header>";
    const char *css = ".bar { background-color: #202020; color: #ffffff; padding: 8px; }";
    hui_ctx *ctx = hui_create(NULL, NULL);
    ASSERT(ctx != NULL);
    ASSERT(hui_feed_html(ctx, (hui_bytes){ (const uint8_t*)html, strlen(html) }, 1) == HUI_OK);
    ASSERT(hui_feed_css(ctx, (hui_bytes){ (const uint8_t*)css, strlen(css) }, 1) == HUI_OK);
    ASSERT(hui_parse(ctx) == HUI_OK);
    hui_build_opts opts = {640.0f, 480.0f, 96.0f, 0};
    ASSERT(hui_build_ir(ctx, &opts) == HUI_OK);
    char path[] = "hui_test.txt";
    ASSERT(hui_dump_text_ir(ctx, path) == 0);
    FILE *f = fopen(path, "r");
    ASSERT(f != NULL);
    fclose(f);
    remove(path);
    hui_destroy(ctx);
}

static const test_case tests[] = {
    {"intern", test_intern},
    {"html_builder", test_html_builder},
    {"css_parser", test_css_parser},
    {"style_layout_paint", test_style_layout_paint},
    {"filter_spec", test_filter_spec},
    {"dom_mutations", test_dom_mutations},
    {"queries", test_queries},
    {"ir_serialization", test_ir_serialization},
    {"button_text_color", test_button_text_color},
    {"input_hover_interaction", test_input_hover_interaction},
    {"font_size_application", test_font_size_application},
    {"text_field_interaction", test_text_field_interaction},
    {"text_field_scroll", test_text_field_scroll},
    {"textarea_multiline", test_textarea_multiline},
    {"text_template_bindings", test_text_template_bindings},
    {"render_caching", test_render_caching},
    {"text_binding_render", test_text_binding_render},
    {"auto_text_input", test_auto_text_input},
    {"input_dirty_flags", test_input_dirty_flags},
    {"ctx_pipeline", test_ctx_pipeline}
};

static int should_run_test(int argc, char **argv, const char *name) {
    if (argc <= 1) return 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) continue;
        if (strcmp(argv[i], name) == 0) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    size_t failed = 0;
    size_t executed = 0;
    size_t total = sizeof(tests) / sizeof(tests[0]);
    if (argc > 1 && strcmp(argv[1], "--list") == 0) {
        for (size_t i = 0; i < total; i++) {
            printf("%s\n", tests[i].name);
        }
        return 0;
    }

    for (size_t i = 0; i < total; i++) {
        if (!should_run_test(argc, argv, tests[i].name)) continue;
        executed++;
        current_failed = 0;
        tests[i].fn();
        if (current_failed) {
            fprintf(stderr, "[X] %s\n", tests[i].name);
            failed++;
        } else {
            fprintf(stdout, "[OK] %s\n", tests[i].name);
        }
    }

    if (executed == 0) {
        fprintf(stderr, "No tests selected.\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "Ran %zu tests: %zu passed, %zu failed\n",
            executed,
            executed - failed,
            failed);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
