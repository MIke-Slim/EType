#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <msctf.h>
#include <inputscope.h>
#include <atomic>
#include <functional>
#include <algorithm>
#include "platform.h"
#include "identity.h"

using namespace etype;
static HMODULE module;
static std::atomic<long> objects{0}, locks{0};
template<class T> static void release(T*& p){if(p){p->Release();p=nullptr;}}

class EditSession final:public ITfEditSession {
    LONG refs_=1; IUnknown* owner_; ITfContext* context_; std::function<HRESULT(TfEditCookie)> action_;
public:
    EditSession(IUnknown* owner,ITfContext* c,std::function<HRESULT(TfEditCookie)> action):owner_(owner),context_(c),action_(std::move(action)){owner_->AddRef();context_->AddRef();}
    ~EditSession(){context_->Release();owner_->Release();}
    STDMETHODIMP QueryInterface(REFIID id,void** p) override {if(!p)return E_POINTER;*p=nullptr;if(id==IID_IUnknown||id==IID_ITfEditSession){*p=static_cast<ITfEditSession*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    STDMETHODIMP_(ULONG) AddRef() override{return InterlockedIncrement(&refs_);}
    STDMETHODIMP_(ULONG) Release() override {auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {try{return action_(cookie);}catch(...){return E_FAIL;}}
};

class TextService final:public ITfTextInputProcessor,public ITfKeyEventSink,public ITfThreadMgrEventSink,public ITfCompositionSink,public IPreviewTextService {
    LONG refs_=1;
    ITfThreadMgr* thread_=nullptr;
    ITfContext* context_=nullptr;
    ITfComposition* composition_=nullptr;
    TfClientId client_=0;
    DWORD threadCookie_=TF_INVALID_COOKIE;
    bool shiftAlone_=false,ending_=false;
    bool preview_=false;
    std::wstring root_;
    Engine engine_;
    Popup popup_;
    POINT anchor_{80,100};
    IUnknown* unknown(){return static_cast<ITfTextInputProcessor*>(this);}

    HRESULT request(ITfContext* context,std::function<HRESULT(TfEditCookie)> action,bool forceAsync=false) {
        auto session=new(std::nothrow) EditSession(unknown(),context,std::move(action));if(!session)return E_OUTOFMEMORY;
        HRESULT result=E_FAIL;
        HRESULT hr=context->RequestEditSession(client_,session,(forceAsync?TF_ES_ASYNC:TF_ES_SYNC)|TF_ES_READWRITE,&result);
        if(!forceAsync && (hr==TF_E_SYNCHRONOUS || result==TF_E_SYNCHRONOUS || result==TF_E_LOCKED))hr=context->RequestEditSession(client_,session,TF_ES_ASYNC|TF_ES_READWRITE,&result);
        session->Release();return FAILED(hr)?hr:result;
    }
    bool enabled(ITfContext* c) {
        if(!c)return false;
        TF_STATUS status{};if(SUCCEEDED(c->GetStatus(&status))&&(status.dwDynamicFlags&TF_SD_READONLY))return false;
        ITfCompartmentMgr* mgr=nullptr;
        if(SUCCEEDED(c->QueryInterface(IID_ITfCompartmentMgr,(void**)&mgr))){
            bool disabled=false;
            for(const auto& guid:{GUID_COMPARTMENT_KEYBOARD_DISABLED,GUID_COMPARTMENT_EMPTYCONTEXT}) {
                ITfCompartment* comp=nullptr;VARIANT v;VariantInit(&v);
                if(SUCCEEDED(mgr->GetCompartment(guid,&comp))){if(SUCCEEDED(comp->GetValue(&v))&&v.vt==VT_I4&&v.lVal)disabled=true;release(comp);}
                VariantClear(&v);
            }
            release(mgr);if(disabled)return false;
        }
        GUITHREADINFO info{};info.cbSize=sizeof(info);
        if(GetGUIThreadInfo(GetCurrentThreadId(),&info)&&info.hwndFocus){wchar_t name[128]{};GetClassNameW(info.hwndFocus,name,128);
            if((_wcsicmp(name,L"Edit")==0||_wcsnicmp(name,L"richedit",8)==0)&&(GetWindowLongPtrW(info.hwndFocus,GWL_STYLE)&ES_PASSWORD))return false;
        }
        return true;
    }
    wchar_t character(WPARAM key,LPARAM param) {
        BYTE state[256]{};GetKeyboardState(state);wchar_t chars[8]{};
        int n=ToUnicodeEx((UINT)key,(UINT)((param>>16)&255),state,chars,8,4,GetKeyboardLayout(0));return n==1?chars[0]:0;
    }
    bool test(ITfContext* c,WPARAM key,LPARAM param) {
        if(!enabled(c))return false;
        if(GetKeyState(VK_CONTROL)&0x8000 || GetKeyState(VK_MENU)&0x8000 || GetKeyState(VK_LWIN)&0x8000 || GetKeyState(VK_RWIN)&0x8000)return false;
        if(key==VK_SHIFT||key==VK_LSHIFT||key==VK_RSHIFT)return true;
        if(engine_.english)return false;
        if(key>='A'&&key<='Z')return true;
        if(!engine_.buffer.empty()) {
            if(key==VK_SPACE||key==VK_RETURN||key==VK_BACK||key==VK_ESCAPE||key==VK_UP||key==VK_DOWN||key==VK_LEFT||key==VK_RIGHT||key==VK_PRIOR||key==VK_NEXT)return true;
            if(key>='0'&&key<='9')return true;
        }
        auto ch=character(key,param);
        if(ch && (wcschr(L",.?!;:()[]\"'",ch)||(!engine_.buffer.empty()&&ch==L'-')))return engine_.chinesePunctuation||!engine_.buffer.empty();
        return false;
    }
    bool passwordScope(TfEditCookie cookie,ITfRange* range) {
        ITfProperty* p=nullptr;VARIANT v;VariantInit(&v);bool password=false;
        if(SUCCEEDED(context_->GetProperty(GUID_PROP_INPUTSCOPE,&p))) {
            if(SUCCEEDED(p->GetValue(cookie,range,&v))&&v.vt==VT_UNKNOWN&&v.punkVal){ITfInputScope* scope=nullptr;
                if(SUCCEEDED(v.punkVal->QueryInterface(IID_ITfInputScope,(void**)&scope))){InputScope* scopes=nullptr;UINT count=0;
                    if(SUCCEEDED(scope->GetInputScopes(&scopes,&count))){for(UINT i=0;i<count;++i)if(scopes[i]==IS_PASSWORD)password=true;CoTaskMemFree(scopes);}release(scope);
                }
            }
            release(p);
        }
        VariantClear(&v);return password;
    }
    HRESULT apply(TfEditCookie cookie,const Result& result) {
        if(!context_)return E_UNEXPECTED;
        ITfRange* range=nullptr;
        if(composition_)composition_->GetRange(&range);
        else {
            TF_SELECTION selection{};ULONG fetched=0;
            HRESULT hr=context_->GetSelection(cookie,TF_DEFAULT_SELECTION,1,&selection,&fetched);
            if(FAILED(hr)||!fetched)return FAILED(hr)?hr:E_FAIL;
            range=selection.range;
            // TSF normally disables the service in password fields. Keep a second check
            // before creating a composition in hosts that publish an input scope.
            if(passwordScope(cookie,range)){release(range);engine_.reset();popup_.hide();return E_ACCESSDENIED;}
            ITfContextComposition* cc=nullptr;
            hr=context_->QueryInterface(IID_ITfContextComposition,(void**)&cc);
            if(SUCCEEDED(hr)){hr=cc->StartComposition(cookie,range,this,&composition_);release(cc);}
            if(FAILED(hr)||!composition_){release(range);return FAILED(hr)?hr:E_FAIL;}
        }
        if(!range)return E_FAIL;
        std::wstring value=result.output.empty()?wide(engine_.buffer):result.output;
        HRESULT hr=range->SetText(cookie,0,value.data(),(LONG)value.size());
        if(SUCCEEDED(hr)){
            ITfRange* caret=nullptr;range->Clone(&caret);
            if(caret){caret->Collapse(cookie,TF_ANCHOR_END);TF_SELECTION sel{caret,{TF_AE_NONE,FALSE}};context_->SetSelection(cookie,1,&sel);release(caret);}
            ITfContextView* view=nullptr;
            if(SUCCEEDED(context_->GetActiveView(&view))){RECT rect{};BOOL clipped=FALSE;
                if(SUCCEEDED(view->GetTextExt(cookie,range,&rect,&clipped)))anchor_={rect.left,rect.bottom+6};release(view);
            }
        }
        release(range);
        if(engine_.buffer.empty()&&composition_){auto old=composition_;composition_=nullptr;ending_=true;old->EndComposition(cookie);ending_=false;old->Release();}
        if(engine_.buffer.empty())popup_.hide();else popup_.show(anchor_);
        return hr;
    }
    void attach(ITfContext* c) {
        if(context_==c)return;
        detach();context_=c;if(context_)context_->AddRef();
    }
    void detach() {
        popup_.hide();engine_.reset();
        auto oldContext=context_;auto oldComposition=composition_;context_=nullptr;composition_=nullptr;
        if(oldComposition&&oldContext){
            // Letters are already present in the old composition. End it there;
            // never insert the old word into the newly focused document.
            auto keep=std::shared_ptr<ITfComposition>(oldComposition,[](ITfComposition* p){p->Release();});
            request(oldContext,[keep](TfEditCookie ec){return keep->EndComposition(ec);},true);
        }else if(oldComposition)oldComposition->Release();
        if(oldContext)oldContext->Release();
    }
    Result dispatch(WPARAM key,wchar_t ch) {
        if(key==VK_SPACE)return engine_.key(Key::Space);
        if(key==VK_RETURN)return engine_.key(Key::Enter);
        if(key==VK_BACK)return engine_.key(Key::Backspace);
        if(key==VK_ESCAPE)return engine_.key(Key::Escape);
        if(key==VK_UP||key==VK_LEFT)return engine_.key(Key::Up);
        if(key==VK_DOWN||key==VK_RIGHT)return engine_.key(Key::Down);
        if(key==VK_PRIOR)return engine_.key(Key::PageUp);
        if(key==VK_NEXT)return engine_.key(Key::PageDown);
        if(key>='A'&&key<='Z')return engine_.type((char)(ch?ch:(wchar_t)key));
        if(ch>=L'1'&&ch<=L'5'&&engine_.count())return engine_.choose(ch-L'1');
        if((ch==L'\''||ch==L'-')&&!engine_.buffer.empty())return engine_.type((char)ch);
        if(ch>=L'0'&&ch<=L'9'){auto r=engine_.finish();r.output.push_back(ch);return r;}
        return engine_.punctuation(ch);
    }
public:
    TextService():root_(moduleRoot(module)),engine_(loadDictionary(root_)),popup_(module,engine_,root_) {
        ++objects;
        engine_.chinesePunctuation=readSettings().chinesePunctuation;
        popup_.select=[this](size_t index){if(context_)request(context_,[this,index](TfEditCookie cookie){return apply(cookie,engine_.choose(index));},true);};
    }
    ~TextService(){release(composition_);release(context_);release(thread_);--objects;}
    STDMETHODIMP QueryInterface(REFIID id,void** p)override {
        if(!p)return E_POINTER;*p=nullptr;
        if(id==IID_IUnknown||id==IID_ITfTextInputProcessor)*p=static_cast<ITfTextInputProcessor*>(this);
        else if(id==IID_ITfKeyEventSink)*p=static_cast<ITfKeyEventSink*>(this);
        else if(id==IID_ITfThreadMgrEventSink)*p=static_cast<ITfThreadMgrEventSink*>(this);
        else if(id==IID_ITfCompositionSink)*p=static_cast<ITfCompositionSink*>(this);
        else if(id==ETypePreviewId)*p=static_cast<IPreviewTextService*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef()override{return InterlockedIncrement(&refs_);}
    STDMETHODIMP_(ULONG) Release()override {auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    STDMETHODIMP InitializeHost(ITfThreadMgr* t,TfClientId id)override {
        if(!t||thread_||!engine_.dictionary->size())return E_INVALIDARG;
        thread_=t;t->AddRef();client_=id;preview_=true;return S_OK;
    }
    STDMETHODIMP Activate(ITfThreadMgr* t,TfClientId id)override {
        if(!t)return E_INVALIDARG;if(thread_)return E_UNEXPECTED;
        if(!engine_.dictionary->size())return E_FAIL;
        thread_=t;t->AddRef();client_=id;
        ITfKeystrokeMgr* km=nullptr;HRESULT hr=t->QueryInterface(IID_ITfKeystrokeMgr,(void**)&km);
        if(SUCCEEDED(hr)){hr=km->AdviseKeyEventSink(id,this,TRUE);release(km);}
        if(FAILED(hr)){release(thread_);return hr;}
        ITfSource* source=nullptr;if(SUCCEEDED(t->QueryInterface(IID_ITfSource,(void**)&source))){source->AdviseSink(IID_ITfThreadMgrEventSink,static_cast<ITfThreadMgrEventSink*>(this),&threadCookie_);release(source);}
        return S_OK;
    }
    STDMETHODIMP Deactivate()override {
        if(!thread_)return S_OK;detach();
        ITfKeystrokeMgr* km=nullptr;if(!preview_&&SUCCEEDED(thread_->QueryInterface(IID_ITfKeystrokeMgr,(void**)&km))){km->UnadviseKeyEventSink(client_);release(km);}
        ITfSource* source=nullptr;if(threadCookie_!=TF_INVALID_COOKIE&&SUCCEEDED(thread_->QueryInterface(IID_ITfSource,(void**)&source))){source->UnadviseSink(threadCookie_);release(source);threadCookie_=TF_INVALID_COOKIE;}
        release(thread_);return S_OK;
    }
    STDMETHODIMP OnSetFocus(BOOL foreground)override {if(!foreground){shiftAlone_=false;detach();}return S_OK;}
    STDMETHODIMP OnTestKeyDown(ITfContext* c,WPARAM w,LPARAM l,BOOL* eaten)override {
        // Track Shift chords even when their letter key passes through in English mode.
        if(w!=VK_SHIFT&&w!=VK_LSHIFT&&w!=VK_RSHIFT)shiftAlone_=false;
        *eaten=test(c,w,l);return S_OK;
    }
    STDMETHODIMP OnTestKeyUp(ITfContext*,WPARAM w,LPARAM,BOOL* eaten)override {*eaten=(w==VK_SHIFT||w==VK_LSHIFT||w==VK_RSHIFT)&&shiftAlone_;return S_OK;}
    STDMETHODIMP OnKeyDown(ITfContext* c,WPARAM w,LPARAM l,BOOL* eaten)override {
        *eaten=test(c,w,l);
        if(w==VK_SHIFT||w==VK_LSHIFT||w==VK_RSHIFT){shiftAlone_=*eaten!=FALSE;return S_OK;}
        shiftAlone_=false;if(!*eaten)return S_OK;attach(c);
        engine_.chinesePunctuation=readSettings().chinesePunctuation;
        wchar_t ch=character(w,l);
        HRESULT hr=request(c,[this,w,ch](TfEditCookie cookie){return apply(cookie,dispatch(w,ch));});
        if(FAILED(hr)){detach();*eaten=FALSE;}
        return S_OK;
    }
    STDMETHODIMP OnKeyUp(ITfContext* c,WPARAM w,LPARAM,BOOL* eaten)override {
        *eaten=FALSE;
        if((w==VK_SHIFT||w==VK_LSHIFT||w==VK_RSHIFT)&&shiftAlone_){
            shiftAlone_=false;*eaten=TRUE;attach(c);
            request(c,[this](TfEditCookie cookie){auto r=engine_.key(Key::Toggle);HRESULT hr=S_OK;if(composition_||!r.output.empty())hr=apply(cookie,r);popup_.show(anchor_,true);return hr;});
        }
        return S_OK;
    }
    STDMETHODIMP OnPreservedKey(ITfContext*,REFGUID,BOOL* eaten)override {*eaten=FALSE;return S_OK;}
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*)override{return S_OK;}
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr*)override{return S_OK;}
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* current,ITfDocumentMgr*)override {
        shiftAlone_=false;ITfContext* next=nullptr;if(current)current->GetTop(&next);if(context_&&next!=context_)detach();release(next);return S_OK;
    }
    STDMETHODIMP OnPushContext(ITfContext* c)override {if(context_&&context_!=c)detach();return S_OK;}
    STDMETHODIMP OnPopContext(ITfContext* c)override {if(context_==c)detach();return S_OK;}
    STDMETHODIMP OnCompositionTerminated(TfEditCookie,ITfComposition* c)override {
        if(c==composition_&&!ending_){release(composition_);engine_.reset();popup_.hide();}return S_OK;
    }
};

class Factory final:public IClassFactory {
    LONG refs_=1;
public:
    Factory(){++objects;}~Factory(){--objects;}
    STDMETHODIMP QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id==IID_IUnknown||id==IID_IClassFactory){*p=static_cast<IClassFactory*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    STDMETHODIMP_(ULONG) AddRef()override{return InterlockedIncrement(&refs_);}
    STDMETHODIMP_(ULONG) Release()override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    STDMETHODIMP CreateInstance(IUnknown* outer,REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(outer)return CLASS_E_NOAGGREGATION;try{auto s=new TextService();auto hr=s->QueryInterface(id,p);s->Release();return hr;}catch(...){return E_OUTOFMEMORY;}}
    STDMETHODIMP LockServer(BOOL lock)override{if(lock)++locks;else --locks;return S_OK;}
};
extern "C" BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){module=instance;DisableThreadLibraryCalls(instance);}return TRUE;}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid,REFIID id,void** p){if(clsid!=ETypeClsid)return CLASS_E_CLASSNOTAVAILABLE;auto f=new(std::nothrow)Factory();if(!f)return E_OUTOFMEMORY;auto hr=f->QueryInterface(id,p);f->Release();return hr;}
extern "C" HRESULT WINAPI DllCanUnloadNow(){return objects==0&&locks==0?S_OK:S_FALSE;}

