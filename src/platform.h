#pragma once
#include <windows.h>
#include <functional>
#include "core.h"
#ifdef ETYPE_ONLINE
#define ETYPE_SCOPE L"ETypeOnline"
#else
#define ETYPE_SCOPE L"EType"
#endif
namespace etype {
std::wstring moduleRoot(HMODULE module);
std::shared_ptr<const Dictionary> loadDictionary(const std::wstring& root);
struct Settings { int fontSize=17, volume=80; bool british=false, chinesePunctuation=true, sentenceMode=false, maleVoice=false, slowSpeech=false; };
std::wstring settingsFile();
Settings readSettings();
bool writeSettings(const Settings& settings);
void launchPanel(const std::wstring& root);
void launchSpeech(const std::wstring& root, const std::string& word);
void stopSpeech();
enum class SpeechState : LONG { Idle, Preparing, Playing, Finished, Stopping, Stopped, Failed };
struct SpeechSnapshot { SpeechState state=SpeechState::Idle; DWORD pid=0; };
SpeechSnapshot speechSnapshot();
HANDLE prepareSpeechWorker();
void publishSpeechState(SpeechState state);
std::wstring speechStatusText(SpeechState state);
bool speechBusy(SpeechState state);
class Popup {
public:
    Popup(HMODULE module, Engine& engine, const std::wstring& root);
    ~Popup();
    void show(POINT anchor, bool modeToast=false);
    void hide();
    void toggleDetails(POINT anchor);
    HWND handle() const { return window_; }
    std::function<void(size_t)> select;
    std::function<void()> changeMode;
    std::function<void()> translate;
private:
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
    void paint(HDC supplied=nullptr);
    HMODULE module_; Engine& engine_; std::wstring root_;
    HWND window_=nullptr; int scale_=100, rowHeight_=38, headerHeight_=108;
    bool toast_=false; Settings settings_;
    std::vector<RECT> candidateRects_;
    bool expanded_=false;
    int scrollOffset_=0,contentHeight_=0;
    uint64_t expandedRevision_=0;
    size_t expandedSelected_=0;
    POINT anchor_{};
    void scroll(int next);
};
}
