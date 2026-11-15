#include "console.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

HANDLE g_out = INVALID_HANDLE_VALUE;
bool g_colour;

struct Rgb {
    int r, g, b;
};

constexpr Rgb kFrame = {90, 96, 110};
constexpr Rgb kDim = {120, 124, 135};
constexpr Rgb kText = {212, 214, 220};
constexpr Rgb kNumber = {255, 255, 255};
constexpr Rgb kGood = {120, 220, 130};
constexpr Rgb kWarn = {255, 190, 80};
constexpr Rgb kError = {255, 95, 95};
constexpr Rgb kLoader = {95, 175, 255};
// plugins get a colour from their name, so one keeps it between runs
constexpr Rgb kPluginColours[] = {
    {255, 165, 95}, {180, 140, 255}, {95, 215, 180}, {255, 130, 175}, {220, 210, 95}, {150, 215, 95},
};
constexpr int kTagWidth = 12;
constexpr int kBoxWidth = 54;

std::string Fg(Rgb c) {
    if (!g_colour) return {};
    char code[32];
    snprintf(code, sizeof(code), "\x1b[38;2;%d;%d;%dm", c.r, c.g, c.b);
    return code;
}

std::string Bold() {
    return g_colour ? "\x1b[1m" : "";
}

std::string Reset() {
    return g_colour ? "\x1b[0m" : "";
}

void Write(const std::string& utf8) {
    if (g_out == INVALID_HANDLE_VALUE) return;
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), n);
    DWORD written;
    WriteConsoleW(g_out, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
}

Rgb PluginColour(const char* plugin) {
    if (strcmp(plugin, "bml") == 0) return kLoader;
    unsigned hash = 0;
    for (const char* c = plugin; *c; ++c) hash = hash * 31 + static_cast<unsigned char>(*c);
    return kPluginColours[hash % (sizeof(kPluginColours) / sizeof(kPluginColours[0]))];
}

// numbers stand out in plain lines: counts, speeds, tiles
std::string Highlight(const char* text, Rgb base) {
    std::string out = Fg(base);
    for (const char* c = text; *c; ++c) {
        bool start = isdigit(static_cast<unsigned char>(*c)) &&
                     (c == text || !isalnum(static_cast<unsigned char>(c[-1])));
        if (!start) {
            out += *c;
            continue;
        }
        out += Fg(kNumber);
        while (isdigit(static_cast<unsigned char>(*c)) || *c == '.' || *c == ',' ||
               ((*c == 'x' || *c == '%') && !isalnum(static_cast<unsigned char>(c[1])))) {
            out += *c++;
        }
        out += Fg(base);
        --c;
    }
    return out + Reset();
}

std::string Padded(const std::string& text, int width) {
    int shown = 0;
    for (unsigned char c : text) shown += (c & 0xC0) != 0x80;  // utf-8 lead bytes only
    return text + std::string(shown < width ? width - shown : 1, ' ');
}

std::string Repeat(const char* piece, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) out += piece;
    return out;
}

std::string BoxRow(const std::string& coloured, const std::string& plain) {
    int pad = kBoxWidth - 2;
    for (unsigned char c : plain) pad -= (c & 0xC0) != 0x80;
    return "  " + Fg(kFrame) + "│ " + Reset() + coloured + std::string(pad > 0 ? pad : 0, ' ') + Fg(kFrame) + " │" +
           Reset() + "\n";
}

