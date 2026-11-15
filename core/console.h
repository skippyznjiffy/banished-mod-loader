#pragma once

#include <windows.h>

#include <string>

#include "game.h"

// the loader's own window, every log line in colour. Off when bml.ini says console = off
void ConsoleOpen(const std::wstring& gameDir);
void ConsoleSection(const char* title);
void ConsoleLine(const SYSTEMTIME& time, const char* plugin, LogLevel level, const char* text);