static std::wstring classKey(){wchar_t guid[40];StringFromGUID2(ETypeClsid,guid,40);return std::wstring(L"Software\\Classes\\CLSID\\")+guid;}
static LONG setString(HKEY parent,const std::wstring& path,const wchar_t* name,const std::wstring& value){HKEY key;LONG err=RegCreateKeyExW(parent,path.c_str(),0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr);if(err!=ERROR_SUCCESS)return err;err=RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)value.c_str(),(DWORD)((value.size()+1)*sizeof(wchar_t)));RegCloseKey(key);return err;}
extern "C" HRESULT WINAPI DllUnregisterServer();
extern "C" HRESULT WINAPI DllRegisterServer(){
    HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);bool uninit=SUCCEEDED(init);
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return init;
    wchar_t path[32768];GetModuleFileNameW(module,path,32768);auto key=classKey();
    LONG err=setString(HKEY_LOCAL_MACHINE,key,nullptr,ETypeName);
    if(err==ERROR_SUCCESS)err=setString(HKEY_LOCAL_MACHINE,key+L"\\InprocServer32",nullptr,path);
    if(err==ERROR_SUCCESS)err=setString(HKEY_LOCAL_MACHINE,key+L"\\InprocServer32",L"ThreadingModel",L"Apartment");
    HRESULT hr=HRESULT_FROM_WIN32(err);
    if(SUCCEEDED(hr)){
        ITfInputProcessorProfileMgr* profiles=nullptr;
        hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfileMgr,(void**)&profiles);
        if(SUCCEEDED(hr)){hr=profiles->RegisterProfile(ETypeClsid,ETypeLanguage,ETypeProfile,ETypeName,(ULONG)wcslen(ETypeName),path,(ULONG)wcslen(path),0,nullptr,0,TRUE,0);release(profiles);}
        if(SUCCEEDED(hr)){ITfCategoryMgr* categories=nullptr;hr=CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfCategoryMgr,(void**)&categories);
            if(SUCCEEDED(hr)){hr=categories->RegisterCategory(ETypeClsid,GUID_TFCAT_TIP_KEYBOARD,ETypeClsid);release(categories);}
        }
        if(FAILED(hr))DllUnregisterServer();
    }
    if(uninit)CoUninitialize();return hr;
}
extern "C" HRESULT WINAPI DllUnregisterServer(){
    HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);bool uninit=SUCCEEDED(init);
    ITfInputProcessorProfileMgr* profiles=nullptr;HRESULT hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfileMgr,(void**)&profiles);
    // The second architecture may see a profile already removed by the first.
    if(SUCCEEDED(hr)){profiles->UnregisterProfile(ETypeClsid,ETypeLanguage,ETypeProfile,0);release(profiles);}
    ITfCategoryMgr* categories=nullptr;if(SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfCategoryMgr,(void**)&categories))){categories->UnregisterCategory(ETypeClsid,GUID_TFCAT_TIP_KEYBOARD,ETypeClsid);release(categories);}
    LONG err=RegDeleteTreeW(HKEY_LOCAL_MACHINE,classKey().c_str());if(err!=ERROR_SUCCESS&&err!=ERROR_FILE_NOT_FOUND)hr=HRESULT_FROM_WIN32(err);
    if(uninit)CoUninitialize();return (err==ERROR_SUCCESS||err==ERROR_FILE_NOT_FOUND)?S_OK:HRESULT_FROM_WIN32(err);
}
