#define WIN32_LEAN_AND_MEAN
#include "local_ai.h"
#include <winhttp.h>
#include <mmsystem.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <filesystem>
#include <sstream>
namespace etype {
#ifdef ETYPE_ONLINE
static constexpr INTERNET_PORT workerPort=49182;
static constexpr const char* workerMode="\"online_only\":true";
#else
static constexpr INTERNET_PORT workerPort=49181;
static constexpr const char* workerMode="\"local_only\":true";
#endif
struct HttpHandle {
    HINTERNET value;
    explicit HttpHandle(HINTERNET v):value(v){if(!v)throw std::runtime_error("Local AI unavailable");}
    ~HttpHandle(){WinHttpCloseHandle(value);}
    HttpHandle(const HttpHandle&)=delete;
};
static bool workerReady(){
    try{
        HttpHandle session(WinHttpOpen(L"EType/0.2",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,0));
        WinHttpSetTimeouts(session.value,200,200,200,200);
        HttpHandle connection(WinHttpConnect(session.value,L"127.0.0.1",workerPort,0));
        HttpHandle request(WinHttpOpenRequest(connection.value,L"GET",L"/health",nullptr,nullptr,nullptr,0));
        DWORD disable=WINHTTP_DISABLE_REDIRECTS;WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable));
        if(!WinHttpSendRequest(request.value,nullptr,0,nullptr,0,0,0)||!WinHttpReceiveResponse(request.value,nullptr))return false;
        DWORD status=0,size=sizeof(status),read=0;char buffer[1024]{};
        if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr)||status!=200||!WinHttpReadData(request.value,buffer,sizeof(buffer)-1,&read))return false;
        std::string body(buffer,read);body.erase(std::remove_if(body.begin(),body.end(),[](unsigned char c){return c==' '||c=='\n'||c=='\r';}),body.end());
        return body.find("\"native_protocol\":1")!=std::string::npos&&body.find(workerMode)!=std::string::npos;
    }catch(...){return false;}
}
static void ensureWorker(HANDLE cancel=nullptr){
    if(cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)throw std::runtime_error("Speech cancelled");
    HMODULE module=nullptr;wchar_t path[32768]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&ensureWorker,&module)||!GetModuleFileNameW(module,path,32768))return;
    auto root=std::filesystem::path(path).parent_path();
    if(root.filename()==L"x64"||root.filename()==L"x86")root=root.parent_path();
    auto executable=root/L"runtime"/L"ETypeService.exe";
#ifdef ETYPE_ONLINE
    executable=root/L"runtime"/L"ETypeOnlineService.exe";
