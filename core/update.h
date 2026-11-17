#pragma once

#include <string>

struct UpdateState {
    enum Kind { None, Available, Installing, Installed, Failed } kind = None;
    std::string version;  // the newer release
    std::string error;
    int generation = 0;   // goes up on every change
};

// clears what an earlier update left behind and, unless bml.ini says not to, asks GitHub for the
// latest release in the background
void UpdateStart(const std::wstring& gameDir, const std::wstring& mods);
UpdateState UpdateGet();
// downloads the newer release's dlls in the background, they load at the next start
void UpdateInstall();
