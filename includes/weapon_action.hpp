#pragma once
#include "weapon_action.h"
namespace kernelloader::weapon_action {
const wa_api_v1 *api();
bool track_owner(kernel_mod_handle_t*,bool resident);
void release_owner(kernel_mod_handle_t*);
}
