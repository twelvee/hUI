#ifndef HUI_HTML_LEXER_H
#define HUI_HTML_LEXER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HUI_TK_EOF = 0,
    HUI_TK_TEXT,
    HUI_TK_OPEN,
    HUI_TK_CLOSE,
    HUI_TK_SELF_CLOSE,
    HUI_TK_ERR
} hui_token_kind;

typedef struct {
    const char *p;
    size_t n;
} hui_slice;

typedef struct {
    hui_token_kind kind;
    hui_slice text;
    hui_slice tag;
    hui_slice id;
    hui_slice class_attr;
    hui_slice type_attr;
    hui_slice placeholder_attr;
    hui_slice value_attr;
    int selected_attr;
} hui_token;

typedef struct {
    const char *buf;
    size_t len;
    size_t pos;
    int finished;
} hui_html_lexer;

void hui_html_lexer_init(hui_html_lexer *lexer, const char *data, size_t len);

int hui_html_next(hui_html_lexer *lexer, hui_token *out);

#endif
