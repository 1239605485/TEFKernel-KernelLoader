#include "gui_ime.hpp"
#include <cassert>
#include <algorithm>
using namespace kernelloader::gui;
static void type(PinyinEditor &e,const char *s){for(;*s;++s)e.key(*s);}
int main(){
    PinyinEditor e;e.open("test","",128,32);type(e,"nihao");
    assert(!e.candidates.empty()&&e.candidates.front().text=="你好");e.choose(0);assert(e.text=="你好");
    type(e,"zhongwen");e.choose(0);assert(e.text=="你好中文");e.backspace();assert(e.text=="你好中");
    e.finish(false);assert(e.canceled&&e.text.empty());
    e.open("test","",128,32);type(e,"tiandingjian");assert(e.candidates.front().text=="天顶剑");e.finish(true);assert(e.done&&e.text=="天顶剑");
    e.open("test","",128,32);type(e,"nihaozhong");auto it=std::find_if(e.candidates.begin(),e.candidates.end(),[](const auto &c){return c.text=="你好";});assert(it!=e.candidates.end());e.choose(it-e.candidates.begin());assert(e.composition=="zhong");
    e.open("test","😀中",128,8);e.backspace();assert(e.text=="😀");e.backspace();assert(e.text.empty());
    e.open("test","中文",5,1);assert(e.text=="中");type(e,"ni");e.choose(0);assert(e.text=="中");
    e.open("test","",16,8);e.mode=1;e.upper=true;e.key('a');assert(e.text=="A");e.change_mode();e.key('1');e.key('!');assert(e.text=="A1!");e.clear();assert(e.text.empty());
    assert(utf8_fit("中😀",6)=="中");assert(utf8_count("中😀")==2);
}
