#include "hui/hui_html_tags.h"

#define HUI_HTML_TAG_NAME(name, upper) HUI_HTML_TAG_##upper,

const char *const hui_html_tag_names[] = {
    HUI_HTML_TAG_LIST(HUI_HTML_TAG_NAME)
};

#undef HUI_HTML_TAG_NAME

const size_t hui_html_tag_count = sizeof(hui_html_tag_names) / sizeof(hui_html_tag_names[0]);

#define HUI_HTML_TAG_ENTRY(name, upper) {HUI_HTML_TAG_##upper, sizeof(#name) - 1},

const hui_html_tag_entry hui_html_tag_entries[] = {
    HUI_HTML_TAG_LIST(HUI_HTML_TAG_ENTRY)
};

#undef HUI_HTML_TAG_ENTRY
