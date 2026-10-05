#include "platform.h"
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <mutex>
#include <algorithm>

namespace etype {
std::wstring moduleRoot(HMODULE module) {
    wchar_t path[32768]; DWORD n=GetModuleFileNameW(module,path,32768);
    std::filesystem::path p(std::wstring(path,n)); auto parent=p.parent_path();
    if(parent.filename()==L"x64" || parent.filename()==L"x86")parent=parent.parent_path();
    return parent.wstring();
}
std::shared_ptr<const Dictionary> loadDictionary(const std::wstring& root) {
    static std::mutex mutex; static std::weak_ptr<const Dictionary> cached;
    std::lock_guard<std::mutex> lock(mutex);
    if(auto old=cached.lock())return old;
    auto d=std::make_shared<Dictionary>();
#ifndef ETYPE_ONLINE
    d->load(root+L"\\data\\dictionary.tsv");
#else
    (void)root;
#endif
    cached=d; return d;
}
std::wstring settingsFile() {
    // Isolate component tests from the user's saved preferences.
    if(GetEnvironmentVariableW(L"ETYPE_HEADLESS_TEST",nullptr,0)){
        wchar_t file[32768]{};DWORD n=GetEnvironmentVariableW(L"ETYPE_TEST_SETTINGS",file,32768);
        if(n&&n<32768)return std::wstring(file,n);
    }
    wchar_t path[MAX_PATH]; if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,path)))return {};
    auto folder=std::wstring(path)+L"\\" ETYPE_SCOPE; CreateDirectoryW(folder.c_str(),nullptr); return folder+L"\\settings.ini";
}
Settings readSettings() {
    Settings s; auto file=settingsFile();
    s.fontSize=std::clamp((int)GetPrivateProfileIntW(L"EType",L"FontSize",17,file.c_str()),12,28);
    s.volume=std::clamp((int)GetPrivateProfileIntW(L"EType",L"Volume",80,file.c_str()),0,100);
    s.british=GetPrivateProfileIntW(L"EType",L"British",0,file.c_str())!=0;
    s.chinesePunctuation=GetPrivateProfileIntW(L"EType",L"ChinesePunctuation",1,file.c_str())!=0;
    s.sentenceMode=GetPrivateProfileIntW(L"EType",L"SentenceMode",0,file.c_str())!=0;
    s.maleVoice=GetPrivateProfileIntW(L"EType",L"MaleVoice",0,file.c_str())!=0;
    s.slowSpeech=GetPrivateProfileIntW(L"EType",L"SlowSpeech",0,file.c_str())!=0;
    return s;
}
bool writeSettings(const Settings& s) {
    auto f=settingsFile(); bool ok=true;
    ok=WritePrivateProfileStringW(L"EType",L"FontSize",std::to_wstring(s.fontSize).c_str(),f.c_str()) && ok;
    ok=WritePrivateProfileStringW(L"EType",L"Volume",std::to_wstring(s.volume).c_str(),f.c_str()) && ok;
    ok=WritePrivateProfileStringW(L"EType",L"British",s.british?L"1":L"0",f.c_str()) && ok;
    ok=WritePrivateProfileStringW(L"EType",L"ChinesePunctuation",s.chinesePunctuation?L"1":L"0",f.c_str()) && ok;
    ok=WritePrivateProfileStringW(L"EType",L"SentenceMode",s.sentenceMode?L"1":L"0",f.c_str()) && ok;
    ok=WritePrivateProfileStringW(L"EType",L"MaleVoice",s.maleVoice?L"1":L"0",f.c_str()) && ok;
    ok=WritePrivateProfileStringW(L"EType",L"SlowSpeech",s.slowSpeech?L"1":L"0",f.c_str()) && ok;
    return ok;
}
static void launch(const std::wstring& root,const std::wstring& arguments) {
    auto exe=root+L"\\EType.exe"; auto cmd=L"\""+exe+L"\" "+arguments;
    STARTUPINFOW si{};si.cb=sizeof(si); PROCESS_INFORMATION pi{};
    if(CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&si,&pi)) {CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
}
void launchPanel(const std::wstring& root){launch(root,L"");}
namespace {
struct SpeechShared { volatile LONG pid,state; };
struct SpeechMapping {
    HANDLE handle=nullptr;SpeechShared* data=nullptr;
    SpeechMapping(){handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SpeechShared),L"Local\\" ETYPE_SCOPE L".Speech.Status.v2");if(handle)data=(SpeechShared*)MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(SpeechShared));}
    ~SpeechMapping(){if(data)UnmapViewOfFile(data);if(handle)CloseHandle(handle);}
};
SpeechMapping& speechMapping(){static SpeechMapping mapping;return mapping;}
std::wstring cancelName(DWORD pid){return L"Local\\" ETYPE_SCOPE L".Speech.Stop.v2."+std::to_wstring(pid);}
void signalSpeech(DWORD pid){auto event=OpenEventW(EVENT_MODIFY_STATE,FALSE,cancelName(pid).c_str());if(event){SetEvent(event);CloseHandle(event);}}
}
bool speechBusy(SpeechState state){return state==SpeechState::Preparing||state==SpeechState::Playing||state==SpeechState::Stopping;}
SpeechSnapshot speechSnapshot(){
    auto data=speechMapping().data;if(!data)return {};
    SpeechSnapshot value;value.pid=(DWORD)InterlockedCompareExchange(&data->pid,0,0);value.state=(SpeechState)InterlockedCompareExchange(&data->state,0,0);
    if(value.pid&&speechBusy(value.state)){
        auto process=OpenProcess(SYNCHRONIZE,FALSE,value.pid);bool ended=process?WaitForSingleObject(process,0)==WAIT_OBJECT_0:GetLastError()==ERROR_INVALID_PARAMETER;
        if(process)CloseHandle(process);
        if(ended&&(DWORD)InterlockedCompareExchange(&data->pid,0,0)==value.pid){InterlockedCompareExchange(&data->state,(LONG)SpeechState::Failed,(LONG)value.state);value.state=SpeechState::Failed;}
    }
    return value;
}
void publishSpeechState(SpeechState state){
    auto data=speechMapping().data;if(!data)return;
    if(state==SpeechState::Preparing)InterlockedExchange(&data->pid,(LONG)GetCurrentProcessId());
    if((DWORD)InterlockedCompareExchange(&data->pid,0,0)==GetCurrentProcessId())InterlockedExchange(&data->state,(LONG)state);
}
HANDLE prepareSpeechWorker(){
    auto previous=speechSnapshot();auto event=CreateEventW(nullptr,TRUE,FALSE,cancelName(GetCurrentProcessId()).c_str());
    if(!event)return nullptr;
    if(previous.pid&&speechBusy(previous.state))signalSpeech(previous.pid);
    publishSpeechState(SpeechState::Preparing);return event;
}
void stopSpeech(){
    auto value=speechSnapshot();if(value.pid&&speechBusy(value.state)){signalSpeech(value.pid);auto data=speechMapping().data;
        if(data&&(DWORD)InterlockedCompareExchange(&data->pid,0,0)==value.pid)InterlockedCompareExchange(&data->state,(LONG)SpeechState::Stopping,(LONG)value.state);}
}
std::wstring speechStatusText(SpeechState state){
    switch(state){case SpeechState::Preparing:return L"正在准备英文语音…";case SpeechState::Playing:return L"正在播放英文";case SpeechState::Finished:return L"播放完成 · 可重播";
    case SpeechState::Stopping:return L"正在停止播放…";case SpeechState::Stopped:return L"播放已停止";case SpeechState::Failed:return L"播放失败 · 请检查本地服务或音源后重试";default:return L"";}
}
void launchSpeech(const std::wstring& root,const std::string& word){
    if(word.empty() || word.size()>500 || std::any_of(word.begin(),word.end(),[](unsigned char c){return c<32||c>126;}))return;
    // Quote a single CreateProcess argument, including trailing backslashes.
    std::wstring quoted=L"\"";size_t slashes=0;
    for(wchar_t c:wide(word)){if(c==L'\\'){++slashes;continue;}quoted.append(slashes*(c==L'"'?2:1),L'\\');slashes=0;if(c==L'"')quoted+=L'\\';quoted+=c;}
    quoted.append(slashes*2,L'\\');quoted+=L'"';
    launch(root,L"--speak "+quoted);
}
Popup::Popup(HMODULE module,Engine& e,const std::wstring& root):module_(module),engine_(e),root_(root) {}
Popup::~Popup(){if(window_)DestroyWindow(window_);UnregisterClassW(L"EType.Candidate.v1",module_);}
void Popup::hide(){expanded_=false;scrollOffset_=0;if(window_){KillTimer(window_,1);KillTimer(window_,2);ShowWindow(window_,SW_HIDE);}}
void Popup::toggleDetails(POINT anchor){
    if(!engine_.sentenceMode||!engine_.count())return;
    expanded_=!expanded_;expandedRevision_=engine_.revision;expandedSelected_=engine_.selected;scrollOffset_=0;show(anchor);
}
void Popup::scroll(int next){
    if(!expanded_||!window_)return;SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(window_,SB_VERT,&info);
    scrollOffset_=std::clamp(next,0,std::max(0,info.nMax-(int)info.nPage+1));SetScrollPos(window_,SB_VERT,scrollOffset_,TRUE);InvalidateRect(window_,nullptr,FALSE);
}
void Popup::show(POINT anchor,bool modeToast) {
    wchar_t testing[2];if(GetEnvironmentVariableW(L"ETYPE_HEADLESS_TEST",testing,2))return;
    if(engine_.buffer.empty() && !modeToast){hide();return;}
    toast_=modeToast;settings_=readSettings();
    anchor_=anchor;
    if(toast_||!engine_.sentenceMode||!engine_.count()||expandedRevision_!=engine_.revision)expanded_=false;
    if(expandedSelected_!=engine_.selected){scrollOffset_=0;expandedSelected_=engine_.selected;}
    if(!window_){
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=proc;wc.hInstance=module_;wc.lpszClassName=L"EType.Candidate.v1";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.style=CS_DROPSHADOW;
        RegisterClassExW(&wc);
        window_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,wc.lpszClassName,L"EType 候选",WS_POPUP|WS_BORDER|WS_VSCROLL,0,0,480,240,nullptr,nullptr,module_,this);
        if(!window_)return;
    }
    scale_=(int)GetDpiForWindow(window_)*100/96;if(scale_<100)scale_=100;
    auto px=[&](int n){return MulDiv(n,scale_,100);};
    rowHeight_=px(engine_.sentenceMode?110:std::max(48,settings_.fontSize+26));headerHeight_=px(engine_.sentenceMode?128:108);
    int rows=(int)std::min<size_t>(5,engine_.count()>engine_.pageStart()?engine_.count()-engine_.pageStart():0);
    MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&mi);
    int width=std::min(px(640),(int)(mi.rcWork.right-mi.rcWork.left)),height;
    if(engine_.sentenceMode&&rows){
        auto dc=GetDC(window_);auto font=CreateFontW(-px(settings_.fontSize),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");auto old=SelectObject(dc,font);
        int needed=px(settings_.fontSize+26);
        for(size_t i=engine_.pageStart();i<engine_.count();++i){RECT measure{0,0,std::max(px(40),width-px(70)),0};DrawTextW(dc,engine_.sentences[i].text.c_str(),-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);needed=std::max(needed,(int)measure.bottom+px(18));}
        rowHeight_=std::min(rowHeight_,needed);SelectObject(dc,old);DeleteObject(font);ReleaseDC(window_,dc);
    }
    if(engine_.sentenceMode)rowHeight_=std::min(rowHeight_,std::max(px(38),((int)(mi.rcWork.bottom-mi.rcWork.top)-headerHeight_-px(50))/std::max(1,rows)));
    height=toast_?px(70):headerHeight_+rowHeight_*(engine_.sentenceMode?std::max(1,rows):1)+px(38);
    contentHeight_=0;
    if(expanded_){
        auto dc=GetDC(window_);auto font=CreateFontW(-px(settings_.fontSize),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");auto old=SelectObject(dc,font);
        RECT measure{0,0,std::max(px(40),width-px(55)-GetSystemMetrics(SM_CXVSCROLL)),0};
        DrawTextW(dc,engine_.sentences[engine_.selected].text.c_str(),-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        contentHeight_=(int)measure.bottom+px(20);SelectObject(dc,old);DeleteObject(font);ReleaseDC(window_,dc);
        height=std::min((int)(mi.rcWork.bottom-mi.rcWork.top)-px(30),headerHeight_+contentHeight_+px(38));
    }
    anchor.x=std::clamp(anchor.x,(LONG)mi.rcWork.left,(LONG)std::max(mi.rcWork.left,mi.rcWork.right-width));
    if(anchor.y+height>mi.rcWork.bottom)anchor.y-=height+px(25);
    anchor.y=std::max(anchor.y,mi.rcWork.top);
    SetWindowPos(window_,HWND_TOPMOST,anchor.x,anchor.y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    RECT client{};GetClientRect(window_,&client);
    SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;info.nMax=expanded_?std::max(0,contentHeight_-1):0;info.nPage=std::max(1,(int)client.bottom-headerHeight_-px(38));info.nPos=scrollOffset_;SetScrollInfo(window_,SB_VERT,&info,TRUE);
    InvalidateRect(window_,nullptr,FALSE);KillTimer(window_,1);if(toast_)SetTimer(window_,1,1100,nullptr);
    SetTimer(window_,2,200,nullptr);
}
static void text(HDC dc,const std::wstring& value,RECT rect,int size,COLORREF color,bool bold=false) {
    HFONT font=CreateFontW(-size,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    DrawTextW(dc,value.c_str(),(int)value.size(),&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,old);DeleteObject(font);
}
void Popup::paint(HDC supplied) {
    PAINTSTRUCT ps{};auto dc=supplied?supplied:BeginPaint(window_,&ps);RECT area;GetClientRect(window_,&area);
    auto buffer=CreateCompatibleDC(dc);auto bmp=CreateCompatibleBitmap(dc,area.right,area.bottom);auto old=SelectObject(buffer,bmp);
    HBRUSH bg=CreateSolidBrush(RGB(250,252,255));FillRect(buffer,&area,bg);DeleteObject(bg);
    auto px=[&](int n){return MulDiv(n,scale_,100);};
    auto draw=[&](const std::wstring& s,int x,int y,int w,int h,int size,COLORREF c,bool bold=false){text(buffer,s,{px(x),px(y),px(x+w),px(y+h)},px(size),c,bold);};
    COLORREF ink=RGB(27,40,62),muted=RGB(101,116,138),green=RGB(35,100,210);
    candidateRects_.clear();
    if(toast_){draw(engine_.english?L"EType · 英文直输":(engine_.sentenceMode?L"EType · 句子模式":L"EType · 单词模式"),18,8,590,50,18,green,true);}
    else {
        draw(engine_.sentenceMode?L"ETYPE   句子模式 ▾":L"ETYPE   单词模式 ▾",16,8,300,25,11,green,true);
        auto speech=speechSnapshot();text(buffer,speechBusy(speech.state)?L"停止":L"发音",{area.right-px(128),px(8),area.right-px(70),px(33)},px(12),green,true);
        text(buffer,L"设置",{area.right-px(64),px(8),area.right-px(8),px(33)},px(12),muted);
        if(engine_.sentenceMode)text(buffer,engine_.translating?L"等待":L"翻译 / 重试",{area.right-px(229),px(8),area.right-px(132),px(33)},px(11),green);
        if(engine_.sentenceMode){
            HFONT font=CreateFontW(-px(17),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");auto before=SelectObject(buffer,font);
            size_t a=engine_.caret>45?engine_.caret-45:0,b=std::min(engine_.buffer.size(),a+90);
            auto shown=wide(engine_.buffer.substr(a,engine_.caret-a))+L"│"+wide(engine_.buffer.substr(engine_.caret,b-engine_.caret));
            if(a)shown=L"…"+shown;if(b<engine_.buffer.size())shown+=L"…";
            RECT r{px(16),px(38),area.right-px(16),px(93)};SetTextColor(buffer,ink);DrawTextW(buffer,shown.c_str(),-1,&r,DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(buffer,before);DeleteObject(font);
        }else text(buffer,wide(engine_.buffer),{px(16),px(35),area.right-px(16),px(69)},px(22),ink,true);
        auto e=engine_.entry();std::wstring detail;
        if(engine_.sentenceMode)detail=engine_.sentenceStatus.empty()?L"空格分词 · Enter 翻译/选中文 · Ctrl+Enter 输出英文":engine_.sentenceStatus;
        else if(engine_.correcting)detail=L"拼写建议 · 确认英文后再选择中文";
        else if(e){if(!e->phonetic.empty())detail=L"/"+e->phonetic+L"/";if(!e->root.empty())detail+=L"   原形 "+e->root+L" · "+e->form;}
        else detail=engine_.onlineWords&&!engine_.wordStatus.empty()?engine_.wordStatus:L"完整拼写后显示释义 · 空格检查拼写";
        if(speechBusy(speech.state)||speech.state==SpeechState::Failed)detail+=L"   ·   "+speechStatusText(speech.state);
        text(buffer,detail,{px(16),px(engine_.sentenceMode?96:74),area.right-px(12),px(engine_.sentenceMode?123:99)},px(12),muted);
        size_t start=engine_.pageStart(),end=std::min(start+5,engine_.count());
        if(start==end)text(buffer,engine_.sentenceMode?(engine_.translating?L"正在翻译，请稍候… · Ctrl+Enter 可输出英文":L"输入完整英文句子，再按 Enter 翻译"):(engine_.correcting?L"未找到该单词，回车可保留英文":L"继续输入英文单词…"),{px(18),headerHeight_,area.right-px(16),headerHeight_+rowHeight_},px(14),muted);
        if(expanded_){
            int saved=SaveDC(buffer);IntersectClipRect(buffer,px(16),headerHeight_,area.right-px(16),area.bottom-px(38));
            HFONT font=CreateFontW(-px(settings_.fontSize),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");auto before=SelectObject(buffer,font);
            RECT r{px(20),headerHeight_+px(8)-scrollOffset_,area.right-px(20),headerHeight_+contentHeight_-scrollOffset_};SetTextColor(buffer,ink);
            DrawTextW(buffer,engine_.sentences[engine_.selected].text.c_str(),-1,&r,DT_WORDBREAK|DT_NOPREFIX);SelectObject(buffer,before);DeleteObject(font);RestoreDC(buffer,saved);
        }
        for(size_t i=start;!expanded_&&i<end;++i){
            int top=headerHeight_+(engine_.sentenceMode?(int)(i-start)*rowHeight_:0);
            int width=(area.right-px(16))/(int)(end-start);
            int left=engine_.sentenceMode?px(8):px(8)+(int)(i-start)*width;
            int right=engine_.sentenceMode?area.right-px(8):left+width-px(4);
            RECT cell{left,top,right,top+rowHeight_-px(3)};candidateRects_.push_back(cell);
            if(i==engine_.selected){auto b=CreateSolidBrush(RGB(225,237,255));FillRect(buffer,&cell,b);DeleteObject(b);}
            text(buffer,std::to_wstring(i-start+1),{left+px(10),top,left+px(32),top+(engine_.sentenceMode?px(settings_.fontSize+30):rowHeight_)},px(13),green,true);
            std::wstring value,pos;
            if(engine_.sentenceMode)value=engine_.sentences[i].text;
            else if(engine_.correcting){value=wide(engine_.corrections[i]);pos=L"确认拼写";}
            else {value=e->candidates[i].text;pos=e->candidates[i].pos;}
            if(engine_.sentenceMode){
                HFONT font=CreateFontW(-px(settings_.fontSize),0,0,0,i==engine_.selected?FW_SEMIBOLD:FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");auto before=SelectObject(buffer,font);
                RECT r{left+px(40),top+px(8),right-px(10),top+rowHeight_-px(8)};SetTextColor(buffer,ink);DrawTextW(buffer,value.c_str(),-1,&r,DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(buffer,before);DeleteObject(font);
            }else text(buffer,value,{left+px(36),top,right-px(8),top+rowHeight_},px(settings_.fontSize),ink,i==engine_.selected);
        }
        std::wstring footer=engine_.sentenceMode?(expanded_?L"F2 收起全文 · ↑↓ 选择 · Enter 确认 · Ctrl+Enter 英文":L"Enter / 1–3 / 空格选中文 · F2 全文 · Ctrl+Enter 英文"):L"空格选词 · Enter 英文 · Ctrl+Shift+空格 句子模式";
        if(expanded_)footer=L"候选 "+std::to_wstring(engine_.selected+1)+L"   "+footer;
        if(engine_.count()>5)footer+=L"   "+std::to_wstring(start/5+1)+L"/"+std::to_wstring((engine_.count()+4)/5);
        text(buffer,footer,{px(16),area.bottom-px(35),area.right-px(12),area.bottom-px(5)},px(11),muted);
    }
    BitBlt(dc,0,0,area.right,area.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bmp);DeleteDC(buffer);if(!supplied)EndPaint(window_,&ps);
}
LRESULT CALLBACK Popup::proc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto p=(Popup*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_NCCREATE){p=(Popup*)((CREATESTRUCTW*)l)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)p);}
    if(!p)return DefWindowProcW(h,m,w,l);
    switch(m){
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_PAINT:p->paint();return 0;
    case WM_PRINTCLIENT:p->paint((HDC)w);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_TIMER:if(w==1)p->hide();else if(w==2)InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_MOUSEWHEEL:p->scroll(p->scrollOffset_-GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*MulDiv(72,p->scale_,100));return 0;
    case WM_VSCROLL:{SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(h,SB_VERT,&info);int next=p->scrollOffset_;
        switch(LOWORD(w)){case SB_TOP:next=0;break;case SB_BOTTOM:next=info.nMax;break;case SB_LINEUP:next-=MulDiv(28,p->scale_,100);break;case SB_LINEDOWN:next+=MulDiv(28,p->scale_,100);break;case SB_PAGEUP:next-=(int)info.nPage;break;case SB_PAGEDOWN:next+=(int)info.nPage;break;case SB_THUMBTRACK:next=info.nTrackPos;break;default:break;}p->scroll(next);return 0;}
    case WM_LBUTTONUP:{int x=(short)LOWORD(l)*100/p->scale_,y=(short)HIWORD(l)*100/p->scale_;
        RECT area;GetClientRect(h,&area);int width=area.right*100/p->scale_;
        if(y<35 && x>=width-64){launchPanel(p->root_);return 0;}
        if(y<35 && x>=width-128){if(speechBusy(speechSnapshot().state))stopSpeech();else if(!p->engine_.correcting&&(p->engine_.sentenceMode||p->engine_.entry()))launchSpeech(p->root_,p->engine_.buffer);return 0;}
        if(y<35&&x>=width-229&&p->engine_.sentenceMode){if(p->translate)p->translate();return 0;}
        if(y<35 && x<300){if(p->changeMode)p->changeMode();return 0;}
        if((short)HIWORD(l)>=area.bottom-MulDiv(38,p->scale_,100)&&p->engine_.sentenceMode){p->toggleDetails(p->anchor_);return 0;}
        POINT point{(short)LOWORD(l),(short)HIWORD(l)};
        if(!p->toast_)for(size_t row=0;row<p->candidateRects_.size();++row)if(PtInRect(&p->candidateRects_[row],point)){if(p->select)p->select(row);break;}return 0;
    }
    }
    return DefWindowProcW(h,m,w,l);
}
}
