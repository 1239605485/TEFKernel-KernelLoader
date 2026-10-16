#pragma once
#include "mod_gui.h"
#include <string>
namespace kernelloader::gui {
void install();
void shutdown();
void attach(void *owner,const std::string &id,const std::string &root);
bool detach(void *owner);
const kl_gui_api *api();
}
