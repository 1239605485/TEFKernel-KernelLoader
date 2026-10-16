/*******************************************************************************
 * LuaLoader - gui (native Terraria GUI)
 * Copyright (C) 2026 lzup333
 * Author: lzup333
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *******************************************************************************/

// Rendering and touch backend adapted from LuaLoader; native API has no Lua dependency.
#include "gui.hpp"
#include "gui_images.hpp"
#include "gui_viewport.hpp"
#include "gui_click_state.hpp"
#include "logger.hpp"
#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/property.h"
#include "tefkernel/patchlib/struct/string.h"
#include "tefkernel/patchlib/type.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
namespace kernelloader::gui {
namespace {
        struct Vec2 {
            float x, y;
        };

        // Microsoft.Xna.Framework.Rectangle
        struct Recti {
            int x, y, w, h;
        };

        // System.Nullable<Matrix>（用于强制 SpriteBatch.Begin 使用单位矩阵）
        // .NET 布局：{ bool hasValue; T value; }（value 按 T 对齐）
        struct Mat4 {
            float m[16];
        };
        struct NullableMat {
            unsigned char has_value;
            unsigned char _pad[3];
            Mat4 value;
        };

        // Microsoft.Xna.Framework.Color 的内存布局因平台而异：
        //   - PE(Unity) 打包为 4 字节 A,B,G,R
        //   - PC(XNA/FNA) 为 R,G,B,A + packedValue（8 字节）
#if defined(__ANDROID__)
        struct Color {
            unsigned char a, b, g, r;
        };
#else
        struct Color {
            unsigned char r, g, b, a;
            unsigned int packed;
        };
#endif

        void make_color(Color &c, int r, int g, int b, int a) {
#if defined(__ANDROID__)
            c.a = static_cast<unsigned char>(a);
            c.b = static_cast<unsigned char>(b);
            c.g = static_cast<unsigned char>(g);
            c.r = static_cast<unsigned char>(r);
#else
            c.r = static_cast<unsigned char>(r);
            c.g = static_cast<unsigned char>(g);
            c.b = static_cast<unsigned char>(b);
            c.a = static_cast<unsigned char>(a);
            c.packed = static_cast<unsigned>(r) | (static_cast<unsigned>(g) << 8) |
                       (static_cast<unsigned>(b) << 16) | (static_cast<unsigned>(a) << 24);
#endif
        }

        // ====================================================================
        // 反射辅助
        // ====================================================================

        /// 一个字段或（属性）访问器。
        struct Accessor {
            patch_handle_t field{nullptr};
            patch_handle_t getter{nullptr};
            patch_handle_t setter{nullptr};

            [[nodiscard]] bool valid() const { return field != nullptr || getter != nullptr; }
        };

        Accessor resolve_accessor(const patch_handle_t type, const char *name) {
            Accessor a;
            if (!type || !name) return a;
            a.field = patchlib_type_get_field(type, name);
            if (!a.field || !patchlib_is_valid(a.field)) {
                a.field = nullptr;
                const patch_handle_t prop = patchlib_type_get_property(type, name);
                if (prop && patchlib_is_valid(prop)) {
                    a.getter = patchlib_property_get_get_method(prop);
                    a.setter = patchlib_property_get_set_method(prop);
                }
            }
            return a;
        }

        int read_int(const Accessor &a, const int fallback = 0) {
            if (a.field) {
#if defined(__ANDROID__)
                void *ptr = patchlib_field_get_pointer(a.field, nullptr);
                if (ptr) return *static_cast<int *>(ptr);
#else
                int v = 0;
                patchlib_field_get_value(a.field, nullptr, &v);
                return v;
#endif
            }
            if (a.getter) {
                int v = 0;
                patchlib_method_invoke_args(a.getter, nullptr, &v, nullptr);
                return v;
            }
            return fallback;
        }

        bool read_bool(const Accessor &a, const bool fallback = false) {
            if (a.field) {
#if defined(__ANDROID__)
                void *ptr = patchlib_field_get_pointer(a.field, nullptr);
                if (ptr) return *static_cast<unsigned char *>(ptr) != 0;
#else
                unsigned char v = 0;
                patchlib_field_get_value(a.field, nullptr, &v);
                return v != 0;
#endif
            }
            if (a.getter) {
                bool v = false;
                patchlib_method_invoke_args(a.getter, nullptr, &v, nullptr);
                return v;
            }
            return fallback;
        }

        void write_bool(const Accessor &a, const bool value) {
            const unsigned char b = value ? 1 : 0;
            if (a.field) {
#if defined(__ANDROID__)
                void *ptr = patchlib_field_get_pointer(a.field, nullptr);
                if (ptr) *static_cast<unsigned char *>(ptr) = b;
#else
                patchlib_field_set_value(a.field, nullptr, const_cast<unsigned char *>(&b));
#endif
            } else if (a.setter) {
                void *args[1] = {const_cast<unsigned char *>(&b)};
                patchlib_method_invoke_args(a.setter, nullptr, nullptr, args);
            }
        }

        patch_handle_t read_static_object(const patch_handle_t field) {
            if (!field) return nullptr;
#if defined(__ANDROID__)
            void *ptr = patchlib_field_get_pointer(field, nullptr);
            return ptr ? *static_cast<patch_handle_t *>(ptr) : nullptr;
#else
            patch_handle_t v = nullptr;
            patchlib_field_get_value(field, nullptr, &v);
            return v;
#endif
        }

        patch_handle_t get_method(const patch_handle_t type, const char *name, const int argc) {
            if (!type || !name) return nullptr;
            patch_handle_t m = patchlib_type_get_method_by_param_count(type, name, argc);
            if (!m || !patchlib_is_valid(m)) m = nullptr;
            return m;
        }

        // ====================================================================
        // 游戏绘制接口缓存
        // ====================================================================

        struct Gfx {
            bool ready{false};
            bool usable{false};

            patch_handle_t t_main{nullptr};
            patch_handle_t f_sprite_batch{nullptr};

            patch_handle_t m_sb_begin{nullptr};
            patch_handle_t m_sb_begin8{nullptr}; // Begin(SpriteSortMode,BlendState,...,Nullable<Matrix>,bool)
            patch_handle_t m_sb_end{nullptr};
            int sb_begin_args{0};

            patch_handle_t m_draw_inv_bg{nullptr};     // Utils.DrawInvBG(SpriteBatch,Rectangle,Color)
            patch_handle_t m_draw_border_string{nullptr}; // Utils.DrawBorderString(...)

