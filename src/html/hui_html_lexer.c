#include "hui_html_lexer.h"

#include <ctype.h>
#include <string.h>

static int is_name_char(char c) {
    return isalnum((unsigned char) c) || c == '-' || c == '_' || c == ':';
}

static void skip_ws(const char *s, size_t len, size_t *i) {
    while (*i < len && isspace((unsigned char) s[*i])) (*i)++;
}

static int slice_eq(hui_slice slice, const char *str) {
    size_t n = strlen(str);
    return slice.n == n && (n == 0 || memcmp(slice.p, str, n) == 0);
}

void hui_html_lexer_init(hui_html_lexer *lexer, const char *data, size_t len) {
    lexer->buf = data;
    lexer->len = len;
    lexer->pos = 0;
    lexer->finished = 0;
}

static void reset_token(hui_token *tk, hui_token_kind kind) {
    tk->kind = kind;
    tk->text.p = tk->tag.p = tk->id.p = tk->class_attr.p = NULL;
    tk->type_attr.p = tk->placeholder_attr.p = tk->value_attr.p = NULL;
    tk->text.n = tk->tag.n = tk->id.n = tk->class_attr.n = 0;
    tk->type_attr.n = tk->placeholder_attr.n = tk->value_attr.n = 0;
    tk->selected_attr = 0;
    tk->event_attr_count = 0;
}

int hui_html_next(hui_html_lexer *lexer, hui_token *out) {
    const char *s = lexer->buf;
    size_t n = lexer->len;
    size_t i = lexer->pos;

    if (lexer->finished || i >= n) {
        reset_token(out, HUI_TK_EOF);
        lexer->finished = 1;
        return 0;
    }

    if (s[i] != '<') {
        size_t start = i;
        while (i < n && s[i] != '<') i++;
        reset_token(out, HUI_TK_TEXT);
        out->text.p = s + start;
        out->text.n = i - start;
        lexer->pos = i;
        return 1;
    }

    i++;
    if (i >= n) {
        reset_token(out, HUI_TK_ERR);
        lexer->pos = n;
        return 0;
    }

    if (s[i] == '!') {
        if (i + 2 < n && s[i + 1] == '-' && s[i + 2] == '-') {
            i += 3;
            while (i + 2 < n && !(s[i] == '-' && s[i + 1] == '-' && s[i + 2] == '>')) i++;
            i = (i + 3 <= n) ? i + 3 : n;
        } else {
            while (i < n && s[i] != '>') i++;
            if (i < n) i++;
        }
        lexer->pos = i;
        return hui_html_next(lexer, out);
    }

    if (s[i] == '/') {
        i++;
        size_t start = i;
        while (i < n && is_name_char(s[i])) i++;
        size_t tag_len = i - start;
        while (i < n && s[i] != '>') i++;
        if (i < n) i++;
        reset_token(out, HUI_TK_CLOSE);
        out->tag.p = s + start;
        out->tag.n = tag_len;
        lexer->pos = i;
        return 1;
    }

    size_t tag_start = i;
    while (i < n && is_name_char(s[i])) i++;
    size_t tag_len = i - tag_start;
    hui_slice id = {0};
    hui_slice class_attr = {0};
    hui_slice type_attr = {0};
    hui_slice placeholder_attr = {0};
    hui_slice value_attr = {0};
    int selected_attr = 0;
    int self_close = 0;
    int terminated = 0;

    reset_token(out, HUI_TK_OPEN);
    out->tag.p = s + tag_start;
    out->tag.n = tag_len;

    while (i < n) {
        skip_ws(s, n, &i);
        if (i >= n) break;
        if (s[i] == '>') {
            i++;
            terminated = 1;
            break;
        }
        if (s[i] == '/' && i + 1 < n && s[i + 1] == '>') {
            i += 2;
            self_close = 1;
            terminated = 1;
            break;
        }

        size_t attr_start = i;
        while (i < n && is_name_char(s[i])) i++;
        size_t attr_len = i - attr_start;
        skip_ws(s, n, &i);
        hui_slice value = {0};
        if (i < n && s[i] == '=') {
            i++;
            skip_ws(s, n, &i);
            if (i < n && (s[i] == '"' || s[i] == '\'')) {
                char quote = s[i++];
                size_t val_start = i;
                while (i < n && s[i] != quote) i++;
                value.p = s + val_start;
                value.n = (i < n) ? i - val_start : 0;
                if (i < n) i++;
            } else {
                size_t val_start = i;
                while (i < n && !isspace((unsigned char) s[i]) && s[i] != '>' && s[i] != '/') i++;
                value.p = s + val_start;
                value.n = i - val_start;
            }
        }
        hui_slice name = {s + attr_start, attr_len};
        if (slice_eq(name, "id")) id = value;
        else if (slice_eq(name, "class")) class_attr = value;
        else if (slice_eq(name, "type")) type_attr = value;
        else if (slice_eq(name, "placeholder")) placeholder_attr = value;
        else if (slice_eq(name, "value")) value_attr = value;
        else if (slice_eq(name, "selected")) selected_attr = 1;
        else if (name.n >= 2 && name.p && (name.p[0] == 'o' || name.p[0] == 'O') &&
                 (name.p[1] == 'n' || name.p[1] == 'N')) {
            if (out->event_attr_count < HUI_MAX_EVENT_ATTRS) {
                out->event_attr_names[out->event_attr_count] = name;
                out->event_attr_values[out->event_attr_count] = value;
                out->event_attr_count++;
            }
        }
    }

    if (!terminated) {
        reset_token(out, HUI_TK_ERR);
        lexer->pos = n;
        return 0;
    }

    if (self_close) out->kind = HUI_TK_SELF_CLOSE;
    out->id = id;
    out->class_attr = class_attr;
    out->type_attr = type_attr;
    out->placeholder_attr = placeholder_attr;
    out->value_attr = value_attr;
    out->selected_attr = selected_attr;
    lexer->pos = i;
    return 1;
}
