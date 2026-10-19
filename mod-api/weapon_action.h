#ifndef KERNEL_WEAPON_ACTION_H
#define KERNEL_WEAPON_ACTION_H
#include <stdint.h>
#include <math.h>
#include "mod_core.h"
#ifdef __cplusplus
extern "C" {
#endif
#define WA_ABI_V1 1u
#define WA_MAX_DRAW 32u
#define WA_MAX_RESERVED 8u
/* API v1 controls visual poses only. Vanilla gameplay remains authoritative. */
enum wa_result {WA_OK=0,WA_INVALID=-1,WA_UNAVAILABLE=-2,WA_CONFLICT=-3,WA_FULL=-4};
enum wa_cap {WA_INPUT=1u,WA_SECONDARY_INPUT=2u,WA_VISUAL_POSE=4u,WA_DRAW=8u};
enum wa_cancel {WA_ITEM_CHANGED=1,WA_SCENE_CHANGED=2,WA_CLIENT_REMOVED=3,WA_BACKEND_REMOVED=4,WA_PLAYER_INVALID=5};
enum wa_pose_flags {WA_APPLY_POSE=1u};
enum wa_layer {WA_BEHIND_PLAYER=0,WA_BEFORE_ITEM=1,WA_AFTER_ITEM=2};
enum wa_texture {WA_ITEM_TEXTURE=0,WA_PIXEL_TEXTURE=1};
typedef struct wa_vec2 {float x,y;} wa_vec2;
typedef struct wa_rect {int32_t x,y,w,h;} wa_rect;
typedef struct wa_color {uint8_t r,g,b,a;} wa_color;
typedef struct wa_input {
 uint32_t size;int32_t player,item_id,direction;uint32_t primary_down,secondary_down;
 uint32_t primary_pressed,primary_released,secondary_pressed,secondary_released;
 uint64_t tick,world_generation,player_generation;
 float dt,gravity,animation_progress;int32_t animation;
 int32_t item_width,item_height;
 wa_vec2 position,vanilla_grip;float vanilla_rotation,item_scale;
} wa_input;
/* World pixels; rotation/arm angles are clockwise from +X in world space.
   Sprite origin is in source-frame pixels. Pose flags affect visuals only.
   Secondary input is the native interaction input, not an assumed heavy key. */
typedef struct wa_pose {
 uint32_t size,flags;
 wa_vec2 grip;float rotation,front_arm,back_arm,body_lean;
 wa_vec2 body_offset;wa_vec2 sprite_origin;float sprite_rotation,scale;
 uint32_t sprite_effects;
} wa_pose;
typedef struct wa_draw_command {
 uint32_t layer,texture;int32_t item_id;wa_rect source;
 wa_vec2 position,origin,scale;float rotation;wa_color color;uint32_t effects;
} wa_draw_command;
typedef struct wa_draw_list {uint32_t size,count;wa_draw_command commands[WA_MAX_DRAW];} wa_draw_list;
typedef struct wa_client {
 uint32_t size,abi;
 /* Update runs once per simulation tick. Draw is read-only and may repeat. */
 void (*update)(const wa_input*,wa_pose*,void*);
 void (*draw)(const wa_input*,const wa_pose*,wa_draw_list*,void*);
 void (*cancel)(int32_t player,uint32_t reason,void*);
} wa_client;
typedef struct wa_backend {
 uint32_t size,abi,capabilities,reserved_count;int32_t reserved_items[WA_MAX_RESERVED];
} wa_backend;
typedef struct wa_api_v1 {
 uint32_t size,abi;
 uint32_t (*capabilities)(void);
 int32_t (*bind)(kernel_mod_handle_t*,int32_t,const wa_client*,void*,uint64_t*);
 int32_t (*unbind)(kernel_mod_handle_t*,uint64_t);
 int32_t (*attach_backend)(kernel_mod_handle_t*,const wa_backend*);
 int32_t (*detach_backend)(kernel_mod_handle_t*);
 /* Backend-only entry points. A wrong owner cannot dispatch or clear frames. */
 int32_t (*update)(kernel_mod_handle_t*,const wa_input*,wa_pose*);
 int32_t (*draw)(kernel_mod_handle_t*,int32_t,uint64_t,wa_draw_list*);
 int32_t (*cancel_all)(kernel_mod_handle_t*,uint32_t);
 int32_t (*cancel_player)(kernel_mod_handle_t*,int32_t,uint32_t);
 int32_t (*wants_item)(kernel_mod_handle_t*,int32_t);
} wa_api_v1;
/* Calibrate each sprite once using normalized grip/tip coordinates. This helper
   transforms the UV reflection, grip pivot and blade axis together. */
static inline int wa_align_sprite(wa_pose *p,float width,float height,
 wa_vec2 grip_uv,wa_vec2 tip_uv,wa_vec2 world_grip,wa_vec2 world_tip,int mirror_y){
 if(!p || !isfinite(width)||!isfinite(height)||width<=0||height<=0||width>8192||height>8192 ||
 !isfinite(grip_uv.x)||!isfinite(grip_uv.y)||!isfinite(tip_uv.x)||!isfinite(tip_uv.y)||
 !isfinite(world_grip.x)||!isfinite(world_grip.y)||!isfinite(world_tip.x)||!isfinite(world_tip.y)||
 grip_uv.x<0||grip_uv.x>1||grip_uv.y<0||grip_uv.y>1||tip_uv.x<0||tip_uv.x>1||tip_uv.y<0||tip_uv.y>1)return 0;
 wa_vec2 origin={width*grip_uv.x,height*grip_uv.y};
 wa_vec2 tip={width*tip_uv.x,height*tip_uv.y};
 if(mirror_y){origin.y=height-origin.y;tip.y=height-tip.y;}
 float ax=tip.x-origin.x,ay=tip.y-origin.y,dx=world_tip.x-world_grip.x,dy=world_tip.y-world_grip.y;
 float source_length=hypotf(ax,ay),world_length=hypotf(dx,dy);
 if(source_length<.0001f||world_length<.0001f)return 0;
 p->size=sizeof(*p);p->flags|=WA_APPLY_POSE;p->grip=world_grip;
 p->rotation=atan2f(dy,dx);p->sprite_origin=origin;p->sprite_rotation=p->rotation-atan2f(ay,ax);
 p->scale=world_length/source_length;p->sprite_effects=mirror_y?2u:0u;return 1;
}
/* Optional injection slot: define this pointer in a mod; loader fills it before init.
   Mods binding callbacks must request kernel_mod_residency_v1. */
#ifdef __cplusplus
}
#endif
#endif
