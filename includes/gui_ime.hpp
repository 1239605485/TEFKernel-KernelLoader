#pragma once
#include <string>
#include <vector>
#include <cstddef>
namespace kernelloader::gui {
struct ImeCandidate {std::string text;size_t consumed=0;};
class PinyinEditor {
public:
    std::string owner,text,original,composition;
    std::vector<ImeCandidate> candidates;
    bool active=false,done=false,canceled=false;
    int mode=0; // 0 Chinese pinyin, 1 Latin, 2 numbers/symbols
    bool upper=false;
    size_t page=0,max_bytes=255;
    int limit=0;
    void open(const std::string &id,const std::string &initial,size_t capacity,int char_limit);
    void key(char c);
    void choose(size_t index);
    void backspace();
    void clear();
    void space();
    void finish(bool commit);
    void change_mode();
private:
    bool append(const std::string &value);
    void suggest();
};
size_t utf8_count(const std::string &text);
std::string utf8_fit(const std::string &text,size_t bytes);
}