            Accessor a_screen_w, a_screen_h;
            Accessor a_mouse_x, a_mouse_y;
            Accessor a_mouse_left, a_mouse_left_release, a_mouse_right,a_scroll_wheel;
            Accessor a_show_splash;
#if defined(__ANDROID__)
            patch_handle_t m_setup_rendering_ui{nullptr};
            patch_handle_t m_adjust_real_screen{nullptr};
            patch_handle_t f_cm_instance{nullptr};
            patch_handle_t m_cm_get_num_cursors{nullptr};
            patch_handle_t m_cm_get_cursor{nullptr};
            patch_handle_t f_cursor_pos{nullptr};
            patch_handle_t f_cursor_down{nullptr};
            patch_handle_t f_cursor_id{nullptr};
            patch_handle_t f_cursor_ignore_world{nullptr};
            patch_handle_t f_cursor_ignore{nullptr};
#endif
        };

        Gfx g_gfx;
        patch_handle_t g_sb{nullptr}; // 当前帧的 spriteBatch

        /// 读取布尔环境变量（"0"/空 视为 false）
        bool env_flag(const char *name) {
            const char *v = std::getenv(name);
            return v && v[0] != '\0' && v[0] != '0';
        }

        void resolve_gfx() {
            if (g_gfx.ready) return;
            g_gfx.ready = true;

            g_gfx.t_main = patchlib_type_get_type("Terraria", "Main");
            if (g_gfx.t_main) {
                g_gfx.f_sprite_batch = patchlib_type_get_field(g_gfx.t_main, "spriteBatch");
                if (g_gfx.f_sprite_batch && !patchlib_is_valid(g_gfx.f_sprite_batch))
                    g_gfx.f_sprite_batch = nullptr;

                g_gfx.a_screen_w = resolve_accessor(g_gfx.t_main, "screenWidth");
                g_gfx.a_screen_h = resolve_accessor(g_gfx.t_main, "screenHeight");
                g_gfx.a_mouse_x = resolve_accessor(g_gfx.t_main, "mouseX");
                g_gfx.a_mouse_y = resolve_accessor(g_gfx.t_main, "mouseY");
                g_gfx.a_mouse_left = resolve_accessor(g_gfx.t_main, "mouseLeft");
                g_gfx.a_mouse_left_release = resolve_accessor(g_gfx.t_main, "mouseLeftRelease");
                if(auto input=patchlib_type_get_type("Terraria.GameInput","PlayerInput"))g_gfx.a_scroll_wheel=resolve_accessor(input,"ScrollWheelDelta");
                g_gfx.a_mouse_right = resolve_accessor(g_gfx.t_main, "mouseRight");
                g_gfx.a_show_splash = resolve_accessor(g_gfx.t_main, "showSplash");
            }

            const patch_handle_t t_sb =
                    patchlib_type_get_type("Microsoft.Xna.Framework.Graphics", "SpriteBatch");
            if (t_sb) {
                g_gfx.m_sb_begin = get_method(t_sb, "Begin", 0);
                if (g_gfx.m_sb_begin) {
                    g_gfx.sb_begin_args = 0;
                } else {
                    g_gfx.m_sb_begin = get_method(t_sb, "Begin", 2);
                    g_gfx.sb_begin_args = 2;
                    if (!g_gfx.m_sb_begin) {
                        g_gfx.m_sb_begin = get_method(t_sb, "Begin", 8);
                        g_gfx.sb_begin_args = 8;
                    }
                }
                g_gfx.m_sb_end = get_method(t_sb, "End", 0);
                g_gfx.m_sb_begin8 = get_method(t_sb, "Begin", 8);
            }

            const patch_handle_t t_utils = patchlib_type_get_type("Terraria", "Utils");
            if (t_utils) {
                g_gfx.m_draw_inv_bg = get_method(t_utils, "DrawInvBG", 3);
                g_gfx.m_draw_border_string = get_method(t_utils, "DrawBorderString", 8);
            }

#if defined(__ANDROID__)
            const patch_handle_t t_runner = patchlib_type_get_type("", "XNAUnityRunner");
            if (t_runner) {
                g_gfx.m_setup_rendering_ui = get_method(t_runner, "SetupRenderingUI", 1);
                g_gfx.m_adjust_real_screen = get_method(t_runner, "AdjustRealScreenForUI", 1);
            }

            const patch_handle_t t_cm = patchlib_type_get_type("", "CursorManager");
            if (t_cm) {
                g_gfx.f_cm_instance = patchlib_type_get_field(t_cm, "Instance");
                g_gfx.m_cm_get_num_cursors = get_method(t_cm, "GetNumCursors", 0);
                g_gfx.m_cm_get_cursor = get_method(t_cm, "GetCursor", 1);
            }

            const patch_handle_t t_cursor = patchlib_type_get_type("", "Cursor");
            if (t_cursor) {
                g_gfx.f_cursor_pos = patchlib_type_get_field(t_cursor, "Position");
                g_gfx.f_cursor_down = patchlib_type_get_field(t_cursor, "Down");
                g_gfx.f_cursor_id = patchlib_type_get_field(t_cursor, "Id");
                g_gfx.f_cursor_ignore_world = patchlib_type_get_field(t_cursor, "IgnoreForWorld");
                g_gfx.f_cursor_ignore = patchlib_type_get_field(t_cursor, "Ignore");
            }
#endif

            g_gfx.usable = g_gfx.f_sprite_batch && g_gfx.m_sb_begin && g_gfx.m_sb_end &&
                           g_gfx.m_draw_inv_bg && g_gfx.m_draw_border_string;

            LOG_INFO("[gui] native GUI backend: spriteBatch={} begin={} end={} invBG={} text={} showSplash={} usable={}",
                     reinterpret_cast<void *>(g_gfx.f_sprite_batch),
                     reinterpret_cast<void *>(g_gfx.m_sb_begin),
                     reinterpret_cast<void *>(g_gfx.m_sb_end),
                     reinterpret_cast<void *>(g_gfx.m_draw_inv_bg),
                     reinterpret_cast<void *>(g_gfx.m_draw_border_string),
                     g_gfx.a_show_splash.valid(),
                     g_gfx.usable);
        }

        // ====================================================================
        // 绘制原语
        // ====================================================================

