// Replant: with the Forester Lodge picked, hold Shift and drag to plant a forest
// over the area. Trees go on free grass only, mixed kinds and ages.
//   Shift        about 6 trees in 10 tiles
//   Shift+Ctrl   every free tile
// Right-click or Escape cancels.
#include <windows.h>

#include <algorithm>
#include <random>
#include <vector>

#include "bml.h"

namespace {

const BmlApi* g_api;

const char* const kTrees[] = {
    "Template/NaturalResourceTree.rsc",
    "Template/NaturalResourceOakTree.rsc",
    "Template/NaturalResourcePineTree.rsc",
};
constexpr double kSparseChance = 0.6;
// young trees give few logs, so most start close to grown
constexpr float kMinGrowth = 0.4f;

struct Drag {
    bool active = false;
    int start = -1;
    int last = -1;
} g_drag;

std::mt19937 g_random{std::random_device{}()};

bool KeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void Plant(int from, int to, bool dense) {
    std::vector<BmlTemplate*> trees;
    for (const char* path : kTrees) {
        if (BmlTemplate* t = g_api->find_template(path)) trees.push_back(t);
    }
    if (trees.empty()) {
        g_api->log_warn("replant", "no tree template is loaded");
        g_api->play_fail_sound();
        return;
    }
    int size = g_api->map_size();
    int x0 = std::min(from % size, to % size), x1 = std::max(from % size, to % size);
    int y0 = std::min(from / size, to / size), y1 = std::max(from / size, to / size);
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    std::uniform_real_distribution<float> growth(kMinGrowth, 1.0f);
    std::uniform_int_distribution<size_t> kind(0, trees.size() - 1);

    int planted = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!dense && chance(g_random) > kSparseChance) continue;
            planted += g_api->plant_resource(trees[kind(g_random)], x + y * size, growth(g_random)) ? 1 : 0;
        }
    }
    g_api->log("replant", "area %d,%d to %d,%d: planted %d trees", x0, y0, x1, y1, planted);
    if (planted > 0) g_api->play_ok_sound();
    else g_api->play_fail_sound();
}

bool OnPlacementUpdate(BmlPlacementTool* tool, bool active, void*) {
    if (!active) {
        g_drag = {};
        return false;
    }
    if (!g_drag.active) {
        if (!KeyDown(VK_SHIFT) || !g_api->action_pressed(BML_ACTION_SELECT)) return false;
        if (g_api->placement_template(tool) != g_api->find_template("Template/ForesterLodge.rsc")) return false;
        int tile = g_api->mouse_tile();
        if (tile < 0) return false;
        g_drag = {true, tile, tile};
        return true;
    }
    if (KeyDown(VK_ESCAPE) || KeyDown(VK_RBUTTON)) {
        g_drag = {};
        return true;
    }
    int tile = g_api->mouse_tile();
    if (tile >= 0) g_drag.last = tile;
    if (g_api->action_released(BML_ACTION_SELECT) || !g_api->action_held(BML_ACTION_SELECT)) {
        Plant(g_drag.start, g_drag.last, KeyDown(VK_CONTROL));
        g_drag = {};
        return true;
    }
    return false;
}

}  // namespace

extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api) {
    if (api->version < 4) return false;
    g_api = api;
    return api->on_placement_update(OnPlacementUpdate, nullptr);
}

extern "C" __declspec(dllexport) const BmlPluginInfo* BmlPluginAbout() {
    static const BmlPluginInfo info = {
        "Replant",
        "Pick the Forester Lodge, hold Shift and drag over open ground to plant a forest there, about 6 "
        "trees in 10 free tiles, or every free tile with Ctrl as well. Trees are mixed kinds and ages.",
    };
    return &info;
}
