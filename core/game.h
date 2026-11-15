#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "bml.h"

// binds to the running exe, false when it is not the build the offsets are for
bool GameBind();
void GameHook(const std::vector<std::string>& plugins);
const BmlApi* GameApi();
enum class LogLevel { Info, Good, Warn, Error };

void Log(const char* plugin, const char* fmt, ...);
void LogAt(LogLevel level, const char* plugin, const char* fmt, ...);
void LogOpen(const wchar_t* path);

// swaps one function pointer in a vtable or an import table
bool PatchPointer(void** slot, void* value);

// one export of the game's runtime dll, logged when it is missing
template <typename T>
bool Bind(HMODULE module, const char* name, T& out) {
    out = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!out) LogAt(LogLevel::Error, "bml", "runtime export missing: %s", name);
    return out != nullptr;
}