        // --------------------------------------------------------------------
        // 绘制指令队列：mod.gui.* 只记录指令，等 on_gui 返回后在 C++ 里统一执行。
        // 原因：托管绘制方法（Utils.*）在 Android 上抛出的异常是 il2cpp 的 C++
        // 异常；若在 Lua 回调内直接调用，异常会穿过 Lua VM 的 setjmp 帧导致
        // std::terminate/abort。放到 Lua 之外执行即可规避。
        // --------------------------------------------------------------------
        struct DrawCmd {
            enum Kind { RECT, TEXT, IMAGE } kind;
            float x, y, w, h; // RECT 用 w/h；TEXT 用 scale
            Color col;
            std::string text;
            float scale;
            int sx=0,sy=0,sw=0,sh=0,border=0;
            float edge=0;
            Viewport clip;
        };

        Viewport g_viewport;
        bool text_in_clip(float x,float y,const char *text,float scale){
            size_t n=0;for(auto p=reinterpret_cast<const unsigned char*>(text);*p;++p)if((*p&0xc0)!=0x80)n+=(*p<128?1:2);
            return g_viewport.covers(x,y,float(n)*10*scale,28*scale);
        }
        std::vector<DrawCmd> g_cmds;

#if defined(__ANDROID__)
        // Android 兜底：内核 patchlib 的按值结构体 FFI 编组在含静态字段的结构体
        // （Vector2/Color/Rectangle 都有）上会生成错误的 ffi_type，导致托管方法
        // 收到错位参数并抛异常（→ std::terminate → SIGABRT）。
        // 这里直接用原生函数指针按正确的 C ABI 调用，绕开 patch_type_to_ffi_type。
        // 详见 TEFKernel/documents/patchlib-ffi-struct-bug.md。
        using FnDrawInvBG = void (*)(void *sb, Recti r, Color c);
        using FnDrawString = Vec2 (*)(void *sb, void *text, Vec2 pos, Color color,
                                      float scale, float ax, float ay, int32_t max_chars);
        using FnAdjustRealScreenForUI = Vec2 (*)(Vec2 pos);
        using FnGetNumCursors = int (*)(void *self);
        using FnGetCursor = void * (*)(void *self, int index);
#endif

        void exec_rect(const float x, const float y, const float w, const float h, const Color &c) {
            if (!g_gfx.m_draw_inv_bg || !g_sb || w <= 0 || h <= 0) return;
            if(g_viewport.enabled){float cx=x,cy=y,cw=w,ch=h;if(g_viewport.crop(cx,cy,cw,ch))images::solid(g_sb,cx,cy,cw,ch,&c);return;}
            if(w<24.0f || h<24.0f){images::solid(g_sb,x,y,w,h,&c);return;}
            Recti r{static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h)};
            Color col = c;
#if defined(__ANDROID__)
            auto fn = reinterpret_cast<FnDrawInvBG>(patchlib_method_get_pointer(g_gfx.m_draw_inv_bg));
            if (fn) fn(g_sb, r, col);
#else
            void *args[3] = {&g_sb, &r, &col};
            patchlib_method_invoke_args(g_gfx.m_draw_inv_bg, nullptr, nullptr, args);
#endif
        }

        struct CachedText {patch_handle_t value=nullptr;unsigned int pin=0;unsigned long long used=0;};
        std::unordered_map<std::string,CachedText> g_text_cache;
        unsigned long long g_text_clock=0;
        void release_text(CachedText &entry){if(entry.pin)images::release_object(entry.pin);if(entry.value)patchlib_free(entry.value);entry={};}
        void clear_text_cache(){for(auto &kv:g_text_cache)release_text(kv.second);g_text_cache.clear();g_text_clock=0;}
        patch_handle_t cached_string(const char *text,bool &temporary){
            temporary=false;auto it=g_text_cache.find(text);
            if(it!=g_text_cache.end()){it->second.used=++g_text_clock;return it->second.value;}
            auto value=patchlib_string_create(text);if(!value)return nullptr;
            // Long dynamic content is not retained; fixed labels are bounded to 256 entries.
            if(std::strlen(text)>512){temporary=true;return value;}
            CachedText entry;entry.value=value;entry.used=++g_text_clock;
#if defined(__ANDROID__)
            entry.pin=images::retain_object(value);
            if(!entry.pin){temporary=true;return value;}
#endif
            if(g_text_cache.size()>=256){auto old=g_text_cache.begin();for(auto i=g_text_cache.begin();i!=g_text_cache.end();++i)if(i->second.used<old->second.used)old=i;release_text(old->second);g_text_cache.erase(old);}
            g_text_cache.emplace(text,entry);return value;
        }
        void exec_string(const float x, const float y, const char *text, const Color &c, const float scale) {
            if (!g_gfx.m_draw_border_string || !g_sb || !text || !*text) return;
            bool temporary=false;patch_handle_t str=cached_string(text,temporary);
            if (!str) return;
            struct TempStringGuard{patch_handle_t value;bool temporary;~TempStringGuard(){if(temporary)patchlib_free(value);}} string_guard{str,temporary};
            Vec2 pos{x, y};
            Color col = c;
#if defined(__ANDROID__)
            auto fn = reinterpret_cast<FnDrawString>(patchlib_method_get_pointer(g_gfx.m_draw_border_string));
            if (fn) fn(g_sb, str, pos, col, scale, 0.0f, 0.0f, -1);
#else
            float ax = 0.0f, ay = 0.0f;
            int max_chars = -1;
            float fscale = scale;
            void *args[8] = {&g_sb, &str, &pos, &col, &fscale, &ax, &ay, &max_chars};
            patchlib_method_invoke_args(g_gfx.m_draw_border_string, nullptr, nullptr, args);
#endif

        }

        void sb_begin();
        void sb_end();

        /// Window backgrounds are one batch; text is a separate foreground batch.
        /// PE's Unity compatibility renderer may regroup textures in deferred mode.
        void flush_cmds() {
            bool has_text=false;
            const auto saved_clip=g_viewport;
            for (const DrawCmd &cmd: g_cmds) {
                g_viewport=cmd.clip;images::set_clip(cmd.clip.enabled,cmd.clip.x,cmd.clip.y,cmd.clip.w,cmd.clip.h);
                if (cmd.kind == DrawCmd::RECT) {
                    exec_rect(cmd.x, cmd.y, cmd.w, cmd.h, cmd.col);
                } else if (cmd.kind == DrawCmd::IMAGE) {
                    images::draw(g_sb,cmd.text,cmd.x,cmd.y,cmd.w,cmd.h,&cmd.col,
                                 cmd.sx,cmd.sy,cmd.sw,cmd.sh,cmd.border,cmd.edge);
                } else has_text=true;
            }
            if(has_text){
                sb_end();
                sb_begin();
                for(const DrawCmd &cmd:g_cmds)if(cmd.kind==DrawCmd::TEXT){
                    g_viewport=cmd.clip;
                    if(text_in_clip(cmd.x,cmd.y,cmd.text.c_str(),cmd.scale))exec_string(cmd.x,cmd.y,cmd.text.c_str(),cmd.col,cmd.scale);
                }
            }
            g_cmds.clear();g_viewport=saved_clip;images::set_clip(false,0,0,0,0);
        }


