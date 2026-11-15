# BML, the Banished Mod Loader

Native code mods for Banished 1.0.7, the 64-bit Steam build. Banished's own mod kit only
changes data. BML loads DLL plugins into the game and gives them a small C API for the
things the data can't reach: tools, input, placing buildings and roads, the frame loop
and the exe's code.

## Install

1. Download the zip from the latest release, or build with `build.bat` (Visual Studio
   2022 with the C++ tools).
2. Put `dinput8.dll` next to `Application-steam-x64.exe`.
3. Put the plugins (`gridbuild.dll`, `speed.dll`, ...) in `Banished\NativeMods\`. Only
   copy the ones you want.

After a build, `install.ps1` does steps 2 and 3 and finds the game through Steam, or takes
`-GameDir <path>`. `install.ps1 -Remove` undoes them. To uninstall by hand, delete
`dinput8.dll` from the game folder. The game's runtime imports `dinput8.dll` and Windows looks in the game folder
first, so BML loads with the game and forwards every DirectInput call to the system copy.
Nothing in the game folder is patched on disk.

BML checks the code at every address it uses before it touches anything. On any other
build (GOG, 32-bit, a future patch) it writes why to `NativeMods\bml.log` and stays off.

When it is running, the window title lists the plugins it loaded, for example
`Banished   (BML: gridbuild, replant, speed)`, and the build line in the corner of the main
menu ends with `Patched for the BML mod loader, 3 native mods running`. No title and no
menu line means BML is not in, and `bml.log` says why.

Some antivirus programs flag any `dinput8.dll` that isn't Windows' own, because loading
through one is also what cheats and malware do. The source is all here if you'd rather
build it yourself.

The main menu and the pause menu also get a BML button under Mods. It opens the game's
own mod details window with the active mods: the ones ticked in the Mods list, then every
running plugin, each with what it does.

BML also opens its own console window next to the game with everything the log gets, in
colour: green for what loaded, amber for warnings, red for errors, one colour per plugin.
The window has no close button because closing a console ends the game with it. Set
`console = off` in `NativeMods\bml.ini` to go without it, the log file is written either way.

## Plugins

### Grid Build

Pick a house (or any building), hold Shift and drag. The area fills with rows of buildings
side by side. Two rows face each other across one road and share it as their door row, and
the next pair starts right behind them, so the area is rows of houses with only roads in
between. Ghosts of the buildings follow the drag and only show where one fits. Let go to
place them, right-click or Escape cancels.

| Keys | Roads |
|---|---|
| Shift + drag | dirt |
| Shift + Ctrl + drag | stone |
| Shift + Alt + drag | none |

The rotate keys turn the rows. Spots that are blocked are skipped. Every building is an
ordinary build order, so it needs builders and materials and saves like any other.

### Area Fill

Pick a crop field, orchard, pasture, stockpile or cemetery, hold Shift and drag over
ground that is only partly usable. The usable part is split into the largest areas that
fit and each one is placed as its own field. While dragging, the areas show in two greens
so the splits are visible and red marks what is left out. Right-click or Escape cancels.

### Replant

Pick the Forester Lodge, hold Shift and drag over open ground to plant a forest there:
about 6 trees in 10 free tiles, every free tile with Ctrl as well. Trees are mixed kinds
and ages, mostly close to grown, and only go where a forester could plant one.

### Farm Limits

Crop fields stop harvesting once the town is at its food limit (planting and tending go
on), and pastures stop culling. With this plugin a field growing wheat keeps harvesting
past the limit and pastures keep culling, fields of every other crop still stop. Eggs and
wool keep their own limit. With the Wheat for Ale data mod, wheat no longer counts as food
either, so the wheat fields keep farming for the tavern however full the barns are.

### Storage Sync

A barn, stockpile, market or tavern keeps the capacity it was built with in the save, so a
data mod that makes storage bigger only reaches buildings put up after it. With this
plugin every storage building is grown to what its template gives when a game loads. It
never makes one smaller.

### Growth Sync

A tree, herb or other plant draws its lifespan from its template once, when it appears, and
keeps it in the save, so a data mod that shortens a lifespan only reaches new plants. With
this plugin every plant is cut back to what its template allows when a game loads. It never
makes one live longer.

### Auto Rebuild

A building that a fire or a tornado damaged waits for you to press Rebuild or Destroy in
its damage window. With this plugin it is set to rebuild on its own, the same as pressing
the button, including buildings that were already damaged in a save when it loads.
Builders still tear the ruin down and build it again, and the materials still have to be
there.

### Speed

Replaces the speed steps 1, 2, 5, 10 with 1 and the three numbers in
`NativeMods\speed.ini` (`steps = 10, 50, 250` by default).

The engine updates on a fixed time step, and at speed N the whole sim runs N times per
update. While those N steps take less than one time step, the game really runs N times
faster. Past that the engine drops the time it can't catch up on, so a higher setting
only lowers the frame rate and the real speed stops climbing. The ceiling depends on the
PC and the size of the town. The plugin logs what the game actually achieved every 10
seconds:

```
[speed] set to 250x: ran <real>x real time, <n> updates a second, <ms> ms each
```

## Writing a plugin

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

`version` went to 3 with `placement_template` and the three after it, and to 4 with the
area calls. Check it in init and decline when the loader is older than what you use.

A door row faces the lowest y at rotation 0, +x at 1, the highest y at 2 and -x at 3.

## How it was found

Every class registers itself with the engine by name, `Object::BaseTypeInfo` with a
factory, and the factories lead to the vtables (`PlacementTool`, `RoadTool`, the game's
update loop). The runtime DLL exports the engine's own API (input, resources, time), so
BML calls that by name. The addresses BML relies on are at the top of `core/game.cpp`.

## License

MIT, see `LICENSE`. Banished belongs to Shining Rock Software, none of its code or data is
in this repository.
