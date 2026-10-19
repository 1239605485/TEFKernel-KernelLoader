#include "weapon_action.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <mutex>
namespace kernelloader::weapon_action {
namespace {
struct Owner{kernel_mod_handle_t *handle{};bool resident{};};
struct Binding{uint64_t id{};kernel_mod_handle_t *owner{};int item{};wa_client client{};void *user{};};
struct Frame{uint64_t revision{};uint64_t token{};wa_input input{};wa_pose pose{};};
std::recursive_mutex mutex;
std::array<Owner,64> owners{};std::array<Binding,32> bindings{};std::array<Frame,256> frames{};
kernel_mod_handle_t *backendOwner{};wa_backend backend{};uint64_t serial{},revision{};
bool finite(float x){return std::isfinite(x);}
bool finite(wa_vec2 v){return finite(v.x)&&finite(v.y);}
Owner *owner(kernel_mod_handle_t *h){for(auto &o:owners)if(h && o.handle==h)return &o;return nullptr;}
Binding *binding(uint64_t token){for(auto &b:bindings)if(token && b.id==token)return &b;return nullptr;}
Binding *forItem(int id){for(auto &b:bindings)if(b.id && b.item==id)return &b;return nullptr;}
void cancelFrame(Frame &f,uint32_t reason,const Binding *saved=nullptr){
 auto b=saved?saved:binding(f.token);wa_client c{};void *user{};int player=f.input.player;
 if(b){c=b->client;user=b->user;}bool used=f.token!=0;f={};f.revision=++revision;
 if(used && c.cancel){try{c.cancel(player,reason,user);}catch(...){}}
}
void removeBinding(Binding &b){
 auto saved=b;b={};
 for(auto &f:frames)if(f.token==saved.id)cancelFrame(f,WA_CLIENT_REMOVED,&saved);
}
uint32_t capabilities(){std::lock_guard<std::recursive_mutex> lock(mutex);return backendOwner?backend.capabilities:0;}
int32_t bind(kernel_mod_handle_t *h,int32_t item,const wa_client *client,void *user,uint64_t *token){
 std::lock_guard<std::recursive_mutex> lock(mutex);auto o=owner(h);
 if(!o || !o->resident || item<=0 || !client || client->size!=sizeof(*client) || client->abi!=WA_ABI_V1 || !client->update || !token)return WA_INVALID;
 if(forItem(item))return WA_CONFLICT;
 if(backendOwner)for(uint32_t i=0;i<backend.reserved_count;++i)if(backend.reserved_items[i]==item)return WA_CONFLICT;
 for(auto &b:bindings)if(!b.id){b={++serial,h,item,*client,user};*token=b.id;return WA_OK;}
 return WA_FULL;
}
int32_t unbind(kernel_mod_handle_t *h,uint64_t token){
 std::lock_guard<std::recursive_mutex> lock(mutex);auto b=binding(token);
 if(!owner(h) || !b || b->owner!=h)return WA_INVALID;
 removeBinding(*b);return WA_OK;
}
int32_t attach(kernel_mod_handle_t *h,const wa_backend *desc){
 std::lock_guard<std::recursive_mutex> lock(mutex);auto o=owner(h);
 constexpr uint32_t supported=WA_INPUT|WA_SECONDARY_INPUT|WA_VISUAL_POSE|WA_DRAW;
 if(!o || !o->resident || !desc || desc->size!=sizeof(*desc) || desc->abi!=WA_ABI_V1 || desc->reserved_count>WA_MAX_RESERVED || (desc->capabilities&~supported))return WA_INVALID;
 if(backendOwner)return WA_CONFLICT;
 for(uint32_t i=0;i<desc->reserved_count;++i)if(desc->reserved_items[i]<=0 || forItem(desc->reserved_items[i]))return WA_CONFLICT;
 backend=*desc;backendOwner=h;return WA_OK;
}
void detach(){backendOwner=nullptr;backend={};for(auto &f:frames)cancelFrame(f,WA_BACKEND_REMOVED);}
int32_t detachBackend(kernel_mod_handle_t *h){std::lock_guard<std::recursive_mutex> lock(mutex);if(!h || backendOwner!=h)return WA_INVALID;detach();return WA_OK;}
bool validPose(const wa_pose &p){
 if(p.size!=sizeof(p) || (p.flags&~uint32_t(WA_APPLY_POSE)))return false;
 if(!p.flags)return true;
 return finite(p.grip)&&finite(p.rotation)&&finite(p.front_arm)&&finite(p.back_arm)&&finite(p.body_lean)&&finite(p.body_offset)&&finite(p.sprite_origin)&&finite(p.sprite_rotation)&&finite(p.scale)&&p.scale>0&&p.scale<=16&&p.sprite_effects<=3&&std::abs(p.body_offset.x)<=128&&std::abs(p.body_offset.y)<=128;
}
int32_t update(kernel_mod_handle_t *h,const wa_input *in,wa_pose *out){
 std::lock_guard<std::recursive_mutex> lock(mutex);if(out)*out={};
 if(!h || h!=backendOwner || !in || !out || in->size!=sizeof(*in) || in->player<0 || in->player>=256 || !finite(in->dt)||in->dt<0||in->dt>1||!finite(in->position)||!finite(in->vanilla_grip)||!finite(in->vanilla_rotation)||!finite(in->gravity)||!finite(in->animation_progress)||!finite(in->item_scale))return WA_INVALID;
 auto &f=frames[in->player];auto b=forItem(in->item_id);uint64_t token=b?b->id:0;
 bool same=f.token && f.token==token && f.input.world_generation==in->world_generation && f.input.player_generation==in->player_generation;
 if(same && in->tick==f.input.tick){*out=f.pose;return WA_OK;}
 if(same && in->tick<f.input.tick)return WA_INVALID;
 if(f.token && !same)cancelFrame(f,f.input.world_generation!=in->world_generation?WA_SCENE_CHANGED:(f.input.player_generation!=in->player_generation?WA_PLAYER_INVALID:WA_ITEM_CHANGED));
 // A cancellation may unsubscribe or detach. Recheck before entering mod code.
 b=forItem(in->item_id);if(backendOwner!=h || !b)return WA_UNAVAILABLE;
 wa_input snapshot=*in;snapshot.primary_down=!!in->primary_down;snapshot.secondary_down=(backend.capabilities&WA_SECONDARY_INPUT)?!!in->secondary_down:0;
 snapshot.primary_pressed=snapshot.primary_down&&(!same||!f.input.primary_down);
 snapshot.primary_released=!snapshot.primary_down&&same&&f.input.primary_down;
 snapshot.secondary_pressed=snapshot.secondary_down&&(!same||!f.input.secondary_down);
 snapshot.secondary_released=!snapshot.secondary_down&&same&&f.input.secondary_down;
 Binding selected=*b;auto dispatched=++revision;f.revision=dispatched;f.token=selected.id;f.input=snapshot;f.pose={};f.pose.size=sizeof(f.pose);
 wa_pose proposed{};proposed.size=sizeof(proposed);
 try{selected.client.update(&snapshot,&proposed,selected.user);}catch(...){return WA_INVALID;}
 if(backendOwner!=h || !binding(selected.id) || f.token!=selected.id || f.revision!=dispatched)return WA_UNAVAILABLE;
 if(!validPose(proposed))return WA_INVALID;
 if(!(backend.capabilities&WA_VISUAL_POSE))proposed.flags=0;
 f.pose=proposed;*out=proposed;return WA_OK;
}
int32_t draw(kernel_mod_handle_t *h,int32_t player,uint64_t tick,wa_draw_list *out){
 std::lock_guard<std::recursive_mutex> lock(mutex);if(out)*out={};
 if(!h || h!=backendOwner || !out || player<0 || player>=256)return WA_INVALID;
 auto &f=frames[player];auto b=binding(f.token);
 if(!b || f.input.tick!=tick || !(backend.capabilities&WA_DRAW))return WA_UNAVAILABLE;
 out->size=sizeof(*out);if(!b->client.draw)return WA_OK;
 Binding selected=*b;auto dispatched=f.revision;wa_input snapshot=f.input;wa_pose pose=f.pose;
 try{selected.client.draw(&snapshot,&pose,out,selected.user);}catch(...){out->count=0;return WA_INVALID;}
 if(backendOwner!=h || !binding(selected.id) || f.token!=selected.id || f.revision!=dispatched){out->count=0;return WA_UNAVAILABLE;}
 if(out->size!=sizeof(*out) || out->count>WA_MAX_DRAW){out->count=0;return WA_INVALID;}
 for(uint32_t i=0;i<out->count;++i){auto &c=out->commands[i];
  if(c.layer>WA_AFTER_ITEM || c.texture>WA_PIXEL_TEXTURE || !finite(c.position)||!finite(c.origin)||!finite(c.scale)||!finite(c.rotation)||c.scale.x<=0||c.scale.y<=0||c.scale.x>128||c.scale.y>128||c.effects>3||c.source.x<0||c.source.y<0||c.source.w<0||c.source.h<0){out->count=0;return WA_INVALID;}
 }
 return WA_OK;
}
int32_t cancelAll(kernel_mod_handle_t *h,uint32_t reason){std::lock_guard<std::recursive_mutex> lock(mutex);if(!h||h!=backendOwner)return WA_INVALID;for(auto &f:frames)cancelFrame(f,reason);return WA_OK;}
int32_t cancelPlayer(kernel_mod_handle_t *h,int32_t player,uint32_t reason){std::lock_guard<std::recursive_mutex> lock(mutex);if(!h||h!=backendOwner||player<0||player>=256)return WA_INVALID;cancelFrame(frames[player],reason);return WA_OK;}
int32_t wantsItem(kernel_mod_handle_t *h,int32_t item){std::lock_guard<std::recursive_mutex> lock(mutex);return h && h==backendOwner && forItem(item)?1:0;}
const wa_api_v1 table{sizeof(table),WA_ABI_V1,capabilities,bind,unbind,attach,detachBackend,update,draw,cancelAll,cancelPlayer,wantsItem};
}
const wa_api_v1 *api(){return &table;}
bool track_owner(kernel_mod_handle_t *h,bool resident){std::lock_guard<std::recursive_mutex> lock(mutex);if(!h||owner(h))return false;for(auto &o:owners)if(!o.handle){o={h,resident};return true;}return false;}
void release_owner(kernel_mod_handle_t *h){std::lock_guard<std::recursive_mutex> lock(mutex);auto o=owner(h);if(!o)return;*o={};if(backendOwner==h)detach();for(auto &b:bindings)if(b.id && b.owner==h)removeBinding(b);}
}
