// Grid Build: with a house (or any building) picked, hold Shift and drag to fill
// the area with rows of buildings packed side by side, two rows facing each road.
//   Shift        dirt roads
//   Shift+Ctrl   stone roads
//   Shift+Alt    buildings only
// The rotate keys pick which way the rows run. Ghosts show what fits while dragging,
// right-click or Escape cancels.
#include <windows.h>

#include <algorithm>
#include <vector>

#include "bml.h"

namespace {

const BmlApi* g_api;

struct Drag {
    bool active = false;
    int start = -1;
    int last = -1;
    int planned = -1;  // tile and rotation the ghosts were worked out for
    int plannedRotation = -1;
    std::vector<BmlSpot> ghosts;
} g_drag;

bool KeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

struct Rect {
    int x0, y0, x1, y1;
};

// way a building's door row faces at each rotation, from how the placement check
// reads the footprint bitmap: unrotated, its last row lands on the lowest y
struct Step {
    int dx, dy;
};
const Step kFront[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

int RotationFacing(int dx, int dy) {
    for (int r = 0; r < 4; ++r) {
        if (kFront[r].dx == dx && kFront[r].dy == dy) return r;
    }
    return 0;
}

struct Plan {
    struct Building {
        int x, y, rotation;
    };
    std::vector<Building> buildings;
    std::vector<int> roads;
};

// Works in (b, a) coordinates, a runs the way the doors face and b along a row.
// Two rows face each other across one road and share it as their door row, which
// the game allows: door rows may lie on roads and on other door rows. Pairs sit
// back to back, so a pair of house rows is 2 * depth - 1 tiles deep.
Plan Layout(const Rect& area, int rotation, int width, int height, int size) {
    Plan plan;
    bool rowsAlongX = (rotation & 1) == 0;
    int a0 = rowsAlongX ? area.y0 : area.x0;
    int a1 = rowsAlongX ? area.y1 : area.x1;
    int b0 = rowsAlongX ? area.x0 : area.y0;
    int b1 = rowsAlongX ? area.x1 : area.y1;
    int depth = height;  // the door row is the last row of the footprint
    int length = width;
    int plus = RotationFacing(rowsAlongX ? 0 : 1, rowsAlongX ? 1 : 0);
    int minus = (plus + 2) & 3;

    auto tile = [&](int b, int a) { return rowsAlongX ? b + a * size : a + b * size; };
    auto row = [&](int a, int r) {
        // (b, a) is the lowest corner in both axes, which is what place_building takes
        for (int b = b0; b + length - 1 <= b1; b += length) {
            if (rowsAlongX) plan.buildings.push_back({b, a, r});
            else plan.buildings.push_back({a, b, r});
        }
    };

    for (int a = a0; a + depth - 1 <= a1;) {
        int road = a + depth - 1;
        row(a, plus);
        for (int b = b0; b <= b1; ++b) plan.roads.push_back(tile(b, road));
        if (road + depth - 1 <= a1) row(road, minus);
        a = road + depth;
    }
    return plan;
}

Rect Area(int from, int to, int size) {
    return {std::min(from % size, to % size), std::min(from / size, to / size),
            std::max(from % size, to % size), std::max(from / size, to / size)};
}

Plan PlanFor(BmlPlacementTool* tool, int from, int to) {
    int width, height;
    g_api->placement_footprint(tool, &width, &height);
    int size = g_api->map_size();
    return Layout(Area(from, to, size), g_api->placement_rotation(tool), width, height, size);
}

void ShowGhosts(BmlPlacementTool* tool) {
    int rotation = g_api->placement_rotation(tool);
    if (g_drag.last != g_drag.planned || rotation != g_drag.plannedRotation) {
        g_drag.ghosts.clear();
        for (const Plan::Building& p : PlanFor(tool, g_drag.start, g_drag.last).buildings) {
            if (g_api->can_place_building(tool, p.x, p.y, p.rotation)) g_drag.ghosts.push_back({p.x, p.y, p.rotation});
        }
        g_drag.planned = g_drag.last;
        g_drag.plannedRotation = rotation;
    }
    g_api->preview_buildings(tool, g_drag.ghosts.data(), static_cast<int>(g_drag.ghosts.size()));
}

void Fill(BmlPlacementTool* tool, int from, int to, BmlTemplate* road) {
    Rect area = Area(from, to, g_api->map_size());
    int rotation = g_api->placement_rotation(tool);
    Plan plan = PlanFor(tool, from, to);

    int roads = 0;
    if (road) {
        for (int t : plan.roads) roads += g_api->place_road(road, t, t) ? 1 : 0;
    }
    int placed = 0;
    for (const Plan::Building& p : plan.buildings) {
        placed += g_api->place_building(tool, p.x, p.y, p.rotation) ? 1 : 0;
    }
    g_api->log("gridbuild", "area %d,%d to %d,%d rotation %d: %d of %zu buildings, %d road tiles",
               area.x0, area.y0, area.x1, area.y1, rotation, placed, plan.buildings.size(), roads);
    if (placed > 0) g_api->play_ok_sound();
    else g_api->play_fail_sound();
}

BmlTemplate* PickRoad() {
    if (KeyDown(VK_MENU)) return nullptr;
    const char* path = KeyDown(VK_CONTROL) ? "Template/StoneRoad.rsc" : "Template/DirtRoad.rsc";
    BmlTemplate* road = g_api->find_template(path);
    if (!road) g_api->log_warn("gridbuild", "%s is not loaded, placing buildings only", path);
    return road;
}

bool OnPlacementUpdate(BmlPlacementTool* tool, bool active, void*) {
    if (!active) {
        g_drag = {};
        return false;
    }
    if (!g_drag.active) {
        if (!KeyDown(VK_SHIFT) || !g_api->action_pressed(BML_ACTION_SELECT)) return false;
        // the replant plugin owns Shift-drag for the forester
        if (g_api->placement_template(tool) == g_api->find_template("Template/ForesterLodge.rsc")) return false;
        int tile = g_api->mouse_tile();
        if (tile < 0) return false;
        g_drag = {};
        g_drag.active = true;
        g_drag.start = g_drag.last = tile;
        return true;
    }
    if (KeyDown(VK_ESCAPE) || KeyDown(VK_RBUTTON)) {
        g_api->preview_buildings(tool, nullptr, 0);
        g_drag = {};
        return true;
    }
    int tile = g_api->mouse_tile();
    if (tile >= 0) g_drag.last = tile;
    if (g_api->action_released(BML_ACTION_SELECT) || !g_api->action_held(BML_ACTION_SELECT)) {
        g_api->preview_buildings(tool, nullptr, 0);
        Fill(tool, g_drag.start, g_drag.last, PickRoad());
        g_drag = {};
        return true;
    }
    ShowGhosts(tool);
    // the tool keeps drawing its ghost under the mouse, it only places on a fresh press
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
        "Grid Build",
        "Pick a house or any building, hold Shift and drag to fill the area with rows of them, two rows "
        "facing each other across a shared road. Add Ctrl for stone roads or Alt for none. Blocked spots "
        "are skipped and every building is an ordinary build order.",
    };
    return &info;
}