        void sb_begin() {
            if (!g_sb) return;
#if defined(__ANDROID__)
            // PE：强制使用单位矩阵，避免 GUI 跟随世界相机/视野缩放
            // （PE 无 0 参 Begin；2 参 Begin 会带上世界变换）
            if (g_gfx.m_sb_begin8) {
                static NullableMat ident = [] {
                    NullableMat n{};
                    n.has_value = 1;
                    n.value.m[0] = n.value.m[5] = n.value.m[10] = n.value.m[15] = 1.0f;
                    return n;
                }();
                using FnBegin8 = void (*)(void *self, int sort_mode, void *blend, void *sampler,
                                          void *depth, void *raster, void *effect, NullableMat *xform,
                                          bool deferred);
                auto fn = reinterpret_cast<FnBegin8>(patchlib_method_get_pointer(g_gfx.m_sb_begin8));
                if (fn) {
                    bool deferred = true;
                    fn(g_sb, 0, nullptr, nullptr, nullptr, nullptr, nullptr, &ident, deferred);
                    return;
                }
            }
#endif
            if (!g_gfx.m_sb_begin) return;
            if (g_gfx.sb_begin_args == 0) {
                patchlib_method_invoke_args(g_gfx.m_sb_begin, g_sb, nullptr, nullptr);
            } else if (g_gfx.sb_begin_args == 2) {
                int sort_mode = 0; // SpriteSortMode.Deferred
                unsigned char deferred = 1;
                void *args[2] = {&sort_mode, &deferred};
                patchlib_method_invoke_args(g_gfx.m_sb_begin, g_sb, nullptr, args);
            }
        }

        void sb_end() {
            if (!g_gfx.m_sb_end || !g_sb) return;
            patchlib_method_invoke_args(g_gfx.m_sb_end, g_sb, nullptr, nullptr);
        }

        // ====================================================================
        // 屏幕尺寸 / UI 缩放 / 输入
        // ====================================================================

        int g_screen_w = 1280;
        int g_screen_h = 720;
        float g_scale = 1.0f;

        float g_mouse_x = -FLT_MAX;
        float g_mouse_y = -FLT_MAX;
        bool g_mouse_down = false;
        bool g_mouse_pressed = false;
        bool g_mouse_released = false;
        bool g_prev_mouse_down = false;
        std::string g_input_owner,g_pointer_owner;
        ClickState g_click_state;
#if defined(__ANDROID__)
        int g_active_cursor_id=0;
        bool g_active_cursor_valid=false;
#endif

        void update_screen_size() {
            const int w = read_int(g_gfx.a_screen_w, 0);
            const int h = read_int(g_gfx.a_screen_h, 0);
            if (w > 0) g_screen_w = w;
            if (h > 0) g_screen_h = h;
            int mn = g_screen_w < g_screen_h ? g_screen_w : g_screen_h;
            if (mn <= 0) mn = 720;
            float s = static_cast<float>(mn) / 720.0f;
            if (s < 1.0f) s = 1.0f;
            if (s > 2.5f) s = 2.5f;
            g_scale = s;
        }

        bool point_in_gui(float x, float y);

