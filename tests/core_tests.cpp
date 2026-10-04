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
    e.reset();e.sentenceMode=true;type(e,"I");e.key(Key::Space);type(e,"have 2 apples");e.punctuation(L'.');
    check(e.buffer=="I have 2 apples."&&e.count()==0&&!e.entry(),"sentence retains spaces numbers and punctuation without word candidates");
    auto original=e.buffer;auto token=e.beginTranslation();check(e.translating,"translation pending state");
    check(e.completeTranslation(token,original,{{L"我有两个苹果。",L""},{L"我有两颗苹果。",L""}})&&e.count()==2,"translation candidates arrive");
    e.key(Key::Down);check(e.key(Key::Space).output==L"我有两颗苹果。","sentence selection commits current expression");
    type(e,"bank");check(e.key(Key::Enter).output.empty()&&e.buffer=="bank","sentence Enter without candidates keeps English for translation");
    check(e.key(Key::CommitEnglish).output==L"bank"&&e.buffer.empty(),"sentence English shortcut commits original text");
    type(e,"I have 2 apples.");original=e.buffer;token=e.beginTranslation();
    check(e.key(Key::Enter).output.empty()&&e.translating&&e.buffer==original,"Enter while translating neither commits nor resets the request");
    e.completeTranslation(token,original,{{L"我有两个苹果。",L""},{L"我有两颗苹果。",L""}});e.key(Key::Down);
    check(e.key(Key::Enter).output==L"我有两颗苹果。"&&e.buffer.empty(),"Enter confirms highlighted Chinese sentence");
    type(e,"I have 2 apples.");original=e.buffer;token=e.beginTranslation();e.completeTranslation(token,original,{{L"我有两个苹果。",L""}});
    check(e.key(Key::CommitEnglish).output==L"I have 2 apples.","English shortcut bypasses available Chinese candidates");
    type(e,"hello");original=e.buffer;token=e.beginTranslation();check(e.key(Key::CommitEnglish).output==L"hello","English shortcut works during pending translation");
    check(!e.completeTranslation(token,original,{{L"你好",L""}})&&e.buffer.empty(),"English commit discards late Chinese results");
    type(e,"hello");original=e.buffer;token=e.beginTranslation();e.type('!');
    check(!e.completeTranslation(token,original,{{L"你好",L""}})&&e.buffer=="hello!"&&e.count()==0,"editing invalidates in-flight translation");
    e.reset();type(e,"hello");token=e.beginTranslation();e.reset();type(e,"hello");
    check(!e.completeTranslation(token,"hello",{{L"你好",L""}}),"same text after cancellation rejects stale result");
    original=e.buffer;token=e.beginTranslation();check(e.completeTranslation(token,original,{},L"offline")&&e.buffer==original&&!e.translating&&!e.sentenceStatus.empty(),"failure preserves English and allows retry");
    check(e.key(Key::Enter).output.empty()&&e.buffer==original,"Enter after translation failure keeps English for retry");
    e.reset();type(e,std::string(500,'a').c_str());e.type('b');check(e.buffer.size()==500,"sentence length bounded");e.key(Key::Escape);check(e.buffer.empty(),"sentence Escape cancels");
    type(e,"hello");original=e.buffer;token=e.beginTranslation();e.key(Key::Toggle);check(e.english&&!e.completeTranslation(token,original,{{L"你好",L""}}),"English toggle cancels pending result");e.key(Key::Toggle);e.sentenceMode=false;
    e.reset();e.sentenceMode=true;type(e,"My cat sleeps.");e.key(Key::Home);e.key(Key::Right);e.key(Key::Right);e.key(Key::Right);type(e,"black ");
    check(e.buffer=="My black cat sleeps."&&e.caret==9,"insert at middle caret");
    e.key(Key::Home);e.key(Key::Right,true);e.key(Key::Right,true);type(e,"Your");
    check(e.buffer=="Your black cat sleeps."&&e.caret==4&&!e.hasSelection(),"Shift selection replaced by typing");
    e.key(Key::Right);e.key(Key::Delete,false,true);check(e.buffer=="Your cat sleeps."&&e.caret==5,"Ctrl Delete removes next word");
    e.key(Key::End);e.key(Key::Backspace);check(e.buffer=="Your cat sleeps"&&e.caret==15,"Backspace deletes before caret");
    e.key(Key::Home);e.key(Key::Delete);check(e.buffer=="our cat sleeps"&&e.caret==0,"Delete removes after caret");
    e.key(Key::Right,false,true);check(e.caret==4,"Ctrl Right moves to next word");
    e.key(Key::Right,true,true);check(e.caret==8&&e.selectionAnchor==4,"Ctrl Shift Right selects next word");
    e.key(Key::Backspace);check(e.buffer=="our sleeps"&&e.caret==4,"selected word deletion");
    e.key(Key::Home);original=e.buffer;token=e.beginTranslation();e.key(Key::Right);
    check(e.translating&&e.completeTranslation(token,original,{{L"我们的睡眠",L""}}),"caret motion alone does not invalidate pending text");
    e.key(Key::Left);check(e.count()==0,"moving caret dismisses completed candidates for editing");
    original=e.buffer;token=e.beginTranslation();e.type('a');check(!e.completeTranslation(token,original,{{L"过期",L""}}),"middle insertion invalidates pending translation");
    e.reset();type(e,std::string(500,'a').c_str());e.key(Key::Home);e.key(Key::End,true);e.type('b');check(e.buffer=="b"&&e.caret==1,"selection replacement allowed at length limit");
    e.key(Key::Home);token=e.beginTranslation();auto revision=e.revision;e.key(Key::Backspace);check(e.revision==revision&&e.translating,"no-op deletion does not cancel translation");
    e.reset();e.sentenceMode=false;
    std::cout<<"checks="<<checks<<" failures="<<failures<<" entries="<<d->size()<<" load_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(loaded-start).count()<<" correction_average_ms="<<elapsed/5<<"\n";
    return failures?1:0;
}
