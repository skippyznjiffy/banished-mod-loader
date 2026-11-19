// Speed: the slower and faster buttons walk the speeds in NativeMods\speed.ini instead of the
// game's 1, 2, 5, 10, and the game's real speed is logged at each, again whenever it changes
// by more than a tenth.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "bml.h"

namespace {

const BmlApi* g_api;

// RVAs in Application-steam-x64.exe 1.0.7
constexpr uint32_t kGame = 0x1CBDD8;
constexpr uint32_t kStep = 0x0BE680;       // the time group's slower and faster: (group, -1 or +1)
constexpr uint32_t kRebuildSet = 0x0BFB23;  // building the time group sets the speed to one of its own steps
constexpr size_t kGameClock = 0x120;
constexpr size_t kClockSpeed = 0x34;

const uint8_t kStepStart[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x8b, 0x05, 0x4c, 0xd7, 0x10, 0x00, 0x33, 0xdb};
const uint8_t kRebuildStore[] = {0x89, 0x4a, 0x34};  // mov [rdx+0x34], ecx
const wchar_t kDefaultSteps[] = L"10, 20, 30, 50, 100, 150, 250";
constexpr size_t kMaxSteps = 16;
constexpr int kMaxSpeed = 10000;
constexpr double kReportSeconds = 10.0;
// a steady speed is only reported again once it drifts this far
constexpr double kReportDrift = 0.1;

std::vector<int> g_steps;  // 1 first, then the ini's, increasing

struct Measure {
    int speed = 0;
    LARGE_INTEGER last = {};
    double real = 0;
    double game = 0;
    int updates = 0;
    double reported = 0;  // real speed last logged at this setting
} g_measure;

double Seconds(LARGE_INTEGER from, LARGE_INTEGER to) {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    return static_cast<double>(to.QuadPart - from.QuadPart) / static_cast<double>(frequency.QuadPart);
}

void Report() {
    double ratio = g_measure.real > 0 ? g_measure.game / g_measure.real : 0;
    bool drifted = g_measure.reported <= 0 || std::abs(ratio - g_measure.reported) > g_measure.reported * kReportDrift;
    if (g_measure.updates > 0 && drifted) {
        g_measure.reported = ratio;
        g_api->log("speed", "set to %dx: ran %.1fx real time, %.0f updates a second, %.1f ms each",
                   g_measure.speed, g_measure.game / g_measure.real, g_measure.updates / g_measure.real,
                   1000.0 * g_measure.real / g_measure.updates);
    }
    g_measure.real = 0;
    g_measure.game = 0;
    g_measure.updates = 0;
}

void OnFrame(float step, void*) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    int speed = g_api->game_speed();
    if (speed != g_measure.speed) {
        Report();
        g_measure.speed = speed;
        g_measure.reported = 0;
    } else if (speed > 0 && g_measure.last.QuadPart != 0) {
        g_measure.real += Seconds(g_measure.last, now);
        g_measure.game += static_cast<double>(step) * speed;
        ++g_measure.updates;
        if (g_measure.real >= kReportSeconds) Report();
    }
    g_measure.last = now;
}

// replaces the game's step, which only knows the time group's own four speeds. A speed
// between two steps counts as the lower one
void Step(void*, int delta) {
    uint8_t* game = *reinterpret_cast<uint8_t**>(g_api->exe_base() + kGame);
    uint8_t* clock = game ? *reinterpret_cast<uint8_t**>(game + kGameClock) : nullptr;
    if (!clock) return;
    int& speed = *reinterpret_cast<int*>(clock + kClockSpeed);
    int at = 0;
    while (at + 1 < static_cast<int>(g_steps.size()) && g_steps[at + 1] <= speed) ++at;
    speed = g_steps[std::clamp(at + delta, 0, static_cast<int>(g_steps.size()) - 1)];
}

std::wstring IniPath() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&IniPath), &self);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring dir(path);
    return dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"speed.ini";
}

bool ReadSteps() {
    std::wstring ini = IniPath();
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringW(L"speed", L"steps", kDefaultSteps, ini.c_str());
    }
    wchar_t text[256] = {};
    GetPrivateProfileStringW(L"speed", L"steps", L"", text, 256, ini.c_str());
    g_steps = {1};
    for (wchar_t* p = text; *p;) {
        wchar_t* end;
        long value = wcstol(p, &end, 10);
        if (end == p) return false;
        // the buttons walk the list in order, so it has to keep rising
        if (value <= g_steps.back() || value > kMaxSpeed || g_steps.size() >= kMaxSteps + 1) return false;
        g_steps.push_back(static_cast<int>(value));
        p = end;
        while (*p == L',' || *p == L' ') ++p;
    }
    return g_steps.size() > 1;
}

}  // namespace

extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api) {
    if (api->version < 4) return false;
    g_api = api;
    if (!ReadSteps()) {
        api->log_warn("speed", "speed.ini needs steps = up to 16 rising numbers above 1, e.g. %ls",
                      kDefaultSteps);
        return false;
    }
    // jmp qword ptr [rip+0] and the address
    uint8_t jump[14] = {0xFF, 0x25, 0, 0, 0, 0};
    void* target = reinterpret_cast<void*>(&Step);
    memcpy(jump + 6, &target, sizeof(target));
    const uint8_t nops[sizeof(kRebuildStore)] = {0x90, 0x90, 0x90};
    // the rebuild patch first, on its own it changes nothing a player would notice
    if (!api->patch_code(kRebuildSet, kRebuildStore, sizeof(kRebuildStore), nops, sizeof(nops)) ||
        !api->patch_code(kStep, kStepStart, sizeof(kStepStart), jump, sizeof(jump))) {
        api->log_warn("speed", "the speed buttons are not where this build expects, nothing changed");
        return false;
    }
    std::string list;
    for (int s : g_steps) list += (list.empty() ? "" : ", ") + std::to_string(s);
    api->log("speed", "speed steps are %s", list.c_str());
    return api->on_frame(OnFrame, nullptr);
}

extern "C" __declspec(dllexport) const BmlPluginInfo* BmlPluginAbout() {
    static const BmlPluginInfo info = {
        "Speed",
        "The slower and faster buttons step through 1, 10, 20, 30, 50, 100, 150 and 250 times instead of 1, "
        "2, 5 and 10. The steps are set in NativeMods\\speed.ini.",
    };
    return &info;
}
