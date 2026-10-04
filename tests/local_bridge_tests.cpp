#include "local_ai.h"
#include <iostream>
#include <algorithm>
using namespace etype;
static int failures=0,checks=0;
static void check(bool yes,const char* name){++checks;if(!yes){++failures;std::cerr<<"FAIL "<<name<<"\n";}}
int main(){
    try {
        auto values=translateLocal("I sat on the bank.");
        check(!values.empty()&&values.size()<=3,"native bridge returns one to three candidates");
        check(std::any_of(values.begin(),values.end(),[](const Candidate& c){return c.text.find(L"河岸")!=std::wstring::npos;}),"bank context selects riverbank");
        auto audio=speechLocal("apple",false,false);
        check(audio.size()>44,"native word speech returns WAV");
        auto requestCancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        check(speechLocal("apple",false,false,requestCancel)==audio,"async speech HTTP receives the complete cached WAV");
        SetEvent(requestCancel);bool requestStopped=false;auto requestStart=GetTickCount64();try{speechLocal("apple",false,false,requestCancel);}catch(...){requestStopped=true;}
        check(requestStopped&&GetTickCount64()-requestStart<200,"already-cancelled speech does not wait for HTTP");CloseHandle(requestCancel);
        check(playLocalAudio(audio,0),"PCM playback completes at zero volume without changing device volume");
        auto cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);auto before=GetTickCount64();
        auto thread=CreateThread(nullptr,0,[](void* handle)->DWORD{Sleep(100);SetEvent((HANDLE)handle);return 0;},cancel,0,nullptr);
        check(!playLocalAudio(audio,0,cancel)&&GetTickCount64()-before<1000,"stop event interrupts native playback");WaitForSingleObject(thread,1000);CloseHandle(thread);CloseHandle(cancel);
        auto sentence=speechLocal("I sat on the bank.",true,true);
        check(playLocalAudio(sentence,0),"male slow sentence WAV plays through native device API");
        check(!playLocalAudio({1,2,3},80),"invalid audio rejected");
        bool rejected=false;try{translateLocal("");}catch(...){rejected=true;}check(rejected,"empty request rejected");
        rejected=false;try{translateLocal(std::string(501,'a'));}catch(...){rejected=true;}check(rejected,"oversized request rejected");
    } catch(const std::exception& e){check(false,e.what());}
    std::cout<<"native_bridge_checks="<<checks<<" failures="<<failures<<"\n";return failures?1:0;
}
