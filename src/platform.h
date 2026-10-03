#pragma once
#include <windows.h>
#include <functional>
#include "core.h"
namespace etype {
std::wstring moduleRoot(HMODULE module);
std::shared_ptr<const Dictionary> loadDictionary(const std::wstring& root);
struct Settings { int fontSize=17, volume=80; bool british=false, chinesePunctuation=true; };
std::wstring settingsFile();
Settings readSettings();
bool writeSettings(const Settings& settings);
void launchPanel(const std::wstring& root);
void launchSpeech(const std::wstring& root, const std::string& word);
class Popup {
public:
    Popup(HMODULE module, Engine& engine, const std::wstring& root);
    ~Popup();
    void show(POINT anchor, bool modeToast=false);
    void hide();
    HWND handle() const { return window_; }
    std::function<void(size_t)> select;
private:
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
    void paint(HDC supplied=nullptr);
    HMODULE module_; Engine& engine_; std::wstring root_;
    HWND window_=nullptr; int scale_=100, rowHeight_=38, headerHeight_=108;
    bool toast_=false; Settings settings_;
};
}
