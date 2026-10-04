#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <msctf.h>
#include <sapi.h>
#include <sphelper.h>
#include <gdiplus.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "platform.h"
#include "identity.h"
#include "editor_store.h"

using namespace etype;
static HINSTANCE instance;
static std::wstring root;
static std::shared_ptr<const Dictionary> dictionary;
static HWND mainWindow,edit,fontCombo,accentCombo,volumeBar,punctuationCombo,statusLabel;
static HFONT bodyFont,titleFont;
static HMODULE trialModule=nullptr;
static ITfThreadMgr* trialThread=nullptr;
static ITfKeyEventSink* trialSink=nullptr;
static ITfDocumentMgr* trialDocument=nullptr;
static ITfContext* trialContext=nullptr;
static EditorStore* trialStore=nullptr;
static bool componentSelfTest=false;
static unsigned testActivationNotifications=0,testEditFocusNotifications=0;
static int scale=100;
static int px(int n){return MulDiv(n,scale,100);}
static std::vector<std::pair<std::wstring,std::wstring>> voices(){
    std::vector<std::pair<std::wstring,std::wstring>> result;
    IEnumSpObjectTokens* enumeration=nullptr;
    if(FAILED(SpEnumTokens(SPCAT_VOICES,nullptr,nullptr,&enumeration)))return result;
    ISpObjectToken* token=nullptr;
    while(enumeration->Next(1,&token,nullptr)==S_OK){wchar_t* name=nullptr,*language=nullptr;
        SpGetDescription(token,&name);ISpDataKey* attributes=nullptr;
        if(SUCCEEDED(token->OpenKey(L"Attributes",&attributes))){attributes->GetStringValue(L"Language",&language);attributes->Release();}
        result.push_back({name?name:L"Unknown",language?language:L""});CoTaskMemFree(name);CoTaskMemFree(language);token->Release();
    }
    enumeration->Release();return result;
}
static bool speak(const std::wstring& word,bool british,bool silent=false,const std::wstring& output=L"",int volume=-1){
    ISpVoice* voice=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_ALL,IID_ISpVoice,(void**)&voice);
    if(FAILED(hr))return false;
    auto settings=readSettings();voice->SetVolume((USHORT)(volume>=0?std::clamp(volume,0,100):settings.volume));
    IEnumSpObjectTokens* enumeration=nullptr;ISpObjectToken* chosen=nullptr;
    const wchar_t* filter=british?L"Language=809":L"Language=409";
    if(SUCCEEDED(SpEnumTokens(SPCAT_VOICES,filter,nullptr,&enumeration))){enumeration->Next(1,&chosen,nullptr);enumeration->Release();}
    if(!chosen){voice->Release();if(!silent)MessageBoxW(nullptr,british?L"未找到英式离线音源。请在 Windows 的语言和语音设置中添加英语（英国）语音。":L"未找到美式离线音源。请在 Windows 的语言和语音设置中添加英语（美国）语音。",L"EType 发音",MB_OK|MB_ICONINFORMATION);return false;}
    hr=voice->SetVoice(chosen);chosen->Release();
    ISpStream* stream=nullptr;
    if(SUCCEEDED(hr)&&!output.empty()){
        hr=CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_INPROC_SERVER,IID_ISpStream,(void**)&stream);
        WAVEFORMATEX format{WAVE_FORMAT_PCM,1,22050,44100,2,16,0};
        if(SUCCEEDED(hr))hr=stream->BindToFile(output.c_str(),SPFM_CREATE_ALWAYS,&SPDFID_WaveFormatEx,&format,0);
        if(SUCCEEDED(hr))hr=voice->SetOutput(stream,TRUE);
    }
    if(SUCCEEDED(hr))hr=voice->Speak(word.c_str(),SPF_IS_NOT_XML,nullptr);
    if(stream){stream->Close();stream->Release();}voice->Release();return SUCCEEDED(hr);
}
static HRESULT activate(){
    if(LOWORD((ULONG_PTR)GetKeyboardLayout(0))!=ETypeLanguage){
        auto layout=LoadKeyboardLayoutW(L"00000804",KLF_NOTELLSHELL);
        if(layout)ActivateKeyboardLayout(layout,0);
    }
    ITfInputProcessorProfileMgr* mgr=nullptr;HRESULT hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfileMgr,(void**)&mgr);
    if(SUCCEEDED(hr)){hr=mgr->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,ETypeLanguage,ETypeClsid,ETypeProfile,nullptr,trialThread?0x10000000:0x10000001);mgr->Release();}return hr;
}
static void endTrial();
static HRESULT focusTrial(bool focused){
    if(!trialSink)return E_UNEXPECTED;
    HRESULT hr=S_OK;
    if(focused&&trialThread&&trialDocument)hr=trialThread->SetFocus(trialDocument);
    trialSink->OnSetFocus(focused?TRUE:FALSE);
    return hr;
}
static HRESULT startTrial(){
    if(trialSink){trialThread->SetFocus(trialDocument);return S_OK;}
    HRESULT hr=CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_ITfThreadMgr,(void**)&trialThread);
    TfClientId id=0;if(SUCCEEDED(hr))hr=trialThread->Activate(&id);
    auto dll=root+L"\\x64\\EType.dll";
    if(SUCCEEDED(hr))trialModule=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(SUCCEEDED(hr)&&!trialModule)hr=HRESULT_FROM_WIN32(GetLastError());
    using FactoryFn=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
    IClassFactory* factory=nullptr;
    if(SUCCEEDED(hr)){auto fn=(FactoryFn)GetProcAddress(trialModule,"DllGetClassObject");hr=fn?fn(ETypeClsid,IID_IClassFactory,(void**)&factory):E_FAIL;}
    IPreviewTextService* preview=nullptr;
    if(SUCCEEDED(hr))hr=factory->CreateInstance(nullptr,ETypePreviewId,(void**)&preview);
    if(factory)factory->Release();
    if(SUCCEEDED(hr))hr=preview->InitializeHost(trialThread,id);
    if(SUCCEEDED(hr))hr=preview->QueryInterface(IID_ITfKeyEventSink,(void**)&trialSink);
    if(preview)preview->Release();
    if(SUCCEEDED(hr)&&!trialSink)hr=E_FAIL;
    if(SUCCEEDED(hr))hr=trialThread->CreateDocumentMgr(&trialDocument);
    if(SUCCEEDED(hr)){trialStore=new EditorStore(edit);TfEditCookie cookie;hr=trialDocument->CreateContext(id,0,trialStore,&trialContext,&cookie);}
    if(SUCCEEDED(hr))hr=trialDocument->Push(trialContext);
    if(SUCCEEDED(hr)){
        // The interactive preview follows the native edit's focus. The directly
        // driven component test owns document focus explicitly; associating an
        // inactive native window would let Windows replace that focus on pump.
        if(!componentSelfTest){ITfDocumentMgr* previous=nullptr;hr=trialThread->AssociateFocus(edit,trialDocument,&previous);if(previous)previous->Release();}
        if(SUCCEEDED(hr))hr=trialThread->SetFocus(trialDocument);
    }
    if(hr!=S_OK)endTrial();return hr;
}
static void endTrial(){
    if(trialThread){
        if(trialSink){ITfTextInputProcessor* service=nullptr;if(SUCCEEDED(trialSink->QueryInterface(IID_ITfTextInputProcessor,(void**)&service))){service->Deactivate();service->Release();}trialSink->Release();trialSink=nullptr;}
        trialThread->SetFocus(nullptr);
        if(trialDocument)trialDocument->Pop(TF_POPF_ALL);
        if(trialContext){trialContext->Release();trialContext=nullptr;}
        if(trialDocument){trialDocument->Release();trialDocument=nullptr;}
        if(trialStore){trialStore->Release();trialStore=nullptr;}
        trialThread->Deactivate();trialThread->Release();trialThread=nullptr;
    }
    // In-flight async TSF callbacks may still refer to the module until exit.
    if(trialModule){using Fn=HRESULT(WINAPI*)();auto fn=(Fn)GetProcAddress(trialModule,"DllCanUnloadNow");if(fn&&fn()==S_OK)FreeLibrary(trialModule);trialModule=nullptr;}
}
static HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int id,int x,int y,int width,int height){
    auto h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),mainWindow,(HMENU)(INT_PTR)id,instance,nullptr);
    SendMessageW(h,WM_SETFONT,(WPARAM)bodyFont,TRUE);return h;
}
static void label(const wchar_t* value,int x,int y,int w,int h,int id=0){control(L"STATIC",value,SS_LEFT,id,x,y,w,h);}
static void comboItem(HWND combo,const wchar_t* item){SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)item);}
static void refreshVoices(){
    auto available=voices();bool us=false,gb=false;
    for(auto& pair:available){if(pair.second.find(L"409")!=std::wstring::npos)us=true;if(pair.second.find(L"809")!=std::wstring::npos)gb=true;}
    std::wstring description=L"离线语音：美式 "+std::wstring(us?L"可用":L"未安装")+L"  ·  英式 "+(gb?L"可用":L"未安装");
    SetWindowTextW(statusLabel,description.c_str());
}
static void createControls(){
    bodyFont=CreateFontW(-px(15),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    titleFont=CreateFontW(-px(29),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    auto logo=control(L"STATIC",L"",SS_ICON|SS_REALSIZEIMAGE,0,34,18,64,64);
    auto icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,px(64),px(64),LR_SHARED);
    SendMessageW(logo,STM_SETICON,(WPARAM)icon,0);
    auto title=control(L"STATIC",L"EType · 英文词汇输入法",SS_LEFT,0,110,27,640,44);SendMessageW(title,WM_SETFONT,(WPARAM)titleFont,TRUE);
    label(L"把每一次中文输入，变成一次英文单词练习。",35,79,750,28);
    std::wstring count=L"离线词库  "+std::to_wstring(dictionary->size())+L" 个词条   ·   完整拼写   ·   固定候选顺序";
    label(count.c_str(),35,119,750,28);
    control(L"BUTTON",L"安装系统输入法",BS_PUSHBUTTON,101,35,166,178,38);
    control(L"BUTTON",L"直接试用（无需安装）",BS_PUSHBUTTON,102,228,166,210,38);
    control(L"BUTTON",L"使用说明",BS_PUSHBUTTON,103,453,166,125,38);
    control(L"BUTTON",L"卸载输入法",BS_PUSHBUTTON,104,593,166,143,38);
    label(L"先点击“直接试用”，再在下方输入英文单词；其他软件使用需安装。",35,227,750,27);
    edit=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,201,35,263,701,139);
    label(L"试试 bank、apple、went；空格选中文，回车留英文，Shift 切换模式。",35,414,750,27);
    label(L"候选字号",35,465,110,28);
    fontCombo=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,301,145,461,108,210);
    for(int i=12;i<=28;++i)comboItem(fontCombo,std::to_wstring(i).c_str());
    label(L"发音口音",287,465,90,28);
    accentCombo=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,302,384,461,151,120);
    comboItem(accentCombo,L"美式英语（默认）");comboItem(accentCombo,L"英式英语");
    control(L"BUTTON",L"试听 apple",BS_PUSHBUTTON,303,560,459,176,34);
    label(L"发音音量",35,513,110,28);
    volumeBar=control(TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_HORZ,304,140,507,390,37);SendMessageW(volumeBar,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
    label(L"默认标点",35,559,110,28);
    punctuationCombo=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,305,145,555,174,130);
    comboItem(punctuationCombo,L"中文标点 ，。？！");comboItem(punctuationCombo,L"英文标点 ,.!?");
    control(L"BUTTON",L"保存设置",BS_PUSHBUTTON,306,560,553,176,35);
    statusLabel=control(L"STATIC",L"",SS_LEFT,307,35,603,700,30);
    label(L"词库 v1.0.1 · 部分词库音标为英式，播放口音以设置为准。",35,640,730,26);
    auto settings=readSettings();SendMessageW(fontCombo,CB_SETCURSEL,settings.fontSize-12,0);
    SendMessageW(accentCombo,CB_SETCURSEL,settings.british?1:0,0);SendMessageW(punctuationCombo,CB_SETCURSEL,settings.chinesePunctuation?0:1,0);SendMessageW(volumeBar,TBM_SETPOS,TRUE,settings.volume);
    refreshVoices();
}
static Settings currentSettings(){Settings s;s.fontSize=(int)SendMessageW(fontCombo,CB_GETCURSEL,0,0)+12;s.british=SendMessageW(accentCombo,CB_GETCURSEL,0,0)==1;s.chinesePunctuation=SendMessageW(punctuationCombo,CB_GETCURSEL,0,0)==0;s.volume=(int)SendMessageW(volumeBar,TBM_GETPOS,0,0);return s;}
static void setup(bool uninstall){
    auto uninstaller=root+L"\\unins000.exe";
    if(uninstall&&std::filesystem::exists(uninstaller)){
        auto result=(INT_PTR)ShellExecuteW(mainWindow,L"open",uninstaller.c_str(),nullptr,root.c_str(),SW_SHOWNORMAL);
        if(result<=32)MessageBoxW(mainWindow,L"未能启动卸载程序。请在 Windows 设置的已安装应用中卸载 EType。",L"EType",MB_OK|MB_ICONERROR);
        else PostMessageW(mainWindow,WM_CLOSE,0,0);
        return;
    }
    auto path=root+(uninstall?L"\\uninstall.ps1":L"\\install.ps1");
    auto args=L"-NoProfile -ExecutionPolicy Bypass -File \""+path+L"\"";
    if(uninstall)args+=L" -InstallDir \""+root+L"\"";
    auto result=(INT_PTR)ShellExecuteW(mainWindow,L"runas",L"powershell.exe",args.c_str(),root.c_str(),SW_HIDE);
    if(result<=32)MessageBoxW(mainWindow,L"安装或卸载未执行。你可以直接运行软件目录里的安装或卸载脚本。",L"EType",MB_OK|MB_ICONINFORMATION);
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE:mainWindow=h;createControls();return 0;
    case WM_CTLCOLORSTATIC:{auto dc=(HDC)w;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(43,63,60));static HBRUSH brush=CreateSolidBrush(RGB(246,249,246));return(LRESULT)brush;}
    case WM_COMMAND:switch(LOWORD(w)){
        case 201:
            if(trialStore&&HIWORD(w)==EN_CHANGE)trialStore->sync();
            if(componentSelfTest&&(HIWORD(w)==EN_KILLFOCUS||HIWORD(w)==EN_SETFOCUS))++testEditFocusNotifications;
            if(!componentSelfTest&&HIWORD(w)==EN_KILLFOCUS)focusTrial(false);
            if(!componentSelfTest&&HIWORD(w)==EN_SETFOCUS)focusTrial(true);
            break;
        case 101:setup(false);break;
        case 102:{auto hr=startTrial();if(hr!=S_OK){std::wstring text=L"未能启用本窗口试用。你仍可尝试安装系统输入法。\n错误码：";wchar_t hex[24];swprintf(hex,24,L"0x%08lX",(unsigned long)hr);text+=hex;MessageBoxW(h,text.c_str(),L"EType",MB_OK|MB_ICONINFORMATION);}else{SetFocus(edit);SetWindowTextW(statusLabel,L"本窗口已启用 EType。输入 bank 测试；在其他软件使用需要安装系统输入法。");}break;}
        case 103:{auto path=root+L"\\使用说明.txt";ShellExecuteW(h,L"open",path.c_str(),nullptr,root.c_str(),SW_SHOWNORMAL);break;}
        case 104:setup(true);break;
        case 303:{auto s=currentSettings();speak(L"apple",s.british,false,L"",s.volume);break;}
        case 306:if(writeSettings(currentSettings())){SetWindowTextW(statusLabel,L"设置已保存，下次显示候选窗口时生效。");}else MessageBoxW(h,L"设置保存失败，请检查本地用户目录的写入权限。",L"EType",MB_OK|MB_ICONERROR);break;
    }return 0;
    case WM_ACTIVATE:
        if(componentSelfTest)++testActivationNotifications;
        if(!componentSelfTest){
            if(LOWORD(w)==WA_INACTIVE)focusTrial(false);
            else if(GetFocus()==edit)focusTrial(true);
        }
        break;
    case WM_CLOSE:endTrial();DestroyWindow(h);return 0;
    case WM_DESTROY:DeleteObject(bodyFont);DeleteObject(titleFont);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
