#include "update.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <mutex>
#include <vector>

#include "game.h"
#include "settings.h"
#include "version.h"

namespace {

constexpr wchar_t kLatest[] = L"https://api.github.com/repos/skippyznjiffy/banished-mod-loader/releases/latest";
// only files from this repo's own releases are ever downloaded
constexpr char kDownloads[] = "https://github.com/skippyznjiffy/banished-mod-loader/releases/download/";

struct Asset {
    std::string name;
    std::string url;
};

std::mutex g_lock;
UpdateState g_state;
std::vector<Asset> g_assets;
std::wstring g_gameDir;
std::wstring g_mods;

// the urls and names here are checked to be plain ascii
std::wstring Ascii(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

void Set(UpdateState::Kind kind, const std::string& error = "") {
    std::lock_guard<std::mutex> hold(g_lock);
    g_state.kind = kind;
    g_state.error = error;
    ++g_state.generation;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

bool Get(const std::wstring& url, std::string& body, std::string& error) {
    wchar_t host[256];
    wchar_t path[2048];
    URL_COMPONENTS parts = {};
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        error = "bad address";
        return false;
    }
    std::wstring agent = L"BML/" + Ascii(kBmlVersion);
    Handle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) {
        error = "no network";
        return false;
    }
    WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 30000);
    Handle connection{WinHttpConnect(session.h, host, parts.nPort, 0)};
    Handle request{connection.h ? WinHttpOpenRequest(connection.h, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr};
    if (!request.h || !WinHttpSendRequest(request.h, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1L),
                                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        error = "no answer from GitHub, error " + std::to_string(GetLastError());
        return false;
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        error = "GitHub answered " + std::to_string(status);
        return false;
    }
    body.clear();
    char buffer[16384];
    DWORD read = 0;
    while (WinHttpReadData(request.h, buffer, sizeof(buffer), &read) && read > 0) body.append(buffer, read);
    return true;
}

// the string value after "key": from a spot in the json, empty when there is none or it has escapes
std::string JsonString(const std::string& json, const char* key, size_t* at) {
    std::string quoted = std::string("\"") + key + "\"";
    size_t k = json.find(quoted, *at);
    if (k == std::string::npos) return "";
    size_t open = json.find('"', json.find(':', k + quoted.size()));
    size_t close = open == std::string::npos ? open : json.find('"', open + 1);
    if (close == std::string::npos) return "";
    *at = close + 1;
    std::string value = json.substr(open + 1, close - open - 1);
    return value.find('\\') == std::string::npos ? value : "";
}

bool Newer(const std::string& tag) {
    int a[3] = {}, b[3] = {};
    if (sscanf_s(tag.c_str() + (tag[0] == 'v'), "%d.%d.%d", &a[0], &a[1], &a[2]) != 3) return false;
    sscanf_s(kBmlVersion, "%d.%d.%d", &b[0], &b[1], &b[2]);
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

bool PlainDllName(const std::string& name) {
    if (name.size() < 5 || name.compare(name.size() - 4, 4, ".dll") != 0) return false;
    for (size_t i = 0; i + 4 < name.size(); ++i) {
        char c = name[i];
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-') return false;
    }
    return true;
}

// the loader goes next to the game, plugins into NativeMods
std::wstring Target(const Asset& a) {
    return (a.name == "dinput8.dll" ? g_gameDir : g_mods) + Ascii(a.name);
}

DWORD WINAPI Check(void*) {
    std::string body;
    std::string error;
    if (!Get(kLatest, body, error)) {
        LogAt(LogLevel::Warn, "bml", "could not check for an update, %s", error.c_str());
        return 0;
    }
    size_t at = 0;
    std::string tag = JsonString(body, "tag_name", &at);
    if (!Newer(tag)) {
        LogAt(LogLevel::Info, "bml", "BML %s is the latest", kBmlVersion);
        return 0;
    }
    std::vector<Asset> assets;
    for (std::string url; !(url = JsonString(body, "browser_download_url", &at)).empty();) {
        std::string name = url.substr(url.find_last_of('/') + 1);
        if (url.rfind(kDownloads, 0) == 0 && PlainDllName(name)) assets.push_back({name, url});
    }
    bool hasLoader = false;
    for (const Asset& a : assets) hasLoader |= a.name == "dinput8.dll";
    if (!hasLoader) {
        LogAt(LogLevel::Warn, "bml", "BML %s is out but its release has no dinput8.dll, update it by hand", tag.c_str());
        return 0;
    }
    {
        std::lock_guard<std::mutex> hold(g_lock);
        g_assets = assets;
        g_state.version = tag[0] == 'v' ? tag.substr(1) : tag;
    }
    Set(UpdateState::Available);
    LogAt(LogLevel::Good, "bml", "BML %s is out, the BML button in the menu installs it", tag.c_str());
    return 0;
}

bool WriteFileAll(const std::wstring& path, const std::string& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    CloseHandle(file);
    return ok;
}

DWORD Fail(const std::vector<Asset>& assets, const std::string& error) {
    for (const Asset& a : assets) DeleteFileW((Target(a) + L".new").c_str());
    LogAt(LogLevel::Error, "bml", "the update failed, %s", error.c_str());
    Set(UpdateState::Failed, error);
    return 0;
}

// every file is downloaded before any is swapped, so a failed download changes nothing
DWORD WINAPI Install(void*) {
    std::vector<Asset> assets;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        assets = g_assets;
    }
    for (const Asset& a : assets) {
        std::string body;
        std::string error;
        if (!Get(Ascii(a.url), body, error)) return Fail(assets, "could not download " + a.name + ", " + error);
        if (body.size() < 1024 || body.compare(0, 2, "MZ") != 0) return Fail(assets, a.name + " is not a dll");
        if (!WriteFileAll(Target(a) + L".new", body)) return Fail(assets, "could not write " + a.name);
    }
    // a loaded dll can't be overwritten but it can be renamed, the .old files go at the next start
    struct Swap {
        std::wstring target;
        bool hadOld;
    };
    std::vector<Swap> swapped;
    // a plugin that is new in this release has no old file and is taken out again
    auto undo = [&swapped] {
        for (const Swap& s : swapped) {
            if (s.hadOld) {
                MoveFileExW((s.target + L".old").c_str(), s.target.c_str(), MOVEFILE_REPLACE_EXISTING);
            } else {
                DeleteFileW(s.target.c_str());
            }
        }
    };
    for (const Asset& a : assets) {
        std::wstring target = Target(a);
        std::wstring old = target + L".old";
        bool exists = GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (exists && !MoveFileExW(target.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DWORD code = GetLastError();
            undo();
            return Fail(assets, "could not move " + a.name + " aside, error " + std::to_string(code));
        }
        if (!MoveFileExW((target + L".new").c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DWORD code = GetLastError();
            if (exists) MoveFileExW(old.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
            undo();
            return Fail(assets, "could not put " + a.name + " in, error " + std::to_string(code));
        }
        swapped.push_back({target, exists});
    }
    LogAt(LogLevel::Good, "bml", "updated, BML %s loads the next time the game starts", UpdateGet().version.c_str());
    Set(UpdateState::Installed);
    return 0;
}

void DeleteMatching(const std::wstring& dir, const wchar_t* pattern) {
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW((dir + pattern).c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        DeleteFileW((dir + found.cFileName).c_str());
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

}  // namespace

void UpdateStart(const std::wstring& gameDir, const std::wstring& mods) {
    g_gameDir = gameDir;
    g_mods = mods;
    DeleteFileW((gameDir + L"dinput8.dll.old").c_str());
    DeleteMatching(mods, L"*.dll.old");
    DeleteMatching(mods, L"*.dll.new");
    DeleteFileW((gameDir + L"dinput8.dll.new").c_str());
    if (!UpdateChecksEnabled()) return;
    HANDLE thread = CreateThread(nullptr, 0, Check, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

UpdateState UpdateGet() {
    std::lock_guard<std::mutex> hold(g_lock);
    return g_state;
}

void UpdateInstall() {
    if (UpdateGet().kind != UpdateState::Available) return;
    Set(UpdateState::Installing);
    HANDLE thread = CreateThread(nullptr, 0, Install, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        Set(UpdateState::Failed, "could not start the download");
    }
}
