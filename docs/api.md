# Writing a plugin

Include `include/bml.h` and export one function:

```c
#include "bml.h"

static const BmlApi* api;

static void OnFrame(float seconds, void* user) {
    // runs after every game update on the main thread
}

__declspec(dllexport) bool BmlPluginInit(const BmlApi* a) {
    if (a->version < 3) return false;
    api = a;
    return api->on_frame(OnFrame, NULL);
}
```

A plugin can also export `BmlPluginAbout`, returning a `BmlPluginInfo` with a title and a
description for the BML screen. Without it the screen shows the file name.

```c
__declspec(dllexport) const BmlPluginInfo* BmlPluginAbout(void) {
    static const BmlPluginInfo info = {"My Plugin", "What it does, in a sentence or two."};
    return &info;
}
```

Init runs once at startup before any game is loaded, so it should only register
callbacks and patch code. Everything else in the API is for the game's main thread, which
is where all callbacks run. `build.bat` builds every folder under `plugins\` into
`out\<folder>.dll`.

| Call | What it does |
|---|---|
| `log`, `log_warn` | a line in `NativeMods\bml.log` and the console, `log_warn` shows as a warning |
| `map_size`, `mouse_tile` | the map is size by size tiles, a tile is `x + y * size` |
| `action_pressed`, `action_released`, `action_held` | the game's key map actions (`BML_ACTION_*`) |
| `on_placement_update` | every frame a building placement tool is active, return true to take the frame |
| `placement_footprint`, `placement_rotation` | the size and quarter turns of the building being placed |
| `place_building` | queue one building of the tool's kind at a tile and rotation |
| `find_template`, `place_road` | look up a loaded template by path, lay road between two tiles |
| `play_ok_sound`, `play_fail_sound` | the placement sounds |
| `on_frame`, `game_speed` | after every game update, and the sim steps it ran |
| `exe_base`, `patch_code` | write over the exe's code, only where the bytes match what you expect |
| `placement_template`, `can_place_building` | which template the tool builds, whether `place_building` would succeed |
| `preview_buildings` | ghosts of the tool's building at a list of spots, call it every frame they should stay |
| `plant_resource` | put a tree or other natural resource on a free tile, growth 0 to 1 |
| `on_area_update`, `area_template`, `area_limits` | the same for area tools (fields, orchards, pastures, stockpiles, clearing) |
| `area_tile_ok`, `area_check`, `place_area` | test one tile or a rectangle the way the tool does, queue a rectangle |
| `mark_tiles` | coloured tiles in the tool's overlay instead of its own selection, call it every frame |
| `on_road_update`, `road_template` | the same for the road tool (dirt and stone roads) |
| `road_tile_ok`, `place_road_tile`, `mark_road_tiles` | test one tile, queue road on one tile, coloured tiles in its overlay |
| `add_gear_button` | a button in the gear menu that calls you back, up to three |
| `capture_input` | keep the mouse or keyboard from the game, for an overlay under the cursor |
| `ui_sprite` | a picture from the game's sprite sheet as a DirectX 11 texture view and its uv |
| `history_series`, `history` | the town's records the Town Hall graphs draw, 48 samples a year |

`version` went to 3 with `placement_template` and the three after it, to 4 with the
area calls, to 5 with the road tool calls and to 6 with the gear button, input, sprite and
history calls. Check it in init and decline when the loader is older than what you use.

A door row faces the lowest y at rotation 0, +x at 1, the highest y at 2 and -x at 3.

## Building from source

`build.bat` needs Visual Studio 2022 with the C++ tools and builds the loader and every
folder under `plugins\` into `out\`. `install.ps1` copies them into the game, finds it
through Steam or takes `-GameDir <path>`, and `install.ps1 -Remove` takes them out again.

## How BML shows it's running

The window title lists the loaded plugins, the build line in the corner of the main menu
says how many are running, and a console window next to the game shows the log in colour.
The console has no close button, because closing a console ends the game with it. Set
`console = off` in `NativeMods\bml.ini` to go without it. `NativeMods\bml.log` is written
either way, and says why when BML stays off.

## Switches and updates

The BML screen saves its checkboxes under `[plugins]` in `NativeMods\bml.ini`, as
`replant = 0`. A plugin that is switched off is still loaded so the screen can show its
`BmlPluginAbout`, but `BmlPluginInit` is never called. So a plugin should do nothing in
`DllMain` or in the constructors of its globals.

The updater reads the latest release from the GitHub API. It only takes `.dll` files
attached to a release of this repo: `dinput8.dll` goes next to the game and the rest into
`NativeMods`. A release attaches each dll on its own next to the zip. A loaded dll can't
be overwritten, so the old files are renamed to `.old` and deleted at the next start.

## How it was found

Every class registers itself with the engine by name, `Object::BaseTypeInfo` with a
factory, and the factories lead to the vtables (`PlacementTool`, `RoadTool`, the game's
update loop). The runtime DLL exports the engine's own API (input, resources, time), so
BML calls that by name. The addresses BML relies on are at the top of `core/game.cpp`.