void Banner(const std::wstring& gameDir) {
    std::string top = "  " + Fg(kFrame) + "┌" + Repeat("─", kBoxWidth) + "┐" + Reset() + "\n";
    std::string bottom = "  " + Fg(kFrame) + "└" + Repeat("─", kBoxWidth) + "┘" + Reset() + "\n";
    const Rgb gradient[] = {{95, 175, 255}, {140, 160, 255}, {180, 140, 255}};
    std::string title, titlePlain = "B M L";
    for (size_t i = 0, letter = 0; i < titlePlain.size(); ++i) {
        title += titlePlain[i] == ' ' ? std::string(" ") : Fg(gradient[letter++]) + Bold() + titlePlain[i];
    }
    title += Reset();
    if (!g_colour) title = titlePlain;
    char api[64];
    snprintf(api, sizeof(api), "Banished Mod Loader · plugin api %d", BML_API_VERSION);
    std::string dir;
    int n = WideCharToMultiByte(CP_UTF8, 0, gameDir.c_str(), -1, nullptr, 0, nullptr, nullptr);
    dir.resize(n > 0 ? n - 1 : 0);
    WideCharToMultiByte(CP_UTF8, 0, gameDir.c_str(), -1, dir.data(), n, nullptr, nullptr);

    std::string out = "\n" + top;
    out += BoxRow("", "");
    out += BoxRow(" " + title, " " + titlePlain);
    out += BoxRow(" " + Fg(kText) + api + Reset(), std::string(" ") + api);
    out += BoxRow(" " + Fg(kDim) + "Banished 1.0.7 x64, Steam" + Reset(), " Banished 1.0.7 x64, Steam");
    out += BoxRow("", "");
    out += bottom + "\n";
    out += "    " + Fg(kDim) + Padded("game", 6) + Reset() + Fg(kText) + dir + Reset() + "\n";
    out += "    " + Fg(kDim) + Padded("mods", 6) + Reset() + Fg(kText) + dir + "NativeMods\\" + Reset() + "\n";
    out += "    " + Fg(kDim) + Padded("log", 6) + Reset() + Fg(kText) + dir + "NativeMods\\bml.log" + Reset() + "\n";
    Write(out);
}

BOOL WINAPI IgnoreBreak(DWORD event) {
    // ctrl+c in this window would end the game with it
    return event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT;
}

bool Wanted(const std::wstring& gameDir) {
    std::wstring ini = gameDir + L"NativeMods\\bml.ini";
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringW(L"bml", L"console", L"on", ini.c_str());
    }
    wchar_t value[16] = {};
    GetPrivateProfileStringW(L"bml", L"console", L"on", value, 16, ini.c_str());
    return _wcsicmp(value, L"off") != 0 && _wcsicmp(value, L"0") != 0 && _wcsicmp(value, L"false") != 0;
}

}  // namespace

void ConsoleOpen(const std::wstring& gameDir) {
    if (!Wanted(gameDir) || !AllocConsole()) return;
    SetConsoleTitleW(L"BML console");
    SetConsoleCtrlHandler(IgnoreBreak, TRUE);
    // closing a console window ends its process, so the button goes
    if (HWND window = GetConsoleWindow()) {
        DeleteMenu(GetSystemMenu(window, FALSE), SC_CLOSE, MF_BYCOMMAND);
    }
    // a click in quick edit mode pauses output, and the game thread that logs with it
    HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    DWORD mode;
    if (in != INVALID_HANDLE_VALUE && GetConsoleMode(in, &mode)) {
        SetConsoleMode(in, (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE);
    }
    g_out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, 0, nullptr);
    if (g_out == INVALID_HANDLE_VALUE) return;
    if (GetConsoleMode(g_out, &mode)) {
        g_colour = SetConsoleMode(g_out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
    }
    COORD buffer = {110, 5000};
    SetConsoleScreenBufferSize(g_out, buffer);
    SMALL_RECT window = {0, 0, 109, 39};
    SetConsoleWindowInfo(g_out, TRUE, &window);
    Banner(gameDir);
}

void ConsoleSection(const char* title) {
    std::string line = "\n  " + Fg(kFrame) + "── " + Reset() + Fg(kText) + Bold() + title + Reset() + " " +
                       Fg(kFrame) + Repeat("─", kBoxWidth - 3 - static_cast<int>(strlen(title))) + Reset() + "\n";
    if (!g_colour) line = std::string("\n  -- ") + title + "\n";
    Write(line);
}

void ConsoleLine(const SYSTEMTIME& t, const char* plugin, LogLevel level, const char* text) {
    char clock[16];
    snprintf(clock, sizeof(clock), "%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
    std::string mark, body;
    switch (level) {
        case LogLevel::Good:
            mark = Fg(kGood) + "+" + Reset();
            body = Fg(kGood) + text + Reset();
            break;
        case LogLevel::Warn:
            mark = Fg(kWarn) + "!" + Reset();
            body = Fg(kWarn) + text + Reset();
            break;
        case LogLevel::Error:
            mark = Fg(kError) + "x" + Reset();
            body = Fg(kError) + Bold() + text + Reset();
            break;
        default:
            mark = Fg(kFrame) + "·" + Reset();
            body = Highlight(text, kText);
            break;
    }
    Write("  " + Fg(kDim) + clock + Reset() + "  " + mark + " " + Fg(PluginColour(plugin)) +
          Padded(plugin, kTagWidth) + Reset() + body + "\n");
}
