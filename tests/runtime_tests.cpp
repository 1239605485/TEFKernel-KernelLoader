#include "core.hpp"
#include "weapon_action.hpp"
#include "logger.hpp"
#include "tefkernel/memdl/memdl.h"
#include "tefkernel/tefplugin/tpf_core.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <spdlog/sinks/base_sink.h>
#define CHECK(x) do {if(!(x)){std::cerr<<__LINE__<<": "<<#x<<'\n';std::exit(1);}} while(0)
namespace {
int closed{},cleaned{},initialized{},residency=1;
bool resident{},hasLogger{};
void *loggerSlot{};
const wa_api_v1 *actionSlot{};
void init(kernel_mod_handle_t *) {++initialized;}
void cleanup(kernel_mod_handle_t *) {++cleaned;}
kernel_mod_info_t info{"test.mod",1,1,"1.0"};
kernel_mod_info_t *getInfo(){return &info;}
kernel_mod_ops_t ops{init,cleanup,getInfo};
kernel_mod_ops_t *create(){return &ops;}
memdl_handle_t openLibrary(const void *,std::size_t,int){return reinterpret_cast<void *>(1);}
int closeLibrary(memdl_handle_t){++closed;return 0;}
int validate(const void *,std::size_t){return 0;}
bool registerLibrary(void *){return true;}
void *symbol(memdl_handle_t,const char *name){
 if(!std::strcmp(name,"create_kernel_mod"))return reinterpret_cast<void *>(create);
 if(!std::strcmp(name,"kernel_mod_residency_v1"))return resident?&residency:nullptr;
 if(!std::strcmp(name,"kernel_weapon_action_api_v1"))return &actionSlot;
 if(!std::strcmp(name,"mod_logger_write"))return hasLogger?&loggerSlot:nullptr;
 return nullptr;
}
class CountSink:public spdlog::sinks::base_sink<std::mutex>{
 public:int count{};
 protected:void sink_it_(const spdlog::details::log_msg &)override{++count;}
 void flush_()override{}
};
}
int main(){
 using namespace kernelloader;
 logger::init();logger::init_mod();
 CHECK(logger::g_logger && logger::g_logger->name()=="KernelLoader");
 CHECK(logger::g_mod_logger && logger::g_mod_logger->name()=="KernelLoaderMod");
 auto coreLogger=logger::g_logger,modLogger=logger::g_mod_logger;
 logger::init();logger::init_mod();
 CHECK(logger::g_logger==coreLogger && logger::g_mod_logger==modLogger);
 auto coreSink=std::make_shared<CountSink>(),modSink=std::make_shared<CountSink>();
 coreLogger->sinks()={coreSink};modLogger->sinks()={modSink};
 logger::mod_logger_write_imp(MOD_LOG_LEVEL_INFO,"Test","once %d",7);
 CHECK(modSink->count==1 && coreSink->count==0);
 memdl_open=openLibrary;memdl_close=closeLibrary;memdl_validate=validate;memdl_sym=symbol;
 tpf_register_shared_plugin_library=registerLibrary;
 auto root=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 auto privateDir=root/"mods/loader/private/test.mod";
 std::filesystem::create_directories(privateDir/"lib");
 std::filesystem::create_directories(root/"logs/tefkernel");
 auto config=root/"mod.json";std::ofstream(config)<<"{\"lib_name\":\"Test\"}";
 // This host regression exercises Linux x64 loader behavior without loading code.
 std::ofstream(privateDir/"lib/libTest.linux.x64.so")<<"fixture";
 auto path=config.string(),priv=privateDir.string();
 mod_manifest_t manifest{path.c_str(),"test.mod",priv.c_str(),nullptr};
 resident=true;hasLogger=true;
 CHECK(core::load_mod(&manifest)==ML_SUCCESS && residency==2 && loggerSlot && actionSlot && actionSlot->abi==1);
 CHECK(core::init_mod(&manifest)==ML_SUCCESS && initialized==1);
 CHECK(core::reload_mod(&manifest)==ML_ERROR);
 CHECK(cleaned==0 && core::kernel_mod_handles.count("test.mod")==1);
 CHECK(core::unload_mod(&manifest)==ML_SUCCESS && cleaned==1 && closed==0 && !loggerSlot && !actionSlot);
 CHECK(core::load_mod(&manifest)==ML_ERROR);
 // A regular mod without an optional logger slot must load and unload normally.
 manifest.mod_id="test.regular";resident=false;hasLogger=false;
 CHECK(core::load_mod(&manifest)==ML_SUCCESS);
 CHECK(core::reload_mod(&manifest)==ML_SUCCESS);
 CHECK(core::unload_mod(&manifest)==ML_SUCCESS);
 CHECK(closed==2);
 std::filesystem::remove_all(root);
 logger::shutdown_mod();logger::shutdown_mod();CHECK(!logger::g_mod_logger);
 logger::shutdown();logger::shutdown();CHECK(!logger::g_logger);
 std::cout<<"loader runtime checks passed\n";
}