        // 前向声明：这两个状态在后面（窗口状态区）才定义，update_input 会用到
        std::string g_drag_id;
        std::string g_slider_active;

#if defined(__ANDROID__)
        bool cursor_captured(void *cursor);
        void release_finished_touches();
#endif
        void update_input() {
#if defined(__ANDROID__)
            bool android_touch_handled = false;
            void *cm_instance = g_gfx.f_cm_instance ? read_static_object(g_gfx.f_cm_instance) : nullptr;
            if (cm_instance && g_gfx.m_cm_get_num_cursors && g_gfx.m_cm_get_cursor &&
                g_gfx.m_adjust_real_screen && g_gfx.f_cursor_pos && g_gfx.f_cursor_down) {
                auto fn_num_cursors = reinterpret_cast<FnGetNumCursors>(patchlib_method_get_pointer(g_gfx.m_cm_get_num_cursors));
                auto fn_get_cursor = reinterpret_cast<FnGetCursor>(patchlib_method_get_pointer(g_gfx.m_cm_get_cursor));
                auto fn_adjust = reinterpret_cast<FnAdjustRealScreenForUI>(patchlib_method_get_pointer(g_gfx.m_adjust_real_screen));

                if (fn_num_cursors && fn_get_cursor && fn_adjust) {
                    const int num = fn_num_cursors(cm_instance);
                    void *active_cursor = nullptr;
                    Vec2 active_ui_pos{g_mouse_x, g_mouse_y};
                    bool touch_down = false;

                    for (int i = 0; i < num; ++i) {
                        void *c = fn_get_cursor(cm_instance, i);
                        if (!c) continue;
                        auto *pos_ptr = static_cast<Vec2 *>(patchlib_field_get_pointer(g_gfx.f_cursor_pos, c));
                        auto *down_ptr = static_cast<unsigned char *>(patchlib_field_get_pointer(g_gfx.f_cursor_down, c));
                        if (!pos_ptr || !down_ptr) continue;

                        auto *id_ptr=g_gfx.f_cursor_id?static_cast<int*>(patchlib_field_get_pointer(g_gfx.f_cursor_id,c)):nullptr;
                        if(g_active_cursor_valid && (!id_ptr || *id_ptr!=g_active_cursor_id))continue;
                        const bool is_down = (*down_ptr != 0);
                        const Vec2 ui_pos = fn_adjust(*pos_ptr);
                        const bool in_gui = point_in_gui(ui_pos.x, ui_pos.y);

                        if (is_down) {
                            if (in_gui || cursor_captured(c) || (!g_drag_id.empty() && g_prev_mouse_down) || (!g_slider_active.empty() && g_prev_mouse_down)) {
                                active_cursor = c;
                                if(id_ptr){g_active_cursor_id=*id_ptr;g_active_cursor_valid=true;}
                                active_ui_pos = ui_pos;
                                touch_down = true;

                                if (g_gfx.f_cursor_ignore_world) {
                                    auto *p = static_cast<unsigned char *>(patchlib_field_get_pointer(g_gfx.f_cursor_ignore_world, c));
                                    if (p) *p = 1;
                                }
                                if (g_gfx.f_cursor_ignore) {
                                    auto *p = static_cast<unsigned char *>(patchlib_field_get_pointer(g_gfx.f_cursor_ignore, c));
                                    if (p) *p = 1;
                                }
                                break;
                            }
                        } else if (g_prev_mouse_down && (in_gui || cursor_captured(c) || !g_drag_id.empty() || !g_slider_active.empty())) {
                            active_cursor = c;
                            active_ui_pos = ui_pos;
                            touch_down = false;
                            break;
                        }
                    }

                    if (active_cursor || g_prev_mouse_down) {
                        android_touch_handled = true;
                        if (active_cursor) {
                            g_mouse_x = active_ui_pos.x;
                            g_mouse_y = active_ui_pos.y;
                        }
                        g_mouse_down = touch_down;
                        g_mouse_pressed = g_mouse_down && !g_prev_mouse_down;
                        g_mouse_released = active_cursor && !g_mouse_down && g_prev_mouse_down;
                        g_prev_mouse_down = g_mouse_down;

                        if (!g_mouse_down && !g_mouse_released) {
                            g_mouse_x = -FLT_MAX;
                            g_mouse_y = -FLT_MAX;
                        }
                    }
                }
            }

            if (!android_touch_handled) {
                const bool raw_down = read_bool(g_gfx.a_mouse_left, false);
                if (raw_down) {
                    g_mouse_x = static_cast<float>(read_int(g_gfx.a_mouse_x, 0));
                    g_mouse_y = static_cast<float>(read_int(g_gfx.a_mouse_y, 0));
                }
                g_mouse_down = raw_down;
                g_mouse_pressed = g_mouse_down && !g_prev_mouse_down;
                g_mouse_released = !g_mouse_down && g_prev_mouse_down;
                g_prev_mouse_down = g_mouse_down;
                if (!g_mouse_down && !g_mouse_released) {
                    g_mouse_x = -FLT_MAX;
                    g_mouse_y = -FLT_MAX;
                }
            }
#else
            g_mouse_x = static_cast<float>(read_int(g_gfx.a_mouse_x, 0));
            g_mouse_y = static_cast<float>(read_int(g_gfx.a_mouse_y, 0));
            g_mouse_down = read_bool(g_gfx.a_mouse_left, false);
            g_mouse_pressed = g_mouse_down && !g_prev_mouse_down;
            g_mouse_released = !g_mouse_down && g_prev_mouse_down;
            g_prev_mouse_down = g_mouse_down;
#endif
        }


// Native registry and API follow.

std::recursive_mutex g_mutex;
struct Entry {
    void *owner=nullptr;
    std::string id,root;
    kl_gui_callback callback=nullptr;
    void *userdata=nullptr;
    kl_gui_rect bounds{40,80,480,320};
    bool visible=true,active=true,failed=false;
    unsigned long long order=0;
    float drag_dx=0,drag_dy=0;
};
std::unordered_map<void*,std::shared_ptr<Entry>> g_entries;
std::shared_ptr<Entry> g_current;
kl_gui_frame g_frame{};
bool g_prune=false;
bool g_backend_installed=false;
#if !defined(__ANDROID__)
bool g_input_sampled=false;
#endif
unsigned long long g_order=0;
bool contains(const kl_gui_rect &r,float x,float y){return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}
bool point_in_gui(float x,float y){for(auto &kv:g_entries){auto &e=*kv.second;if(e.active&&e.visible&&e.callback&&!e.failed&&contains(e.bounds,x,y))return true;}return false;}
bool valid_frame(const kl_gui_frame *f){return f==&g_frame&&g_current&&g_current->active;}
Color native_color(kl_gui_color c){Color out;make_color(out,c.r,c.g,c.b,c.a);return out;}
void queue_rect(float x,float y,float w,float h,kl_gui_color c){
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0)return;
    DrawCmd cmd{};cmd.kind=DrawCmd::RECT;cmd.x=x;cmd.y=y;cmd.w=w;cmd.h=h;cmd.col=native_color(c);cmd.clip=g_viewport;g_cmds.push_back(std::move(cmd));
}
void queue_text(float x,float y,const char *s,float scale,kl_gui_color c){
    if(!s||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(scale)||scale<=0||std::strlen(s)>8192)return;
    DrawCmd cmd{};cmd.kind=DrawCmd::TEXT;cmd.x=x;cmd.y=y;cmd.text=s;cmd.scale=scale;cmd.col=native_color(c);cmd.clip=g_viewport;g_cmds.push_back(std::move(cmd));
}
int register_window(void *owner,kl_gui_callback cb,void *user,kl_gui_rect bounds){
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto it=g_entries.find(owner);
    if(it==g_entries.end()||!cb||!std::isfinite(bounds.x)||!std::isfinite(bounds.y)||!std::isfinite(bounds.w)||!std::isfinite(bounds.h)||bounds.w<32||bounds.h<32)return 0;
    auto &e=*it->second;e.callback=cb;e.userdata=user;e.bounds=bounds;e.visible=true;e.failed=false;e.order=++g_order;return 1;
}
void set_visible(void *owner,int visible){std::lock_guard<std::recursive_mutex> lock(g_mutex);auto it=g_entries.find(owner);if(it!=g_entries.end())it->second->visible=visible!=0;}
void unregister_window(void *owner){std::lock_guard<std::recursive_mutex> lock(g_mutex);auto it=g_entries.find(owner);if(it!=g_entries.end()){it->second->callback=nullptr;it->second->userdata=nullptr;g_prune=true;}}
int available(){std::lock_guard<std::recursive_mutex> lock(g_mutex);return g_backend_installed&&g_gfx.usable;}
void api_rect(const kl_gui_frame *f,float x,float y,float w,float h,kl_gui_color c){std::lock_guard<std::recursive_mutex> lock(g_mutex);if(valid_frame(f))queue_rect(x,y,w,h,c);}
void api_text(const kl_gui_frame *f,float x,float y,const char *s,float scale,kl_gui_color c){std::lock_guard<std::recursive_mutex> lock(g_mutex);if(valid_frame(f))queue_text(x,y,s,scale,c);}
bool hover(float x,float y,float w,float h){return g_current&&g_current->id==g_input_owner&&contains({x,y,w,h},g_mouse_x,g_mouse_y)&&g_viewport.contains(g_mouse_x,g_mouse_y);}
int api_button(const kl_gui_frame *f,const char *id,const char *label,float x,float y,float w,float h){
    std::lock_guard<std::recursive_mutex> lock(g_mutex);if(!valid_frame(f)||!id||!label)return 0;
    bool over=hover(x,y,w,h);queue_rect(x,y,w,h,over?kl_gui_color{68,94,130,255}:kl_gui_color{40,57,82,255});queue_text(x+10,y+5,label,0.8f,{245,245,245,255});
    return g_click_state.update(g_current->id+"#"+id,over,g_mouse_pressed,g_mouse_released);
}
float api_slider(const kl_gui_frame *f,const char *id,float v,float lo,float hi,float x,float y,float w,float h){
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if(!valid_frame(f)||!id||!std::isfinite(v)||!std::isfinite(lo)||!std::isfinite(hi)||hi<=lo||w<=0||h<=0)return v;
    const auto key=g_current->id+"#"+id;
    if(g_mouse_pressed&&hover(x,y,w,h))g_slider_active=key;
    if(g_slider_active==key&&g_mouse_down)v=lo+std::clamp((g_mouse_x-x)/w,0.f,1.f)*(hi-lo);
    v=std::clamp(v,lo,hi);queue_rect(x,y,w,h,{40,57,82,255});queue_rect(x,y,w*(v-lo)/(hi-lo),h,{80,165,215,255});return v;
}
void api_image(const kl_gui_frame *f,const char *path,float x,float y,float w,float h,int sx,int sy,int sw,int sh,kl_gui_color c){
    std::lock_guard<std::recursive_mutex> lock(g_mutex);if(!valid_frame(f)||!path||!*path||w<=0||h<=0)return;
    auto id=images::request(g_current->id,g_current->root,path);if(id.empty())return;
    DrawCmd cmd{};cmd.kind=DrawCmd::IMAGE;cmd.text=id;cmd.x=x;cmd.y=y;cmd.w=w;cmd.h=h;cmd.sx=sx;cmd.sy=sy;cmd.sw=sw;cmd.sh=sh;cmd.col=native_color(c);cmd.clip=g_viewport;g_cmds.push_back(std::move(cmd));
}
void drag_window(const kl_gui_frame *f,float title_h){
    std::lock_guard<std::recursive_mutex> lock(g_mutex);if(!valid_frame(f)||title_h<=0)return;auto &e=*g_current;
    if(g_mouse_pressed&&hover(e.bounds.x,e.bounds.y,e.bounds.w,title_h)){g_drag_id=e.id;e.drag_dx=g_mouse_x-e.bounds.x;e.drag_dy=g_mouse_y-e.bounds.y;}
    if(g_drag_id==e.id&&g_mouse_down){e.bounds.x=std::clamp(g_mouse_x-e.drag_dx,0.f,std::max(0.f,float(g_screen_w)-e.bounds.w));e.bounds.y=std::clamp(g_mouse_y-e.drag_dy,0.f,std::max(0.f,float(g_screen_h)-title_h));g_frame.window=e.bounds;g_viewport={true,e.bounds.x,e.bounds.y,e.bounds.w,e.bounds.h};}
}

        patch_hook_id_t g_draw_hook = PATCH_HOOK_INVALID_ID;
        patch_hook_id_t g_update_hook = PATCH_HOOK_INVALID_ID;
        bool g_installed = false;
        bool g_in_render = false;
