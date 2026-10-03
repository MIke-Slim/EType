#include "core.h"
#include <windows.h>
#include <iostream>
#include <chrono>
#include <algorithm>
using namespace etype;
static int checks=0,failures=0;
static void check(bool condition,const char* name){++checks;if(!condition){++failures;std::cerr<<"FAIL: "<<name<<"\n";}}
static void type(Engine& e,const char* word){for(;*word;++word)e.type(*word);}
static bool has(const Entry* e,const wchar_t* word){return e&&std::any_of(e->candidates.begin(),e->candidates.end(),[&](const Candidate& c){return c.text==word;});}
int wmain(int argc,wchar_t** argv){
    if(argc!=2){std::cerr<<"dictionary path required\n";return 2;}
    auto d=std::make_shared<Dictionary>();auto start=std::chrono::steady_clock::now();
    check(d->load(argv[1]),"dictionary loads");check(d->size()>30000,"common dictionary coverage");
    auto loaded=std::chrono::steady_clock::now();
    Engine e(d);type(e,"ban");check(!has(e.entry(),L"银行")&&!has(e.entry(),L"河岸"),"valid ban does not prefix-complete to bank");check(!e.correcting&&e.corrections.empty(),"no correction while typing");e.type('k');
    check(e.count()==2,"bank has two fixed primary senses");check(e.choose(1).output==L"河岸","bank second sense commits");check(e.buffer.empty(),"commit clears buffer");
    type(e,"bank");check(e.key(Key::Space).output==L"银行","bank order remains fixed after selecting riverbank");
    type(e,"app");check(has(e.entry(),L"应用程序")&&!has(e.entry(),L"苹果"),"valid short word has its own meaning");e.reset();
    type(e,"aple");check(!e.correcting&&e.corrections.empty(),"typo does not autocomplete");
    auto first=e.key(Key::Space);check(first.output.empty()&&e.correcting,"space triggers suggestions without committing");
    auto apple=std::find(e.corrections.begin(),e.corrections.end(),"apple");check(apple!=e.corrections.end(),"apple is suggested for aple");
    if(apple!=e.corrections.end()){check(e.choose((size_t)(apple-e.corrections.begin())).output.empty(),"correction selection does not commit Chinese");check(e.buffer=="apple"&&!e.correcting,"correct spelling becomes composition");check(e.key(Key::Space).output==L"苹果","second confirmation commits Chinese");}else e.reset();
    type(e,"aple");check(e.key(Key::Enter).output==L"aple","Enter preserves misspelled original");
    type(e,"BANK");check(has(e.entry(),L"银行"),"lookup ignores case");check(e.key(Key::Enter).output==L"BANK","English case preserved");
    type(e,"books");check(e.entry()&&e.entry()->root==L"book","plural base shown");check(e.key(Key::Space).output==L"书","plural uses everyday root meaning");
    type(e,"went");check(e.entry()&&e.entry()->root==L"go"&&e.entry()->form.find(L"过去式")!=std::wstring::npos,"irregular past shown");check(e.key(Key::Space).output==L"去","irregular past commits base sense");
    type(e,"running");check(e.entry()&&e.entry()->root==L"run","participle root shown");check(e.key(Key::Space).output==L"跑步","participle uses everyday sense");
    type(e,"saw");check(has(e.entry(),L"锯子")&&has(e.entry(),L"看见"),"homograph retains its exact and inflected senses");e.reset();
    type(e,"happy");e.key(Key::Down);check(e.key(Key::Space).output==L"快乐","arrows select candidate");
    type(e,"aple");e.key(Key::Space);check(e.punctuation(L',').output==L"aple，","punctuation does not silently accept correction");
    type(e,"apple");check(e.punctuation(L',').output==L"苹果，","punctuation commits selected Chinese");
    check(e.punctuation(L'.').output==L"。","Chinese punctuation outside composition");
    e.chinesePunctuation=false;check(e.punctuation(L'.').output==L".","English punctuation preference");e.chinesePunctuation=true;
    type(e,"hello");auto toggle=e.key(Key::Toggle);check(toggle.output==L"hello"&&e.english,"Shift ends composition as English");
    check(e.punctuation(L',').output==L",","English mode keeps ASCII punctuation");check(!e.type('a').consumed,"English mode letters pass through");e.key(Key::Toggle);
    type(e,"apple");e.key(Key::Escape);check(e.buffer.empty(),"Escape cancels");type(e,"apple");e.key(Key::Backspace);check(e.buffer=="appl"&&!e.entry(),"backspace recomputes exact match");e.reset();
    type(e,"zzzzzzzzzzzz");e.key(Key::Space);check(e.correcting&&e.corrections.empty(),"unknown token remains uncommitted");check(e.key(Key::Enter).output==L"zzzzzzzzzzzz","unknown token can be output unchanged");
    type(e,"learn");check(e.finish().output==L"learn","focus exit keeps original English");
    type(e,"abandon");size_t count=e.count();check(count>5,"long candidate list available");e.key(Key::PageDown);check(e.pageStart()==5,"candidate pagination");check(!e.choose(0).output.empty(),"page candidate commits");
    auto before=std::chrono::steady_clock::now();for(const char* word:{"aple","compter","beutiful","freind","lern"})check(!d->correct(word).empty(),"common typo has suggestions");
    auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-before).count();
    check(d->correct("apple").empty(),"valid word never corrected");
    std::cout<<"checks="<<checks<<" failures="<<failures<<" entries="<<d->size()<<" load_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(loaded-start).count()<<" correction_average_ms="<<elapsed/5<<"\n";
    return failures?1:0;
}
