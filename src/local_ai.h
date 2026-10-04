#pragma once
#include "core.h"
#include <windows.h>
namespace etype {
// Starts the bundled worker when installed; uses loopback, off the TSF thread.
std::vector<Candidate> translateLocal(const std::string& text);
std::vector<unsigned char> speechLocal(const std::string& text, bool male, bool slow, HANDLE cancel = nullptr);
bool playLocalAudio(const std::vector<unsigned char>& wav, int volume, HANDLE cancel = nullptr);
}
