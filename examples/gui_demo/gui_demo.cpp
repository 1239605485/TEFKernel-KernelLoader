#include "mod_core.h"
#include "mod_gui.h"
#include <cstdio>
static const kl_gui_api *gui=nullptr;
static void *owner=nullptr;
static int clicks=0,checked=1,light=0;
static float value=50;
static char input[256]="";
static void on_gui(const kl_gui_frame *f,void*){
    auto r=f->window;float x=r.x,y=r.y;
    const kl_gui_color text=light?kl_gui_color{25,35,48,255}:kl_gui_color{240,245,250,255};
    gui->drag_window(f,38);
    r=f->window;x=r.x;y=r.y;
    gui->panel(f);
    gui->text(f,x+12,y+7,"原生 GUI 功能测试",0.8f,text);
    if(gui->button(f,"collapse",gui->is_collapsed(owner)?"展开":"收起",x+r.w-82,y+3,72,32))gui->set_collapsed(owner,!gui->is_collapsed(owner));
    if(gui->is_collapsed(owner))return;
    char label[96];std::snprintf(label,sizeof(label),"缩放 %.0f%%   按钮次数 %d",gui->get_window_scale(owner)*100,clicks);
    gui->text(f,x+12,y+52,label,0.8f,text);
    float next_scale=gui->get_window_scale(owner);
    if(gui->button(f,"smaller","缩小",x+12,y+88,100,34))next_scale-=0.1f;
    if(gui->button(f,"larger","放大",x+122,y+88,100,34))next_scale+=0.1f;
    if(gui->button(f,"theme",light?"暗色":"浅色",x+232,y+88,100,34)){
        light=!light;
        gui->set_style(owner,light?kl_gui_color{224,232,242,255}:kl_gui_color{22,30,44,245},
                       light?kl_gui_color{190,205,223,255}:kl_gui_color{40,57,82,255},
                       light?kl_gui_color{125,180,215,255}:kl_gui_color{68,120,160,255},
                       light?kl_gui_color{25,35,48,255}:kl_gui_color{240,245,250,255});
    }
    if(gui->button(f,"count","点击 +1",x+12,y+136,150,36))++clicks;
    checked=gui->checkbox(f,"check","复选框",checked,x+184,y+138,160,30);
    value=gui->slider(f,"value",value,0,100,x+12,y+186,300,24);
    gui->progress_bar(f,value/100,x+12,y+222,300,14);
    gui->input_text(f,"chinese",input,sizeof(input),"点击输入中文（内置拼音键盘）",x+12,y+250,r.w-24,40,64);
    if(gui->image_button(f,"icon","gui_test.png",{x+r.w-140,y+132,118,104}))++clicks;
    gui->begin_scroll(f,"list",{x+12,y+306,r.w-24,164},540);
    if(gui->collapsing_header(f,"details","展开更多测试",x+18,y+312,r.w-60,34))
        gui->text_wrapped(f,x+22,y+354,r.w-70,"支持中文输入、整体缩放、触摸拖动、滚动裁剪和明暗换肤。",0.8f,text);
    for(int i=0;i<10;++i){std::snprintf(label,sizeof(label),"滚动列表 %d：点击测试",i+1);
        if(gui->button(f,label,label,x+18,y+410+i*40,r.w-60,34))++clicks;}
    gui->end_scroll(f);
    gui->set_window_scale(owner,next_scale);
}
#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
EXPORT void kernel_mod_set_gui(const kl_gui_api *api,void *token){
    if(api&&api->version==KL_GUI_API_VERSION&&api->size>=sizeof(kl_gui_api)){gui=api;owner=token;}
}
static void init(kernel_mod_handle_t*){if(gui){gui->register_window(owner,on_gui,nullptr,{32,32,520,490});gui->set_window_scale(owner,0.75f);}}
static void cleanup(kernel_mod_handle_t*){if(gui)gui->unregister_window(owner);gui=nullptr;owner=nullptr;}
static kernel_mod_info_t *info(){static kernel_mod_info_t value={"liuxin.native.gui.test",2,1,"1.1.0"};return &value;}
EXPORT kernel_mod_ops_t *create_kernel_mod(){static kernel_mod_ops_t ops={init,cleanup,info};return &ops;}
