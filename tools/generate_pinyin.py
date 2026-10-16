# Dictionary derived from pypinyin 0.55.0 and jieba 0.42.1 (MIT; see LICENSES).
# Optional regeneration: pip install pypinyin==0.55.0 jieba==0.42.1
import re
from pathlib import Path
import jieba
from pypinyin import lazy_pinyin,pinyin,Style
freq={};phrases=[]
for line in (Path(jieba.__file__).parent/'dict.txt').read_text().splitlines():
    word,f,*_=line.split();f=int(f)
    if all('\u4e00'<=c<='\u9fff' for c in word):
        if len(word)==1:freq[word]=f
        elif 2<=len(word)<=6 and f>=30:phrases.append((word,f))
phrases=sorted(phrases,key=lambda x:-x[1])[:35000]
entries={}
def add(key,word,score):
    if key.isascii() and key.isalpha():entries.setdefault(key,{})[word]=max(entries.get(key,{}).get(word,0),score)
for code in range(0x4e00,0xa000):
    c=chr(code)
    for key in pinyin(c,style=Style.NORMAL,heteronym=True)[0]:add(key,c,freq.get(c,1))
for word,f in phrases:add(''.join(lazy_pinyin(word)),word,f)
for key,word in [('nihao','你好'),('zhongwen','中文'),('ceshi','测试'),('shuru','输入'),('tianjian','天剑'),('tiandingjian','天顶剑'),('mofacunchu','魔法存储'),('tailaruiya','泰拉瑞亚'),('wuqi','武器'),('shipin','饰品'),('hecheng','合成'),('beibao','背包'),('shezhi','设置'),('chuangkou','窗口')]:add(key,word,10**9)
(Path(__file__).resolve().parents[1]/'resources/ime/pinyin.tsv').write_text(''.join(k+'\t'+' '.join(w for w,_ in sorted(v.items(),key=lambda x:(-x[1],x[0])))+'\n' for k,v in sorted(entries.items())))
print(len(entries),'pinyin keys',sum(map(len,entries.values())),'candidate entries')
