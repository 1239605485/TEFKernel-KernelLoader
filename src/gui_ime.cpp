#include "gui_ime.hpp"
#include "gui_pinyin_data.hpp"
#include <map>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_set>
namespace kernelloader::gui {
size_t utf8_count(const std::string &s){size_t n=0;for(unsigned char c:s)if((c&0xc0)!=0x80)++n;return n;}
std::string utf8_fit(const std::string &s,size_t n){n=std::min(n,s.size());if(n<s.size())while(n&&(static_cast<unsigned char>(s[n])&0xc0)==0x80)--n;return s.substr(0,n);}
namespace {
const std::map<std::string,std::vector<std::string>> &dictionary(){
    static const auto data=[]{
        std::map<std::string,std::vector<std::string>> result;
        std::istringstream input(gui_pinyin_data);std::string line;
        while(std::getline(input,line)){auto tab=line.find('\t');if(tab==std::string::npos)continue;
            std::istringstream words(line.substr(tab+1));std::string value;auto &out=result[line.substr(0,tab)];
            while(words>>value)out.push_back(value);
        }return result;
    }();return data;
}
}
void PinyinEditor::open(const std::string &id,const std::string &initial,size_t capacity,int char_limit){
    owner=id;max_bytes=capacity?capacity-1:0;limit=char_limit;
    text=original=utf8_fit(initial,max_bytes);composition.clear();candidates.clear();page=0;active=true;done=canceled=false;
}
bool PinyinEditor::append(const std::string &s){
    if(text.size()+s.size()>max_bytes||(limit>0&&utf8_count(text)+utf8_count(s)>static_cast<size_t>(limit)))return false;
    text+=s;return true;
}
void PinyinEditor::suggest(){
    candidates.clear();page=0;if(composition.empty())return;
    const auto &dict=dictionary();std::unordered_set<std::string> seen;
    for(size_t n=composition.size();n>0;--n){
        auto it=dict.find(composition.substr(0,n));if(it==dict.end())continue;
        for(const auto &word:it->second)if(seen.insert(word).second)candidates.push_back({word,n});
        // Full phrases first, followed by individual syllables for arbitrary text.
        if(candidates.size()>=512)break;
    }
    if(candidates.empty()){
        auto it=dict.lower_bound(composition);size_t checked=0;
        while(it!=dict.end()&&it->first.rfind(composition,0)==0&&checked++<8){
            for(const auto &word:it->second)if(seen.insert(word).second){candidates.push_back({word,composition.size()});if(candidates.size()>=24)break;}
            if(candidates.size()>=24)break;++it;
        }
    }
}
void PinyinEditor::key(char c){if(!active)return;
    if(mode==0&&std::isalpha(static_cast<unsigned char>(c))){if(composition.size()<48){composition+=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));suggest();}}
    else append(std::string(1,(mode==1&&upper)?static_cast<char>(std::toupper(static_cast<unsigned char>(c))):c));
}
void PinyinEditor::choose(size_t i){if(!active||i>=candidates.size())return;const auto value=candidates[i];if(append(value.text)){composition.erase(0,value.consumed);suggest();}}
void PinyinEditor::backspace(){if(!active)return;if(!composition.empty()){composition.pop_back();suggest();}else if(!text.empty()){size_t n=text.size()-1;while(n&&(static_cast<unsigned char>(text[n])&0xc0)==0x80)--n;text.resize(n);}}
void PinyinEditor::clear(){if(!active)return;text.clear();composition.clear();candidates.clear();page=0;}
void PinyinEditor::space(){if(!active)return;if(!composition.empty()){if(!candidates.empty())choose(0);else if(append(composition)){composition.clear();suggest();}}else append(" ");}
void PinyinEditor::finish(bool commit){if(!active)return;if(commit){while(!composition.empty()){const auto old=composition;space();if(old==composition)break;}done=true;}else{text=original;canceled=true;}composition.clear();candidates.clear();active=false;}
void PinyinEditor::change_mode(){while(!composition.empty()){auto old=composition;space();if(old==composition)return;}mode=(mode+1)%3;}
}
