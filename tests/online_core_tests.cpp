#include "core.h"
#include "local_ai.h"
#include <iostream>
#include <algorithm>
using namespace etype;
int main(){
    int failures=0,checks=0;auto check=[&](bool value){++checks;if(!value){++failures;std::cerr<<"FAIL check "<<checks<<"\n";}};
    auto dictionary=std::make_shared<Dictionary>();Engine engine(dictionary);engine.onlineWords=true;
    for(char c:std::string("bank"))engine.type(c);auto token=engine.beginWordLookup();
    Entry entry;entry.word="bank";entry.candidates={{L"银行",L"名词"},{L"河岸",L"名词"}};
    check(engine.count()==0);check(engine.completeWordLookup(token,"bank",entry));check(engine.count()==2);
    engine.key(Key::Down);check(engine.key(Key::Space).output==L"河岸");
    for(char c:std::string("bank"))engine.type(c);token=engine.beginWordLookup();engine.type('s');
    check(!engine.completeWordLookup(token,"bank",entry));check(engine.buffer=="banks"&&engine.count()==0);
    engine.reset();for(char c:std::string("aple"))engine.type(c);token=engine.beginWordLookup(true);
    check(engine.completeWordLookup(token,"aple",{}, {"apple"}));check(engine.key(Key::Space).output.empty());
    check(engine.buffer=="apple"&&engine.count()==0);token=engine.beginWordLookup();
    engine.finish();check(!engine.completeWordLookup(token,"apple",{}));
    engine.sentenceMode=true;for(char c:std::string("I have 2 apples."))engine.type(c);
    token=engine.beginTranslation();check(engine.key(Key::CommitEnglish).output==L"I have 2 apples.");
    check(!engine.completeTranslation(token,"I have 2 apples.",{{L"我有两个苹果。",L""}}));
    try{auto online=lookupOnlineWord("bank");check(online.candidates.size()==2);check(online.candidates[1].text==L"河岸");
        auto corrections=correctOnlineWord("aple");check(std::find(corrections.begin(),corrections.end(),"apple")!=corrections.end());
        auto chinese=translateLocal("I sat on the bank.");check(chinese.size()==1&&chinese[0].text.find(L"岸")!=std::wstring::npos);
        auto audio=speechLocal("I sat on the bank.",false,false);check(audio.size()>100&&playLocalAudio(audio,0));
    }catch(const std::exception& error){std::cout<<error.what()<<"\n";++failures;}
    std::cout<<"online_checks="<<checks<<" failures="<<failures<<"\n";return failures?1:0;
}