#if defined(__ANDROID__)
        patch_hook_id_t g_layer_hook = PATCH_HOOK_INVALID_ID;
        patch_hook_id_t g_cursor_list_hook = PATCH_HOOK_INVALID_ID;
        std::unordered_map<int, bool> g_captured_touches;
        bool cursor_captured(void *cursor) {
            const auto f=g_gfx.f_cursor_id;
            auto *id=f && cursor ? static_cast<int*>(patchlib_field_get_pointer(f,cursor)) : nullptr;
            return id && g_captured_touches.count(*id);
        }
        void release_finished_touches() {
            if(!g_gfx.f_cm_instance || !g_gfx.m_cm_get_num_cursors || !g_gfx.m_cm_get_cursor)return;
            void *instance=read_static_object(g_gfx.f_cm_instance);if(!instance)return;
            auto count=reinterpret_cast<FnGetNumCursors>(patchlib_method_get_pointer(g_gfx.m_cm_get_num_cursors));
            auto at=reinterpret_cast<FnGetCursor>(patchlib_method_get_pointer(g_gfx.m_cm_get_cursor));
            if(!count||!at)return;
            const auto f=g_gfx.f_cursor_id;
            for(int i=0,n=count(instance);i<n;++i){void *c=at(instance,i);if(!c)continue;
                auto *id=f?static_cast<int*>(patchlib_field_get_pointer(f,c)):nullptr;
                auto *down=static_cast<unsigned char*>(patchlib_field_get_pointer(g_gfx.f_cursor_down,c));
                if(id && down && !*down)g_captured_touches.erase(*id);
            }
        }
        bool capture_cursor(void *cursor) {
            if (!cursor || !g_gfx.f_cursor_pos || !g_gfx.f_cursor_down || !g_gfx.m_adjust_real_screen) return false;
            auto *position=static_cast<Vec2*>(patchlib_field_get_pointer(g_gfx.f_cursor_pos,cursor));
            auto *down=static_cast<unsigned char*>(patchlib_field_get_pointer(g_gfx.f_cursor_down,cursor));
            const auto id_field=g_gfx.f_cursor_id;
            auto *id=id_field ? static_cast<int*>(patchlib_field_get_pointer(id_field,cursor)) : nullptr;
            if (!position || !down || !id) return false;
            auto adjust=reinterpret_cast<FnAdjustRealScreenForUI>(patchlib_method_get_pointer(g_gfx.m_adjust_real_screen));
            if (!adjust) return false;
            const Vec2 pos=adjust(*position);
            // Once claimed, keep this finger captured through its release frame.
            if (*down && point_in_gui(pos.x,pos.y)) g_captured_touches[*id]=true;
            if (!g_captured_touches.count(*id)) return false;
            for (auto field : {g_gfx.f_cursor_ignore_world,g_gfx.f_cursor_ignore}) {
                if (field) {auto *v=static_cast<unsigned char*>(patchlib_field_get_pointer(field,cursor));if(v)*v=1;}
            }
            return true;
        }
        bool cursor_layer_prefix(patch_handle_t, void **args, const patch_method_signature_t *, void *) {
            std::lock_guard<std::recursive_mutex> lock(g_mutex);
            return args && args[0] && capture_cursor(*static_cast<void**>(args[0]));
        }
        void cursor_list_postfix(patch_handle_t, void **, void *, const patch_method_signature_t *) {
            std::lock_guard<std::recursive_mutex> lock(g_mutex);
            if (!g_gfx.f_cm_instance || !g_gfx.m_cm_get_num_cursors || !g_gfx.m_cm_get_cursor) return;
            void *instance=read_static_object(g_gfx.f_cm_instance);
            if (!instance) return;
            auto count=reinterpret_cast<FnGetNumCursors>(patchlib_method_get_pointer(g_gfx.m_cm_get_num_cursors));
            auto at=reinterpret_cast<FnGetCursor>(patchlib_method_get_pointer(g_gfx.m_cm_get_cursor));
            if (!count || !at) return;
            std::unordered_map<int,bool> live;
            const auto id_field=g_gfx.f_cursor_id;
            for(int i=0,n=count(instance);i<n;++i){void *c=at(instance,i);if(!c)continue;
                auto *id=id_field?static_cast<int*>(patchlib_field_get_pointer(id_field,c)):nullptr;
                if(id)live[*id]=true;
                capture_cursor(c);
            }
            // Cursor objects persist for the release frame. Prune only disappeared IDs.
            for(auto it=g_captured_touches.begin();it!=g_captured_touches.end();){
                if(!live.count(it->first))it=g_captured_touches.erase(it);else ++it;
            }
        }
