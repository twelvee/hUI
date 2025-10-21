#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hui_err.h"
#include "hui/hui.h"

static hui_filter_decision only_ui(const hui_tag_probe *probe, void *user) {
    (void) user;
    const char *keep[] = {"header", "main", "footer", "h1", "p", "button", "div", "span"};
    for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++) {
        size_t len = strlen(keep[i]);
        if (probe->tag_len == len && memcmp(probe->tag, keep[i], len) == 0) return HUI_FILTER_TAKE;
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
