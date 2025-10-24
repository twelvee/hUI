#include "hui_profiler_internal.h"

#include "../include/hui/hui.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#define HUI_PROF_CONSOLE_TARGET_WIDTH 600
#define HUI_PROF_CONSOLE_TARGET_HEIGHT 400
#define HUI_PROF_CONSOLE_MIN_COLS 80
#define HUI_PROF_CONSOLE_MIN_ROWS 30
#define HUI_PROF_CONSOLE_FONT_SIZE 18

typedef struct {
    double last_ms;
    double avg_ms;
    double min_ms;
    double max_ms;
    uint64_t count;
} hui_profiler_stage_stats;

typedef struct {
    size_t dom_nodes;
    size_t draw_cmds;
    size_t auto_text_fields;
    size_t auto_select_fields;
    size_t bindings;
    size_t input_events;
    size_t text_input;
    size_t key_pressed;
    size_t key_released;
    uint32_t step_dirty;
    uint32_t render_pending_dirty;
    uint32_t render_dirty_after;
    int draw_changed;
    double dt_ms;
} hui_profiler_frame_data;

struct hui_profiler {
    double tick_to_ms;
    uint64_t frame_start_ticks;
    int frame_started;
    uint64_t frame_index;
    double last_cpu_ms;
    double avg_frame_ms;
    double min_frame_ms;
    double max_frame_ms;
    uint64_t overhead_ticks;
    int console_px_w;
    int console_px_h;
    int console_cols;
    int console_rows;

    hui_profiler_stage_stats stages[HUI_PROF_STAGE__COUNT];
    double frame_stage_accum[HUI_PROF_STAGE__COUNT];
    uint8_t frame_stage_active[HUI_PROF_STAGE__COUNT];

    hui_profiler_frame_data current;
    hui_profiler_frame_data last;

    void (*free_fn)(void *);
    void *native_window;
    int console_ready;
    int ansi_supported;
#if defined(_WIN32)
    int console_attached;
#endif
};

static const char *const HUI_PROF_STAGE_NAMES[HUI_PROF_STAGE__COUNT] = {
    "Step::Total",
    "Step::Input",
    "Step::AutoText",
    "Step::AutoSelect",
    "Step::Bindings",
    "Render::Total",
    "Render::Style",
    "Render::Layout",
    "Render::Paint"
};

static uint64_t hui_profiler_now_ticks(void) {
#if defined(_WIN32)
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t) counter.QuadPart;
#elif defined(__APPLE__)
    return mach_absolute_time();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
#endif
}

static double hui_profiler_tick_to_ms_factor(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
        return 0.001;
    }
    return 1000.0 / (double) freq.QuadPart;
#elif defined(__APPLE__)
    static mach_timebase_info_data_t timebase;
    static int initialized = 0;
    if (!initialized) {
        mach_timebase_info(&timebase);
        initialized = 1;
    }
    double numer = (double) timebase.numer;
    double denom = (double) timebase.denom;
    if (denom == 0.0) return 0.001;
    return (numer / denom) / 1000000.0;
#else
    return 1.0 / 1000000.0;
#endif
}

static uint64_t hui_profiler_overhead_begin(struct hui_profiler *prof) {
    if (!prof) return 0;
    return hui_profiler_now_ticks();
}

static void hui_profiler_overhead_end(struct hui_profiler *prof, uint64_t start) {
    if (!prof || start == 0) return;
    uint64_t end = hui_profiler_now_ticks();
    if (end >= start) {
        prof->overhead_ticks += (end - start);
    }
}

static void hui_profiler_stage_stats_update(hui_profiler_stage_stats *stats, double sample_ms) {
    stats->last_ms = sample_ms;
    if (stats->count == 0) {
        stats->avg_ms = sample_ms;
        stats->min_ms = sample_ms;
        stats->max_ms = sample_ms;
    } else {
        stats->avg_ms += (sample_ms - stats->avg_ms) / (double) (stats->count + 1);
        if (sample_ms < stats->min_ms) stats->min_ms = sample_ms;
        if (sample_ms > stats->max_ms) stats->max_ms = sample_ms;
    }
    stats->count++;
}