#endif

        /// 当前指针是否落在任一已打开窗口内（用上一帧缓存的位置）
        bool pointer_over_windows() {
            if (g_entries.empty()) return false;
#if defined(__ANDROID__)
            if (!g_mouse_down && !g_prev_mouse_down) return false;
            return point_in_gui(g_mouse_x, g_mouse_y);
#else
            if (!g_gfx.usable || !g_gfx.a_mouse_x.valid() || !g_gfx.a_mouse_left.valid()) return false;
            const float mx = static_cast<float>(read_int(g_gfx.a_mouse_x, 0));
            const float my = static_cast<float>(read_int(g_gfx.a_mouse_y, 0));
            return point_in_gui(mx, my);
#endif
        }

        /// Main.Update 前缀：指针悬停在 GUI 上时屏蔽游戏鼠标，避免误触游戏操作
        bool update_prefix(patch_handle_t, void **, const patch_method_signature_t *, void *) {
            std::lock_guard<std::recursive_mutex> lock(g_mutex);
#if !defined(__ANDROID__)
            // Preserve raw button state before this hook suppresses it for the game.
            update_input();g_input_sampled=true;
#endif
#if defined(__ANDROID__)
            void *cm_instance = g_gfx.f_cm_instance ? read_static_object(g_gfx.f_cm_instance) : nullptr;
            if (cm_instance && g_gfx.m_cm_get_num_cursors && g_gfx.m_cm_get_cursor &&
                g_gfx.m_adjust_real_screen && g_gfx.f_cursor_pos && g_gfx.f_cursor_down) {
                auto fn_num_cursors = reinterpret_cast<FnGetNumCursors>(patchlib_method_get_pointer(g_gfx.m_cm_get_num_cursors));
                auto fn_get_cursor = reinterpret_cast<FnGetCursor>(patchlib_method_get_pointer(g_gfx.m_cm_get_cursor));
                auto fn_adjust = reinterpret_cast<FnAdjustRealScreenForUI>(patchlib_method_get_pointer(g_gfx.m_adjust_real_screen));

                if (fn_num_cursors && fn_get_cursor && fn_adjust) {
                    const int num = fn_num_cursors(cm_instance);
                    for (int i = 0; i < num; ++i) {
                        void *c = fn_get_cursor(cm_instance, i);
                        if (!c) continue;
                        auto *pos_ptr = static_cast<Vec2 *>(patchlib_field_get_pointer(g_gfx.f_cursor_pos, c));
                        auto *down_ptr = static_cast<unsigned char *>(patchlib_field_get_pointer(g_gfx.f_cursor_down, c));
                        if (!pos_ptr || !down_ptr || *down_ptr == 0) continue;

                        const Vec2 ui_pos = fn_adjust(*pos_ptr);
                        if (point_in_gui(ui_pos.x, ui_pos.y) || cursor_captured(c)) {
                            if (g_gfx.f_cursor_ignore_world) {
                                auto *p = static_cast<unsigned char *>(patchlib_field_get_pointer(g_gfx.f_cursor_ignore_world, c));
                                if (p) *p = 1;
                            }
                            if (g_gfx.f_cursor_ignore) {
                                auto *p = static_cast<unsigned char *>(patchlib_field_get_pointer(g_gfx.f_cursor_ignore, c));
                                if (p) *p = 1;
                            }
                            write_bool(g_gfx.a_mouse_left, false);
                            write_bool(g_gfx.a_mouse_right, false);
                        }
                    }
                }
            }
#endif
            if (pointer_over_windows()) {
                write_bool(g_gfx.a_mouse_left, false);
                write_bool(g_gfx.a_mouse_right, false);
            }
            return false; // 不跳过原函数
        }