static std::string json(const std::wstring& value){std::string result="\"";for(unsigned char ch:utf8(value)){if(ch=='"'||ch=='\\'){result+='\\';result+=(char)ch;}else if(ch<32)result+=' ';else result+=(char)ch;}return result+"\"";}
static void diagnostics(const std::wstring& output){
    std::ofstream file{std::filesystem::path(output)};file<<"{\"dictionary_entries\":"<<dictionary->size()<<",\"voices\":[";bool first=true;
    for(auto& pair:voices()){if(!first)file<<",";first=false;file<<"{\"name\":"<<json(pair.first)<<",\"language\":"<<json(pair.second)<<"}";}
    file<<"]}";
}
// Render only this application's own windows for layout QA. This does not
// automate or capture any unrelated desktop application.
static bool snapshot(HWND window,const std::wstring& path){
    RECT rect;GetWindowRect(window,&rect);int width=rect.right-rect.left,height=rect.bottom-rect.top;
    auto dc=GetWindowDC(window);auto memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,width,height);auto old=SelectObject(memory,bitmap);
    BOOL ok=PrintWindow(window,memory,2);SelectObject(memory,old);
    UINT count=0,bytes=0;Gdiplus::GetImageEncodersSize(&count,&bytes);std::vector<BYTE> encoders(bytes);auto info=(Gdiplus::ImageCodecInfo*)encoders.data();Gdiplus::GetImageEncoders(count,bytes,info);
    Gdiplus::Status status=Gdiplus::GenericError;
    for(UINT i=0;i<count;++i)if(wcscmp(info[i].MimeType,L"image/png")==0){Gdiplus::Bitmap image(bitmap,nullptr);status=image.Save(path.c_str(),&info[i].Clsid,nullptr);break;}
    DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,dc);return ok&&status==Gdiplus::Ok;
}
static int render(const std::wstring& folder){
    std::filesystem::create_directories(folder);ULONG_PTR token;Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr);
    ShowWindow(mainWindow,SW_SHOWNOACTIVATE);UpdateWindow(mainWindow);bool ok=snapshot(mainWindow,folder+L"\\settings.png");
    {
        Engine engine(dictionary);Popup popup(instance,engine,root);
        RECT rect;GetWindowRect(mainWindow,&rect);POINT point{rect.left+px(42),rect.top+px(290)};
        for(char c:std::string("bank"))engine.type(c);popup.show(point);UpdateWindow(popup.handle());ok=snapshot(popup.handle(),folder+L"\\bank.png")&&ok;
        engine.reset();for(char c:std::string("running"))engine.type(c);popup.show(point);UpdateWindow(popup.handle());ok=snapshot(popup.handle(),folder+L"\\running.png")&&ok;
        engine.reset();for(char c:std::string("aple"))engine.type(c);engine.key(Key::Space);popup.show(point);UpdateWindow(popup.handle());ok=snapshot(popup.handle(),folder+L"\\correction.png")&&ok;
    }
    DestroyWindow(mainWindow);Gdiplus::GdiplusShutdown(token);return ok?0:1;
}
static void pump(){MSG m;for(int i=0;i<15;++i){while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(2);}}
static int uiSelfTest(const std::wstring& report){
    // This is a component test that directly forwards keys to the preview sink.
    // Desktop foreground activation is not guaranteed, and unrelated activation
    // messages must not detach its context halfway through those synthetic keys.
    // Exercise the same focus handler explicitly at the intended boundaries.
    componentSelfTest=true;
    ShowWindow(mainWindow,SW_SHOWNOACTIVATE);UpdateWindow(mainWindow);pump();
    auto hr=startTrial();focusTrial(true);pump();
    auto value=[](){int n=GetWindowTextLengthW(edit);std::wstring s(n+1,L'\0');GetWindowTextW(edit,s.data(),n+1);s.resize(n);return s;};
    std::ostringstream trace;trace<<"[";unsigned keyIndex=0;
    auto key=[&](UINT vk){BYTE state[256]{};BOOL keyboardSet=SetKeyboardState(state);auto scan=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC);LPARAM param=(LPARAM)scan<<16;BOOL eaten=FALSE,testEaten=FALSE;
        int ctrl=GetKeyState(VK_CONTROL),alt=GetKeyState(VK_MENU),leftWin=GetKeyState(VK_LWIN),rightWin=GetKeyState(VK_RWIN);
        if(trialStore)trialStore->sync();
        if(trialSink&&trialContext){trialSink->OnTestKeyDown(trialContext,vk,param,&eaten);testEaten=eaten;if(eaten)trialSink->OnKeyDown(trialContext,vk,param,&eaten);}pump();
        if(keyIndex++)trace<<",";
        trace<<"{\"key\":"<<vk<<",\"tested\":"<<(testEaten?"true":"false")<<",\"eaten\":"<<(eaten?"true":"false")<<",\"keyboard_state_set\":"<<(keyboardSet?"true":"false")<<",\"ctrl\":"<<ctrl<<",\"alt\":"<<alt<<",\"left_win\":"<<leftWin<<",\"right_win\":"<<rightWin<<",\"text\":"<<json(value())<<"}";
        return eaten;};
    auto type=[&](const char* s){for(;*s;++s)key((UINT)toupper(*s));};
    type("ba");
    // Regression: native desktop activation noise cannot interrupt a directly
    // driven component test; actual preview focus changes are tested below.
    SendMessageW(mainWindow,WM_ACTIVATE,WA_INACTIVE,0);
    SendMessageW(mainWindow,WM_ACTIVATE,WA_ACTIVE,0);
    type("nk");key('2');auto bank=value();SetWindowTextW(edit,L"");pump();
    type("apple");key(VK_SPACE);auto apple=value();SetWindowTextW(edit,L"");pump();
    type("went");key(VK_SPACE);auto went=value();SetWindowTextW(edit,L"");pump();
    type("hello");key(VK_RETURN);auto english=value();SetWindowTextW(edit,L"");pump();
    type("aple");key(VK_SPACE);auto typo=value();
    auto suggestions=dictionary->correct("aple");auto found=std::find(suggestions.begin(),suggestions.end(),"apple");
    if(found!=suggestions.end())key('1'+(UINT)(found-suggestions.begin()));auto correction=value();key(VK_SPACE);auto correctedChinese=value();
    SetWindowTextW(edit,L"");pump();type("apple");key(VK_ESCAPE);auto cancelled=value();
    type("app");focusTrial(false);pump();auto focusEnglish=value();
    // Simulate the thread manager changing document focus during an external
    // focus transition. Entering the preview must restore its document manager.
    HRESULT lostFocusHr=trialThread?trialThread->SetFocus(nullptr):E_UNEXPECTED;
    HRESULT restoreFocusHr=focusTrial(true);ITfDocumentMgr* restoredDocument=nullptr;
    HRESULT queryFocusHr=trialThread?trialThread->GetFocus(&restoredDocument):E_UNEXPECTED;
    IUnknown* restoredIdentity=nullptr;IUnknown* expectedIdentity=nullptr;
    if(restoredDocument)restoredDocument->QueryInterface(IID_IUnknown,(void**)&restoredIdentity);
    if(trialDocument)trialDocument->QueryInterface(IID_IUnknown,(void**)&expectedIdentity);
    bool restored=restoredIdentity&&restoredIdentity==expectedIdentity;
    if(restoredIdentity)restoredIdentity->Release();
    if(expectedIdentity)expectedIdentity->Release();
    if(restoredDocument)restoredDocument->Release();
    SetWindowTextW(edit,L"");pump();type("bank");key('2');auto resumed=value();trace<<"]";
    // Windows may report another native document for an inactive test window.
    // Assert the explicit request and actual preview text restoration; retain
    // the observed desktop document identity separately in the report.
    bool ok=hr==S_OK&&bank==L"河岸"&&apple==L"苹果"&&went==L"去"&&english==L"hello"&&typo==L"aple"&&correction==L"apple"&&correctedChinese==L"苹果"&&cancelled.empty()&&focusEnglish==L"app"&&SUCCEEDED(lostFocusHr)&&SUCCEEDED(restoreFocusHr)&&resumed==L"河岸";
    std::ofstream f{std::filesystem::path(report)};f<<"{\"passed\":"<<(ok?"true":"false")<<",\"scope\":\"component-preview\",\"activation_hr\":"<<(unsigned long)hr<<",\"bank\":"<<json(bank)<<",\"apple\":"<<json(apple)<<",\"went\":"<<json(went)<<",\"english\":"<<json(english)<<",\"typo_before_confirmation\":"<<json(typo)<<",\"corrected_english\":"<<json(correction)<<",\"corrected_chinese\":"<<json(correctedChinese)<<",\"cancelled\":"<<json(cancelled)<<",\"focus_english\":"<<json(focusEnglish)<<",\"resumed\":"<<json(resumed);
    f<<",\"restored_document_focus\":"<<(restored?"true":"false")<<",\"lose_focus_hr\":"<<(unsigned long)lostFocusHr<<",\"restore_focus_hr\":"<<(unsigned long)restoreFocusHr<<",\"query_focus_hr\":"<<(unsigned long)queryFocusHr<<",\"native_activation_notifications\":"<<testActivationNotifications<<",\"native_edit_focus_notifications\":"<<testEditFocusNotifications<<",\"key_trace\":"<<trace.str()<<"}";f.close();
    endTrial();DestroyWindow(mainWindow);return ok?0:1;
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int show){
    instance=inst;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    root=moduleRoot(inst);int argc;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>=3&&wcscmp(argv[1],L"--speak")==0){bool ok=speak(argv[2],readSettings().british);LocalFree(argv);CoUninitialize();return ok?0:1;}
    if(argc>=3&&wcscmp(argv[1],L"--speech-test")==0){bool ok=speak(L"apple",false,true,argv[2]);LocalFree(argv);CoUninitialize();return ok?0:1;}
    dictionary=loadDictionary(root);
    if(argc>=3&&wcscmp(argv[1],L"--diagnose")==0){diagnostics(argv[2]);LocalFree(argv);CoUninitialize();return 0;}
    if(argc>=2&&wcscmp(argv[1],L"--activate")==0){auto hr=activate();LocalFree(argv);CoUninitialize();return hr==S_OK?0:1;}
    std::wstring renderFolder,uiReport;if(argc>=3&&wcscmp(argv[1],L"--render")==0)renderFolder=argv[2];if(argc>=3&&wcscmp(argv[1],L"--ui-selftest")==0)uiReport=argv[2];
    LocalFree(argv);
    if(!dictionary->size()){MessageBoxW(nullptr,L"未能加载离线词库。请将整个 EType 文件夹解压后运行，或重新安装。",L"EType",MB_OK|MB_ICONERROR);CoUninitialize();return 1;}
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_BAR_CLASSES};InitCommonControlsEx(&cc);
    scale=(int)GetDpiForSystem()*100/96;
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=proc;wc.hInstance=inst;wc.lpszClassName=L"EType.Settings";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(1));wc.hbrBackground=CreateSolidBrush(RGB(246,249,246));RegisterClassExW(&wc);
    RECT area{0,0,px(776),px(687)};AdjustWindowRectEx(&area,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0);
    mainWindow=CreateWindowExW(0,wc.lpszClassName,ETypeName,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,area.right-area.left,area.bottom-area.top,nullptr,nullptr,inst,nullptr);
    if(!renderFolder.empty()){int code=render(renderFolder);CoUninitialize();return code;}
    if(!uiReport.empty()){int code=uiSelfTest(uiReport);CoUninitialize();return code;}
    ShowWindow(mainWindow,show);UpdateWindow(mainWindow);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){
        BOOL eaten=FALSE;
        if(trialSink&&trialContext&&msg.hwnd==edit&&(msg.message==WM_KEYDOWN||msg.message==WM_KEYUP)){
            if(trialStore)trialStore->sync();
            if(msg.message==WM_KEYDOWN){trialSink->OnTestKeyDown(trialContext,msg.wParam,msg.lParam,&eaten);if(eaten)trialSink->OnKeyDown(trialContext,msg.wParam,msg.lParam,&eaten);}
            else{trialSink->OnTestKeyUp(trialContext,msg.wParam,msg.lParam,&eaten);if(eaten)trialSink->OnKeyUp(trialContext,msg.wParam,msg.lParam,&eaten);}
        }
        if(!eaten&&!IsDialogMessageW(mainWindow,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    endTrial();CoUninitialize();return 0;
}
