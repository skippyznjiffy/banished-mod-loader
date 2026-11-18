// dinput8.dll stand-in: the game's runtime imports dinput8 and Windows looks next
// to the exe first, so this loads with the game and forwards to the system copy
#include <windows.h>

#include <string>
#include <vector>

#include "console.h"
#include "game.h"
#include "input.h"
#include "menu.h"
#include "settings.h"
#include "update.h"
#include "version.h"

namespace {

HMODULE g_real;

FARPROC Real(const char* name) {
    if (!g_real) {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);
        wcscat_s(path, L"\\dinput8.dll");
        g_real = LoadLibraryW(path);
    }
    return g_real ? GetProcAddress(g_real, name) : nullptr;
}

std::wstring GameDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

std::wstring Widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(n > 0 ? n - 1 : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

std::string Narrow(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

// every plugin in the folder, only the ones switched on in bml.ini are started
std::vector<PluginEntry> LoadPlugins(const std::wstring& dir) {
    std::vector<PluginEntry> plugins;
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW((dir + L"*.dll").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        LogAt(LogLevel::Warn, "bml", "no plugins in NativeMods");
        return plugins;
    }
    do {
        std::string name = Narrow(found.cFileName);
        // the pattern also matches short names, so updater leftovers like x.dll.old can show up
        if (name.size() < 5 || _stricmp(name.c_str() + name.size() - 4, ".dll") != 0) continue;
        HMODULE plugin = LoadLibraryW((dir + found.cFileName).c_str());
        if (!plugin) {
            LogAt(LogLevel::Error, "bml", "%s failed to load, error %lu", name.c_str(), GetLastError());
            continue;
        }
        auto init = reinterpret_cast<BmlPluginInitFn>(GetProcAddress(plugin, "BmlPluginInit"));
        if (!init) {
            LogAt(LogLevel::Error, "bml", "%s has no BmlPluginInit, unloaded", name.c_str());
            FreeLibrary(plugin);
            continue;
        }
        std::string file = name.substr(0, name.size() - 4);
        PluginEntry entry{file, file, "", PluginEntry::State::Running};
        auto about = reinterpret_cast<BmlPluginAboutFn>(GetProcAddress(plugin, "BmlPluginAbout"));
        if (const BmlPluginInfo* info = about ? about() : nullptr) {
            if (info->title) entry.title = info->title;
            if (info->description) entry.description = info->description;
        }
        if (!PluginEnabled(file)) {
            LogAt(LogLevel::Info, "bml", "%s is switched off", name.c_str());
            entry.state = PluginEntry::State::Off;
            FreeLibrary(plugin);
        } else if (init(GameApi())) {
            LogAt(LogLevel::Good, "bml", "%s loaded", name.c_str());
        } else {
            LogAt(LogLevel::Warn, "bml", "%s declined to start", name.c_str());
            entry.state = PluginEntry::State::Failed;
        }
        plugins.push_back(entry);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return plugins;
}

struct WindowSearch {
    DWORD process;
    HWND found;
};

BOOL CALLBACK FindGameWindow(HWND window, LPARAM param) {
    auto* search = reinterpret_cast<WindowSearch*>(param);
    DWORD process;
    GetWindowThreadProcessId(window, &process);
    // our own console belongs to the process as well and shows up first
    if (process != search->process || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) ||
        window == GetConsoleWindow()) {
        return TRUE;
    }
    search->found = window;
    return FALSE;
}

// the title bar says BML is in and what it loaded, the window shows up a while after we start
void MarkWindowTitle(const std::vector<std::string>& plugins) {
    std::wstring note = L"   (BML: ";
    for (size_t i = 0; i < plugins.size(); ++i) {
        if (i > 0) note += L", ";
        note += Widen(plugins[i]);
    }
    note += plugins.empty() ? L"no native mods)" : L")";
    for (int tries = 0; tries < 600; ++tries) {
        WindowSearch search = {GetCurrentProcessId(), nullptr};
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&search));
        wchar_t title[256];
        if (search.found && GetWindowTextW(search.found, title, 256) > 0) {
            SetWindowTextW(search.found, (title + note).c_str());
            return;
        }
        Sleep(100);
    }
    LogAt(LogLevel::Warn, "bml", "the game window never showed up, title left alone");
}

DWORD WINAPI Start(void*) {
    std::wstring mods = GameDir() + L"NativeMods\\";
    CreateDirectoryW(mods.c_str(), nullptr);
    LogOpen((mods + L"bml.log").c_str());
    ConsoleOpen(GameDir());
    ConsoleSection("startup");
    LogAt(LogLevel::Info, "bml", "BML %s", kBmlVersion);
    SettingsOpen(mods);
    if (!GameBind()) return 0;
    ConsoleSection("plugins");
    // plugins register their callbacks before the hook can call any of them
    std::vector<PluginEntry> loaded = LoadPlugins(mods);
    std::vector<std::string> plugins;
    for (const PluginEntry& p : loaded) {
        if (p.state == PluginEntry::State::Running) plugins.push_back(p.file);
    }
    GameHook(plugins);
    MenuHook(loaded);
    LogAt(plugins.empty() ? LogLevel::Warn : LogLevel::Good, "bml", "ready, %d native mods running",
          static_cast<int>(plugins.size()));
    UpdateStart(GameDir(), mods);
    ConsoleSection("game");
    MarkWindowTitle(plugins);
    return 0;
}

}  // namespace

extern "C" {

HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, REFIID iid, void** out, IUnknown* outer) {
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
    auto fn = reinterpret_cast<Fn>(Real("DirectInput8Create"));
    if (!fn) return E_FAIL;
    HRESULT result = fn(inst, version, iid, out, outer);
    if (SUCCEEDED(result) && out) InputWrap(*out);
    return result;
}

HRESULT WINAPI DllCanUnloadNow() {
    using Fn = HRESULT(WINAPI*)();
    auto fn = reinterpret_cast<Fn>(Real("DllCanUnloadNow"));
    return fn ? fn() : S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto fn = reinterpret_cast<Fn>(Real("DllGetClassObject"));
    return fn ? fn(clsid, iid, out) : E_FAIL;
}

HRESULT WINAPI DllRegisterServer() {
    using Fn = HRESULT(WINAPI*)();
    auto fn = reinterpret_cast<Fn>(Real("DllRegisterServer"));
    return fn ? fn() : E_FAIL;
}

HRESULT WINAPI DllUnregisterServer() {
    using Fn = HRESULT(WINAPI*)();
    auto fn = reinterpret_cast<Fn>(Real("DllUnregisterServer"));
    return fn ? fn() : E_FAIL;
}

}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        // LoadLibrary is off limits under the loader lock, the thread waits for it
        HANDLE thread = CreateThread(nullptr, 0, Start, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