void render(){
    resolve_gfx();if(!g_gfx.usable)return;
    if(g_prune){std::vector<std::string> live;for(auto &kv:g_entries)if(kv.second->callback)live.push_back(kv.second->id);images::prune(live);g_prune=false;}
    std::vector<std::shared_ptr<Entry>> entries;
    for(auto &kv:g_entries)if(kv.second->active&&kv.second->visible&&kv.second->callback&&!kv.second->failed)entries.push_back(kv.second);
    if(entries.empty())return;
    if(read_bool(g_gfx.a_show_splash,false))return;
    update_screen_size();
#if defined(__ANDROID__)
    update_input();
#else
    if(!g_input_sampled)update_input();g_input_sampled=false;
#endif
    images::begin_frame();
    g_input_owner.clear();unsigned long long top=0;
    for(auto &e:entries)if(contains(e->bounds,g_mouse_x,g_mouse_y)&&e->order>=top){top=e->order;g_input_owner=e->id;}
    if(g_mouse_pressed&&!g_input_owner.empty())g_pointer_owner=g_input_owner;
    if(!g_pointer_owner.empty())g_input_owner=g_pointer_owner;
    for(auto &e:entries)if(g_mouse_pressed&&e->id==g_input_owner)e->order=++g_order;
    std::sort(entries.begin(),entries.end(),[](auto &a,auto &b){return a->order<b->order;});
    g_sb=read_static_object(g_gfx.f_sprite_batch);if(!g_sb)return;
    sb_begin();
    for(auto &e:entries){
        if(!e->active||!e->callback||!e->visible)continue;
        g_current=e;g_viewport={true,e->bounds.x,e->bounds.y,e->bounds.w,e->bounds.h};
        g_frame={sizeof(kl_gui_frame),g_screen_w,g_screen_h,g_scale,g_mouse_x,g_mouse_y,
                 g_mouse_down,g_mouse_pressed,g_mouse_released,read_int(g_gfx.a_scroll_wheel,0),e->bounds};
        g_cmds.clear();
        try{e->callback(&g_frame,e->userdata);}catch(...){e->failed=true;g_cmds.clear();LOG_ERROR("[gui] callback failed for {}; disabled until re-registration",e->id);}
        // Flush per window to preserve window stacking, including its text.
        flush_cmds();g_current.reset();
    }
    sb_end();g_sb=nullptr;g_viewport={};
    if(!g_mouse_down){g_drag_id.clear();g_slider_active.clear();g_pointer_owner.clear();}
    g_click_state.finish(g_mouse_down,g_mouse_released);
#if defined(__ANDROID__)
    if(!g_mouse_down)g_active_cursor_valid=false;
    release_finished_touches();
#endif
}
void frame_postfix(patch_handle_t,void **,void *,const patch_method_signature_t *){
    std::lock_guard<std::recursive_mutex> lock(g_mutex);if(g_in_render)return;g_in_render=true;
    try{render();}catch(...){LOG_ERROR("[gui] rendering exception; frame aborted");if(g_sb){try{sb_end();}catch(...){}}g_click_state.reset();}
    g_current.reset();g_sb=nullptr;g_cmds.clear();g_viewport={};g_in_render=false;
}
} // anonymous namespace
void attach(void *owner,const std::string &id,const std::string &root){
    std::lock_guard<std::recursive_mutex> lock(g_mutex);auto e=std::make_shared<Entry>();e->owner=owner;e->id=id;e->root=root;g_entries[owner]=std::move(e);
}
bool detach(void *owner){
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if(g_in_render&&g_current&&g_current->owner==owner)return false;
    auto it=g_entries.find(owner);if(it==g_entries.end())return true;
    const auto id=it->second->id;it->second->active=false;it->second->callback=nullptr;it->second->userdata=nullptr;g_entries.erase(it);g_prune=true;
    if(g_input_owner==id)g_input_owner.clear();if(g_pointer_owner==id)g_pointer_owner.clear();if(g_drag_id==id)g_drag_id.clear();
    if(g_slider_active.rfind(id+"#",0)==0)g_slider_active.clear();g_click_state.reset();return true;
}
const kl_gui_api *api(){static const kl_gui_api table={KL_GUI_API_VERSION,sizeof(kl_gui_api),register_window,set_visible,unregister_window,available,api_rect,api_text,api_button,api_slider,api_image,drag_window};return &table;}
void install(){
    std::lock_guard<std::recursive_mutex> lock(g_mutex);if(g_installed)return;
    // Server builds need not export client rendering APIs.
    if(!patchlib_type_get_type||!patchlib_install_prepost_hook||!patchlib_method_invoke_args)return;
    if(env_flag("KERNELLOADER_GUI_DISABLE"))return;
    resolve_gfx();if(!g_gfx.usable){LOG_WARN("[gui] backend unavailable");return;}
    auto type=patchlib_type_get_type("Terraria","Main");
    auto draw=get_method(type,"Draw",1);auto update=get_method(type,"Update",1);if(!update)update=get_method(type,"DoUpdate",1);
    if(!draw||!update){LOG_WARN("[gui] Draw/Update unavailable");return;}
    g_draw_hook=patchlib_install_prepost_hook(draw,nullptr,frame_postfix);
    g_update_hook=patchlib_install_prepost_hook(update,update_prefix,nullptr);
    if(g_draw_hook==PATCH_HOOK_INVALID_ID||g_update_hook==PATCH_HOOK_INVALID_ID){
        if(g_draw_hook!=PATCH_HOOK_INVALID_ID)patchlib_uninstall_hook(g_draw_hook);
        if(g_update_hook!=PATCH_HOOK_INVALID_ID)patchlib_uninstall_hook(g_update_hook);
        g_draw_hook=g_update_hook=PATCH_HOOK_INVALID_ID;LOG_ERROR("[gui] hook installation failed");return;
    }
#if defined(__ANDROID__)
    auto layer=patchlib_type_get_type("","CursorInputLayer");auto cursor_update=get_method(layer,"UpdateCursor",1);
    auto manager=patchlib_type_get_type("","CursorManager");auto list_update=get_method(manager,"UpdateCursorList",0);
    if(cursor_update)g_layer_hook=patchlib_install_prepost_hook(cursor_update,cursor_layer_prefix,nullptr);
    if(list_update)g_cursor_list_hook=patchlib_install_prepost_hook(list_update,nullptr,cursor_list_postfix);
    if(g_layer_hook==PATCH_HOOK_INVALID_ID||g_cursor_list_hook==PATCH_HOOK_INVALID_ID){
        for(auto id:{g_draw_hook,g_update_hook,g_layer_hook,g_cursor_list_hook})if(id!=PATCH_HOOK_INVALID_ID)patchlib_uninstall_hook(id);
        g_draw_hook=g_update_hook=g_layer_hook=g_cursor_list_hook=PATCH_HOOK_INVALID_ID;
        LOG_ERROR("[gui] mobile dispatch capture unavailable; GUI disabled");return;
    }
    LOG_INFO("[gui] mobile touch hooks layer={} list={}",int(g_layer_hook),int(g_cursor_list_hook));
#endif
    g_backend_installed=g_installed=true;LOG_INFO("[gui] native API v1 installed");
}
void shutdown(){
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    for(auto id:{g_draw_hook,g_update_hook})if(id!=PATCH_HOOK_INVALID_ID)patchlib_uninstall_hook(id);
    g_draw_hook=g_update_hook=PATCH_HOOK_INVALID_ID;
#if defined(__ANDROID__)
    for(auto id:{g_layer_hook,g_cursor_list_hook})if(id!=PATCH_HOOK_INVALID_ID)patchlib_uninstall_hook(id);
    g_layer_hook=g_cursor_list_hook=PATCH_HOOK_INVALID_ID;g_captured_touches.clear();g_active_cursor_valid=false;
#endif
    clear_text_cache();images::clear();g_entries.clear();g_current.reset();g_cmds.clear();g_gfx={};g_sb=nullptr;
    g_click_state.reset();g_pointer_owner.clear();g_input_owner.clear();g_drag_id.clear();g_slider_active.clear();
    g_mouse_down=g_prev_mouse_down=g_mouse_pressed=g_mouse_released=false;
#if !defined(__ANDROID__)
    g_input_sampled=false;
#endif
    g_prune=false;g_order=0;g_in_render=false;g_backend_installed=g_installed=false;
}
} // namespace kernelloader::gui

