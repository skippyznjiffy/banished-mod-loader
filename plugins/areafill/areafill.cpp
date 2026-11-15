// Area Fill: with a field, orchard, pasture, stockpile or cemetery picked, hold Shift
// and drag over rough ground. The usable part of the selection is split into the
// largest areas that fit and each is placed as its own field.
// Green shows the areas, red what is left out. Right-click or Escape cancels.
#include <windows.h>

#include <algorithm>
#include <vector>

#include "bml.h"

namespace {

const BmlApi* g_api;

constexpr uint32_t kGreen = 0xFF00FF00;
constexpr uint32_t kDarkGreen = 0xFF00A000;  // every other area, so the splits show
constexpr uint32_t kRed = 0xFFFF0000;

struct Rect {
    int x0, y0, x1, y1;
};

struct Drag {
    bool active = false;
    int start = -1;
    int last = -1;
    int planned = -2;  // the end tile the plan was made for
    std::vector<Rect> plan;
    std::vector<BmlTileMark> marks;
} g_drag;

bool KeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// largest rectangle of free cells at least min in both sides, by the histogram walk
// over every row, which visits every maximal rectangle
bool Largest(const std::vector<char>& free, int w, int h, int minW, int minH, Rect* best) {
    std::vector<int> height(w, 0);
    std::vector<int> stack;
    long long bestArea = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) height[x] = free[x + y * w] ? height[x] + 1 : 0;
        stack.clear();
        for (int x = 0; x <= w; ++x) {
            int cur = x < w ? height[x] : 0;
            while (!stack.empty() && height[stack.back()] >= cur) {
                int top = stack.back();
                stack.pop_back();
                int rh = height[top];
                int left = stack.empty() ? 0 : stack.back() + 1;
                int rw = x - left;
                long long area = static_cast<long long>(rw) * rh;
                if (rw >= minW && rh >= minH && area > bestArea) {
                    bestArea = area;
                    *best = {left, y - rh + 1, x - 1, y};
                }
            }
            stack.push_back(x);
        }
    }
    return bestArea > 0;
}

void Plan(BmlAreaTool* tool) {
    int size = g_api->map_size();
    int ax = g_drag.start % size, ay = g_drag.start / size;
    int bx = g_drag.last % size, by = g_drag.last / size;
    int x0 = std::min(ax, bx), y0 = std::min(ay, by);
    int w = std::abs(ax - bx) + 1, h = std::abs(ay - by) + 1;
    int minW, minH, maxW, maxH;
    g_api->area_limits(tool, &minW, &minH, &maxW, &maxH);

    std::vector<char> free(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) free[x + y * w] = g_api->area_tile_ok(tool, x0 + x, y0 + y);
    }
    std::vector<char> used(free.size(), 0);
    g_drag.plan.clear();
    Rect r;
    while (Largest(free, w, h, minW, minH, &r)) {
        r.x1 = std::min(r.x1, r.x0 + maxW - 1);
        r.y1 = std::min(r.y1, r.y0 + maxH - 1);
        Rect world = {x0 + r.x0, y0 + r.y0, x0 + r.x1, y0 + r.y1};
        bool fits = g_api->area_check(tool, world.x0, world.y0, world.x1, world.y1) == BML_AREA_OK;
        // a pasture can still refuse a spot for its fence, drop a row and try again
        int lastRow = fits ? r.y1 : r.y0;
        for (int y = r.y0; y <= lastRow; ++y) {
            for (int x = r.x0; x <= r.x1; ++x) {
                free[x + y * w] = 0;
                if (fits) used[x + y * w] = static_cast<char>(1 + g_drag.plan.size() % 2);
            }
        }
        if (fits) g_drag.plan.push_back(world);
    }

    g_drag.marks.clear();
    g_drag.marks.reserve(used.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            char u = used[x + y * w];
            g_drag.marks.push_back({x0 + x, y0 + y, u == 1 ? kGreen : u == 2 ? kDarkGreen : kRed});
        }
    }
}

void Fill(BmlAreaTool* tool) {
    int placed = 0, tiles = 0;
    for (const Rect& r : g_drag.plan) {
        if (!g_api->place_area(tool, r.x0, r.y0, r.x1, r.y1)) continue;
        ++placed;
        tiles += (r.x1 - r.x0 + 1) * (r.y1 - r.y0 + 1);
    }
    g_api->log("areafill", "placed %d of %d areas, %d tiles", placed, static_cast<int>(g_drag.plan.size()), tiles);
    if (placed > 0) g_api->play_ok_sound();
    else g_api->play_fail_sound();
}

bool OnAreaUpdate(BmlAreaTool* tool, bool active, void*) {
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
    g_api->mark_tiles(tool, g_drag.marks.data(), static_cast<int>(g_drag.marks.size()));
    return true;
}

}  // namespace

extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api) {
    if (api->version < 4) return false;
    g_api = api;
    return api->on_area_update(OnAreaUpdate, nullptr);
}

extern "C" __declspec(dllexport) const BmlPluginInfo* BmlPluginAbout() {
    static const BmlPluginInfo info = {
        "Area Fill",
        "Pick a field, orchard, pasture, stockpile or cemetery, hold Shift and drag over ground that is "
        "only partly usable. The usable part is split into the largest areas that fit, each placed as its "
        "own field.",
    };
    return &info;
}