#endif
    // Development builds keep their explicitly started development service.
    if(!std::filesystem::exists(executable))return;
    if(workerReady())return;
    std::wstring command=L"\""+executable.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&process))throw std::runtime_error("Cannot start local AI");
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    auto deadline=GetTickCount64()+70000;
    while(GetTickCount64()<deadline){
        if(cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)throw std::runtime_error("Speech cancelled");
        if(workerReady())return;
        if(cancel)WaitForSingleObject(cancel,150);else Sleep(150);
    }
    throw std::runtime_error("Local AI startup timed out");
}
// Keep callback state and buffers alive until HANDLE_CLOSING, the final
// notification. Only async request handles can safely cancel in-flight HTTP.
struct AsyncHttpState {
    HANDLE ready=nullptr,closed=nullptr;bool callbackInstalled=false;
    volatile LONG status=0,bytes=0;
    unsigned char buffer[8192]{};
    AsyncHttpState(){ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);closed=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!ready||!closed){if(ready)CloseHandle(ready);if(closed)CloseHandle(closed);throw std::runtime_error("HTTP event creation failed");}}
    ~AsyncHttpState(){if(callbackInstalled)WaitForSingleObject(closed,INFINITE);CloseHandle(ready);CloseHandle(closed);}
    void reset(){ResetEvent(ready);InterlockedExchange(&status,0);InterlockedExchange(&bytes,0);}
    DWORD wait(DWORD expected,HANDLE cancel){HANDLE events[]{cancel,ready};auto value=WaitForMultipleObjects(2,events,FALSE,65000);
        if(value!=WAIT_OBJECT_0+1||(DWORD)InterlockedCompareExchange(&status,0,0)!=expected)throw std::runtime_error("Local AI request cancelled or failed");
        return (DWORD)InterlockedCompareExchange(&bytes,0,0);}
    static void CALLBACK callback(HINTERNET,DWORD_PTR context,DWORD notice,LPVOID,DWORD length){
        auto state=(AsyncHttpState*)context;if(!state)return;
        if(notice==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){SetEvent(state->closed);return;}
        if(notice==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE||notice==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE||notice==WINHTTP_CALLBACK_STATUS_READ_COMPLETE||notice==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR){
            InterlockedExchange(&state->bytes,(LONG)length);InterlockedExchange(&state->status,(LONG)notice);SetEvent(state->ready);}
    }
};
static std::vector<unsigned char> postCancellable(const std::string& text,const std::wstring& extra,HANDLE cancel){
    if(text.empty()||text.size()>500)throw std::runtime_error("Invalid input length");
    if(WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)throw std::runtime_error("Speech cancelled");
    ensureWorker(cancel);
    HttpHandle session(WinHttpOpen(L"EType/0.2-dev",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,WINHTTP_FLAG_ASYNC));
    WinHttpSetTimeouts(session.value,1000,1000,5000,60000);
    HttpHandle connection(WinHttpConnect(session.value,L"127.0.0.1",workerPort,0));
    AsyncHttpState state;
    // Declaration order closes the request before waiting for callback teardown,
    // and only then closes its parent connection/session.
    HttpHandle request(WinHttpOpenRequest(connection.value,L"POST",L"/native/speak",nullptr,nullptr,nullptr,0));
    DWORD_PTR context=(DWORD_PTR)&state;DWORD disable=WINHTTP_DISABLE_REDIRECTS;
    if(!WinHttpSetOption(request.value,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context))||
       !WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable)))throw std::runtime_error("HTTP options failed");
    if(WinHttpSetStatusCallback(request.value,AsyncHttpState::callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)throw std::runtime_error("HTTP callback registration failed");
    state.callbackInstalled=true;
    auto headers=L"Content-Type: text/plain; charset=utf-8\r\nX-EType-Lab: 1\r\n"+extra;
    state.reset();if(!WinHttpSendRequest(request.value,headers.c_str(),(DWORD)-1L,(void*)text.data(),(DWORD)text.size(),(DWORD)text.size(),context))throw std::runtime_error("Local AI request failed");
    state.wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,cancel);
    state.reset();if(!WinHttpReceiveResponse(request.value,nullptr))throw std::runtime_error("Local AI response failed");
    state.wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,cancel);
    DWORD status=0,size=sizeof(status);
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr)||status!=200)throw std::runtime_error("Local AI service rejected request");
    std::vector<unsigned char> result;
    for(;;){state.reset();if(!WinHttpReadData(request.value,state.buffer,sizeof(state.buffer),nullptr))throw std::runtime_error("Read failed");
        auto read=state.wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE,cancel);if(!read)break;
        if(read>sizeof(state.buffer)||read>8*1024*1024-result.size())throw std::runtime_error("Response too large");
        result.insert(result.end(),state.buffer,state.buffer+read);}
    return result;
}
static std::vector<unsigned char> post(const wchar_t* path,const std::string& text,const std::wstring& extra,size_t limit) {
    if(text.empty()||text.size()>500)throw std::runtime_error("Invalid input length");
    ensureWorker();
    HttpHandle session(WinHttpOpen(L"EType/0.2-dev",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,0));
    WinHttpSetTimeouts(session.value,1000,1000,5000,60000);
    HttpHandle connection(WinHttpConnect(session.value,L"127.0.0.1",workerPort,0));
    HttpHandle request(WinHttpOpenRequest(connection.value,L"POST",path,nullptr,nullptr,nullptr,0));
    DWORD disable=WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable));
    auto headers=L"Content-Type: text/plain; charset=utf-8\r\nX-EType-Lab: 1\r\n"+extra;
    if(!WinHttpSendRequest(request.value,headers.c_str(),(DWORD)-1L,(void*)text.data(),(DWORD)text.size(),(DWORD)text.size(),0)
        ||!WinHttpReceiveResponse(request.value,nullptr))throw std::runtime_error("Local AI request failed");
    DWORD status=0,size=sizeof(status);
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr)||status!=200)
        throw std::runtime_error(status==429?"FREE_QUOTA":"Local AI service rejected request");
    std::vector<unsigned char> result;
    for(;;){DWORD available=0;if(!WinHttpQueryDataAvailable(request.value,&available))throw std::runtime_error("Read failed");
        if(!available)break;
        if(available>limit-result.size())throw std::runtime_error("Response too large");
        size_t start=result.size();result.resize(start+available);DWORD read=0;
        if(!WinHttpReadData(request.value,result.data()+start,available,&read)||!read)throw std::runtime_error("Truncated response");
        result.resize(start+read);
    }
    return result;
}
std::vector<Candidate> translateLocal(const std::string& text) {
    auto bytes=post(L"/native/translate",text,L"",16384);
    std::string body(bytes.begin(),bytes.end());std::vector<Candidate> result;
    for(size_t start=0;start<body.size();){auto end=body.find('\n',start);auto line=body.substr(start,end==std::string::npos?end:end-start);
        auto value=wide(line);
        if(value.empty()||value.size()>1000||result.size()>=3||std::none_of(value.begin(),value.end(),[](wchar_t c){return c>=0x3400&&c<=0x9fff;}))
            throw std::runtime_error("Invalid candidate");
        if(std::none_of(result.begin(),result.end(),[&](const Candidate& c){return c.text==value;}))result.push_back({value,L""});
        if(end==std::string::npos)break;start=end+1;
    }
    if(result.empty())throw std::runtime_error("No candidates");return result;
}
Entry lookupOnlineWord(const std::string& text){
    auto bytes=post(L"/native/word",text,L"",65536);
    std::string body(bytes.begin(),bytes.end());if(body=="#MISSING")return {};
    std::istringstream rows(body);std::string line;Entry result;
    while(std::getline(rows,line)){
        std::istringstream fields(line);std::vector<std::string> values;std::string value;
        while(std::getline(fields,value,'\t'))values.push_back(value);
        if(!line.empty()&&line.back()=='\t')values.emplace_back();
        if(values.size()!=7||values[0]!=lower(text)||values[5].empty()||result.candidates.size()>=50)throw std::runtime_error("Invalid online word");
        result.word=values[0];result.phonetic=wide(values[1]);result.root=wide(values[2]);result.form=wide(values[3]);
        result.candidates.push_back({wide(values[5]),wide(values[6])});
    }
    if(result.candidates.empty())throw std::runtime_error("Invalid online word");return result;
}
std::vector<std::string> correctOnlineWord(const std::string& text){
    auto bytes=post(L"/native/correct",text,L"",1024);std::istringstream rows(std::string(bytes.begin(),bytes.end()));
    std::vector<std::string> result;std::string word;
    while(std::getline(rows,word)){
        if(word.empty()||word.size()>48||result.size()>=5||std::any_of(word.begin(),word.end(),[](char c){return !(c>='a'&&c<='z')&&c!='\''&&c!='-';}))throw std::runtime_error("Invalid correction");
        result.push_back(word);
    }return result;
}
std::vector<unsigned char> speechLocal(const std::string& text,bool male,bool slow,HANDLE cancel,bool british) {
    auto headers=std::wstring(L"X-EType-Voice: ")+(male?L"male":L"female")+L"\r\nX-EType-Speed: "+(slow?L"0.8":L"1.0")+L"\r\n";
#ifdef ETYPE_ONLINE
    headers+=std::wstring(L"X-EType-Accent: ")+(british?L"gb":L"us")+L"\r\n";
#else
    (void)british;
#endif
    return cancel?postCancellable(text,headers,cancel):post(L"/native/speak",text,headers,8*1024*1024);
}
bool playLocalAudio(const std::vector<unsigned char>& wav,int volume,HANDLE cancel) {
    if(cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)return false;
#ifdef ETYPE_ONLINE
    if(wav.size()>=100&&wav.size()<=8*1024*1024&&(!memcmp(wav.data(),"ID3",3)||(wav[0]==0xff&&(wav[1]&0xe0)==0xe0))){
        wchar_t directory[MAX_PATH]{},path[MAX_PATH]{};
        if(!GetTempPathW(MAX_PATH,directory)||!GetTempFileNameW(directory,L"eto",0,path))return false;
        HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,FILE_ATTRIBUTE_TEMPORARY,nullptr);
        DWORD written=0;bool stored=file!=INVALID_HANDLE_VALUE&&WriteFile(file,wav.data(),(DWORD)wav.size(),&written,nullptr)&&written==wav.size();
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        bool ok=false;std::wstring alias=L"ETypeOnlineAudio"+std::to_wstring(GetCurrentProcessId());
        if(stored){
            auto command=L"open \""+std::wstring(path)+L"\" type mpegvideo alias "+alias;
            if(!mciSendStringW(command.c_str(),nullptr,0,nullptr)){
                command=L"setaudio "+alias+L" volume to "+std::to_wstring(std::clamp(volume,0,100)*10);
                if(!mciSendStringW(command.c_str(),nullptr,0,nullptr)){
                    command=L"play "+alias;ok=!mciSendStringW(command.c_str(),nullptr,0,nullptr);
                    auto deadline=GetTickCount64()+120000;
                    while(ok){
                        if(cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0){ok=false;break;}
                        wchar_t mode[64]{};command=L"status "+alias+L" mode";
                        if(mciSendStringW(command.c_str(),mode,64,nullptr)){ok=false;break;}
                        if(wcscmp(mode,L"stopped")==0)break;
                        if(GetTickCount64()>deadline){ok=false;break;}
                        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                        Sleep(25);
                    }
                }
                command=L"close "+alias;mciSendStringW(command.c_str(),nullptr,0,nullptr);
            }
        }
        DeleteFileW(path);return ok;
    }
