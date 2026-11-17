// Road Fill: with a dirt or stone road picked, hold Shift and drag to pave the whole
// rectangle instead of a line. Green shows where road goes, red what the tool refuses.
// Right-click or Escape cancels.
#include <windows.h>

#include <algorithm>
#include <vector>

#include "bml.h"

namespace {

const BmlApi* g_api;

constexpr uint32_t kGreen = 0xFF00FF00;
constexpr uint32_t kRed = 0xFFFF0000;

struct Drag {
    bool active = false;
    int start = -1;
    int last = -1;
    int planned = -2;  // the end tile the marks were made for
    std::vector<BmlTileMark> marks;
} g_drag;

bool KeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void Plan(BmlRoadTool* tool) {
    int size = g_api->map_size();
    int ax = g_drag.start % size, ay = g_drag.start / size;
    int bx = g_drag.last % size, by = g_drag.last / size;
    g_drag.marks.clear();
    for (int y = std::min(ay, by); y <= std::max(ay, by); ++y) {
        for (int x = std::min(ax, bx); x <= std::max(ax, bx); ++x) {
            g_drag.marks.push_back({x, y, g_api->road_tile_ok(tool, x, y) ? kGreen : kRed});
        }
    }
}

void Fill(BmlRoadTool* tool) {
    int placed = 0, planned = 0;
    for (const BmlTileMark& m : g_drag.marks) {
        if (m.color != kGreen) continue;
        ++planned;
        placed += g_api->place_road_tile(tool, m.x, m.y) ? 1 : 0;
    }
    g_api->log("roadfill", "%d of %d tiles paved, %d refused", placed, planned,
               static_cast<int>(g_drag.marks.size()) - planned);
    if (placed > 0) g_api->play_ok_sound();
    else g_api->play_fail_sound();
}

bool OnRoadUpdate(BmlRoadTool* tool, bool active, void*) {
    if (!active) {
        g_drag = {};
        return false;
    }
    if (!g_drag.active) {
        if (!KeyDown(VK_SHIFT) || !g_api->action_pressed(BML_ACTION_SELECT)) return false;
        int tile = g_api->mouse_tile();
        if (tile < 0) return false;
        g_drag.active = true;
        g_drag.start = g_drag.last = tile;
        return true;
    }
    if (KeyDown(VK_ESCAPE) || KeyDown(VK_RBUTTON)) {
        g_drag = {};
        return true;
    }
    int tile = g_api->mouse_tile();
    if (tile >= 0) g_drag.last = tile;
    if (g_drag.planned != g_drag.last) {
        Plan(tool);
        g_drag.planned = g_drag.last;
    }
    if (g_api->action_released(BML_ACTION_SELECT) || !g_api->action_held(BML_ACTION_SELECT)) {
        Fill(tool);
        g_drag = {};
        return true;
    }
    g_api->mark_road_tiles(tool, g_drag.marks.data(), static_cast<int>(g_drag.marks.size()));
    return true;
}

}  // namespace

extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api) {
    if (api->version < 5) return false;
    g_api = api;
    return api->on_road_update(OnRoadUpdate, nullptr);
}

extern "C" __declspec(dllexport) const BmlPluginInfo* BmlPluginAbout() {
    static const BmlPluginInfo info = {
        "Road Fill",
        "Pick a dirt or stone road, hold Shift and drag to pave the whole area instead of one line. "
        "Tiles the road tool would refuse are skipped.",
    };
    return &info;
}
