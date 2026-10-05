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
#include "local_ai.h"

using namespace etype;
static HINSTANCE instance;
static std::wstring root;
static std::shared_ptr<const Dictionary> dictionary;
static HWND mainWindow,edit,fontCombo,accentCombo,volumeBar,punctuationCombo,statusLabel;
static HWND modeCombo,voiceCombo,speedCombo;
static HFONT bodyFont,titleFont;
static HMODULE trialModule=nullptr;
static ITfThreadMgr* trialThread=nullptr;
static ITfKeyEventSink* trialSink=nullptr;
static ITfDocumentMgr* trialDocument=nullptr;
static ITfContext* trialContext=nullptr;
static EditorStore* trialStore=nullptr;
static bool componentSelfTest=false;
static bool sentenceSelfTest=false;
static bool onlineSelfTest=false;
static unsigned testActivationNotifications=0,testEditFocusNotifications=0;
static int scale=100;
static int settingsScroll=0;
static std::string lastEnglish;
static DWORD lastSpeechPid=0;
static SpeechState lastSpeechState=SpeechState::Idle;
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
static bool speak(const std::wstring& word,bool british,bool silent=false,const std::wstring& output=L"",int volume=-1,HANDLE cancel=nullptr){
    ISpVoice* voice=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_ALL,IID_ISpVoice,(void**)&voice);
    if(FAILED(hr))return false;
    auto settings=readSettings();voice->SetVolume((USHORT)(volume>=0?std::clamp(volume,0,100):settings.volume));
    voice->SetRate(settings.slowSpeech?-2:0);
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
    if(SUCCEEDED(hr))hr=voice->Speak(word.c_str(),SPF_IS_NOT_XML|(cancel?SPF_ASYNC:0),nullptr);
    if(SUCCEEDED(hr)&&cancel){while(voice->WaitUntilDone(30)==S_FALSE){if(WaitForSingleObject(cancel,0)==WAIT_OBJECT_0){voice->Speak(L"",SPF_PURGEBEFORESPEAK,nullptr);hr=E_ABORT;break;}}}
    if(stream){stream->Close();stream->Release();}voice->Release();return SUCCEEDED(hr);
}
static HRESULT activate(){
    if(LOWORD((ULONG_PTR)GetKeyboardLayout(0))!=ETypeLanguage){
        auto layout=LoadKeyboardLayoutW(L"00000804",KLF_NOTELLSHELL);
        if(layout)ActivateKeyboardLayout(layout,0);
    }
    ITfInputProcessorProfileMgr* mgr=nullptr;HRESULT hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfileMgr,(void**)&mgr);
    if(SUCCEEDED(hr)){auto flags=trialThread?ETypeProfileForProcess:(ETypeEnableProfile|ETypeProfileForProcess|ETypeProfileForSession);hr=mgr->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,ETypeLanguage,ETypeClsid,ETypeProfile,nullptr,flags);mgr->Release();}return hr;
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
    TfClientId id=0;
    if(SUCCEEDED(hr)&&
#ifdef ETYPE_ONLINE
       true
#else
       componentSelfTest
#endif
       ){
        // Only the explicitly loaded preview service belongs to this test.
        // Do not activate the desktop's installed TIP in the same text store.
        ITfThreadMgrEx* isolated=nullptr;
        hr=trialThread->QueryInterface(IID_ITfThreadMgrEx,(void**)&isolated);
        if(SUCCEEDED(hr)){hr=isolated->ActivateEx(&id,TF_TMAE_NOACTIVATETIP|TF_TMAE_NOACTIVATEKEYBOARDLAYOUT);isolated->Release();}
    }else if(SUCCEEDED(hr))hr=trialThread->Activate(&id);
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
    auto available=voices();bool gb=false;
    for(auto& pair:available)if(pair.second.find(L"809")!=std::wstring::npos)gb=true;
    std::wstring description=L"美式：本地 Kokoro 自然语音 · 英式 Windows 音源："+std::wstring(gb?L"可用":L"未安装");
#ifdef ETYPE_ONLINE
    description=L"免费在线自然语音 · 发音需要联网";