static void hui_profiler_format_dirty(uint32_t flags, char *buffer, size_t cap) {
    if (!buffer || cap == 0) return;
    if (flags == 0) {
        snprintf(buffer, cap, "none");
        return;
    }
    struct {
        uint32_t flag;
        const char *name;
    } entries[] = {
        {HUI_DIRTY_STYLE, "style"},
        {HUI_DIRTY_LAYOUT, "layout"},
        {HUI_DIRTY_PAINT, "paint"},
    };
    size_t len = 0;
    buffer[0] = '\0';
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if ((flags & entries[i].flag) == 0) continue;
        if (len > 0 && len + 1 < cap) {
            buffer[len++] = '|';
        }
        const char *name = entries[i].name;
        size_t name_len = strlen(name);
        size_t to_copy = (name_len < (cap - len - 1)) ? name_len : (cap - len - 1);
        if (to_copy > 0) {
            memcpy(buffer + len, name, to_copy);
            len += to_copy;
        }
    }
    buffer[len] = '\0';
}

#if defined(_WIN32)
static void hui_profiler_position_console(struct hui_profiler *prof, int width, int height) {
    if (!prof) return;
    HWND console = GetConsoleWindow();
    if (!console) return;
    int x = 40;
    int y = 40;
    if (prof->native_window) {
        RECT rect;
        HWND hwnd = (HWND) prof->native_window;
        if (hwnd && GetWindowRect(hwnd, &rect)) {
            x = rect.right + 16;
            y = rect.top;
        }
    }
    SetWindowPos(console, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void hui_profiler_configure_console(struct hui_profiler *prof, HANDLE console) {
    if (!prof || console == INVALID_HANDLE_VALUE) return;

    CONSOLE_FONT_INFOEX font;
    memset(&font, 0, sizeof(font));
    font.cbSize = sizeof(font);
    if (GetCurrentConsoleFontEx(console, FALSE, &font)) {
        font.dwFontSize.Y = (SHORT) HUI_PROF_CONSOLE_FONT_SIZE;
        font.dwFontSize.X = 0;
        font.FontWeight = FW_NORMAL;
        wcscpy(font.FaceName, L"Consolas");
        SetCurrentConsoleFontEx(console, FALSE, &font);
        GetCurrentConsoleFontEx(console, FALSE, &font);
    } else {
        font.dwFontSize.Y = (SHORT) HUI_PROF_CONSOLE_FONT_SIZE;
        font.dwFontSize.X = 0;
    }

    int font_w = font.dwFontSize.X > 0 ? font.dwFontSize.X : 8;
    int font_h = font.dwFontSize.Y > 0 ? font.dwFontSize.Y : HUI_PROF_CONSOLE_FONT_SIZE;

    SHORT target_cols = (SHORT) (HUI_PROF_CONSOLE_TARGET_WIDTH / (font_w ? font_w : 8));
    SHORT target_rows = (SHORT) (HUI_PROF_CONSOLE_TARGET_HEIGHT / (font_h ? font_h : 16));
    if (target_cols < HUI_PROF_CONSOLE_MIN_COLS) target_cols = HUI_PROF_CONSOLE_MIN_COLS;
    if (target_rows < HUI_PROF_CONSOLE_MIN_ROWS) target_rows = HUI_PROF_CONSOLE_MIN_ROWS;

    COORD largest = GetLargestConsoleWindowSize(console);
    if (largest.X > 0 && target_cols > largest.X) target_cols = largest.X;
    if (largest.Y > 0 && target_rows > largest.Y) target_rows = largest.Y;

    SMALL_RECT minimal = {0, 0, 0, 0};
    SetConsoleWindowInfo(console, TRUE, &minimal);

    COORD buffer = {target_cols, target_rows};
    if (!SetConsoleScreenBufferSize(console, buffer)) {
        CONSOLE_SCREEN_BUFFER_INFO current;
        if (GetConsoleScreenBufferInfo(console, &current)) {
            buffer = current.dwSize;
        }
    } else {
        CONSOLE_SCREEN_BUFFER_INFO current;
        if (GetConsoleScreenBufferInfo(console, &current)) {
            buffer = current.dwSize;
        }
    }

    if (buffer.X > 0) target_cols = buffer.X;
    if (buffer.Y > 0) target_rows = buffer.Y;
    if (target_cols < HUI_PROF_CONSOLE_MIN_COLS) target_cols = HUI_PROF_CONSOLE_MIN_COLS;
    if (target_rows < HUI_PROF_CONSOLE_MIN_ROWS) target_rows = HUI_PROF_CONSOLE_MIN_ROWS;

    SMALL_RECT rect = {0, 0, (SHORT) (target_cols - 1), (SHORT) (target_rows - 1)};
    SetConsoleWindowInfo(console, TRUE, &rect);

    int pixel_w = target_cols * font_w;
    int pixel_h = target_rows * font_h;
    if (pixel_w < HUI_PROF_CONSOLE_TARGET_WIDTH) pixel_w = HUI_PROF_CONSOLE_TARGET_WIDTH;
    if (pixel_h < HUI_PROF_CONSOLE_TARGET_HEIGHT) pixel_h = HUI_PROF_CONSOLE_TARGET_HEIGHT;

    prof->console_cols = target_cols;
    prof->console_rows = target_rows;
    prof->console_px_w = pixel_w;
    prof->console_px_h = pixel_h;

    hui_profiler_position_console(prof, pixel_w, pixel_h);
    ShowWindow(GetConsoleWindow(), SW_SHOW);
}
#endif

static void hui_profiler_console_clear(struct hui_profiler *prof) {
    if (!prof) return;
#if defined(_WIN32)
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (console == INVALID_HANDLE_VALUE) {
        fputs("\033[2J\033[H", stdout);
        return;
    }
    if (prof->ansi_supported) {
        fputs("\033[2J\033[H", stdout);
        return;
    }
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(console, &info)) return;
    DWORD cells = (DWORD) info.dwSize.X * (DWORD) info.dwSize.Y;
    COORD home = {0, 0};
    DWORD written = 0;
    FillConsoleOutputCharacter(console, ' ', cells, home, &written);
    FillConsoleOutputAttribute(console, info.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(console, home);
#else
    fputs("\033[2J\033[H", stdout);
#endif
}

static void hui_profiler_setup_console(struct hui_profiler *prof) {
    if (!prof || prof->console_ready) return;
    uint64_t guard = hui_profiler_overhead_begin(prof);
#if defined(_WIN32)
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (AllocConsole()) {
            prof->console_attached = 1;
        }
    }

    FILE *dummy = freopen("CONOUT$", "w", stdout);
    (void) dummy;
    dummy = freopen("CONOUT$", "w", stderr);
    (void) dummy;

    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (console != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(console, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (SetConsoleMode(console, mode)) {
                prof->ansi_supported = 1;
            }
        }
        hui_profiler_configure_console(prof, console);
    }
    if (!prof->ansi_supported) {
        prof->ansi_supported = 0;
    }
    SetConsoleTitleA("hUI Profiler");
#else
    prof->ansi_supported = 1;
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    prof->console_ready = 1;
    hui_profiler_overhead_end(prof, guard);
}

static void hui_profiler_print(struct hui_profiler *prof) {
    if (!prof || !prof->console_ready) return;
    const hui_profiler_frame_data *frame = &prof->last;
    char dirty_step[64];
    char dirty_render_pending[64];
    char dirty_render_after[64];
    hui_profiler_format_dirty(frame->step_dirty, dirty_step, sizeof(dirty_step));
    hui_profiler_format_dirty(frame->render_pending_dirty, dirty_render_pending, sizeof(dirty_render_pending));
    hui_profiler_format_dirty(frame->render_dirty_after, dirty_render_after, sizeof(dirty_render_after));

    hui_profiler_console_clear(prof);

    double fps = (prof->last_cpu_ms > 0.0) ? (1000.0 / prof->last_cpu_ms) : 0.0;
    printf("hUI Profiler - Frame %" PRIu64 "\n", prof->frame_index);
    printf(" Frame CPU: %7.3f ms   Avg: %7.3f   Min: %7.3f   Max: %7.3f   FPS: %6.1f\n",
           prof->last_cpu_ms,
           prof->avg_frame_ms,
           prof->min_frame_ms,
           prof->max_frame_ms,
           fps);
    printf(" Frame dt:  %7.3f ms   DOM nodes: %zu   Draw cmds: %zu   Bindings: %zu\n",
           frame->dt_ms,
           frame->dom_nodes,
           frame->draw_cmds,
           frame->bindings);
    printf(" Auto fields: text=%zu select=%zu   Dirty(step)=%s   Dirty(render)=%s -> %s\n",
           frame->auto_text_fields,
           frame->auto_select_fields,
           dirty_step,
           dirty_render_pending,
           dirty_render_after);
    printf(" Input events: %zu   Text input: %zu   Key press: %zu   Key release: %zu   Draw changed: %s\n\n",
           frame->input_events,
           frame->text_input,
           frame->key_pressed,
           frame->key_released,
           frame->draw_changed ? "yes" : "no");

    printf(" %-24s %10s %10s %10s %10s\n", "Stage", "Last", "Average", "Min", "Max");
    printf(" %-24s %10s %10s %10s %10s\n", "------------------------", "----------", "----------", "----------", "----------");
    for (size_t i = 0; i < HUI_PROF_STAGE__COUNT; i++) {
        const hui_profiler_stage_stats *stats = &prof->stages[i];
        if (stats->count == 0) {
            printf(" %-24s %10.3f %10.3f %10.3f %10.3f\n",
                   HUI_PROF_STAGE_NAMES[i],
                   0.0, 0.0, 0.0, 0.0);
        } else {
            printf(" %-24s %10.3f %10.3f %10.3f %10.3f\n",
                   HUI_PROF_STAGE_NAMES[i],
                   stats->last_ms,
                   stats->avg_ms,
                   stats->min_ms,
                   stats->max_ms);
        }
    }
    fflush(stdout);
}

static void hui_profiler_finalize_frame(struct hui_profiler *prof, double frame_ms) {
    uint64_t guard = hui_profiler_overhead_begin(prof);
    prof->frame_index++;
    prof->last_cpu_ms = frame_ms;
    if (prof->frame_index == 1) {
        prof->avg_frame_ms = frame_ms;
        prof->min_frame_ms = frame_ms;
        prof->max_frame_ms = frame_ms;
    } else {
        prof->avg_frame_ms += (frame_ms - prof->avg_frame_ms) / (double) prof->frame_index;
        if (frame_ms < prof->min_frame_ms) prof->min_frame_ms = frame_ms;
        if (frame_ms > prof->max_frame_ms) prof->max_frame_ms = frame_ms;
    }

    for (size_t i = 0; i < HUI_PROF_STAGE__COUNT; i++) {
        if (!prof->frame_stage_active[i]) continue;
        hui_profiler_stage_stats_update(&prof->stages[i], prof->frame_stage_accum[i]);
    }

    prof->last = prof->current;
    hui_profiler_print(prof);

    memset(prof->frame_stage_accum, 0, sizeof(prof->frame_stage_accum));
    memset(prof->frame_stage_active, 0, sizeof(prof->frame_stage_active));
    hui_profiler_overhead_end(prof, guard);
}

struct hui_profiler *hui_profiler_enable(void * (*alloc_fn)(size_t),
                                         void (*free_fn)(void *),
                                         void *native_window_handle) {
    void * (*afn)(size_t) = alloc_fn ? alloc_fn : malloc;
    void (*ffn)(void *) = free_fn ? free_fn : free;
    struct hui_profiler *prof = (struct hui_profiler *) afn(sizeof(struct hui_profiler));
    if (!prof) return NULL;
    memset(prof, 0, sizeof(*prof));
    prof->free_fn = ffn;
    prof->native_window = native_window_handle;
    prof->tick_to_ms = hui_profiler_tick_to_ms_factor();
    if (prof->tick_to_ms <= 0.0) prof->tick_to_ms = 0.001;
    prof->min_frame_ms = DBL_MAX;
    hui_profiler_setup_console(prof);
    return prof;
}

void hui_profiler_disable(struct hui_profiler *profiler, void (*free_fn)(void *)) {
    if (!profiler) return;
#if defined(_WIN32)
    if (profiler->console_attached) {
        FreeConsole();
    }
#endif
    void (*ffn)(void *) = free_fn ? free_fn : profiler->free_fn;
    if (!ffn) ffn = free;
    ffn(profiler);
}

void hui_profiler_set_window(struct hui_profiler *profiler, void *native_window_handle) {
    if (!profiler) return;
    profiler->native_window = native_window_handle;
#if defined(_WIN32)
    if (profiler->console_ready) {
        int w = profiler->console_px_w > 0 ? profiler->console_px_w : HUI_PROF_CONSOLE_TARGET_WIDTH;
        int h = profiler->console_px_h > 0 ? profiler->console_px_h : HUI_PROF_CONSOLE_TARGET_HEIGHT;
        hui_profiler_position_console(profiler, w, h);
    }
#endif
}

void hui_profiler_frame_tick(struct hui_profiler *profiler) {
    if (!profiler) return;
    hui_profiler_setup_console(profiler);
    uint64_t now = hui_profiler_now_ticks();
    if (profiler->frame_started) {
        double frame_ms = (double) (now - profiler->frame_start_ticks) * profiler->tick_to_ms;
        double overhead_ms = (double) profiler->overhead_ticks * profiler->tick_to_ms;
        if (overhead_ms > frame_ms) overhead_ms = frame_ms;
        profiler->overhead_ticks = 0;
        hui_profiler_finalize_frame(profiler, frame_ms - overhead_ms);
    } else {
        profiler->frame_started = 1;
        profiler->overhead_ticks = 0;
    }
    profiler->frame_start_ticks = now;

    uint64_t guard = hui_profiler_overhead_begin(profiler);
    hui_profiler_frame_data baseline = profiler->last;
    baseline.step_dirty = 0;
    baseline.render_pending_dirty = 0;
    baseline.render_dirty_after = 0;
    baseline.draw_changed = 0;
    baseline.dt_ms = 0.0;
    baseline.input_events = 0;
    baseline.text_input = 0;
    baseline.key_pressed = 0;
    baseline.key_released = 0;
    profiler->current = baseline;
    hui_profiler_overhead_end(profiler, guard);
}

uint64_t hui_profiler_stage_begin(struct hui_profiler *profiler, hui_profiler_stage stage) {
    (void) stage;
    if (!profiler) return 0;
    uint64_t guard = hui_profiler_overhead_begin(profiler);
    uint64_t token = hui_profiler_now_ticks();
    hui_profiler_overhead_end(profiler, guard);
    return token;
}

void hui_profiler_stage_end(struct hui_profiler *profiler, hui_profiler_stage stage, uint64_t token) {
    if (!profiler || token == 0) return;
    uint64_t guard = hui_profiler_overhead_begin(profiler);
    uint64_t end = hui_profiler_now_ticks();
    double ms = (double) (end - token) * profiler->tick_to_ms;
    if (ms < 0.0) ms = 0.0;
    if (stage < HUI_PROF_STAGE__COUNT) {
        profiler->frame_stage_accum[stage] += ms;
        profiler->frame_stage_active[stage] = 1;
    }
    hui_profiler_overhead_end(profiler, guard);
}

void hui_profiler_capture_step(struct hui_profiler *profiler,
                               uint32_t step_dirty,
                               float dt_ms,
                               size_t input_events,
                               size_t text_input,
                               size_t key_pressed,
                               size_t key_released) {
    if (!profiler) return;
    uint64_t guard = hui_profiler_overhead_begin(profiler);
    profiler->current.step_dirty |= step_dirty;
    profiler->current.dt_ms = (double) dt_ms;
    profiler->current.input_events = input_events;
    profiler->current.text_input = text_input;
    profiler->current.key_pressed = key_pressed;
    profiler->current.key_released = key_released;
    hui_profiler_overhead_end(profiler, guard);
}

void hui_profiler_capture_render(struct hui_profiler *profiler,
                                 size_t dom_nodes,
                                 size_t draw_cmds,
                                 size_t auto_text_fields,
                                 size_t auto_select_fields,
                                 size_t bindings,
                                 uint32_t pending_dirty,
                                 uint32_t dirty_after,
                                 int draw_changed) {
    if (!profiler) return;
    uint64_t guard = hui_profiler_overhead_begin(profiler);
    profiler->current.dom_nodes = dom_nodes;
    profiler->current.draw_cmds = draw_cmds;
    profiler->current.auto_text_fields = auto_text_fields;
    profiler->current.auto_select_fields = auto_select_fields;
    profiler->current.bindings = bindings;
    profiler->current.render_pending_dirty |= pending_dirty;
    profiler->current.render_dirty_after = dirty_after;
    profiler->current.draw_changed = draw_changed;
    hui_profiler_overhead_end(profiler, guard);
}
