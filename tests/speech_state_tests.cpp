#include "platform.h"
#include <filesystem>
#include <iostream>
using namespace etype;
static int checks=0,failures=0;
static void check(bool yes,const char* name){++checks;if(!yes){++failures;std::cerr<<"FAIL "<<name<<"\n";}}
struct Child {
    HANDLE process=nullptr;DWORD pid=0;
    Child(const std::wstring& exe,const std::wstring& args){
        auto command=L"\""+exe+L"\" "+args;STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION info{};
        if(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)){process=info.hProcess;pid=info.dwProcessId;CloseHandle(info.hThread);}
    }
    ~Child(){if(process){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){TerminateProcess(process,99);WaitForSingleObject(process,1000);}CloseHandle(process);}}
    DWORD finish(){if(!process||WaitForSingleObject(process,18000)!=WAIT_OBJECT_0)return 99;DWORD code=99;GetExitCodeProcess(process,&code);return code;}
};
static bool state(DWORD pid,SpeechState wanted,DWORD timeout=20000){auto end=GetTickCount64()+timeout;do{auto value=speechSnapshot();if(value.pid==pid&&value.state==wanted)return true;Sleep(5);}while(GetTickCount64()<end);return false;}
int wmain(int argc,wchar_t** argv){
    if(argc>=2&&std::wstring(argv[1])==L"--owner-exit"){auto event=prepareSpeechWorker();if(event)CloseHandle(event);return event?0:1;}
    if(argc<2)return 2;
    auto isolated=std::filesystem::absolute(L"build/speech-state-test.ini").wstring();
    SetEnvironmentVariableW(L"ETYPE_HEADLESS_TEST",L"1");SetEnvironmentVariableW(L"ETYPE_TEST_SETTINGS",isolated.c_str());Settings settings;settings.volume=0;check(writeSettings(settings),"isolated zero-volume profile");
    speechSnapshot(); // Keep the mapping alive across workers so final state remains observable.
    const std::wstring longSpeech=L"--speak \"Please check all the details carefully before you send the final report to the entire team tomorrow morning.\"";
    {
        Child child(argv[1],L"--speak apple");check(state(child.pid,SpeechState::Preparing),"preparing state crosses process boundary");check(state(child.pid,SpeechState::Playing),"playing state crosses process boundary");check(child.finish()==0&&state(child.pid,SpeechState::Finished,100),"completed playback offers replay");
    }
    {
        Child child(argv[1],longSpeech);check(state(child.pid,SpeechState::Playing),"sentence playback starts");auto start=GetTickCount64();stopSpeech();auto value=speechSnapshot();check(value.pid==child.pid&&(value.state==SpeechState::Stopping||value.state==SpeechState::Stopped),"stop request reports state");check(child.finish()==1&&state(child.pid,SpeechState::Stopped,100)&&GetTickCount64()-start<1500,"stop interrupts actual audio promptly");
    }
    {
        auto unique=L"--speak \"A new voice preparation test, number "+std::to_wstring(GetCurrentProcessId())+L".\"";
        Child child(argv[1],unique);check(state(child.pid,SpeechState::Preparing),"uncached preparation visible");Sleep(200);auto start=GetTickCount64();stopSpeech();check(child.finish()==1&&state(child.pid,SpeechState::Stopped,100)&&GetTickCount64()-start<1500,"stop during generation exits promptly without later audio");
    }
    {
        Child old(argv[1],longSpeech);check(state(old.pid,SpeechState::Playing),"first playback starts before replay");Child latest(argv[1],L"--speak apple");check(latest.finish()==0,"replay completes");check(old.finish()==1,"replay cancels previous worker");auto value=speechSnapshot();check(value.pid==latest.pid&&value.state==SpeechState::Finished,"previous worker cannot overwrite replay status");
    }
    {
        Child child(argv[1],L"--speak \"\"");check(child.finish()==1&&state(child.pid,SpeechState::Failed,100),"failed synthesis reports failure without a dialog");
    }
    {
        auto self=std::filesystem::absolute(argv[0]).wstring();Child child(self,L"--owner-exit");check(child.finish()==0&&state(child.pid,SpeechState::Failed,100),"unexpected owner exit is detected");
    }
    DeleteFileW(isolated.c_str());std::cout<<"speech_state_checks="<<checks<<" failures="<<failures<<"\n";return failures?1:0;
}
