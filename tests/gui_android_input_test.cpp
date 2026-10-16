// Execute the Android-only input/projection code on the host with fake game APIs.
#ifndef __ANDROID__
#define __ANDROID__ 1
#endif
#include "../src/gui.cpp"
#include <cassert>
#include <stdexcept>
namespace kernelloader::gui::images {
std::string request(const std::string&,const std::string&,const std::string&){return {};}
std::string status(const std::string&,int&,int&){return {};}
void draw(void*,const std::string&,float,float,float,float,const void*,int,int,int,int,int,float){}
void release(const std::string&){}
void prune(const std::vector<std::string>&){}
void solid(void*,float,float,float,float,const void*){}
unsigned int retain_object(void*){return 0;}
void release_object(unsigned int){}
void begin_frame(){}
void set_clip(bool,float,float,float,float){}
void set_budget(size_t){}
void stats(size_t&,size_t&,size_t&,uint64_t&){}
void clear(){}
}
using namespace kernelloader::gui;
static int cursors=1,cursor_id=7,clicks=0,ui_enter=0,ui_exit=0;
static bool ui_active=false,throw_begin=false,input_mode=false;
static int input_flags=0;
static char input[64]="";
static unsigned char down=1,ignore=0,ignore_world=0;
static Vec2 raw{124,84}; // UI (20,20), with a simulated scale and safe-area offset.
static void *batch=reinterpret_cast<void*>(30),*manager=reinterpret_cast<void*>(31);
static int count(void*){return cursors;}
static void *cursor(void*,int){return &cursor_id;}
static Vec2 adjust(Vec2 p){return {(p.x-100)/1.2f,(p.y-60)/1.2f};}
static void *field(patch_handle_t f,patch_handle_t){
    switch(reinterpret_cast<uintptr_t>(f)){
    case 1:return &batch;case 10:return &manager;case 11:return &raw;
    case 12:return &down;case 13:return &cursor_id;case 14:return &ignore;case 15:return &ignore_world;
    }return nullptr;
}
static void *method(patch_handle_t m){
    switch(reinterpret_cast<uintptr_t>(m)){
    case 100:return reinterpret_cast<void*>(count);
    case 101:return reinterpret_cast<void*>(cursor);
    case 102:return reinterpret_cast<void*>(adjust);
    }return nullptr;
}
static bool invoke(patch_handle_t m,patch_handle_t,void *result,void **args){
    auto id=reinterpret_cast<uintptr_t>(m);
    if(id==51){ui_active=*static_cast<unsigned char*>(args[0])!=0;if(ui_active)++ui_enter;else ++ui_exit;}
    if(id==52||id==53){assert(ui_active);if(throw_begin&&id==52)throw std::runtime_error("fake Begin failure");}
    return true;
}
static void callback(const kl_gui_frame *f,void*){
    assert(ui_active);
    if(input_mode)input_flags=api()->input_text(f,"search",input,sizeof(input),"中文",10,10,80,40,20);
    else clicks+=api()->button(f,"button","test",10,10,80,40);
}
static void frame(){frame_postfix(nullptr,nullptr,nullptr,nullptr);assert(!ui_active);}
int main(){
    patchlib_field_get_pointer=field;patchlib_method_get_pointer=method;patchlib_method_invoke_args=invoke;
    auto h=[](uintptr_t n){return reinterpret_cast<patch_handle_t>(n);};
    g_gfx.ready=g_gfx.usable=true;g_gfx.f_sprite_batch=h(1);
    g_gfx.f_cm_instance=h(10);g_gfx.f_cursor_pos=h(11);g_gfx.f_cursor_down=h(12);g_gfx.f_cursor_id=h(13);
    g_gfx.f_cursor_ignore=h(14);g_gfx.f_cursor_ignore_world=h(15);
    g_gfx.m_cm_get_num_cursors=h(100);g_gfx.m_cm_get_cursor=h(101);g_gfx.m_adjust_real_screen=h(102);
    g_gfx.m_setup_rendering_ui=h(51);g_gfx.m_sb_begin=h(52);g_gfx.m_sb_end=h(53);
    auto owner=h(200);attach(owner,"test",".");api()->register_window(owner,callback,nullptr,{0,0,100,80});
    cursor_list_postfix(nullptr,nullptr,nullptr,nullptr);assert(ignore&&ignore_world);
    frame();assert(g_mouse_pressed&&clicks==0);
    cursors=0; // Finger removed before Draw: still emit release at last position.
    frame();assert(g_mouse_released&&clicks==1);
    frame();assert(clicks==1&&!g_mouse_released);
    cursors=1;down=1;cursor_list_postfix(nullptr,nullptr,nullptr,nullptr);frame();
    raw={244,84}; // Drag outside before release must cancel the armed button.
    frame();cursors=0;frame();assert(clicks==1);
    assert(ui_enter==ui_exit&&ui_enter>=6);
    input_mode=true;raw={124,84};cursors=1;down=1;
    cursor_list_postfix(nullptr,nullptr,nullptr,nullptr);frame();cursors=0;frame();
    assert((input_flags&KL_GUI_INPUT_EDITING)&&g_editor.active);
    const auto kb=keyboard_bounds();const float ks=kb.h/330.f,kw=(kb.w-20*ks)/10;
    auto tap=[&](float x,float y){raw={100+x*1.2f,60+y*1.2f};cursors=1;down=1;cursor_list_postfix(nullptr,nullptr,nullptr,nullptr);frame();cursors=0;cursor_list_postfix(nullptr,nullptr,nullptr,nullptr);frame();};
    auto letter=[&](int row,int index,int length){tap(kb.x+10*ks+(10-length)*kw/2+(index+0.5f)*kw,kb.y+(104+44*row+19)*ks);};
    letter(2,5,7);letter(0,7,10);letter(1,5,9);letter(1,0,9);letter(0,8,10);
    assert(g_editor.composition=="nihao");
    const float cw=(kb.w-112*ks)/6;tap(kb.x+10*ks+cw/2,kb.y+78*ks);frame();
    assert(std::string(input)=="你好"&&(input_flags&KL_GUI_INPUT_CHANGED));
    const float fw=(kb.w-20*ks)/5;tap(kb.x+10*ks+4.5f*fw,kb.y+260*ks);frame();assert(input_flags&KL_GUI_INPUT_DONE);
    frame();assert(!(input_flags&KL_GUI_INPUT_DONE));
    // Restore the runner projection even if the SpriteBatch raises an exception.
    throw_begin=true;frame();assert(!ui_active&&ui_enter==ui_exit);
    detach(owner);shutdown();
}
