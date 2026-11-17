#pragma once

#include <string>
#include <vector>

struct PluginEntry {
    enum class State { Running, Off, Failed };
    std::string file;  // without .dll
    std::string title;
    std::string description;
    State state;
};

// a BML button in the main menu and the pause menu, opening the game's Mods list window with
// a checkbox for each plugin
void MenuHook(const std::vector<PluginEntry>& plugins);
