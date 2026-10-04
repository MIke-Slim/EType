// Integration tests use real Windows TSF contexts and the actual input-service
// DLL, backed by an in-memory text store. No registration or global key hooks.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <msctf.h>
#include <textstor.h>
#include <iostream>
#include <algorithm>
#include <string>
#include <filesystem>
#include <cctype>
#include "identity.h"
#include "core.h"
template<class T>static void drop(T*& p){if(p){p->Release();p=nullptr;}}
static int checks=0,failures=0;
static void check(bool yes,const char* name){++checks;if(!yes){++failures;std::cerr<<"FAIL "<<name<<"\n";}}
// This process-local factory captures the instance Windows itself activates.
// TF_RP_LOCALPROCESS keeps the profile out of the user's persistent IME list.
class CapturingFactory final:public IClassFactory {
    LONG refs_=1;IClassFactory* forward_;
public:
    ITfTextInputProcessor* active=nullptr;
    explicit CapturingFactory(IClassFactory* f):forward_(f){f->AddRef();}
    ~CapturingFactory(){drop(active);forward_->Release();}
    STDMETHODIMP QueryInterface(REFIID id,void** p)override{*p=nullptr;if(id==IID_IUnknown||id==IID_IClassFactory){*p=static_cast<IClassFactory*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    STDMETHODIMP_(ULONG) AddRef()override{return InterlockedIncrement(&refs_);}
    STDMETHODIMP_(ULONG) Release()override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    STDMETHODIMP CreateInstance(IUnknown* outer,REFIID id,void** p)override{auto hr=forward_->CreateInstance(outer,id,p);if(SUCCEEDED(hr)){drop(active);((IUnknown*)*p)->QueryInterface(IID_ITfTextInputProcessor,(void**)&active);}return hr;}
    STDMETHODIMP LockServer(BOOL lock)override{return forward_->LockServer(lock);}
};
class Store final:public ITextStoreACP {
    LONG refs_=1;DWORD lock_=0;ITextStoreACPSink* sink_=nullptr;
public:
    std::wstring value;LONG start=0,end=0;bool readOnly=false,deferLocks=false;DWORD queuedLock=0;HWND window;
    TsActiveSelEnd activeEnd=TS_AE_END;
    Store(){window=CreateWindowExW(0,L"STATIC",L"EType integration test",WS_OVERLAPPED,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);}
    ~Store(){drop(sink_);DestroyWindow(window);}
    void clear(){LONG old=(LONG)value.size();value.clear();start=end=0;if(sink_){TS_TEXTCHANGE c{0,old,0};sink_->OnTextChange(0,&c);sink_->OnSelectionChange();}}
    void select(LONG a,LONG b,TsActiveSelEnd active=TS_AE_END){start=a;end=b;activeEnd=active;if(sink_)sink_->OnSelectionChange();}
    void flushLock(){deferLocks=false;if(queuedLock&&sink_){auto flags=queuedLock;queuedLock=0;lock_=flags;sink_->OnLockGranted(flags);lock_=0;}}
    STDMETHODIMP QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id==IID_IUnknown||id==IID_ITextStoreACP){*p=static_cast<ITextStoreACP*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    STDMETHODIMP_(ULONG) AddRef()override{return InterlockedIncrement(&refs_);}
    STDMETHODIMP_(ULONG) Release()override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    STDMETHODIMP AdviseSink(REFIID id,IUnknown* p,DWORD)override{if(id!=IID_ITextStoreACPSink)return E_INVALIDARG;drop(sink_);return p->QueryInterface(id,(void**)&sink_);}
    STDMETHODIMP UnadviseSink(IUnknown*)override{drop(sink_);return S_OK;}
    STDMETHODIMP RequestLock(DWORD flags,HRESULT* session)override{
        if(!sink_){*session=E_UNEXPECTED;return E_UNEXPECTED;}
        if(deferLocks){if(flags&TS_LF_SYNC)*session=TS_E_SYNCHRONOUS;else{queuedLock=flags;*session=TS_S_ASYNC;}return S_OK;}
        if(lock_){*session=TS_E_SYNCHRONOUS;return S_OK;}
        lock_=flags;*session=sink_->OnLockGranted(flags);lock_=0;return S_OK;
    }
    STDMETHODIMP GetStatus(TS_STATUS* s)override{s->dwDynamicFlags=readOnly?TS_SD_READONLY:0;s->dwStaticFlags=0;return S_OK;}
    STDMETHODIMP QueryInsert(LONG a,LONG b,ULONG,LONG* x,LONG* y)override{if(a<0||b<a||b>(LONG)value.size())return TS_E_INVALIDPOS;*x=a;*y=b;return S_OK;}
    STDMETHODIMP GetSelection(ULONG index,ULONG count,TS_SELECTION_ACP* p,ULONG* fetched)override{
        if(!lock_)return TS_E_NOLOCK;*fetched=0;if(count&&(index==TS_DEFAULT_SELECTION||index==0)){p[0]={start,end,{activeEnd,FALSE}};*fetched=1;}return S_OK;
    }
    STDMETHODIMP SetSelection(ULONG count,const TS_SELECTION_ACP* p)override{
        if(!lock_)return TS_E_NOLOCK;if(!count)return E_INVALIDARG;start=p[0].acpStart;end=p[0].acpEnd;activeEnd=p[0].style.ase;return S_OK;
    }
    STDMETHODIMP GetText(LONG a,LONG b,WCHAR* text,ULONG capacity,ULONG* length,TS_RUNINFO* runs,ULONG runCapacity,ULONG* runCount,LONG* next)override{
        if(!lock_)return TS_E_NOLOCK;if(b==-1)b=(LONG)value.size();if(a<0||b<a||b>(LONG)value.size())return TS_E_INVALIDPOS;
        ULONG n=std::min(capacity,(ULONG)(b-a));if(text&&n)std::copy_n(value.data()+a,n,text);*length=n;
        *runCount=0;if(runs&&runCapacity){runs[0]={(ULONG)(capacity?n:b-a),TS_RT_PLAIN};*runCount=1;}
        *next=a+(capacity?n:b-a);return S_OK;
    }
    STDMETHODIMP SetText(DWORD,LONG a,LONG b,const WCHAR* text,ULONG n,TS_TEXTCHANGE* change)override{
        if((lock_&TS_LF_READWRITE)!=TS_LF_READWRITE)return TS_E_NOLOCK;if(a<0||b<a||b>(LONG)value.size())return TS_E_INVALIDPOS;
        value.replace(a,b-a,text,n);start=end=a+n;if(change)*change={a,b,a+(LONG)n};return S_OK;
    }
    STDMETHODIMP GetFormattedText(LONG,LONG,IDataObject**)override{return E_NOTIMPL;}
    STDMETHODIMP GetEmbedded(LONG,REFGUID,REFIID,IUnknown**)override{return E_NOTIMPL;}
    STDMETHODIMP QueryInsertEmbedded(const GUID*,const FORMATETC*,BOOL* yes)override{*yes=FALSE;return S_OK;}
    STDMETHODIMP InsertEmbedded(DWORD,LONG,LONG,IDataObject*,TS_TEXTCHANGE*)override{return E_NOTIMPL;}
    STDMETHODIMP InsertTextAtSelection(DWORD flags,const WCHAR* text,ULONG n,LONG* a,LONG* b,TS_TEXTCHANGE* change)override{
        if(!lock_)return TS_E_NOLOCK;if(a)*a=start;if(b)*b=end;
        if(flags&TS_IAS_QUERYONLY)return S_OK;
        LONG oldStart=start,oldEnd=end;auto hr=SetText(0,start,end,text,n,change);if(a)*a=oldStart;if(b)*b=oldEnd;return hr;
    }
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD,IDataObject*,LONG*,LONG*,TS_TEXTCHANGE*)override{return E_NOTIMPL;}
    STDMETHODIMP RequestSupportedAttrs(DWORD,ULONG,const TS_ATTRID*)override{return S_OK;}
    STDMETHODIMP RequestAttrsAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD)override{return S_OK;}
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD)override{return S_OK;}
    STDMETHODIMP FindNextAttrTransition(LONG,LONG stop,ULONG,const TS_ATTRID*,DWORD,LONG* next,BOOL* found,LONG* offset)override{*next=stop;*found=FALSE;*offset=0;return S_OK;}
    STDMETHODIMP RetrieveRequestedAttrs(ULONG,TS_ATTRVAL*,ULONG* fetched)override{*fetched=0;return S_OK;}
    STDMETHODIMP GetEndACP(LONG* length)override{if(!lock_)return TS_E_NOLOCK;*length=(LONG)value.size();return S_OK;}
    STDMETHODIMP GetActiveView(TsViewCookie* view)override{*view=0;return S_OK;}
    STDMETHODIMP GetACPFromPoint(TsViewCookie,const POINT*,DWORD,LONG* pos)override{*pos=end;return S_OK;}
    STDMETHODIMP GetTextExt(TsViewCookie,LONG a,LONG b,RECT* r,BOOL* clipped)override{*r={100+a*8,100,100+b*8,124};*clipped=FALSE;return S_OK;}
    STDMETHODIMP GetScreenExt(TsViewCookie,RECT* r)override{*r={100,100,740,580};return S_OK;}
    STDMETHODIMP GetWnd(TsViewCookie,HWND* hwnd)override{*hwnd=window;return S_OK;}
};
static void pump(){MSG m;for(int i=0;i<10;++i){while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}}
static bool key(ITfKeyEventSink* sink,ITfContext* context,UINT vk,bool shift=false,bool control=false){
    // A TSF call can pump native messages. Refresh each test key's modifiers at
    // every sink call, then restore the user's state before pumping the queue.
    BYTE previous[256]{};GetKeyboardState(previous);BYTE state[256]{};if(shift)state[VK_SHIFT]=0x80;if(control)state[VK_CONTROL]=0x80;SetKeyboardState(state);
    LPARAM param=(LPARAM)MapVirtualKeyW(vk,MAPVK_VK_TO_VSC)<<16;BOOL eaten=FALSE;
    auto hr=sink->OnTestKeyDown(context,vk,param,&eaten);
    if(SUCCEEDED(hr)&&eaten){SetKeyboardState(state);hr=sink->OnKeyDown(context,vk,param,&eaten);}
    if(vk==VK_SHIFT){SetKeyboardState(state);sink->OnTestKeyUp(context,vk,param,&eaten);if(eaten){SetKeyboardState(state);sink->OnKeyUp(context,vk,param,&eaten);}}
    SetKeyboardState(previous);pump();return SUCCEEDED(hr)&&eaten;
}
static void word(ITfKeyEventSink* sink,ITfContext* c,const char* text){for(;*text;++text)check(key(sink,c,(UINT)toupper(*text)),"letter handled via TSF sink");}
int wmain(int argc,wchar_t** argv){
    if(argc!=2&&argc!=3)return 2;bool live=argc==3&&wcscmp(argv[2],L"--local-ai")==0;SetEnvironmentVariableW(L"ETYPE_HEADLESS_TEST",L"1");CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    auto settingsPath=(std::filesystem::absolute(argv[1]).parent_path().parent_path().parent_path()/(L"tsf-test-settings-"+std::to_wstring(GetCurrentProcessId())+L".ini")).wstring();
    SetEnvironmentVariableW(L"ETYPE_TEST_SETTINGS",settingsPath.c_str());
    WritePrivateProfileStringW(L"EType",L"ChinesePunctuation",L"1",settingsPath.c_str());
    auto dll=LoadLibraryW(argv[1]);check(dll!=nullptr,"native DLL loads without external runtime");if(!dll)return 2;
    using FactoryFn=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);auto factoryFn=(FactoryFn)GetProcAddress(dll,"DllGetClassObject");check(factoryFn!=nullptr,"COM export available");if(!factoryFn)return 2;
    IClassFactory* factory=nullptr;check(SUCCEEDED(factoryFn(ETypeClsid,IID_IClassFactory,(void**)&factory)),"COM factory created");
    auto capture=new CapturingFactory(factory);drop(factory);DWORD registrationCookie=0;
    check(SUCCEEDED(CoRegisterClassObject(ETypeClsid,capture,CLSCTX_INPROC_SERVER,REGCLS_MULTIPLEUSE,&registrationCookie)),"test-only process-local COM registration");
    ITfThreadMgr* mgr=nullptr;check(SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfThreadMgr,(void**)&mgr)),"real Windows TSF thread manager");
    TfClientId client=0;check(SUCCEEDED(mgr->Activate(&client)),"TSF thread activated");
    ITfDocumentMgr* document=nullptr;mgr->CreateDocumentMgr(&document);auto store=new Store;
    ITfContext* context=nullptr;TfEditCookie editCookie=0;check(SUCCEEDED(document->CreateContext(client,0,store,&context,&editCookie)),"real TSF context with text store");document->Push(context);mgr->SetFocus(document);
    auto layout=LoadKeyboardLayoutW(L"00000804",KLF_NOTELLSHELL);ActivateKeyboardLayout(layout,0);
    ITfInputProcessorProfileMgr* profiles=nullptr;CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfileMgr,(void**)&profiles);
    auto fullPath=std::filesystem::absolute(argv[1]).wstring();
    auto registrationHr=profiles->RegisterProfile(ETypeClsid,ETypeLanguage,ETypeProfile,ETypeName,(ULONG)wcslen(ETypeName),fullPath.c_str(),(ULONG)fullPath.size(),0,nullptr,0,TRUE,4);
    check(registrationHr==S_OK,"temporary local-process TSF profile registers");
    auto activationHr=profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,ETypeLanguage,ETypeClsid,ETypeProfile,nullptr,0x10000001);
    std::cout<<"activation_hr=0x"<<std::hex<<(unsigned long)activationHr<<std::dec<<"\n";
    check(activationHr==S_OK,"Windows activates the registered input profile");pump();
    mgr->SetFocus(document);
    check(capture->active!=nullptr,"Windows creates actual input-service instance");
    if(!capture->active){std::cout<<"tsf_checks="<<checks<<" failures="<<failures<<"\n";return 2;}
    ITfTextInputProcessor* service=capture->active;service->AddRef();ITfKeyEventSink* sink=nullptr;service->QueryInterface(IID_ITfKeyEventSink,(void**)&sink);
    check(!key(sink,context,VK_RETURN,false,true),"word mode Ctrl Enter is left to the host");
    word(sink,context,"bank");check(store->value==L"bank","English composition appears in host text store");check(key(sink,context,'2'),"candidate key handled");check(store->value==L"河岸","selected Chinese replaces English in host");
    store->clear();word(sink,context,"apple");key(sink,context,VK_SPACE);check(store->value==L"苹果","space commits Chinese through TSF");
    store->clear();word(sink,context,"aple");key(sink,context,VK_SPACE);check(store->value==L"aple","correction prompt preserves composition");
    etype::Dictionary dictionary;auto parent=std::filesystem::path(argv[1]).parent_path().parent_path();dictionary.load((parent/L"data"/L"dictionary.tsv").wstring());auto suggestions=dictionary.correct("aple");auto it=std::find(suggestions.begin(),suggestions.end(),"apple");
    if(it!=suggestions.end()){key(sink,context,'1'+(UINT)(it-suggestions.begin()));check(store->value==L"apple","confirming correction updates composition only");key(sink,context,VK_SPACE);check(store->value==L"苹果","second confirmation commits corrected Chinese");}
    store->clear();word(sink,context,"went");key(sink,context,VK_SPACE);check(store->value==L"去","inflected form commits Chinese through TSF");
    store->clear();word(sink,context,"bank");key(sink,context,VK_RETURN);check(store->value==L"bank","Enter commits English through TSF");
    store->clear();word(sink,context,"apple");key(sink,context,VK_ESCAPE);check(store->value.empty(),"Escape removes composition without text");
    store->clear();word(sink,context,"hello");key(sink,context,VK_OEM_COMMA);check(store->value==L"你好，","punctuation commits Chinese plus punctuation");
    store->clear();word(sink,context,"bank");sink->OnSetFocus(FALSE);pump();check(store->value==L"bank","focus exit retains English in old text store");
    store->clear();store->readOnly=true;check(!key(sink,context,'A'),"read-only host passes key through");check(store->value.empty(),"read-only text untouched");store->readOnly=false;
    sink->OnSetFocus(TRUE);key(sink,context,VK_SHIFT);check(!key(sink,context,'A'),"English mode passes letters through");key(sink,context,VK_SHIFT);check(key(sink,context,'A'),"Shift restores translation mode");key(sink,context,VK_ESCAPE);
    store->clear();WritePrivateProfileStringW(L"EType",L"ChinesePunctuation",L"0",settingsPath.c_str());
    check(!key(sink,context,VK_OEM_COMMA),"first punctuation respects newly saved English punctuation setting");
    WritePrivateProfileStringW(L"EType",L"ChinesePunctuation",L"1",settingsPath.c_str());
    check(key(sink,context,VK_OEM_COMMA),"first punctuation respects newly saved Chinese punctuation setting");
    check(store->value==L"，","new punctuation setting applies without an intervening letter");
    // Real TSF async locks must not redirect old keystrokes into a newly focused host.
    store->clear();store->deferLocks=true;check(key(sink,context,'A'),"old-context key queued asynchronously");
    sink->OnSetFocus(FALSE);
    ITfDocumentMgr* secondDocument=nullptr;mgr->CreateDocumentMgr(&secondDocument);auto secondStore=new Store;
    ITfContext* secondContext=nullptr;TfEditCookie secondCookie=0;secondDocument->CreateContext(client,0,secondStore,&secondContext,&secondCookie);secondDocument->Push(secondContext);mgr->SetFocus(secondDocument);
    word(sink,secondContext,"bank");store->flushLock();pump();
    check(store->value.empty(),"cancelled deferred key leaves old document untouched");
    check(secondStore->value==L"bank","old async callback leaves new composition untouched");
    key(sink,secondContext,VK_SPACE);check(secondStore->value==L"银行","old async callback leaves new engine buffer untouched");
    sink->OnSetFocus(FALSE);mgr->SetFocus(document);secondDocument->Pop(TF_POPF_ALL);drop(secondContext);drop(secondDocument);secondStore->Release();
    store->clear();check(key(sink,context,VK_SPACE,true,true),"Ctrl Shift Space selects sentence mode");
    word(sink,context,"i");key(sink,context,VK_SPACE);word(sink,context,"have");key(sink,context,VK_SPACE);key(sink,context,'2');key(sink,context,VK_OEM_PERIOD);
    check(store->value==L"i have 2.","sentence spaces numbers and punctuation remain in composition");check(key(sink,context,VK_RETURN,false,true),"sentence Ctrl Enter is consumed");check(store->value==L"i have 2.","sentence Ctrl Enter preserves original English");
    store->clear();key(sink,context,'2');key(sink,context,VK_SPACE);word(sink,context,"apples");check(store->value==L"2 apples","sentence can start with a number");key(sink,context,VK_ESCAPE);check(store->value.empty(),"sentence Escape cancels composition");
    auto typeText=[&](const char* text){for(;*text;++text){SHORT mapped=VkKeyScanA(*text);check(key(sink,context,LOBYTE(mapped),(HIBYTE(mapped)&1)!=0),"sentence character handled");}};
    typeText("i have red books.");key(sink,context,VK_HOME);key(sink,context,VK_RIGHT,false,true);key(sink,context,VK_RIGHT,false,true);
    check(store->start==7&&store->end==7,"real TSF caret moves by word inside composition");
    key(sink,context,VK_RIGHT,true,true);check(store->start==7&&store->end==11,"real TSF Shift selection retains anchor");typeText("blue ");
    check(store->value==L"i have blue books."&&store->start==12&&store->end==12,"middle word replacement updates real TSF host");
    key(sink,context,VK_BACK,false,true);check(store->value==L"i have books."&&store->start==7,"Ctrl Backspace removes preceding word");
    key(sink,context,VK_DELETE);check(store->value==L"i have ooks."&&store->start==7,"Delete at middle keeps caret position");
    key(sink,context,VK_END);key(sink,context,VK_LEFT,true);key(sink,context,VK_LEFT,true);check(store->end-store->start==2&&store->activeEnd==TS_AE_START,"repeated Shift Left extends in reverse direction");
    key(sink,context,VK_ESCAPE);store->clear();typeText("i have red books.");store->select(7,10);typeText("green");check(store->value==L"i have green books.","host mouse selection reconciled before typing");key(sink,context,VK_RETURN,false,true);
    store->clear();typeText("prefix ");key(sink,context,VK_RETURN,false,true);typeText("draft");store->select(0,0);key(sink,context,'N');check(store->value==L"nprefix draft","mouse outside composition preserves draft and starts at new host caret");key(sink,context,VK_ESCAPE);store->clear();
    if(live){
        auto sentence=[&](){for(const char* text="i sat on the bank.";*text;++text){SHORT mapped=VkKeyScanA(*text);key(sink,context,LOBYTE(mapped),(HIBYTE(mapped)&1)!=0);}};
        auto wait=[](){auto until=GetTickCount64()+15000;while(GetTickCount64()<until)pump();};
        sentence();check(key(sink,context,VK_RETURN),"Enter starts local translation");check(key(sink,context,VK_RETURN)&&store->value==L"i sat on the bank.","repeated Enter while pending preserves composition");wait();check(key(sink,context,VK_RETURN),"Enter confirms translated Chinese");
        check(store->value.find(L"河岸")!=std::wstring::npos&&store->value.find(L"bank")==std::wstring::npos,"local Chinese candidate commits through real TSF");
        store->clear();sentence();key(sink,context,VK_RETURN);wait();key(sink,context,VK_RETURN,false,true);check(store->value==L"i sat on the bank.","Ctrl Enter outputs English when Chinese is available");
        store->clear();sentence();key(sink,context,VK_RETURN);key(sink,context,VK_RETURN,false,true);wait();check(store->value==L"i sat on the bank.","Ctrl Enter during translation rejects late Chinese");
        store->clear();sentence();key(sink,context,VK_RETURN);key(sink,context,'S');wait();check(store->value==L"i sat on the bank.s","editing pending sentence keeps edited English");key(sink,context,VK_RETURN,false,true);
        store->clear();sentence();key(sink,context,VK_RETURN);sink->OnSetFocus(FALSE);pump();store->clear();word(sink,context,"new");wait();check(store->value==L"new","late translation cannot affect refocused composition");key(sink,context,VK_ESCAPE);
    }
    key(sink,context,VK_SPACE,true,true);store->clear();word(sink,context,"bank");key(sink,context,'2');check(store->value==L"河岸","returning to word mode restores original candidate behavior");
    profiles->DeactivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,ETypeLanguage,ETypeClsid,ETypeProfile,nullptr,0x10000000);drop(profiles);
    drop(sink);drop(service);mgr->SetFocus(nullptr);document->Pop(TF_POPF_ALL);drop(context);drop(document);store->Release();mgr->Deactivate();drop(mgr);pump();
    CoRevokeClassObject(registrationCookie);capture->Release();
    using UnloadFn=HRESULT(WINAPI*)();auto canUnload=(UnloadFn)GetProcAddress(dll,"DllCanUnloadNow");check(canUnload&&canUnload()==S_OK,"service and edit sessions release cleanly");
    FreeLibrary(dll);CoUninitialize();DeleteFileW(settingsPath.c_str());std::cout<<"tsf_checks="<<checks<<" failures="<<failures<<"\n";return failures?1:0;
}