#endif
    SetWindowTextW(statusLabel,description.c_str());
}
static LRESULT CALLBACK isolatedEdit(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    // Test keys go straight to the preview sink. Native keyboard, clipboard and
    // IMM input must not insert the user's concurrent desktop composition.
    switch(message){
    case WM_CHAR:case WM_UNICHAR:case WM_KEYDOWN:case WM_KEYUP:
    case WM_SYSCHAR:case WM_SYSKEYDOWN:case WM_SYSKEYUP:case WM_PASTE:
    case WM_IME_STARTCOMPOSITION:case WM_IME_COMPOSITION:case WM_IME_ENDCOMPOSITION:
    case WM_IME_CHAR:return 0;
    }
    return DefSubclassProc(h,message,w,l);
}
static void createControls(){
    bodyFont=CreateFontW(-px(15),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    titleFont=CreateFontW(-px(29),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    auto logo=control(L"STATIC",L"",SS_ICON|SS_REALSIZEIMAGE,0,34,18,64,64);
    auto icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,px(64),px(64),LR_SHARED);
    SendMessageW(logo,STM_SETICON,(WPARAM)icon,0);
    auto title=control(L"STATIC",L"EType · 英文学习输入法",SS_LEFT,0,110,27,640,44);SendMessageW(title,WM_SETFONT,(WPARAM)titleFont,TRUE);
    label(L"练习英文单词与句子，选择中文表达。",35,79,750,28);
    std::wstring count=L"离线词库  "+std::to_wstring(dictionary->size())+L" 个词条   ·   完整拼写   ·   固定候选顺序";
#ifdef ETYPE_ONLINE
    count=L"在线词库 · 完整拼写 · 多义候选 · 不内置大模型";
#endif
    label(count.c_str(),35,119,750,28);
#ifdef ETYPE_ONLINE
    label(L"在线轻量开发版",35,172,178,30);
    SetWindowTextW(title,L"EType · 在线轻量版");
#else
    control(L"BUTTON",L"安装系统输入法",BS_PUSHBUTTON,101,35,166,178,38);
#endif
    control(L"BUTTON",L"直接试用（无需安装）",BS_PUSHBUTTON,102,228,166,210,38);
    control(L"BUTTON",L"使用说明",BS_PUSHBUTTON,103,453,166,125,38);
#ifndef ETYPE_ONLINE
    control(L"BUTTON",L"卸载输入法",BS_PUSHBUTTON,104,593,166,143,38);
#endif
    label(L"先点击“直接试用”，再在下方输入英文单词；其他软件使用需安装。",35,227,750,27);
    edit=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,201,35,263,701,139);
    if(componentSelfTest)SetWindowSubclass(edit,isolatedEdit,1,0);
    label(L"Ctrl+Shift+空格：单词/句子；句子 Enter 翻译/选中文，Ctrl+Enter 英文。",35,414,750,27);
    label(L"候选字号",35,465,110,28);
    fontCombo=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,301,145,461,108,210);
    for(int i=12;i<=28;++i)comboItem(fontCombo,std::to_wstring(i).c_str());
    label(L"发音口音",287,465,90,28);
    accentCombo=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,302,384,461,151,120);
    comboItem(accentCombo,L"美式英语（默认）");comboItem(accentCombo,L"英式英语");
    control(L"BUTTON",L"播放英文 / 重播",BS_PUSHBUTTON,303,560,459,176,34);
    label(L"发音音量",35,513,110,28);
    volumeBar=control(TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_HORZ,304,140,507,390,37);SendMessageW(volumeBar,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
    label(L"默认标点",35,559,110,28);
    punctuationCombo=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,305,145,555,174,130);
    comboItem(punctuationCombo,L"中文标点 ，。？！");comboItem(punctuationCombo,L"英文标点 ,.!?");
    control(L"BUTTON",L"保存设置",BS_PUSHBUTTON,306,560,553,176,35);
    label(L"输入模式",35,613,110,28);
    modeCombo=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,312,145,609,174,130);
    comboItem(modeCombo,L"单词模式");comboItem(modeCombo,L"句子模式");
    label(L"美式音色",355,613,100,28);
    voiceCombo=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,313,465,609,271,130);
#ifdef ETYPE_ONLINE
    comboItem(voiceCombo,L"Aria · 在线女声");comboItem(voiceCombo,L"Guy · 在线男声");
#else
    comboItem(voiceCombo,L"Heart · 自然女声");comboItem(voiceCombo,L"Michael · 自然男声");
