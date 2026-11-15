#pragma once

#include <string>
#include <vector>

struct PluginEntry {
    std::string file;  // without .dll
    std::string title;
    std::string description;
};

// a BML button in the main menu and the pause menu, opening a list of the active mods and plugins
void MenuHook(const std::vector<PluginEntry>& plugins, const std::string& folder);
