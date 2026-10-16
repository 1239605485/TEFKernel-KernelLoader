// Host-side runtime tests with mocked game fields. No game/Android claim is made.
#include "../src/gui.cpp"
#include <cassert>
#include <thread>
#include <future>
#include <atomic>
using namespace kernelloader::gui;
static int mx=20,my=20;static unsigned char down=0;
static void get_field(patch_handle_t field,patch_handle_t,void *value){
    switch(reinterpret_cast<uintptr_t>(field)){
    case 1:*static_cast<void**>(value)=reinterpret_cast<void*>(10);break;
    case 2:*static_cast<int*>(value)=mx;break;
    case 3:*static_cast<int*>(value)=my;break;
    case 4:*static_cast<unsigned char*>(value)=down;break;
    }
}
static void set_field(patch_handle_t field,patch_handle_t,void *value){if(reinterpret_cast<uintptr_t>(field)==4)down=*static_cast<unsigned char*>(value);}
static int a_clicks=0,b_clicks=0,a_frames=0,b_frames=0;
static void a(const kl_gui_frame *f,void*){assert(!detach(reinterpret_cast<void*>(100)));++a_frames;a_clicks+=api()->button(f,"button","A",10,10,80,40);}
static void b(const kl_gui_frame *f,void*){++b_frames;b_clicks+=api()->button(f,"button","B",10,10,80,40);}
static void frame(){frame_postfix(nullptr,nullptr,nullptr,nullptr);}
static std::promise<void> started,proceed;
static std::shared_future<void> gate;
static void blocking(const kl_gui_frame*,void*){started.set_value();gate.wait();}
int main(){
    patchlib_field_get_value=get_field;patchlib_field_set_value=set_field;
    g_gfx.ready=g_gfx.usable=true;g_gfx.f_sprite_batch=reinterpret_cast<void*>(1);
    g_gfx.a_mouse_x.field=reinterpret_cast<void*>(2);g_gfx.a_mouse_y.field=reinterpret_cast<void*>(3);g_gfx.a_mouse_left.field=reinterpret_cast<void*>(4);
    void *oa=reinterpret_cast<void*>(100),*ob=reinterpret_cast<void*>(101);
    assert(!api()->register_window(oa,a,nullptr,{0,0,100,80}));
    attach(oa,"a",".");attach(ob,"b",".");
    assert(api()->register_window(oa,a,nullptr,{0,0,100,80}));assert(api()->register_window(ob,b,nullptr,{0,0,100,80}));
    down=1;frame();down=0;frame();assert(a_clicks==0&&b_clicks==1); // only top window
    down=1;frame();mx=120;down=0;frame();assert(b_clicks==1); // drag outside cancels
    api()->set_visible(ob,0);mx=20;
    down=1;update_prefix(nullptr,nullptr,nullptr,nullptr);assert(!down);frame();
    down=0;update_prefix(nullptr,nullptr,nullptr,nullptr);frame();assert(a_clicks==1); // suppression preserves GUI input
    auto prior=a_frames;detach(oa);frame();assert(a_frames==prior);
    api()->unregister_window(ob);auto old=b_frames;api()->set_visible(ob,1);frame();assert(b_frames==old);
    gate=proceed.get_future().share();api()->register_window(ob,blocking,nullptr,{0,0,100,80});
    std::thread drawing([]{frame();});started.get_future().wait();
    std::promise<void> attempting,finished;
    auto finished_future=finished.get_future();
    std::thread unloading([&]{attempting.set_value();detach(ob);finished.set_value();});
    attempting.get_future().wait();
    assert(finished_future.wait_for(std::chrono::milliseconds(25))==std::future_status::timeout);
    proceed.set_value();drawing.join();unloading.join();finished_future.get();
    frame();assert(g_entries.empty());shutdown();
}