#endif
    label(L"播放速度",35,665,110,28);
    speedCombo=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,314,145,661,174,130);
    comboItem(speedCombo,L"正常 1.0×");comboItem(speedCombo,L"慢速 0.8×");
    control(L"BUTTON",L"停止播放",BS_PUSHBUTTON,315,560,659,176,34);
    statusLabel=control(L"STATIC",L"",SS_LEFT,307,35,715,700,30);
#ifdef ETYPE_ONLINE
    label(L"翻译和朗读需要联网；免费额度不足时保留英文，不启用付费服务。",35,756,730,26);
#else
    label(L"美式使用本地 Kokoro；英式沿用 Windows 音源。句子最长 500 字符。",35,756,730,26);
#endif
    auto settings=readSettings();SendMessageW(fontCombo,CB_SETCURSEL,settings.fontSize-12,0);
    SendMessageW(accentCombo,CB_SETCURSEL,settings.british?1:0,0);SendMessageW(punctuationCombo,CB_SETCURSEL,settings.chinesePunctuation?0:1,0);SendMessageW(volumeBar,TBM_SETPOS,TRUE,settings.volume);
    SendMessageW(modeCombo,CB_SETCURSEL,settings.sentenceMode?1:0,0);SendMessageW(voiceCombo,CB_SETCURSEL,settings.maleVoice?1:0,0);SendMessageW(speedCombo,CB_SETCURSEL,settings.slowSpeech?1:0,0);
    SetTimer(mainWindow,2,300,nullptr);
    refreshVoices();
}
static Settings currentSettings(){Settings s;s.fontSize=(int)SendMessageW(fontCombo,CB_GETCURSEL,0,0)+12;s.british=SendMessageW(accentCombo,CB_GETCURSEL,0,0)==1;s.chinesePunctuation=SendMessageW(punctuationCombo,CB_GETCURSEL,0,0)==0;s.volume=(int)SendMessageW(volumeBar,TBM_GETPOS,0,0);s.sentenceMode=SendMessageW(modeCombo,CB_GETCURSEL,0,0)==1;s.maleVoice=SendMessageW(voiceCombo,CB_GETCURSEL,0,0)==1;s.slowSpeech=SendMessageW(speedCombo,CB_GETCURSEL,0,0)==1;return s;}
#ifndef ETYPE_ONLINE
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
#endif
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE:mainWindow=h;createControls();return 0;
    case WM_CTLCOLORSTATIC:{auto dc=(HDC)w;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(43,63,60));static HBRUSH brush=CreateSolidBrush(RGB(246,249,246));return(LRESULT)brush;}
    case WM_COMMAND:switch(LOWORD(w)){
        case 201:
            if(trialStore&&HIWORD(w)==EN_CHANGE)trialStore->sync();
            if(HIWORD(w)==EN_CHANGE){int n=GetWindowTextLengthW(edit);std::wstring value(n+1,L'\0');GetWindowTextW(edit,value.data(),n+1);value.resize(n);auto text=utf8(value);
                for(auto& c:text)if(c=='\r'||c=='\n'||c=='\t')c=' ';
                if(!text.empty()&&text.size()<=500&&std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32&&c<=126;}))lastEnglish=text;}
            if(componentSelfTest&&(HIWORD(w)==EN_KILLFOCUS||HIWORD(w)==EN_SETFOCUS))++testEditFocusNotifications;
            if(!componentSelfTest&&HIWORD(w)==EN_KILLFOCUS)focusTrial(false);
            if(!componentSelfTest&&HIWORD(w)==EN_SETFOCUS)focusTrial(true);
            break;
        case 101:
#ifndef ETYPE_ONLINE
            setup(false);
#endif
            break;
        case 102:{auto hr=startTrial();if(hr!=S_OK){std::wstring text=L"未能启用本窗口试用。你仍可尝试安装系统输入法。\n错误码：";wchar_t hex[24];swprintf(hex,24,L"0x%08lX",(unsigned long)hr);text+=hex;MessageBoxW(h,text.c_str(),L"EType",MB_OK|MB_ICONINFORMATION);}else{SetFocus(edit);SetWindowTextW(statusLabel,L"本窗口已启用 EType。输入 bank 测试；在其他软件使用需要安装系统输入法。");}break;}
        case 103:{auto path=root+L"\\使用说明.txt";ShellExecuteW(h,L"open",path.c_str(),nullptr,root.c_str(),SW_SHOWNORMAL);break;}
        case 104:
