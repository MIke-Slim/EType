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
    check(has(d->find("lay"),L"放置")&&has(d->find("lay"),L"躺着")&&!has(d->find("lay"),L"说谎")&&!has(d->find("lay"),L"谎言"),"lay retains direct meanings without unrelated lie senses");
    check(has(d->find("wound"),L"伤口")&&has(d->find("wound"),L"缠绕")&&!has(d->find("wound"),L"风"),"wound retains both genuine homograph meanings without wind noun");
    check(has(d->find("left"),L"左边的")&&has(d->find("left"),L"离开")&&!has(d->find("left"),L"休假")&&!has(d->find("left"),L"生叶"),"left preserves its direct sense and correct verb form");
    check(has(d->find("better"),L"更好")&&!has(d->find("better"),L"好"),"comparative keeps comparative Chinese meaning");
    check(has(d->find("best"),L"最好")&&!has(d->find("best"),L"好"),"superlative keeps superlative Chinese meaning");
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
    type(e,"abandon");size_t count=e.count();check(count>5,"long candidate list available");
    e.key(Key::Down);auto firstPageSelection=e.selected;e.key(Key::PageUp);
    check(e.selected==firstPageSelection,"Page Up on first page preserves highlighted sense");
    e.key(Key::PageDown);check(e.pageStart()==5,"candidate pagination");
    while(e.pageStart()+5<count)e.key(Key::PageDown);
    auto finalPageSelection=e.selected;e.key(Key::PageDown);
    check(e.selected==finalPageSelection,"Page Down on last page preserves highlighted sense");
    check(e.choose(4).output.empty()&&!e.buffer.empty(),"unused numeric slot on short last page cannot commit");
    check(!e.choose(0).output.empty(),"page candidate commits");
    type(e,"bank");e.key(Key::Up);check(e.key(Key::Space).output==L"河岸","Up wraps to final sense");
    type(e,"bank");e.key(Key::Down);e.key(Key::Down);check(e.key(Key::Space).output==L"银行","Down wraps to first sense");
    type(e,"aple");e.key(Key::Space);e.key(Key::Backspace);check(e.buffer=="apl"&&!e.correcting&&e.corrections.empty(),"editing typo dismisses stale corrections");e.reset();
    type(e,"aple");e.key(Key::Space);e.type('s');check(e.buffer=="aples"&&!e.correcting&&e.selected==0,"typing after suggestions restarts exact lookup");e.reset();
    type(e,"unknownword");check(e.punctuation(L'!').output==L"unknownword！","punctuation preserves unmatched English");
    check(d->correct("a").empty()&&d->correct(std::string(49,'z')).empty(),"too short or too long tokens do not trigger correction");
    auto transposed=d->correct("freind");check(std::find(transposed.begin(),transposed.end(),"friend")!=transposed.end(),"adjacent transposition suggests friend");
    auto upperCorrections=d->correct("APLE");check(upperCorrections==d->correct("aple"),"correction ranking ignores input case");
    check(d->correct("aple")==d->correct("aple"),"correction ranking is deterministic");
    auto before=std::chrono::steady_clock::now();for(const char* word:{"aple","compter","beutiful","freind","lern"})check(!d->correct(word).empty(),"common typo has suggestions");
    auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-before).count();
    check(d->correct("apple").empty(),"valid word never corrected");
    std::cout<<"checks="<<checks<<" failures="<<failures<<" entries="<<d->size()<<" load_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(loaded-start).count()<<" correction_average_ms="<<elapsed/5<<"\n";
    return failures?1:0;
}
