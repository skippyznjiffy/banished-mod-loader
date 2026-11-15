// Speed: replaces the game's speed steps (1, 2, 5, 10) with the ones in
// NativeMods\speed.ini and logs how fast the game really runs at each, again
// whenever that changes by more than a tenth.
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "bml.h"

namespace {

const BmlApi* g_api;

// mov eax, imm32 for each step after the first, as the toolbar builds its list
const uint32_t kStepSites[] = {0xBF92A, 0xBF9EA, 0xBFAAA};
const int kDefaultSteps[] = {10, 50, 250};
constexpr double kReportSeconds = 10.0;
// a steady speed is only reported again once it drifts this far
constexpr double kReportDrift = 0.1;

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

std::wstring IniPath() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&IniPath), &self);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring dir(path);
    return dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"speed.ini";
}

bool ReadSteps(int steps[3]) {
    std::wstring ini = IniPath();
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t text[64];
        swprintf_s(text, L"%d, %d, %d", kDefaultSteps[0], kDefaultSteps[1], kDefaultSteps[2]);
        WritePrivateProfileStringW(L"speed", L"steps", text, ini.c_str());
    }
    wchar_t text[128] = {};
    GetPrivateProfileStringW(L"speed", L"steps", L"", text, 128, ini.c_str());
    if (swscanf_s(text, L"%d , %d , %d", &steps[0], &steps[1], &steps[2]) != 3) return false;
    // the slower and faster buttons walk the list in order
    return steps[0] > 1 && steps[0] < steps[1] && steps[1] < steps[2] && steps[2] <= 10000;
}

}  // namespace

extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api) {
    if (api->version < 4) return false;
    g_api = api;
    int steps[3];
    if (!ReadSteps(steps)) {
        api->log_warn("speed", "speed.ini needs steps = three increasing numbers above 1, e.g. 10, 50, 250");
        return false;
    }
    const uint8_t movEax = 0xB8;
    for (uint32_t site : kStepSites) {
        if (api->exe_base()[site] != movEax) {
            api->log_warn("speed", "the speed list is not built where this build expects, nothing changed");
            return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        uint8_t code[5] = {movEax};
        memcpy(code + 1, &steps[i], 4);
        if (!api->patch_code(kStepSites[i], &movEax, 1, code, sizeof(code))) return false;
    }
    api->log("speed", "speed steps are 1, %d, %d, %d", steps[0], steps[1], steps[2]);
    return api->on_frame(OnFrame, nullptr);
}

extern "C" __declspec(dllexport) const BmlPluginInfo* BmlPluginAbout() {
    static const BmlPluginInfo info = {
        "Speed",
        "The speed buttons run the game at 1, 10, 50 and 250 times instead of 1, 2, 5 and 10. The three "
        "fast steps are set in NativeMods\\speed.ini.",
    };
    return &info;
}