#ifndef ETYPE_ONLINE
            setup(true);
#endif
            break;
        case 303:{auto s=currentSettings();s.sentenceMode=readSettings().sentenceMode;writeSettings(s);int n=GetWindowTextLengthW(edit);std::wstring value(n+1,L'\0');GetWindowTextW(edit,value.data(),n+1);value.resize(n);
            LONG a=0,b=0;SendMessageW(edit,EM_GETSEL,(WPARAM)&a,(LPARAM)&b);if(b>a)value=value.substr((size_t)a,(size_t)(b-a));
            auto text=utf8(value);for(auto& c:text)if(c=='\r'||c=='\n'||c=='\t')c=' ';
            if(text.size()>500){SetWindowTextW(statusLabel,L"本次语音最多 500 字符，请选中较短的英文后播放。");break;}
            if(text.empty()||std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32||c>126;}))text=lastEnglish.empty()?"apple":lastEnglish;
            launchSpeech(root,text);SetWindowTextW(statusLabel,L"正在准备英文发音；首次使用可能需要加载模型。停止按钮可取消播放。");break;}
        case 315:stopSpeech();SetWindowTextW(statusLabel,speechStatusText(speechSnapshot().state).c_str());break;
        case 312:if(HIWORD(w)==CBN_SELCHANGE)writeSettings(currentSettings());break;
        case 306:if(writeSettings(currentSettings())){SetWindowTextW(statusLabel,L"设置已保存，下次显示候选窗口时生效。");}else MessageBoxW(h,L"设置保存失败，请检查本地用户目录的写入权限。",L"EType",MB_OK|MB_ICONERROR);break;
    }return 0;
    case WM_TIMER:if(w==2){SendMessageW(modeCombo,CB_SETCURSEL,readSettings().sentenceMode?1:0,0);auto speech=speechSnapshot();
        if(speech.pid!=lastSpeechPid||speech.state!=lastSpeechState){lastSpeechPid=speech.pid;lastSpeechState=speech.state;auto status=speechStatusText(speech.state);if(!status.empty())SetWindowTextW(statusLabel,status.c_str());}}return 0;
    case WM_SIZE:{SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;info.nMin=0;info.nMax=px(802)-1;info.nPage=HIWORD(l);info.nPos=settingsScroll;SetScrollInfo(h,SB_VERT,&info,TRUE);return 0;}
    case WM_MOUSEWHEEL:SendMessageW(h,WM_VSCROLL,GET_WHEEL_DELTA_WPARAM(w)>0?SB_LINEUP:SB_LINEDOWN,0);return 0;
    case WM_VSCROLL:{SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(h,SB_VERT,&info);int next=settingsScroll;
        switch(LOWORD(w)){case SB_LINEUP:next-=px(48);break;case SB_LINEDOWN:next+=px(48);break;case SB_PAGEUP:next-=(int)info.nPage;break;case SB_PAGEDOWN:next+=(int)info.nPage;break;case SB_THUMBTRACK:next=info.nTrackPos;break;default:break;}
        next=std::clamp(next,0,std::max(0,info.nMax-(int)info.nPage+1));
        if(next!=settingsScroll){ScrollWindowEx(h,0,settingsScroll-next,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE|SW_ERASE);settingsScroll=next;info.fMask=SIF_POS;info.nPos=next;SetScrollInfo(h,SB_VERT,&info,TRUE);}return 0;}
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
        engine.reset();engine.sentenceMode=true;for(char c:std::string("I sat on the bank."))engine.type(c);auto revision=engine.beginTranslation();engine.completeTranslation(revision,engine.buffer,{{L"我坐在河岸上。",L""},{L"我坐在河边。",L""}});popup.show(point);UpdateWindow(popup.handle());ok=snapshot(popup.handle(),folder+L"\\sentence.png")&&ok;
        engine.reset();for(char c:std::string("Please check the report before sending it to the team."))engine.type(c);revision=engine.beginTranslation();
        std::wstring longText;for(int i=0;i<25;++i)longText+=L"这是用于检查候选全文滚动的测试内容：请核对数量、日期和否定范围，并保留所有细节。";longText+=L"【全文结束】";
        engine.completeTranslation(revision,engine.buffer,{{longText,L""},{L"请检查报告，然后发给团队。",L""}});popup.show(point);UpdateWindow(popup.handle());ok=snapshot(popup.handle(),folder+L"\\long-collapsed.png")&&ok;
        auto original=engine.buffer;popup.toggleDetails(point);UpdateWindow(popup.handle());SCROLLINFO scroll{};scroll.cbSize=sizeof(scroll);scroll.fMask=SIF_ALL;GetScrollInfo(popup.handle(),SB_VERT,&scroll);
        bool full=engine.buffer==original&&engine.count()==2&&scroll.nMax>(int)scroll.nPage;
        ok=snapshot(popup.handle(),folder+L"\\long-expanded.png")&&ok;
        SendMessageW(popup.handle(),WM_VSCROLL,SB_BOTTOM,0);UpdateWindow(popup.handle());GetScrollInfo(popup.handle(),SB_VERT,&scroll);bool bottom=scroll.nPos==scroll.nMax-(int)scroll.nPage+1;
        ok=snapshot(popup.handle(),folder+L"\\long-bottom.png")&&ok;
        int retryCalls=0;popup.translate=[&](){++retryCalls;};RECT client{};GetClientRect(popup.handle(),&client);int dpi=(int)GetDpiForWindow(popup.handle());
        SendMessageW(popup.handle(),WM_LBUTTONUP,0,MAKELPARAM(client.right-MulDiv(180,dpi,96),MulDiv(16,dpi,96)));
        popup.toggleDetails(point);GetScrollInfo(popup.handle(),SB_VERT,&scroll);bool collapsed=scroll.nMax==0&&retryCalls==1&&engine.buffer==original&&engine.count()==2;
        std::ofstream checks{std::filesystem::path(folder+L"\\interaction.json")};checks<<"{\"expanded_without_commit\":"<<(full?"true":"false")<<",\"scrolled_to_end\":"<<(bottom?"true":"false")<<",\"collapsed_and_retry_callback\":"<<(collapsed?"true":"false")<<"}";ok=ok&&full&&bottom&&collapsed;
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
    // Component keys are driven below. A disabled test window must never accept
    // the user's live keyboard/IME composition while its message queue pumps.
    EnableWindow(mainWindow,FALSE);
    ShowWindow(mainWindow,SW_SHOWNOACTIVATE);UpdateWindow(mainWindow);pump();
    auto hr=startTrial();focusTrial(true);pump();
    auto value=[](){int n=GetWindowTextLengthW(edit);std::wstring s(n+1,L'\0');GetWindowTextW(edit,s.data(),n+1);s.resize(n);return s;};
    SendMessageW(edit,WM_CHAR,L'外',0);
    SendMessageW(edit,WM_IME_CHAR,L'部',0);
    bool externalInputBlocked=value().empty();
    std::ostringstream trace;trace<<"[";unsigned keyIndex=0;
    auto key=[&](UINT vk,bool control=false,bool shifted=false){
        // TSF host sync may pump desktop keyboard messages. Set this test key's
        // modifiers after syncing, and again at each direct sink invocation.
        if(trialStore)trialStore->sync();
        BYTE previous[256]{};GetKeyboardState(previous);BYTE state[256]{};if(control)state[VK_CONTROL]=0x80;if(shifted)state[VK_SHIFT]=0x80;BOOL keyboardSet=SetKeyboardState(state);auto scan=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC);LPARAM param=(LPARAM)scan<<16;BOOL eaten=FALSE,testEaten=FALSE;
        int ctrl=GetKeyState(VK_CONTROL),alt=GetKeyState(VK_MENU),leftWin=GetKeyState(VK_LWIN),rightWin=GetKeyState(VK_RWIN);
        if(trialSink&&trialContext){SetKeyboardState(state);trialSink->OnTestKeyDown(trialContext,vk,param,&eaten);testEaten=eaten;if(eaten){SetKeyboardState(state);trialSink->OnKeyDown(trialContext,vk,param,&eaten);}}SetKeyboardState(previous);pump();
        if(keyIndex++)trace<<",";
        trace<<"{\"key\":"<<vk<<",\"tested\":"<<(testEaten?"true":"false")<<",\"eaten\":"<<(eaten?"true":"false")<<",\"keyboard_state_set\":"<<(keyboardSet?"true":"false")<<",\"ctrl\":"<<ctrl<<",\"alt\":"<<alt<<",\"left_win\":"<<leftWin<<",\"right_win\":"<<rightWin<<",\"text\":"<<json(value())<<"}";
        return eaten;};
    auto type=[&](const char* s){for(;*s;++s)key((UINT)toupper(*s));};
    if(onlineSelfTest){
        auto wait=[&](unsigned ms){auto until=GetTickCount64()+ms;while(GetTickCount64()<until)pump();};
        IOnlinePreviewState* state=nullptr;if(trialSink)trialSink->QueryInterface(ETypeOnlineStateId,(void**)&state);
        auto ready=[&](bool missing=false){auto until=GetTickCount64()+25000;DWORD flags=0;UINT count=0;
            do{pump();if(state&&SUCCEEDED(state->GetState(&flags,&count))&&!(flags&3)&&(count||(missing&&(flags&4))))return true;}while(GetTickCount64()<until);return false;};
        auto sentence=[&](const char* s){for(;*s;++s){SHORT mapped=VkKeyScanA(*s);key(LOBYTE(mapped),false,(HIBYTE(mapped)&1)!=0);}};
        type("bank");ready();key('2');auto bank=value();SetWindowTextW(edit,L"");pump();
        type("aple");ready(true);key(VK_SPACE);ready();INT index=-1;
        if(state)state->FindCorrection("apple",&index);if(index>=0)key('1'+index);auto correction=value();ready();key(VK_SPACE);auto apple=value();
        SetWindowTextW(edit,L"");pump();key(VK_SPACE,true,true);sentence("I sat on the bank.");auto original=value();key(VK_RETURN);ready();key(VK_RETURN);auto chinese=value();
        SetWindowTextW(edit,L"");pump();sentence("I have 2 apples.");key(VK_RETURN,true);auto english=value();
        SetWindowTextW(edit,L"");pump();sentence("I sat on the bank.");key(VK_RETURN);key('S');wait(3000);auto stale=value();key(VK_RETURN,true);
        bool ok=externalInputBlocked&&SUCCEEDED(hr)&&bank==L"河岸"&&correction==L"apple"&&apple==L"苹果"&&original==L"I sat on the bank."&&chinese.find(L"岸")!=std::wstring::npos&&chinese.find(L"bank")==std::wstring::npos&&english==L"I have 2 apples."&&stale==L"I sat on the bank.s";
        trace<<"]";std::ofstream f{std::filesystem::path(report)};f<<"{\"passed\":"<<(ok?"true":"false")<<",\"scope\":\"online-native-component\",\"bank\":"<<json(bank)<<",\"correction\":"<<json(correction)<<",\"apple\":"<<json(apple)<<",\"sentence\":"<<json(chinese)<<",\"english\":"<<json(english)<<",\"stale\":"<<json(stale)<<",\"key_trace\":"<<trace.str()<<"}";f.close();
        if(state)state->Release();endTrial();DestroyWindow(mainWindow);return ok?0:1;
    }
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
    SetWindowTextW(edit,L"");pump();type("bank");key('2');auto resumed=value();
    std::wstring sentenceComposition,sentenceChinese,sentenceEnglish,staleEdited,staleFocus;
    bool sentenceOk=true;
    if(sentenceSelfTest){
        auto typeSentence=[&](const char* text){for(;*text;++text){SHORT mapped=VkKeyScanA(*text);key(LOBYTE(mapped),false,(HIBYTE(mapped)&1)!=0);}};
        auto wait=[](){auto until=GetTickCount64()+15000;while(GetTickCount64()<until)pump();};
        SetWindowTextW(edit,L"");pump();key(VK_SPACE,true,true);typeSentence("I sat on the bank.");sentenceComposition=value();key(VK_RETURN);wait();key(VK_RETURN);sentenceChinese=value();
        SetWindowTextW(edit,L"");pump();typeSentence("I have 2 apples.");key(VK_RETURN,true);sentenceEnglish=value();
        SetWindowTextW(edit,L"");pump();typeSentence("i have red books.");key(VK_HOME);key(VK_RIGHT,true);key(VK_RIGHT,true);key(VK_RIGHT,true,true);typeSentence("blue ");auto replaced=value();
        key(VK_END);key(VK_LEFT,false,true);key(VK_LEFT,false,true);DWORD selStart=0,selEnd=0;SendMessageW(edit,EM_GETSEL,(WPARAM)&selStart,(LPARAM)&selEnd);bool reversed=selEnd-selStart==2;
        SendMessageW(edit,EM_SETSEL,7,11);pump();typeSentence("green");auto mouseEdited=value();key(VK_RETURN,true);
        sentenceOk=replaced==L"i have blue books."&&reversed&&mouseEdited==L"i have green books.";
        SetWindowTextW(edit,L"");pump();typeSentence("I sat on the bank.");key(VK_RETURN);key('S');wait();staleEdited=value();key(VK_RETURN,true);
        SetWindowTextW(edit,L"");pump();typeSentence("I sat on the bank.");key(VK_RETURN);focusTrial(false);pump();focusTrial(true);SetWindowTextW(edit,L"");pump();typeSentence("new text");wait();staleFocus=value();key(VK_RETURN,true);
        sentenceOk=sentenceOk&&sentenceComposition==L"I sat on the bank."&&sentenceChinese.find(L"河岸")!=std::wstring::npos&&sentenceChinese.find(L"bank")==std::wstring::npos&&sentenceEnglish==L"I have 2 apples."&&staleEdited==L"I sat on the bank.s"&&staleFocus==L"new text";
        std::ofstream detail{std::filesystem::path(report+L".sentences.json")};detail<<"{\"passed\":"<<(sentenceOk?"true":"false")<<",\"composition\":"<<json(sentenceComposition)<<",\"chinese\":"<<json(sentenceChinese)<<",\"english\":"<<json(sentenceEnglish)<<",\"middle_replaced\":"<<json(replaced)<<",\"shift_selection_start\":"<<selStart<<",\"shift_selection_end\":"<<selEnd<<",\"mouse_replaced\":"<<json(mouseEdited)<<",\"stale_edited\":"<<json(staleEdited)<<",\"stale_focus\":"<<json(staleFocus)<<"}";
    }
    trace<<"]";
    // Windows may report another native document for an inactive test window.
    // Assert the explicit request and actual preview text restoration; retain
    // the observed desktop document identity separately in the report.
    bool ok=externalInputBlocked&&sentenceOk&&hr==S_OK&&bank==L"河岸"&&apple==L"苹果"&&went==L"去"&&english==L"hello"&&typo==L"aple"&&correction==L"apple"&&correctedChinese==L"苹果"&&cancelled.empty()&&focusEnglish==L"app"&&SUCCEEDED(lostFocusHr)&&SUCCEEDED(restoreFocusHr)&&resumed==L"河岸";
    std::ofstream f{std::filesystem::path(report)};f<<"{\"passed\":"<<(ok?"true":"false")<<",\"scope\":\"component-preview\",\"activation_hr\":"<<(unsigned long)hr<<",\"bank\":"<<json(bank)<<",\"apple\":"<<json(apple)<<",\"went\":"<<json(went)<<",\"english\":"<<json(english)<<",\"typo_before_confirmation\":"<<json(typo)<<",\"corrected_english\":"<<json(correction)<<",\"corrected_chinese\":"<<json(correctedChinese)<<",\"cancelled\":"<<json(cancelled)<<",\"focus_english\":"<<json(focusEnglish)<<",\"resumed\":"<<json(resumed);
    f<<",\"restored_document_focus\":"<<(restored?"true":"false")<<",\"lose_focus_hr\":"<<(unsigned long)lostFocusHr<<",\"restore_focus_hr\":"<<(unsigned long)restoreFocusHr<<",\"query_focus_hr\":"<<(unsigned long)queryFocusHr<<",\"native_activation_notifications\":"<<testActivationNotifications<<",\"native_edit_focus_notifications\":"<<testEditFocusNotifications<<",\"key_trace\":"<<trace.str()<<"}";f.close();
    endTrial();DestroyWindow(mainWindow);return ok?0:1;
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int show){
    instance=inst;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    root=moduleRoot(inst);int argc;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>=3&&wcscmp(argv[1],L"--speak")==0){
        auto settings=readSettings();bool ok=false;
        HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\" ETYPE_SCOPE L".Speech.Mutex.v2");
        HANDLE cancel=prepareSpeechWorker();
        if(mutex&&cancel){HANDLE waits[]{cancel,mutex};DWORD lock=WaitForMultipleObjects(2,waits,FALSE,65000);
            if(lock==WAIT_OBJECT_0+1||lock==WAIT_ABANDONED_0+1){
                if(settings.british
#ifdef ETYPE_ONLINE
                   && false
#endif
                   ){publishSpeechState(SpeechState::Playing);ok=speak(argv[2],true,true,L"",settings.volume,cancel);}
else try{auto wav=speechLocal(utf8(argv[2]),settings.maleVoice,settings.slowSpeech,cancel,settings.british);if(WaitForSingleObject(cancel,0)!=WAIT_OBJECT_0){publishSpeechState(SpeechState::Playing);ok=playLocalAudio(wav,settings.volume,cancel);}}catch(...){}
                ReleaseMutex(mutex);
            }
        }
        publishSpeechState(ok?SpeechState::Finished:(cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0?SpeechState::Stopped:SpeechState::Failed));
        if(cancel)CloseHandle(cancel);if(mutex)CloseHandle(mutex);LocalFree(argv);CoUninitialize();return ok?0:1;
    }
    if(argc>=3&&wcscmp(argv[1],L"--natural-speech-test")==0){bool ok=false;try{auto wav=speechLocal("I sat on the bank.",false,false);std::ofstream f{std::filesystem::path(argv[2]),std::ios::binary};f.write((const char*)wav.data(),(std::streamsize)wav.size());ok=(bool)f;}catch(...){}LocalFree(argv);CoUninitialize();return ok?0:1;}
    if(argc>=3&&wcscmp(argv[1],L"--speech-test")==0){bool ok=speak(L"apple",false,true,argv[2]);LocalFree(argv);CoUninitialize();return ok?0:1;}
    dictionary=loadDictionary(root);
    if(argc>=3&&wcscmp(argv[1],L"--diagnose")==0){diagnostics(argv[2]);LocalFree(argv);CoUninitialize();return 0;}
    if(argc>=2&&wcscmp(argv[1],L"--activate")==0){auto hr=activate();LocalFree(argv);CoUninitialize();return hr==S_OK?0:1;}
    std::wstring renderFolder,uiReport;if(argc>=3&&wcscmp(argv[1],L"--render")==0)renderFolder=argv[2];if(argc>=3&&(wcscmp(argv[1],L"--ui-selftest")==0||wcscmp(argv[1],L"--sentence-selftest")==0||wcscmp(argv[1],L"--online-selftest")==0)){uiReport=argv[2];sentenceSelfTest=wcscmp(argv[1],L"--sentence-selftest")==0;onlineSelfTest=wcscmp(argv[1],L"--online-selftest")==0;
        componentSelfTest=true;
        SetEnvironmentVariableW(L"ETYPE_HEADLESS_TEST",L"1");auto isolated=uiReport+L".ini";DeleteFileW(isolated.c_str());SetEnvironmentVariableW(L"ETYPE_TEST_SETTINGS",isolated.c_str());}
    LocalFree(argv);
