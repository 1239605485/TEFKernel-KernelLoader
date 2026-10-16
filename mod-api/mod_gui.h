/* Native GUI API v1. Call draw functions only in on_gui.
 * Coordinates are logical pixels anchored at frame.window.x/y. Window scale
 * transforms drawing, clipping and input together; frame.mouse_x/y are local
 * logical coordinates in the same space.
 * Loader injects this table through optional kernel_mod_set_gui before init_mod.
 * Old kernel_mod_ops_t layout is unchanged. No linking against loader is required.
 * Register one window per mod. owner is supplied by loader, never fabricate it.
 * GUI callbacks and mod unloading are serialized. Do not unload a mod from on_gui.
 * Images are PNG paths relative to the mod private directory, loaded lazily.
 */
#ifndef KERNELLOADER_MOD_GUI_H
#define KERNELLOADER_MOD_GUI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KL_GUI_API_VERSION 1u
typedef struct kl_gui_rect { float x,y,w,h; } kl_gui_rect;
typedef struct kl_gui_color { uint8_t r,g,b,a; } kl_gui_color;
typedef struct kl_gui_frame {
    uint32_t size;
    int screen_width,screen_height;
    float scale,mouse_x,mouse_y;
    int down,pressed,released,wheel;
    kl_gui_rect window;
    float window_scale;
} kl_gui_frame;
typedef void (*kl_gui_callback)(const kl_gui_frame *frame,void *userdata);
enum kl_gui_input_flags {
    KL_GUI_INPUT_CHANGED=1, KL_GUI_INPUT_DONE=2, KL_GUI_INPUT_CANCELED=4,
    KL_GUI_INPUT_UNAVAILABLE=8, KL_GUI_INPUT_ERROR=16, KL_GUI_INPUT_EDITING=32
};
typedef struct kl_gui_api {
    uint32_t version,size;
    int (*register_window)(void *owner,kl_gui_callback callback,void *userdata,kl_gui_rect bounds);
    void (*set_visible)(void *owner,int visible);
    void (*unregister_window)(void *owner);
    int (*available)(void);
    void (*rect)(const kl_gui_frame*,float x,float y,float w,float h,kl_gui_color);
    void (*text)(const kl_gui_frame*,float x,float y,const char *utf8,float scale,kl_gui_color);
    int (*button)(const kl_gui_frame*,const char *id,const char *label,float x,float y,float w,float h);
    float (*slider)(const kl_gui_frame*,const char *id,float value,float min,float max,float x,float y,float w,float h);
    void (*image)(const kl_gui_frame*,const char *path,float x,float y,float w,float h,
                  int sx,int sy,int sw,int sh,kl_gui_color);
    void (*drag_window)(const kl_gui_frame*,float title_height);
    /* Appended extension: UTF-8 buffer capacity includes its terminating zero.
     * Uses the built-in offline pinyin keyboard; no system IME calls.
     * Returns kl_gui_input_flags. Buffer remains owned by the calling mod.
     */
    int (*input_text)(const kl_gui_frame*,const char *id,char *utf8,uint32_t capacity,
                      const char *placeholder,float x,float y,float w,float h,int char_limit);
    void (*set_window_scale)(void *owner,float scale); /* 0.5 to 1.5 */
    float (*get_window_scale)(void *owner);
    void (*set_bounds)(void *owner,kl_gui_rect bounds);
    void (*set_collapsed)(void *owner,int collapsed);
    int (*is_collapsed)(void *owner);
    void (*set_style)(void *owner,kl_gui_color panel,kl_gui_color button,kl_gui_color hover,kl_gui_color text);
    void (*panel)(const kl_gui_frame*);
    int (*checkbox)(const kl_gui_frame*,const char *id,const char *label,int value,float x,float y,float w,float h);
    void (*progress_bar)(const kl_gui_frame*,float value,float x,float y,float w,float h);
    int (*collapsing_header)(const kl_gui_frame*,const char *id,const char *label,float x,float y,float w,float h);
    /* Content uses viewport-based coordinates. The scroll scope automatically
     * offsets drawing and input; callers must not subtract the returned offset. */
    float (*begin_scroll)(const kl_gui_frame*,const char *id,kl_gui_rect viewport,float content_height);
    void (*end_scroll)(const kl_gui_frame*);
    void (*scroll_to)(void *owner,const char *id,float offset);
    void (*text_wrapped)(const kl_gui_frame*,float x,float y,float width,const char *utf8,float scale,kl_gui_color);
    void (*nine_slice)(const kl_gui_frame*,const char *path,kl_gui_rect rect,int border,float edge,kl_gui_color);
    void (*image_fit)(const kl_gui_frame*,const char *path,kl_gui_rect rect,kl_gui_color);
    int (*image_button)(const kl_gui_frame*,const char *id,const char *path,kl_gui_rect rect);
    int (*image_status)(void *owner,const char *path,int *width,int *height);
    void (*release_image)(void *owner,const char *path);
    void (*image_cache_stats)(uint64_t *used,uint64_t *limit,uint64_t *entries,uint64_t *evictions);
} kl_gui_api;
/* Export this OPTIONAL symbol in the mod (use dllexport on Windows):
 * void kernel_mod_set_gui(const kl_gui_api *api,void *owner);
 * Check version and size before calling the table. GUI may be unavailable in a
 * dedicated server or unsupported build; available() reports hook/backend status.
 */
#ifdef __cplusplus
}
#endif
#endif
