#include "settings.h"

#include <windows.h>

namespace {

std::wstring g_path;

std::wstring Wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(n > 0 ? n - 1 : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

}  // namespace

void SettingsOpen(const std::wstring& folder) {
    g_path = folder + L"bml.ini";
}

bool PluginEnabled(const std::string& file) {
    return GetPrivateProfileIntW(L"plugins", Wide(file).c_str(), 1, g_path.c_str()) != 0;
}

void SetPluginEnabled(const std::string& file, bool on) {
    WritePrivateProfileStringW(L"plugins", Wide(file).c_str(), on ? L"1" : L"0", g_path.c_str());
}

bool UpdateChecksEnabled() {
    return GetPrivateProfileIntW(L"update", L"check", 1, g_path.c_str()) != 0;
}
