#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hui_err.h"
#include "hui/hui.h"
#include "hui/hui_html_tags.h"

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
        if (probe->tag_len == tag->length && memcmp(probe->tag, tag->name, tag->length) == 0) return HUI_FILTER_TAKE;
    }
    return HUI_FILTER_SKIP_DESCEND;
}

int main(void) {
    const char *html =
            "<!doctype html><html><body>"
            "<header class='bar'><h1 id='title'>Hello, hUI!</h1></header>"
            "<main><p class='lead'>Demo</p></main>"
            "</body></html>";

    const char *css =
            ".bar { background-color: #202020; color: #ffffff; padding: 16px; }"
            "#title { font-size: 22px; }"
            "p.lead { font-size: 18px; }";

    hui_ctx *ctx = hui_create(NULL, NULL);
    hui_set_dom_filter(ctx, only_ui, NULL);
    hui_feed_html(ctx, (hui_bytes){(const uint8_t *) html, strlen(html)}, 1);
    hui_feed_css(ctx, (hui_bytes){(const uint8_t *) css, strlen(css)}, 1);

    if (hui_parse(ctx) != HUI_OK) {
        fprintf(stderr, "parse: %s\n", hui_last_error(ctx));
        return 1;
    }

    hui_build_opts opts = {800, 600, 96, 0};
    if (hui_build_ir(ctx, &opts) != HUI_OK) {
        fprintf(stderr, "build: %s\n", hui_last_error(ctx));
        return 1;
    }

    hui_write_ir_file(ctx, "ui.huir");
    hui_dump_text_ir(ctx, "ui.txt");
    printf("Wrote ui.huir and ui.txt\n");

    hui_destroy(ctx);
    return 0;
}
