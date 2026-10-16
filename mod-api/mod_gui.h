/* Native GUI API v1. Coordinates are screen pixels; call draw functions only in on_gui.
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
} kl_gui_frame;
typedef void (*kl_gui_callback)(const kl_gui_frame *frame,void *userdata);
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