#ifndef ETYPE_ONLINE
    if(!dictionary->size()){MessageBoxW(nullptr,L"未能加载离线词库。请将整个 EType 文件夹解压后运行，或重新安装。",L"EType",MB_OK|MB_ICONERROR);CoUninitialize();return 1;}
#endif
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_BAR_CLASSES};InitCommonControlsEx(&cc);
    scale=(int)GetDpiForSystem()*100/96;
    RECT workArea{};SystemParametersInfoW(SPI_GETWORKAREA,0,&workArea,0);scale=std::min(scale,std::max(75,(int)(workArea.right-workArea.left-48)*100/776));
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=proc;wc.hInstance=inst;wc.lpszClassName=L"EType.Settings";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(1));wc.hbrBackground=CreateSolidBrush(RGB(246,249,246));RegisterClassExW(&wc);
    DWORD windowStyle=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VSCROLL;
    RECT area{0,0,px(776),std::min(px(802),(int)(workArea.bottom-workArea.top-80))};AdjustWindowRectEx(&area,windowStyle,FALSE,0);
    mainWindow=CreateWindowExW(0,wc.lpszClassName,ETypeName,windowStyle,CW_USEDEFAULT,CW_USEDEFAULT,area.right-area.left,area.bottom-area.top,nullptr,nullptr,inst,nullptr);
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
