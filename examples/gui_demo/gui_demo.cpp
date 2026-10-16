#include "mod_core.h"
#include "mod_gui.h"
#include <cstdio>
static const kl_gui_api *gui=nullptr;
static void *owner=nullptr;
static int clicks=0;
static float value=50;
static bool compact=false;
static void on_gui(const kl_gui_frame *f,void*){
    gui->drag_window(f,38);
    const auto r=f->window;float x=r.x,y=r.y;
    gui->rect(f,x,y,r.w,r.h,{22,30,44,245});
    gui->rect(f,x,y,r.w,38,{45,66,94,255});
    gui->text(f,x+12,y+7,"原生 GUI 测试 / 拖动标题栏",0.8f,{255,255,255,255});
    if(gui->button(f,"toggle",compact?"展开测试":"收起内容",x+12,y+48,150,34))compact=!compact;
    if(compact)return;
    char label[96];std::snprintf(label,sizeof(label),"按钮次数: %d  滑块: %.0f",clicks,value);
    gui->text(f,x+12,y+94,label,0.85f,{245,245,245,255});
    if(gui->button(f,"count","点击 +1",x+12,y+134,150,38))++clicks;
    value=gui->slider(f,"value",value,0,100,x+12,y+190,270,24);
    gui->image(f,"gui_test.png",x+310,y+58,96,96,0,0,0,0,{255,255,255,255});
    gui->text(f,x+12,y+236,"检查：图片、中文、拖动、点击穿透",0.75f,{180,220,245,255});
}
#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
EXPORT void kernel_mod_set_gui(const kl_gui_api *api,void *token){
    if(api&&api->version==KL_GUI_API_VERSION&&api->size>=sizeof(kl_gui_api)){gui=api;owner=token;}
}
static void init(kernel_mod_handle_t*){if(gui)gui->register_window(owner,on_gui,nullptr,{36,90,440,290});}
static void cleanup(kernel_mod_handle_t*){if(gui)gui->unregister_window(owner);gui=nullptr;owner=nullptr;}
static kernel_mod_info_t *info(){static kernel_mod_info_t value={"liuxin.native.gui.test",1,1,"1.0.0"};return &value;}
EXPORT kernel_mod_ops_t *create_kernel_mod(){static kernel_mod_ops_t ops={init,cleanup,info};return &ops;}
