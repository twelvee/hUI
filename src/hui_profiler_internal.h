#ifndef HUI_PROFILER_INTERNAL_H
#define HUI_PROFILER_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

struct hui_profiler;

typedef enum {
    HUI_PROF_STAGE_STEP_TOTAL = 0,
    HUI_PROF_STAGE_STEP_INPUT,
    HUI_PROF_STAGE_STEP_AUTO_TEXT,
    HUI_PROF_STAGE_STEP_AUTO_SELECT,
    HUI_PROF_STAGE_STEP_BINDINGS,
    HUI_PROF_STAGE_RENDER_TOTAL,
    HUI_PROF_STAGE_RENDER_STYLE,
    HUI_PROF_STAGE_RENDER_LAYOUT,
    HUI_PROF_STAGE_RENDER_PAINT,
    HUI_PROF_STAGE__COUNT
} hui_profiler_stage;

struct hui_profiler *hui_profiler_enable(void * (*alloc_fn)(size_t),
                                         void (*free_fn)(void *),
                                         void *native_window_handle);

void hui_profiler_disable(struct hui_profiler *profiler, void (*free_fn)(void *));

void hui_profiler_set_window(struct hui_profiler *profiler, void *native_window_handle);

void hui_profiler_frame_tick(struct hui_profiler *profiler);

uint64_t hui_profiler_stage_begin(struct hui_profiler *profiler, hui_profiler_stage stage);

void hui_profiler_stage_end(struct hui_profiler *profiler, hui_profiler_stage stage, uint64_t token);

void hui_profiler_capture_step(struct hui_profiler *profiler,
                               uint32_t step_dirty,
                               float dt_ms,
                               size_t input_events,
                               size_t text_input,
                               size_t key_pressed,
                               size_t key_released);

void hui_profiler_capture_render(struct hui_profiler *profiler,
                                 size_t dom_nodes,
                                 size_t draw_cmds,
                                 size_t auto_text_fields,
                                 size_t auto_select_fields,
                                 size_t bindings,
                                 uint32_t pending_dirty,
                                 uint32_t dirty_after,
                                 int draw_changed);

#endif
