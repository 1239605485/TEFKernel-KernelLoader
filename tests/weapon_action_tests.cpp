#include "weapon_action.hpp"
#include <cassert>
#include <cmath>
#include <limits>
static int calls,cancels;static uint32_t reason;static bool reenter;static const wa_api_v1 *table;static kernel_mod_handle_t *backendHandle;static wa_input seen{};static bool invalid;
static void update(const wa_input *in,wa_pose *pose,void*){++calls;seen=*in;if(reenter){reenter=false;auto next=*in;++next.tick;wa_pose nested{};assert(table->update(backendHandle,&next,&nested)==WA_OK);}if(invalid){pose->flags=WA_APPLY_POSE;pose->scale=std::numeric_limits<float>::quiet_NaN();}}
static void cancel(int32_t,uint32_t why,void*){++cancels;reason=why;}
int main(){
 using namespace kernelloader::weapon_action;
 kernel_mod_handle_t host{},owner{},other{};assert(track_owner(&host,true));assert(track_owner(&owner,true));assert(track_owner(&other,false));
 auto a=api();assert(a->abi==1 && !a->capabilities());wa_client c{sizeof(c),1,update,nullptr,cancel};uint64_t token;
 assert(a->bind(&other,4,&c,nullptr,&token)==WA_INVALID);
 assert(a->bind(&owner,4,&c,nullptr,&token)==WA_OK);uint64_t duplicate;
 assert(a->bind(&owner,4,&c,nullptr,&duplicate)==WA_CONFLICT);
 wa_backend b{sizeof(b),1,WA_INPUT|WA_VISUAL_POSE|WA_DRAW,1,{24}};
 assert(a->attach_backend(&host,&b)==WA_OK);assert(a->bind(&owner,24,&c,nullptr,&duplicate)==WA_CONFLICT);
 wa_input in{};in.size=sizeof(in);in.player=0;in.item_id=4;in.direction=1;in.gravity=1;in.item_scale=1;in.world_generation=1;in.player_generation=1;in.tick=5;in.primary_down=1;
 wa_pose p{};assert(a->update(&other,&in,&p)==WA_INVALID);assert(a->update(&host,&in,&p)==WA_OK);assert(calls==1 && seen.primary_pressed);
 assert(a->update(&host,&in,&p)==WA_OK && calls==1);
 in.tick++;in.primary_down=0;assert(a->update(&host,&in,&p)==WA_OK && seen.primary_released);
 invalid=true;in.tick++;assert(a->update(&host,&in,&p)==WA_INVALID && !p.flags);invalid=false;
 in.item_id=5;in.tick++;assert(a->update(&host,&in,&p)==WA_UNAVAILABLE && cancels==1);
 in.item_id=4;in.tick++;assert(a->update(&host,&in,&p)==WA_OK);
 table=a;backendHandle=&host;reenter=true;in.tick++;assert(a->update(&host,&in,&p)==WA_UNAVAILABLE);
 in.tick++;assert(a->update(&host,&in,&p)==WA_OK);
 ++in.player_generation;++in.tick;assert(a->update(&host,&in,&p)==WA_OK && reason==WA_PLAYER_INVALID);
 release_owner(&owner);assert(cancels==3);in.tick++;assert(a->update(&host,&in,&p)==WA_UNAVAILABLE);
 wa_pose aligned{};
 assert(wa_align_sprite(&aligned,48,54,{8.f/48,46.f/54},{45.f/48,2.f/54},{0,0},{34,0},1));
 assert(aligned.sprite_effects==2 && aligned.flags==WA_APPLY_POSE);
 assert(!wa_align_sprite(&aligned,48,54,{0,0},{0,0},{0,0},{34,0},0));
 release_owner(&host);assert(!a->capabilities());release_owner(&other);
}