#endif
    // Validate the PCM container instead of trusting an arbitrary HTTP response.
    if(wav.size()<44||memcmp(wav.data(),"RIFF",4)||memcmp(wav.data()+8,"WAVE",4))return false;
    auto u32=[&](size_t n){uint32_t v=0;memcpy(&v,wav.data()+n,4);return v;};
    WAVEFORMATEX format{};const unsigned char* pcm=nullptr;DWORD length=0;bool hasFormat=false;
    for(size_t at=12;at+8<=wav.size();){uint32_t n=u32(at+4);size_t data=at+8;if(n>wav.size()-data)return false;
        if(!memcmp(wav.data()+at,"fmt ",4)&&n>=16){memcpy(&format,wav.data()+data,16);hasFormat=true;}
        if(!memcmp(wav.data()+at,"data",4)){pcm=wav.data()+data;length=n;}
        at=data+n+(n&1);
    }
    if(!hasFormat||!pcm||!length||format.wFormatTag!=WAVE_FORMAT_PCM||format.nChannels!=1||format.nSamplesPerSec!=24000||format.wBitsPerSample!=16||format.nBlockAlign!=2||format.nAvgBytesPerSec!=48000||length%2)return false;
    HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!done)return false;
    HWAVEOUT output=nullptr;
    if(waveOutOpen(&output,WAVE_MAPPER,&format,(DWORD_PTR)done,0,CALLBACK_EVENT)!=MMSYSERR_NOERROR){CloseHandle(done);return false;}
    ResetEvent(done);
    // Scale our own samples; never modify the user's device or system volume.
    std::vector<short> samples(length/2);memcpy(samples.data(),pcm,length);
    for(auto& sample:samples)sample=(short)((int)sample*std::clamp(volume,0,100)/100);
    WAVEHDR header{};header.lpData=(LPSTR)samples.data();header.dwBufferLength=length;
    bool prepared=waveOutPrepareHeader(output,&header,sizeof(header))==MMSYSERR_NOERROR;
    bool ok=prepared&&waveOutWrite(output,&header,sizeof(header))==MMSYSERR_NOERROR;
    if(ok){while(!(header.dwFlags&WHDR_DONE)){
        if(cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0){ok=false;break;}
        if(WaitForSingleObject(done,30)==WAIT_FAILED){ok=false;break;}
    }}
    waveOutReset(output);if(prepared)waveOutUnprepareHeader(output,&header,sizeof(header));waveOutClose(output);CloseHandle(done);return ok;
}
}
