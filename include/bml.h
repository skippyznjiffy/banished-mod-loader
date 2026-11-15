// BML, the Banished Mod Loader. Native plugins include this header and export
//   extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api);
// The loader calls it once at startup, before any game is loaded, so init should
// only register callbacks and patch code. Every callback runs on the game's main
// thread, and the rest of BmlApi must only be called from there.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BML_API_VERSION 4

// action ids of the game's key map, as its tools read them
#define BML_ACTION_SELECT 5
#define BML_ACTION_ROTATE_LEFT 19
#define BML_ACTION_ROTATE_RIGHT 20

typedef struct BmlPlacementTool BmlPlacementTool;
typedef struct BmlTemplate BmlTemplate;
typedef struct BmlAreaTool BmlAreaTool;

typedef struct BmlSpot {
    int x, y, rotation;
} BmlSpot;

typedef struct BmlTileMark {
    int x, y;
    uint32_t color;  // 0xAARRGGBB
} BmlTileMark;

// area_check results
#define BML_AREA_OK 0
#define BML_AREA_TOO_SMALL 1
#define BML_AREA_TOO_BIG 2
#define BML_AREA_BLOCKED 3

// return true to swallow the frame, the game's own update then does not run
typedef bool (*BmlPlacementUpdateFn)(BmlPlacementTool* tool, bool active, void* user);
// seconds is the engine's fixed update step, the sim ran game_speed() steps of it
// the same for area tools: fields, orchards, stockpiles, clearing, pastures, cemeteries
typedef bool (*BmlAreaUpdateFn)(BmlAreaTool* tool, bool active, void* user);
typedef void (*BmlFrameFn)(float seconds, void* user);

typedef struct BmlApi {
    uint32_t version;

    void (*log)(const char* plugin, const char* fmt, ...);

    // the map is size x size tiles, a tile index is x + y * size
    int (*map_size)(void);
    // tile under the mouse, -1 when the mouse is off the map or over the ui
    int (*mouse_tile)(void);

    bool (*action_pressed)(int action);
    bool (*action_released)(int action);
    bool (*action_held)(int action);

    // runs every frame a building placement tool is the active tool
    bool (*on_placement_update)(BmlPlacementUpdateFn fn, void* user);
    // footprint at rotation 0, before any swap for 90 or 270 degrees
    void (*placement_footprint)(BmlPlacementTool* tool, int* width, int* height);
    // 0 to 3, quarter turns as the player set them with the rotate keys
    int (*placement_rotation)(BmlPlacementTool* tool);
    // queues one building of the tool's kind, (x, y) is the corner with the lowest
    // tile index, false when the spot is blocked or off the map
    bool (*place_building)(BmlPlacementTool* tool, int x, int y, int rotation);

    // a loaded template such as "Template/StoneRoad.rsc", NULL if it is not loaded
    BmlTemplate* (*find_template)(const char* path);
    // queues road from one tile to another the way the road tool's drag does,
    // template must be a road (Template/DirtRoad.rsc, Template/StoneRoad.rsc).
    // Only works once a tool has been active, the road tool borrows its ui state.
    bool (*place_road)(BmlTemplate* road, int from_tile, int to_tile);

    void (*play_ok_sound)(void);
    void (*play_fail_sound)(void);

    // added in version 2

    // runs after every game update while a game is loaded
    bool (*on_frame)(BmlFrameFn fn, void* user);
    // sim steps per update, 0 while paused, in a menu that pauses or with no game loaded
    int (*game_speed)(void);
    // where Application-steam-x64.exe is mapped, rvas are relative to it
    uint8_t* (*exe_base)(void);
    // writes bytes over the exe's code at rva, only when the first expect_length
    // bytes there are expect, so a different build is left alone
    bool (*patch_code)(uint32_t rva, const void* expect, size_t expect_length, const void* bytes, size_t length);

    // added in version 3

    // the template the tool builds, to compare with find_template
    BmlTemplate* (*placement_template)(BmlPlacementTool* tool);
    // whether place_building would succeed there now
    bool (*can_place_building)(BmlPlacementTool* tool, int x, int y, int rotation);
    // shows ghosts of the tool's building at the spots, count 0 clears them.
    // Call it every frame you want them up, they go when the tool closes
    bool (*preview_buildings)(BmlPlacementTool* tool, const BmlSpot* spots, int count);
    // puts a natural resource such as "Template/NaturalResourceTree.rsc" on a free
    // tile the way a forester plants, growth runs from 0 (sapling) to 1 (grown)
    bool (*plant_resource)(BmlTemplate* resource, int tile, float growth);

    // added in version 4

    // runs every frame an area tool is the active tool
    bool (*on_area_update)(BmlAreaUpdateFn fn, void* user);
    BmlTemplate* (*area_template)(BmlAreaTool* tool);
    // the size limits from the template
    void (*area_limits)(BmlAreaTool* tool, int* min_width, int* min_height, int* max_width, int* max_height);
    // whether one tile could be part of an area now
    bool (*area_tile_ok)(BmlAreaTool* tool, int x, int y);
    // BML_AREA_*, what the tool would say about the rectangle between two corners
    int (*area_check)(BmlAreaTool* tool, int x0, int y0, int x1, int y1);
    // queues the area the way the tool's own drag does, false when area_check fails
    bool (*place_area)(BmlAreaTool* tool, int x0, int y0, int x1, int y1);
    // draws these tiles in the tool's overlay instead of its own selection.
    // Call it every frame you want them up
    bool (*mark_tiles)(BmlAreaTool* tool, const BmlTileMark* tiles, int count);
    // a line in the log that the console shows as a warning
    void (*log_warn)(const char* plugin, const char* fmt, ...);
} BmlApi;

typedef bool (*BmlPluginInitFn)(const BmlApi* api);

// optional second export, BmlPluginAbout, for the BML screen in the game's menu
typedef struct BmlPluginInfo {
    const char* title;
    const char* description;
} BmlPluginInfo;
typedef const BmlPluginInfo* (*BmlPluginAboutFn)(void);
