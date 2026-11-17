#pragma once

#include <string>

// NativeMods\bml.ini, read every time so the BML screen always shows what is saved
void SettingsOpen(const std::wstring& folder);
bool PluginEnabled(const std::string& file);
void SetPluginEnabled(const std::string& file, bool on);
bool UpdateChecksEnabled();
