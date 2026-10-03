#pragma once
#include <windows.h>
#include <textstor.h>
#include <string>
#include <algorithm>

// A TSF-aware text host for the settings window's component preview.
// The system input service itself has no dependency on this host.
class EditorStore final:public ITextStoreACP {
    LONG refs_=1;DWORD lock_=0;ITextStoreACPSink* sink_=nullptr;HWND edit_;
    std::wstring text_;LONG start_=0,end_=0;bool updating_=false;
    void selection(){SendMessageW(edit_,EM_GETSEL,(WPARAM)&start_,(LPARAM)&end_);}
public:
    explicit EditorStore(HWND edit):edit_(edit){sync();}
    ~EditorStore(){if(sink_)sink_->Release();}
    void sync(){
        if(updating_||lock_)return;
        LONG oldStart=start_,oldEnd=end_;selection();bool changed=false;
        int count=GetWindowTextLengthW(edit_);std::wstring next(count+1,L'\0');GetWindowTextW(edit_,next.data(),count+1);next.resize(count);
        if(next!=text_){changed=true;size_t prefix=0,suffix=0;
            while(prefix<std::min(next.size(),text_.size())&&next[prefix]==text_[prefix])++prefix;
            while(suffix<std::min(next.size(),text_.size())-prefix&&next[next.size()-1-suffix]==text_[text_.size()-1-suffix])++suffix;
            TS_TEXTCHANGE change{(LONG)prefix,(LONG)(text_.size()-suffix),(LONG)(next.size()-suffix)};text_=std::move(next);
            if(sink_)sink_->OnTextChange(0,&change);
        }
        if(sink_&&(changed||start_!=oldStart||end_!=oldEnd))sink_->OnSelectionChange();
    }
    STDMETHODIMP QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id==IID_IUnknown||id==IID_ITextStoreACP){*p=static_cast<ITextStoreACP*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    STDMETHODIMP_(ULONG) AddRef()override{return InterlockedIncrement(&refs_);}
    STDMETHODIMP_(ULONG) Release()override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    STDMETHODIMP AdviseSink(REFIID id,IUnknown* p,DWORD)override{if(id!=IID_ITextStoreACPSink)return E_INVALIDARG;if(sink_)sink_->Release();sink_=nullptr;return p->QueryInterface(id,(void**)&sink_);}
    STDMETHODIMP UnadviseSink(IUnknown*)override{if(sink_)sink_->Release();sink_=nullptr;return S_OK;}
    STDMETHODIMP RequestLock(DWORD flags,HRESULT* result)override{if(!sink_)return E_UNEXPECTED;if(lock_){*result=TS_E_SYNCHRONOUS;return S_OK;}lock_=flags;*result=sink_->OnLockGranted(flags);lock_=0;return S_OK;}
    STDMETHODIMP GetStatus(TS_STATUS* s)override{s->dwDynamicFlags=(GetWindowLongPtrW(edit_,GWL_STYLE)&ES_READONLY)?TS_SD_READONLY:0;s->dwStaticFlags=0;return S_OK;}
    STDMETHODIMP QueryInsert(LONG a,LONG b,ULONG,LONG* x,LONG* y)override{if(a<0||b<a||b>(LONG)text_.size())return TS_E_INVALIDPOS;*x=a;*y=b;return S_OK;}
    STDMETHODIMP GetSelection(ULONG index,ULONG count,TS_SELECTION_ACP* p,ULONG* fetched)override{if(!lock_)return TS_E_NOLOCK;selection();*fetched=0;if(count&&(index==TS_DEFAULT_SELECTION||index==0)){p[0]={start_,end_,{TS_AE_END,FALSE}};*fetched=1;}return S_OK;}
    STDMETHODIMP SetSelection(ULONG count,const TS_SELECTION_ACP* p)override{if(!lock_)return TS_E_NOLOCK;if(!count)return E_INVALIDARG;start_=p[0].acpStart;end_=p[0].acpEnd;updating_=true;SendMessageW(edit_,EM_SETSEL,start_,end_);updating_=false;return S_OK;}
    STDMETHODIMP GetText(LONG a,LONG b,WCHAR* text,ULONG capacity,ULONG* length,TS_RUNINFO* runs,ULONG runCapacity,ULONG* runCount,LONG* next)override{
        if(!lock_)return TS_E_NOLOCK;if(b==-1)b=(LONG)text_.size();if(a<0||b<a||b>(LONG)text_.size())return TS_E_INVALIDPOS;
        ULONG n=std::min(capacity,(ULONG)(b-a));if(text&&n)std::copy_n(text_.data()+a,n,text);*length=n;*runCount=0;if(runs&&runCapacity){runs[0]={(ULONG)(capacity?n:b-a),TS_RT_PLAIN};*runCount=1;}*next=a+(capacity?n:b-a);return S_OK;
    }
    STDMETHODIMP SetText(DWORD,LONG a,LONG b,const WCHAR* text,ULONG n,TS_TEXTCHANGE* change)override{
        if((lock_&TS_LF_READWRITE)!=TS_LF_READWRITE)return TS_E_NOLOCK;if(a<0||b<a||b>(LONG)text_.size())return TS_E_INVALIDPOS;
        std::wstring replacement(text,n);text_.replace(a,b-a,replacement);start_=end_=a+n;updating_=true;
        SendMessageW(edit_,EM_SETSEL,a,b);SendMessageW(edit_,EM_REPLACESEL,TRUE,(LPARAM)replacement.c_str());SendMessageW(edit_,EM_SETSEL,start_,end_);updating_=false;
        if(change)*change={a,b,a+(LONG)n};return S_OK;
    }
    STDMETHODIMP GetFormattedText(LONG,LONG,IDataObject**)override{return E_NOTIMPL;}
    STDMETHODIMP GetEmbedded(LONG,REFGUID,REFIID,IUnknown**)override{return E_NOTIMPL;}
    STDMETHODIMP QueryInsertEmbedded(const GUID*,const FORMATETC*,BOOL* yes)override{*yes=FALSE;return S_OK;}
    STDMETHODIMP InsertEmbedded(DWORD,LONG,LONG,IDataObject*,TS_TEXTCHANGE*)override{return E_NOTIMPL;}
    STDMETHODIMP InsertTextAtSelection(DWORD flags,const WCHAR* text,ULONG n,LONG* a,LONG* b,TS_TEXTCHANGE* change)override{if(!lock_)return TS_E_NOLOCK;selection();if(a)*a=start_;if(b)*b=end_;if(flags&TS_IAS_QUERYONLY)return S_OK;return SetText(0,start_,end_,text,n,change);}
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD,IDataObject*,LONG*,LONG*,TS_TEXTCHANGE*)override{return E_NOTIMPL;}
    STDMETHODIMP RequestSupportedAttrs(DWORD,ULONG,const TS_ATTRID*)override{return S_OK;}
    STDMETHODIMP RequestAttrsAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD)override{return S_OK;}
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD)override{return S_OK;}
    STDMETHODIMP FindNextAttrTransition(LONG,LONG stop,ULONG,const TS_ATTRID*,DWORD,LONG* next,BOOL* found,LONG* offset)override{*next=stop;*found=FALSE;*offset=0;return S_OK;}
    STDMETHODIMP RetrieveRequestedAttrs(ULONG,TS_ATTRVAL*,ULONG* fetched)override{*fetched=0;return S_OK;}
    STDMETHODIMP GetEndACP(LONG* length)override{if(!lock_)return TS_E_NOLOCK;*length=(LONG)text_.size();return S_OK;}
    STDMETHODIMP GetActiveView(TsViewCookie* view)override{*view=0;return S_OK;}
    STDMETHODIMP GetACPFromPoint(TsViewCookie,const POINT*,DWORD,LONG* pos)override{selection();*pos=end_;return S_OK;}
    STDMETHODIMP GetTextExt(TsViewCookie,LONG,LONG,RECT* rect,BOOL* clipped)override{POINT point{};GetCaretPos(&point);ClientToScreen(edit_,&point);*rect={point.x,point.y,point.x+2,point.y+24};*clipped=FALSE;return S_OK;}
    STDMETHODIMP GetScreenExt(TsViewCookie,RECT* rect)override{GetWindowRect(edit_,rect);return S_OK;}
    STDMETHODIMP GetWnd(TsViewCookie,HWND* window)override{*window=edit_;return S_OK;}
};
