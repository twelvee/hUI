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
    hui_paint_build(&list, &dom, &store);
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
    draw.op = HUI_OP_RECT;
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
    {"ctx_pipeline", test_ctx_pipeline}
};

int main(void) {
    size_t failed = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        current_failed = 0;
        tests[i].fn();
        if (current_failed) {
            fprintf(stderr, "[X] %s\n", tests[i].name);
            failed++;
        } else {
            fprintf(stdout, "[OK] %s\n", tests[i].name);
        }
    }
    fprintf(stdout, "Ran %zu tests: %zu passed, %zu failed\n",
            sizeof(tests) / sizeof(tests[0]),
            sizeof(tests) / sizeof(tests[0]) - failed,
            failed);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
