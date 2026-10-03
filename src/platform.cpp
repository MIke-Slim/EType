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
    auto d=std::make_shared<Dictionary>(); d->load(root+L"\\data\\dictionary.tsv"); cached=d; return d;
}
std::wstring settingsFile() {
    wchar_t path[MAX_PATH]; if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,path)))return {};
    auto folder=std::wstring(path)+L"\\EType"; CreateDirectoryW(folder.c_str(),nullptr); return folder+L"\\settings.ini";
}
Settings readSettings() {
    Settings s; auto file=settingsFile();
    s.fontSize=std::clamp((int)GetPrivateProfileIntW(L"EType",L"FontSize",17,file.c_str()),12,28);
    s.volume=std::clamp((int)GetPrivateProfileIntW(L"EType",L"Volume",80,file.c_str()),0,100);
    s.british=GetPrivateProfileIntW(L"EType",L"British",0,file.c_str())!=0;
    s.chinesePunctuation=GetPrivateProfileIntW(L"EType",L"ChinesePunctuation",1,file.c_str())!=0;
    return s;
}
bool writeSettings(const Settings& s) {
    auto f=settingsFile(); bool ok=true;
    ok=WritePrivateProfileStringW(L"EType",L"FontSize",std::to_wstring(s.fontSize).c_str(),f.c_str()) && ok;
    ok=WritePrivateProfileStringW(L"EType",L"Volume",std::to_wstring(s.volume).c_str(),f.c_str()) && ok;
    ok=WritePrivateProfileStringW(L"EType",L"British",s.british?L"1":L"0",f.c_str()) && ok;
    ok=WritePrivateProfileStringW(L"EType",L"ChinesePunctuation",s.chinesePunctuation?L"1":L"0",f.c_str()) && ok;
    return ok;
}
static void launch(const std::wstring& root,const std::wstring& arguments) {
    auto exe=root+L"\\EType.exe"; auto cmd=L"\""+exe+L"\" "+arguments;
    STARTUPINFOW si{};si.cb=sizeof(si); PROCESS_INFORMATION pi{};
    if(CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&si,&pi)) {CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
}
void launchPanel(const std::wstring& root){launch(root,L"");}
void launchSpeech(const std::wstring& root,const std::string& word){
    if(word.empty() || word.size()>80 || std::any_of(word.begin(),word.end(),[](unsigned char c){return !(std::isalpha(c)||c=='-'||c=='\'');}))return;
    launch(root,L"--speak \""+wide(word)+L"\"");
}
Popup::Popup(HMODULE module,Engine& e,const std::wstring& root):module_(module),engine_(e),root_(root) {}
Popup::~Popup(){if(window_)DestroyWindow(window_);UnregisterClassW(L"EType.Candidate.v1",module_);}
void Popup::hide(){if(window_){KillTimer(window_,1);ShowWindow(window_,SW_HIDE);}}
void Popup::show(POINT anchor,bool modeToast) {
    wchar_t testing[2];if(GetEnvironmentVariableW(L"ETYPE_HEADLESS_TEST",testing,2))return;
    if(engine_.buffer.empty() && !modeToast){hide();return;}
    toast_=modeToast;settings_=readSettings();
    if(!window_){
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=proc;wc.hInstance=module_;wc.lpszClassName=L"EType.Candidate.v1";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.style=CS_DROPSHADOW;
        RegisterClassExW(&wc);
        window_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,wc.lpszClassName,L"EType 候选",WS_POPUP|WS_BORDER,0,0,480,240,nullptr,nullptr,module_,this);
        if(!window_)return;
    }
    scale_=(int)GetDpiForWindow(window_)*100/96;if(scale_<100)scale_=100;
    auto px=[&](int n){return MulDiv(n,scale_,100);};
    rowHeight_=px(std::max(38,settings_.fontSize+21));headerHeight_=px(108);
    int rows=(int)std::min<size_t>(5,engine_.count()>engine_.pageStart()?engine_.count()-engine_.pageStart():0);
    int width=px(490),height=toast_?px(70):headerHeight_+rowHeight_*std::max(1,rows)+px(38);
    MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&mi);
    anchor.x=std::clamp(anchor.x,(LONG)mi.rcWork.left,(LONG)std::max(mi.rcWork.left,mi.rcWork.right-width));
    if(anchor.y+height>mi.rcWork.bottom)anchor.y-=height+px(25);
    anchor.y=std::max(anchor.y,mi.rcWork.top);
    SetWindowPos(window_,HWND_TOPMOST,anchor.x,anchor.y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    InvalidateRect(window_,nullptr,FALSE);KillTimer(window_,1);if(toast_)SetTimer(window_,1,1100,nullptr);
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
    HBRUSH bg=CreateSolidBrush(RGB(250,251,249));FillRect(buffer,&area,bg);DeleteObject(bg);
    auto px=[&](int n){return MulDiv(n,scale_,100);};
    auto draw=[&](const std::wstring& s,int x,int y,int w,int h,int size,COLORREF c,bool bold=false){text(buffer,s,{px(x),px(y),px(x+w),px(y+h)},px(size),c,bold);};
    COLORREF ink=RGB(29,49,48),muted=RGB(104,118,116),green=RGB(17,121,102);
    if(toast_){draw(engine_.english?L"EType · 英文直输":L"EType · 英文转中文",18,8,440,50,18,green,true);}
    else {
        draw(L"ETYPE   英文词汇输入法",16,8,300,25,11,green,true);
        draw(L"发音",365,8,50,25,12,green,true);draw(L"设置",427,8,50,25,12,muted);
        draw(wide(engine_.buffer),16,35,460,34,22,ink,true);
        auto e=engine_.entry();std::wstring detail;
        if(engine_.correcting)detail=L"拼写建议 · 确认英文后再选择中文";
        else if(e){if(!e->phonetic.empty())detail=L"/"+e->phonetic+L"/";if(!e->root.empty())detail+=L"   原形 "+e->root+L" · "+e->form;}
        else detail=L"完整拼写后显示释义 · 空格检查拼写";
        draw(detail,16,74,458,25,12,muted);
        size_t start=engine_.pageStart(),end=std::min(start+5,engine_.count());
        if(start==end)draw(engine_.correcting?L"未找到该单词，回车可保留英文":L"继续输入英文单词…",18,110,450,38,14,muted);
        for(size_t i=start;i<end;++i){
            int top=headerHeight_+(int)(i-start)*rowHeight_;
            if(i==engine_.selected){RECT r{px(8),top,area.right-px(8),top+rowHeight_-px(3)};auto b=CreateSolidBrush(RGB(222,239,232));FillRect(buffer,&r,b);DeleteObject(b);}
            text(buffer,std::to_wstring(i-start+1),{px(20),top,px(44),top+rowHeight_},px(13),green,true);
            std::wstring value,pos;
            if(engine_.correcting){value=wide(engine_.corrections[i]);pos=L"确认拼写";}
            else {value=e->candidates[i].text;pos=e->candidates[i].pos;}
            text(buffer,value,{px(53),top,area.right-px(118),top+rowHeight_},px(settings_.fontSize),ink,i==engine_.selected);
            text(buffer,pos,{area.right-px(112),top,area.right-px(14),top+rowHeight_},px(11),muted);
        }
        std::wstring footer=L"空格选词 · 回车英文 · Shift 切换";
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
    case WM_TIMER:p->hide();return 0;
    case WM_LBUTTONUP:{int x=(short)LOWORD(l)*100/p->scale_,y=(short)HIWORD(l)*100/p->scale_;
        if(y<35 && x>=427){launchPanel(p->root_);return 0;}
        if(y<35 && x>=355){if(!p->engine_.correcting && p->engine_.entry())launchSpeech(p->root_,p->engine_.buffer);return 0;}
        int rawY=(short)HIWORD(l);if(!p->toast_ && rawY>=p->headerHeight_){size_t row=(rawY-p->headerHeight_)/p->rowHeight_;if(row<5 && p->select)p->select(row);}return 0;
    }
    }
    return DefWindowProcW(h,m,w,l);
}
}
