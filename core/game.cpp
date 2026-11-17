#include "game.h"

#include "console.h"

#include <windows.h>

#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// RVAs in Application-steam-x64.exe 1.0.7, the image base is randomised
constexpr uintptr_t kGame = 0x1CBDD8;               // pointer to the game object
constexpr uintptr_t kPlacementVtable = 0x168130;
constexpr int kToolUpdateSlot = 6;
constexpr uintptr_t kPlacementUpdate = 0x103770;
constexpr uintptr_t kPlace = 0x102B20;
constexpr uintptr_t kMouseTile = 0x106740;
constexpr uintptr_t kTerrainPoint = 0x0A48B0;
constexpr uintptr_t kRoadFactory = 0x02F1D0;
constexpr uintptr_t kRoadVtable = 0x168290;
constexpr int kRoadInitSlot = 4;
constexpr uintptr_t kRoadInit = 0x10ABB0;
constexpr int kRoadCreateSlot = 13;
constexpr uintptr_t kRoadCreate = 0x10A200;
constexpr int kRoadCheckSlot = 14;
constexpr uintptr_t kRoadCheck = 0x10A4B0;
constexpr uintptr_t kRoadUpdate = 0x10AB60;  // the drag tool's update, then the path method key
constexpr uintptr_t kRoadDraw = 0x10A660;
constexpr uintptr_t kSoundOk = 0x1055C0;
constexpr uintptr_t kSoundFail = 0x105610;
constexpr uintptr_t kComponentDescriptionType = 0x1CF318;  // type id the engine registered
constexpr uintptr_t kGameVtable = 0x164880;
constexpr int kGameUpdateSlot = 3;
constexpr uintptr_t kGameUpdate = 0x09B850;
constexpr uintptr_t kPlaceCheck = 0x0A2A50;
constexpr uintptr_t kTileBlocked = 0x0A1FD0;      // water, roads, zones, tiles held for a job
constexpr uintptr_t kEntityCtor = 0x0C8BF0;
constexpr uintptr_t kEntityAdd = 0x0C9D70;
constexpr uintptr_t kGetNaturalResource = 0x0052C0;
constexpr uintptr_t kNaturalSetTile = 0x12BA10;
constexpr uintptr_t kGetGrowth = 0x00C460;
constexpr uintptr_t kGrowthSet = 0x126280;
constexpr uintptr_t kGetModel = 0x003F60;
constexpr uintptr_t kModelGroups = 0x12A7C0;
constexpr uintptr_t kModelVariants = 0x12A7F0;
constexpr uintptr_t kModelSelect = 0x12A8A0;
constexpr uintptr_t kMeshVtable = 0x1647D0;       // the exe's copy of MeshInstance's vtable
constexpr uintptr_t kMeshDelete = 0x01A7E0;
constexpr uintptr_t kDragVtable = 0x167FB0;
constexpr uintptr_t kFencedVtable = 0x166A10;
constexpr int kAreaUpdateSlot = 6;
constexpr int kAreaDrawSlot = 8;
constexpr int kAreaCreateSlot = 13;
constexpr int kAreaCheckSlot = 14;
constexpr uintptr_t kTileCheck = 0x0A4AA0;       // one tile of an area, the area tools' own test
constexpr uintptr_t kDrawTiles = 0x105D90;       // the selection overlay
constexpr uintptr_t kBuildIdImport = 0x1587D0;   // Configuration::CurrentBuildId, only the main menu calls it

// offsets into game objects
constexpr size_t kGameMap = 248;           // int map size is the first field
constexpr size_t kGameUi = 152;
constexpr size_t kGameClock = 288;
constexpr size_t kClockPaused = 48;        // byte
constexpr size_t kClockSpeed = 52;         // sim steps per update
constexpr size_t kToolContext = 16;        // ui state every tool shares
constexpr size_t kToolTemplate = 32;
constexpr size_t kPlacementDesc = 40;      // CreatePlacedDescription
constexpr size_t kPlacementRotation = 72;  // object with the quarter turns at +16
constexpr size_t kDescWidth = 68;
constexpr size_t kDescHeight = 72;
constexpr size_t kRoadDesc = 56;           // CreateRoadDescription, set by init
constexpr size_t kPlacementPathDesc = 48;  // MapDescription, its path bitmap at +40
constexpr size_t kPlacementModel = 56;     // ModelDescription
constexpr size_t kPlacementVariant = 100;
constexpr size_t kDescPlaceBits = 32;
constexpr size_t kDescPlaceBitmap = 40;
constexpr size_t kDescPlaceBitArray = 48;
constexpr size_t kDescRemoves = 80;
constexpr size_t kModelVariantList = 32;   // 24 byte entries, meshes at +8 and their count at +16
constexpr size_t kTemplatePriority = 32;
constexpr size_t kGameRandom = 216;
constexpr size_t kGameEntities = 232;
constexpr size_t kGameScene = 96;
constexpr size_t kMapTiles = 128;
constexpr size_t kTileSize = 48;
constexpr size_t kTileEntity = 0;
constexpr size_t kTileResource = 8;
constexpr size_t kTileWalkway = 40;        // a door row lies here
constexpr size_t kEntitySize = 0x48;
constexpr size_t kMeshInstanceSize = 0x120;
constexpr size_t kAreaDesc = 56;           // CreateDragDescription or CreateFencedDescription
constexpr size_t kDescZones = 36;

struct Prologue {
    uintptr_t rva;
    uint8_t bytes[12];
};

// first bytes of every function called, none hold an address the loader relocates
const Prologue kPrologues[] = {
    {kPlacementUpdate, {0x4c, 0x8b, 0xdc, 0x53, 0x56, 0x48, 0x83, 0xec, 0x68, 0x49, 0x89, 0x6b}},
    {kPlace, {0x4c, 0x8b, 0xdc, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}},
    {kMouseTile, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x48, 0x08, 0x48, 0x83, 0xec, 0x68, 0x4c}},
    {kTerrainPoint, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48}},
    {kRoadFactory, {0x48, 0x83, 0xec, 0x38, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe, 0xff, 0xff}},
    {kRoadInit, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89}},
    {kRoadCreate, {0x41, 0x57, 0xb8, 0x70, 0x20, 0x00, 0x00, 0xe8, 0xb4, 0x6c, 0xf3, 0xff}},
    {kRoadCheck, {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0xb8, 0x40, 0x20, 0x00}},
    {kRoadUpdate, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8, 0x12, 0x70}},
    {kRoadDraw, {0x44, 0x88, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89}},
    {kSoundOk, {0x48, 0x89, 0x4c, 0x24, 0x08, 0x48, 0x83, 0xec, 0x28, 0x0f, 0x57, 0xc9}},
    {kSoundFail, {0x48, 0x89, 0x4c, 0x24, 0x08, 0x48, 0x83, 0xec, 0x28, 0x0f, 0x57, 0xc9}},
    {kGameUpdate, {0x48, 0x8b, 0xc4, 0x56, 0x57, 0x41, 0x56, 0x48, 0x81, 0xec, 0xe0, 0x00}},
    {kPlaceCheck, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x20, 0x89, 0x50, 0x10, 0x48, 0x89}},
    {kTileBlocked, {0x48, 0x63, 0xc2, 0x4c, 0x8b, 0xc9, 0x4c, 0x8d, 0x04, 0x40, 0xb8, 0x01}},
    {kEntityCtor, {0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4c, 0x24, 0x08, 0x53, 0x55}},
    {kEntityAdd, {0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7c, 0x24, 0x18, 0x41, 0x56}},
    {kGetNaturalResource, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89}},
    {kNaturalSetTile, {0x48, 0x8b, 0xc4, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41}},
    {kGetGrowth, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89}},
    {kGrowthSet, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x60, 0x0f, 0x29}},
    {kGetModel, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89}},
    {kModelGroups, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x59, 0x18, 0x8b, 0x15}},
    {kModelVariants, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b}},
    {kModelSelect, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48}},
    {kTileCheck, {0x4c, 0x8b, 0xdc, 0x49, 0x89, 0x73, 0x18, 0x57, 0x48, 0x83, 0xec, 0x70}},
    {kDrawTiles, {0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x48, 0x20, 0x4c, 0x89, 0x40, 0x18, 0x48}},
};

using ToolUpdateFn = char (*)(void* tool, char active);
using GameUpdateFn = char (*)(void* game, const int64_t* step);
using PlaceCheckFn = char (*)(int* map, int x, int y, int w, int h, int bits, void* bitArray, void* bitmap,
                              float a9, float a10, float a11, char a12, void* pathBitmap, int degrees,
                              void* removes);
using TileBlockedFn = char (*)(int* map, int tile);
using EntityCtorFn = void* (*)(void* memory, void* templ);
using EntityAddFn = void (*)(void* entities, void** entity, uint32_t priority);
using GetComponentFn = void** (*)(void* entity, void** out);
using NaturalSetTileFn = void (*)(void* resource, int tile);
using GrowthSetFn = void (*)(void* growth, float fraction);
using ModelGroupsFn = int (*)(void* model);
using ModelVariantsFn = int (*)(void* model, int group);
using ModelSelectFn = void (*)(void* model, int group, int variant);
using PlaceFn = char (*)(void* tool, int x0, int y0, int x1, int y1, float* pos, float degrees);
using MouseTileFn = int (*)(void*);
using TerrainPointFn = float* (*)(int* map, float* out, const float* in, char exact, void* normal);
using FactoryFn = void* (*)();
using RoadInitFn = void (*)(void* tool);
using RoadCreateFn = char (*)(void* tool, int from, int to);
using VoidFn = void (*)(void*);

using KeyMapGetConfig = void* (*)();
using KeyMapGetKeyMap = void* (*)(void* config);
using KeyMapQuery = bool (*)(void* keymap, int player, int id);
using KeyMapValue = int16_t (*)(void* keymap, int player, int id);
using FactoryGet = void* (*)();
using FactoryFind = void* (*)(void* factory, const void* filename);
using FilenameCtor = void* (*)(void* self, const char* path);
using FilenameDtor = void (*)(void* self);
using IsDerivedFromFn = bool (*)(const void* object, uint32_t type);
using TimeSecondsFn = float (*)(const int64_t* time);
using PausesGameFn = bool (*)(const void* ui);
using MemAllocFn = void* (*)(size_t size, int align);
using MeshCtorFn = void* (*)(void* self);
using MeshSetMeshFn = void (*)(void* self, void* mesh);
using MeshUpdateFn = void (*)(void* self);
using SceneAddFn = uint32_t (*)(void* scene, void* geometry);
using SceneRemoveFn = void (*)(void* scene, uint32_t id);
using SetTransformFn = void (*)(void* geometry, const void* transform);
using FromYawPitchOffsetFn = void* (*)(void* out, const float* yaw, const float* pitch, const float* offset);
using RandFn = uint32_t (*)(void* random, uint32_t below);
using RandFloatFn = float (*)(void* random);
using TileCheckFn = char (*)(int* map, int x, int y, int bits, float minDepth, float maxHeight, float height,
                             char zones, char door, void* allow);
using AreaDrawFn = void (*)(void* tool, void* context, char hidden);
using DrawTilesFn = char (*)(void* unused, void* context, const void* tiles, const void* lines);
using AreaCheckFn = int (*)(void* tool, int from, int to, int* clear, int* width, int* height, int* extra);
using AreaCreateFn = char (*)(void* tool, int from, int to);
using StringFn = void* (*)(void* out);  // returns a System::String by value
using StringGetFn = const char* (*)(const void* self);
using StringSetFn = void (*)(void* self, const char* text);

uint8_t* g_base;
ToolUpdateFn g_originalUpdate;
GameUpdateFn g_originalGameUpdate;
StringFn g_originalBuildId;
std::string g_menuNote;
void* g_toolContext;

struct Runtime {
    KeyMapGetConfig getConfig;
    KeyMapGetKeyMap getKeyMap;
    KeyMapQuery pressed;
    KeyMapQuery released;
    KeyMapValue value;
    FactoryGet factoryGet;
    FactoryFind factoryFind;
    FilenameCtor filenameCtor;
    FilenameDtor filenameDtor;
    IsDerivedFromFn isDerivedFrom;
    TimeSecondsFn timeSeconds;
    PausesGameFn pausesGame;
    MemAllocFn memAlloc;
    MeshCtorFn meshCtor;
    MeshSetMeshFn meshSetMesh;
    MeshUpdateFn meshUpdate;
    SceneAddFn sceneAdd;
    SceneRemoveFn sceneRemove;
    SetTransformFn setTransform;
    FromYawPitchOffsetFn fromYawPitchOffset;
    RandFn rand;
    RandFloatFn randFloat;
    StringGetFn stringGet;
    StringSetFn stringSet;
    StringFn buildId;
} g_rt;

struct Callback {
    BmlPlacementUpdateFn fn;
    void* user;
};
std::vector<Callback> g_placementCallbacks;

struct FrameCallback {
    BmlFrameFn fn;
    void* user;
};
std::vector<FrameCallback> g_frameCallbacks;
std::unordered_map<void*, void*> g_roadTools;  // template to the road tool made for it

std::mutex g_logLock;
FILE* g_log;

template <typename T>
T At(uintptr_t rva) {
    return reinterpret_cast<T>(g_base + rva);
}

template <typename T>
T& Field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

void** Vtable(void* object) {
    return *static_cast<void***>(object);
}

void* Game() {
    return *At<void**>(kGame);
}

int* Map() {
    void* game = Game();
    return game ? Field<int*>(game, kGameMap) : nullptr;
}

void* KeyMap() {
    void* config = g_rt.getConfig();
    return config ? g_rt.getKeyMap(config) : nullptr;
}

char HookedPlacementUpdate(void* tool, char active) {
    g_toolContext = Field<void*>(tool, kToolContext);
    for (const Callback& c : g_placementCallbacks) {
        if (c.fn(static_cast<BmlPlacementTool*>(tool), active != 0, c.user)) return 1;
    }
    return g_originalUpdate(tool, active);
}

// the main menu's build line, with BML and its plugins added
void* HookedBuildId(void* out) {
    g_originalBuildId(out);
    std::string text = g_rt.stringGet(out);
    text += g_menuNote;
    g_rt.stringSet(out, text.c_str());
    return out;
}

// api

void ApiLog(const char* plugin, const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    Log(plugin, "%s", line);
}

void ApiLogWarn(const char* plugin, const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    LogAt(LogLevel::Warn, plugin, "%s", line);
}

int ApiMapSize() {
    int* map = Map();
    return map ? *map : 0;
}

int ApiMouseTile() {
    return Map() ? At<MouseTileFn>(kMouseTile)(nullptr) : -1;
}

bool ApiPressed(int action) {
    void* km = KeyMap();
    return km && g_rt.pressed(km, 0, action);
}

bool ApiReleased(int action) {
    void* km = KeyMap();
    return km && g_rt.released(km, 0, action);
}

bool ApiHeld(int action) {
    void* km = KeyMap();
    return km && g_rt.value(km, 0, action) != 0;
}

bool ApiOnPlacementUpdate(BmlPlacementUpdateFn fn, void* user) {
    if (!fn) return false;
    g_placementCallbacks.push_back({fn, user});
    return true;
}

void ApiFootprint(BmlPlacementTool* tool, int* width, int* height) {
    void* desc = Field<void*>(tool, kPlacementDesc);
    *width = Field<int>(desc, kDescWidth);
    *height = Field<int>(desc, kDescHeight);
}

int ApiRotation(BmlPlacementTool* tool) {
    return Field<int>(Field<void*>(tool, kPlacementRotation), 16) & 3;
}

struct Footprint {
    int x0, y0, x1, y1;
    int degrees;
    float centre[4];
};

// the same corner and centre maths the tool runs on the tile under the mouse
bool FootprintAt(BmlPlacementTool* tool, int x, int y, int rotation, Footprint* out) {
    int* map = Map();
    if (!map) return false;
    int size = *map;
    int w, h;
    ApiFootprint(tool, &w, &h);
    rotation &= 3;
    if (rotation & 1) std::swap(w, h);
    out->x0 = x;
    out->y0 = y;
    out->x1 = x + w - 1;
    out->y1 = y + h - 1;
    out->degrees = rotation * 90;
    if (x < 1 || y < 0 || out->x1 >= size || out->y1 >= size) return false;

    float half = static_cast<float>(size >> 1) - 0.5f;
    alignas(16) float in0[4] = {x - half, y - half, 0, 0};
    alignas(16) float in1[4] = {out->x1 - half, out->y1 - half, 0, 0};
    alignas(16) float out0[4] = {};
    alignas(16) float out1[4] = {};
    auto terrain = At<TerrainPointFn>(kTerrainPoint);
    terrain(map, out0, in0, 0, nullptr);
    terrain(map, out1, in1, 0, nullptr);
    out->centre[0] = (out0[0] + out1[0]) * 0.5f;
    out->centre[1] = (out0[1] + out1[1]) * 0.5f;
    out->centre[2] = 0;
    out->centre[3] = 0;
    return true;
}

bool ApiPlaceBuilding(BmlPlacementTool* tool, int x, int y, int rotation) {
    Footprint f;
    if (!FootprintAt(tool, x, y, rotation, &f)) return false;
    return At<PlaceFn>(kPlace)(tool, f.x0, f.y0, f.x1, f.y1, f.centre, static_cast<float>(f.degrees)) != 0;
}

// the check Place runs before it queues anything
bool ApiCanPlaceBuilding(BmlPlacementTool* tool, int x, int y, int rotation) {
    Footprint f;
    if (!FootprintAt(tool, x, y, rotation, &f)) return false;
    void* desc = Field<void*>(tool, kPlacementDesc);
    uint8_t* d = static_cast<uint8_t*>(desc);
    uint8_t* path = static_cast<uint8_t*>(Field<void*>(tool, kPlacementPathDesc));
    return At<PlaceCheckFn>(kPlaceCheck)(Map(), f.x0, f.y0, f.x1 - f.x0 + 1, f.y1 - f.y0 + 1,
                                         Field<int>(desc, kDescPlaceBits), d + kDescPlaceBitArray,
                                         d + kDescPlaceBitmap, 0.0f, 1000.0f, 0.0f, 0, path + 40, f.degrees,
                                         Field<void*>(desc, kDescRemoves)) != 0;
}

BmlTemplate* ApiPlacementTemplate(BmlPlacementTool* tool) {
    return static_cast<BmlTemplate*>(Field<void*>(tool, kToolTemplate));
}

// the finished building's mesh for the variant the tool shows
void* GhostMesh(BmlPlacementTool* tool) {
    uint8_t* variants = Field<uint8_t*>(Field<void*>(tool, kPlacementModel), kModelVariantList);
    uint8_t* entry = variants + 24 * Field<int>(tool, kPlacementVariant);
    void** meshes = Field<void**>(entry, 8);
    return meshes[Field<int>(entry, 16) - 1];
}

struct Ghost {
    void* instance;
    uint32_t sceneId;
    bool shown;
};

struct GhostPool {
    void* mesh = nullptr;
    std::vector<Ghost> ghosts;
    bool touched = false;  // previewed since the last game update
} g_ghosts;

void* Scene() {
    return Field<void*>(Game(), kGameScene);
}

void HideGhost(Ghost& g) {
    if (!g.shown) return;
    g_rt.sceneRemove(Scene(), g.sceneId);
    g.shown = false;
}

void ClearGhosts() {
    using DeleteFn = void (*)(void* self, int flags);
    for (Ghost& g : g_ghosts.ghosts) {
        HideGhost(g);
        reinterpret_cast<DeleteFn>(Vtable(g.instance)[0])(g.instance, 1);
    }
    g_ghosts.ghosts.clear();
    g_ghosts.mesh = nullptr;
}

char HookedGameUpdate(void* game, const int64_t* step) {
    char result = g_originalGameUpdate(game, step);
    // a tool that stopped updating was closed, its ghosts go with it
    if (!g_ghosts.touched && !g_ghosts.ghosts.empty()) ClearGhosts();
    g_ghosts.touched = false;
    float seconds = g_rt.timeSeconds(step);
    for (const FrameCallback& c : g_frameCallbacks) c.fn(seconds, c.user);
    return result;
}

bool ApiPreviewBuildings(BmlPlacementTool* tool, const BmlSpot* spots, int count) {
    if (!Game()) return false;
    g_ghosts.touched = true;
    void* mesh = count > 0 ? GhostMesh(tool) : g_ghosts.mesh;
    if (mesh != g_ghosts.mesh) {
        ClearGhosts();
        g_ghosts.mesh = mesh;
    }
    for (int i = 0; i < count; ++i) {
        if (i == static_cast<int>(g_ghosts.ghosts.size())) {
            void* instance = g_rt.memAlloc(kMeshInstanceSize, 8);
            g_rt.meshCtor(instance);
            *static_cast<void**>(instance) = At<void*>(kMeshVtable);
            g_rt.meshSetMesh(instance, mesh);
            g_ghosts.ghosts.push_back({instance, 0, false});
        }
        Ghost& g = g_ghosts.ghosts[i];
        Footprint f;
        if (!FootprintAt(tool, spots[i].x, spots[i].y, spots[i].rotation, &f)) {
            HideGhost(g);
            continue;
        }
        float degrees = static_cast<float>(f.degrees);
        alignas(16) float yaw[4] = {degrees, degrees, degrees, degrees};
        alignas(16) float pitch[4] = {};
        alignas(16) uint8_t transform[128] = {};
        g_rt.fromYawPitchOffset(transform, yaw, pitch, f.centre);
        g_rt.setTransform(g.instance, transform);
        g_rt.meshUpdate(g.instance);
        if (!g.shown) {
            g.sceneId = g_rt.sceneAdd(Scene(), g.instance);
            g.shown = true;
        }
    }
    for (size_t i = count; i < g_ghosts.ghosts.size(); ++i) HideGhost(g_ghosts.ghosts[i]);
    return true;
}

// the forester's plant step, with the tile checks it makes first
bool ApiPlantResource(BmlTemplate* templ, int tile, float growth) {
    int* map = Map();
    if (!map || !templ || tile < 0 || tile >= *map * *map) return false;
    uint8_t* t = Field<uint8_t*>(map, kMapTiles) + kTileSize * tile;
    if (Field<void*>(t, kTileEntity) || Field<void*>(t, kTileResource) || t[kTileWalkway] ||
        At<TileBlockedFn>(kTileBlocked)(map, tile)) {
        return false;
    }
    void* entity = At<EntityCtorFn>(kEntityCtor)(g_rt.memAlloc(kEntitySize, 8), templ);
    void* out = nullptr;
    void* resource = *At<GetComponentFn>(kGetNaturalResource)(entity, &out);
    if (!resource) {
        using DeleteFn = void (*)(void* self, int flags);
        reinterpret_cast<DeleteFn>(Vtable(entity)[0])(entity, 1);
        LogAt(LogLevel::Warn, "bml", "plant_resource: template %p is not a natural resource", templ);
        return false;
    }
    At<NaturalSetTileFn>(kNaturalSetTile)(resource, tile);
    if (void* g = *At<GetComponentFn>(kGetGrowth)(entity, &out)) {
        At<GrowthSetFn>(kGrowthSet)(g, growth < 0 ? 0 : growth > 1 ? 1 : growth);
    }
    void* random = Field<void*>(Game(), kGameRandom);
    if (void* model = *At<GetComponentFn>(kGetModel)(entity, &out)) {
        int groups = At<ModelGroupsFn>(kModelGroups)(model);
        if (groups > 0) {
            int group = static_cast<int>(g_rt.rand(random, groups));
            int variants = At<ModelVariantsFn>(kModelVariants)(model, group);
            int variant = variants > 0 ? static_cast<int>(g_rt.rand(random, variants)) : 0;
            At<ModelSelectFn>(kModelSelect)(model, group, variant);
        }
    }
    At<EntityAddFn>(kEntityAdd)(Field<void*>(Game(), kGameEntities), &entity,
                                Field<uint32_t>(templ, kTemplatePriority));
    return true;
}

BmlTemplate* ApiFindTemplate(const char* path) {
    void* factory = g_rt.factoryGet();
    if (!factory) return nullptr;
    alignas(16) uint8_t filename[256] = {};
    g_rt.filenameCtor(filename, path);
    void* found = g_rt.factoryFind(factory, filename);
    g_rt.filenameDtor(filename);
    // the tools read a template's description list, anything else would be garbage to them
    if (found && !g_rt.isDerivedFrom(found, *At<uint32_t*>(kComponentDescriptionType))) {
        LogAt(LogLevel::Warn, "bml", "%s is not a ComponentDescription", path);
        return nullptr;
    }
    return static_cast<BmlTemplate*>(found);
}

void* RoadToolFor(void* road) {
    auto it = g_roadTools.find(road);
    if (it != g_roadTools.end()) return it->second;
    void* tool = At<FactoryFn>(kRoadFactory)();
    if (!tool) return nullptr;
    Field<void*>(tool, kToolContext) = g_toolContext;
    Field<void*>(tool, kToolTemplate) = road;
    reinterpret_cast<RoadInitFn>(Vtable(tool)[kRoadInitSlot])(tool);
    if (!Field<void*>(tool, kRoadDesc)) {
        // not a road template, the tool is kept so nothing it points at is released
        LogAt(LogLevel::Warn, "bml", "template %p has no road description", road);
        tool = nullptr;
    }
    g_roadTools[road] = tool;
    return tool;
}

bool ApiPlaceRoad(BmlTemplate* road, int from, int to) {
    if (!road || !g_toolContext || !Map()) return false;
    void* tool = RoadToolFor(road);
    if (!tool) return false;
    // a new game makes a new ui state
    Field<void*>(tool, kToolContext) = g_toolContext;
    return reinterpret_cast<RoadCreateFn>(Vtable(tool)[kRoadCreateSlot])(tool, from, to) != 0;
}

bool ApiOnFrame(BmlFrameFn fn, void* user) {
    if (!fn) return false;
    g_frameCallbacks.push_back({fn, user});
    return true;
}

int ApiGameSpeed() {
    void* game = Game();
    void* clock = game ? Field<void*>(game, kGameClock) : nullptr;
    if (!clock || Field<uint8_t>(clock, kClockPaused) || g_rt.pausesGame(Field<void*>(game, kGameUi))) return 0;
    return Field<int>(clock, kClockSpeed);
}

uint8_t* ApiExeBase() {
    return g_base;
}

bool ApiPatchCode(uint32_t rva, const void* expect, size_t expectLength, const void* bytes, size_t length) {
    uint8_t* at = g_base + rva;
    if (memcmp(at, expect, expectLength) != 0) {
        LogAt(LogLevel::Warn, "bml", "patch at rva %x refused, the code there is not what was expected", rva);
        return false;
    }
    DWORD old;
    if (!VirtualProtect(at, length, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at, bytes, length);
    VirtualProtect(at, length, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, length);
    return true;
}

void ApiSoundOk() {
    At<VoidFn>(kSoundOk)(nullptr);
}

void ApiSoundFail() {
    At<VoidFn>(kSoundFail)(nullptr);
}

// area tools: the drag tool (fields, orchards, stockpiles, clearing) and the fenced
// tool (pastures, cemeteries) share a layout, only their descriptions differ
struct AreaKind {
    uintptr_t vtable;
    uintptr_t update;
    uintptr_t draw;
    uintptr_t create;
    uintptr_t check;
    size_t maxWidth;  // in the tool's description, max height, min width and min height follow
    size_t allow;     // what the area may clear to make room
    bool zones;       // the drag description's ignore zones byte
};

const AreaKind kAreaKinds[] = {
    {kDragVtable, 0x101B80, 0x101310, 0x100DD0, 0x100FB0, 40, 56, true},
    {kFencedVtable, 0x0E20B0, 0x0E1910, 0x0E02D0, 0x0E0960, 88, 112, false},
};
constexpr int kAreaKindCount = sizeof(kAreaKinds) / sizeof(kAreaKinds[0]);

struct AreaCallback {
    BmlAreaUpdateFn fn;
    void* user;
};
std::vector<AreaCallback> g_areaCallbacks;
ToolUpdateFn g_originalAreaUpdate[kAreaKindCount];
AreaDrawFn g_originalAreaDraw[kAreaKindCount];

// the tile list the game's selection overlay draws
struct OverlayTile {
    int x, y;
    int icon;
    uint32_t color;
    int unused;
};

struct OverlayList {
    const void* data;
    int count;
    int capacity;
};

struct AreaMarks {
    std::vector<OverlayTile> tiles;
    bool touched = false;  // marked during this update
} g_areaMarks;

const AreaKind* KindOf(void* tool) {
    for (const AreaKind& k : kAreaKinds) {
        if (Vtable(tool) == At<void**>(k.vtable)) return &k;
    }
    return nullptr;
}

void* AreaDesc(void* tool) {
    return Field<void*>(tool, kAreaDesc);
}

template <int K>
char HookedAreaUpdate(void* tool, char active) {
    g_toolContext = Field<void*>(tool, kToolContext);
    g_areaMarks.touched = false;
    char result = 1;
    bool swallowed = false;
    for (const AreaCallback& c : g_areaCallbacks) {
        if (c.fn(static_cast<BmlAreaTool*>(tool), active != 0, c.user)) {
            swallowed = true;
            break;
        }
    }
    if (!swallowed) result = g_originalAreaUpdate[K](tool, active);
    if (!g_areaMarks.touched) g_areaMarks.tiles.clear();
    return result;
}

// draws the marks instead of the tool's own overlay, false when there are none
bool DrawMarks(void* context, char hidden) {
    if (g_areaMarks.tiles.empty() || hidden) return false;
    OverlayList tiles = {g_areaMarks.tiles.data(), static_cast<int>(g_areaMarks.tiles.size()),
                         static_cast<int>(g_areaMarks.tiles.size())};
    OverlayList lines = {nullptr, 0, 0};
    At<DrawTilesFn>(kDrawTiles)(nullptr, context, &tiles, &lines);
    return true;
}

void SetMarks(const BmlTileMark* marks, int count) {
    g_areaMarks.touched = true;
    g_areaMarks.tiles.resize(count > 0 ? count : 0);
    for (int i = 0; i < count; ++i) g_areaMarks.tiles[i] = {marks[i].x, marks[i].y, 0, marks[i].color, 0};
}

template <int K>
void HookedAreaDraw(void* tool, void* context, char hidden) {
    if (!DrawMarks(context, hidden)) g_originalAreaDraw[K](tool, context, hidden);
}

bool ApiOnAreaUpdate(BmlAreaUpdateFn fn, void* user) {
    if (!fn) return false;
    g_areaCallbacks.push_back({fn, user});
    return true;
}

BmlTemplate* ApiAreaTemplate(BmlAreaTool* tool) {
    return static_cast<BmlTemplate*>(Field<void*>(tool, kToolTemplate));
}

void ApiAreaLimits(BmlAreaTool* tool, int* minWidth, int* minHeight, int* maxWidth, int* maxHeight) {
    const AreaKind* kind = KindOf(tool);
    if (!kind) {
        *minWidth = *minHeight = *maxWidth = *maxHeight = 0;
        return;
    }
    void* desc = AreaDesc(tool);
    *maxWidth = Field<int>(desc, kind->maxWidth);
    *maxHeight = Field<int>(desc, kind->maxWidth + 4);
    *minWidth = Field<int>(desc, kind->maxWidth + 8);
    *minHeight = Field<int>(desc, kind->maxWidth + 12);
}

// the test the tool runs on every tile of an area
bool ApiAreaTileOk(BmlAreaTool* tool, int x, int y) {
    const AreaKind* kind = KindOf(tool);
    int* map = Map();
    if (!kind || !map) return false;
    void* desc = AreaDesc(tool);
    char zones = kind->zones ? Field<char>(desc, kDescZones) : 0;
    return At<TileCheckFn>(kTileCheck)(map, x, y, Field<int>(desc, kDescPlaceBits), 0.0f, 100.0f, 0.0f, zones, 0,
                                       Field<void*>(desc, kind->allow)) != 0;
}

int ApiAreaCheck(BmlAreaTool* tool, int x0, int y0, int x1, int y1) {
    const AreaKind* kind = KindOf(tool);
    int* map = Map();
    if (!kind || !map) return BML_AREA_BLOCKED;
    int size = *map;
    int clear, width, height, extra;
    return At<AreaCheckFn>(kind->check)(tool, x0 + y0 * size, x1 + y1 * size, &clear, &width, &height, &extra);
}

bool ApiPlaceArea(BmlAreaTool* tool, int x0, int y0, int x1, int y1) {
    const AreaKind* kind = KindOf(tool);
    int* map = Map();
    if (!kind || !map) return false;
    int size = *map;
    return At<AreaCreateFn>(kind->create)(tool, x0 + y0 * size, x1 + y1 * size) != 0;
}

bool ApiMarkTiles(BmlAreaTool* tool, const BmlTileMark* marks, int count) {
    if (!KindOf(tool)) return false;
    SetMarks(marks, count);
    return true;
}

// the road tool is a drag tool too, with its own check and create
struct RoadCallback {
    BmlRoadUpdateFn fn;
    void* user;
};
std::vector<RoadCallback> g_roadCallbacks;
ToolUpdateFn g_originalRoadUpdate;
AreaDrawFn g_originalRoadDraw;

bool IsRoadTool(void* tool) {
    return tool && Vtable(tool) == At<void**>(kRoadVtable);
}

char HookedRoadUpdate(void* tool, char active) {
    g_toolContext = Field<void*>(tool, kToolContext);
    g_areaMarks.touched = false;
    char result = 1;
    bool swallowed = false;
    for (const RoadCallback& c : g_roadCallbacks) {
        if (c.fn(static_cast<BmlRoadTool*>(tool), active != 0, c.user)) {
            swallowed = true;
            break;
        }
    }
    if (!swallowed) result = g_originalRoadUpdate(tool, active);
    if (!g_areaMarks.touched) g_areaMarks.tiles.clear();
    return result;
}

void HookedRoadDraw(void* tool, void* context, char hidden) {
    if (!DrawMarks(context, hidden)) g_originalRoadDraw(tool, context, hidden);
}

bool ApiOnRoadUpdate(BmlRoadUpdateFn fn, void* user) {
    if (!fn) return false;
    g_roadCallbacks.push_back({fn, user});
    return true;
}

BmlTemplate* ApiRoadTemplate(BmlRoadTool* tool) {
    return IsRoadTool(tool) ? static_cast<BmlTemplate*>(Field<void*>(tool, kToolTemplate)) : nullptr;
}

// the tool's own test for a road between two tiles, run on a single tile. While the tool
// is mid drag it only answers for the ends of that drag
bool ApiRoadTileOk(BmlRoadTool* tool, int x, int y) {
    int* map = Map();
    if (!IsRoadTool(tool) || !map || x < 0 || y < 0 || x >= *map || y >= *map) return false;
    int tile = x + y * *map;
    int clear, width, height, extra;
    return At<AreaCheckFn>(kRoadCheck)(tool, tile, tile, &clear, &width, &height, &extra) == 0;
}

bool ApiPlaceRoadTile(BmlRoadTool* tool, int x, int y) {
    int* map = Map();
    if (!IsRoadTool(tool) || !map || x < 0 || y < 0 || x >= *map || y >= *map) return false;
    int tile = x + y * *map;
    return reinterpret_cast<RoadCreateFn>(Vtable(tool)[kRoadCreateSlot])(tool, tile, tile) != 0;
}

bool ApiMarkRoadTiles(BmlRoadTool* tool, const BmlTileMark* marks, int count) {
    if (!IsRoadTool(tool)) return false;
    SetMarks(marks, count);
    return true;
}

const BmlApi kApi = {
    BML_API_VERSION,
    ApiLog,
    ApiMapSize,
    ApiMouseTile,
    ApiPressed,
    ApiReleased,
    ApiHeld,
    ApiOnPlacementUpdate,
    ApiFootprint,
    ApiRotation,
    ApiPlaceBuilding,
    ApiFindTemplate,
    ApiPlaceRoad,
    ApiSoundOk,
    ApiSoundFail,
    ApiOnFrame,
    ApiGameSpeed,
    ApiExeBase,
    ApiPatchCode,
    ApiPlacementTemplate,
    ApiCanPlaceBuilding,
    ApiPreviewBuildings,
    ApiPlantResource,
    ApiOnAreaUpdate,
    ApiAreaTemplate,
    ApiAreaLimits,
    ApiAreaTileOk,
    ApiAreaCheck,
    ApiPlaceArea,
    ApiMarkTiles,
    ApiLogWarn,
    ApiOnRoadUpdate,
    ApiRoadTemplate,
    ApiRoadTileOk,
    ApiPlaceRoadTile,
    ApiMarkRoadTiles,
};

}  // namespace

namespace {

const char* const kLevelTags[] = {"", "", "warning: ", "error: "};

void LogV(LogLevel level, const char* plugin, const char* fmt, va_list args) {
    char line[1024];
    vsnprintf(line, sizeof(line), fmt, args);
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::lock_guard<std::mutex> lock(g_logLock);
    if (g_log) {
        fprintf(g_log, "%02d:%02d:%02d [%s] %s%s\n", t.wHour, t.wMinute, t.wSecond, plugin,
                kLevelTags[static_cast<int>(level)], line);
        fflush(g_log);
    }
    ConsoleLine(t, plugin, level, line);
}

}  // namespace

void LogOpen(const wchar_t* path) {
    // shared so the log can be read while the game runs
    g_log = _wfsopen(path, L"w", _SH_DENYWR);
}

void Log(const char* plugin, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogV(LogLevel::Info, plugin, fmt, args);
    va_end(args);
}

void LogAt(LogLevel level, const char* plugin, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogV(level, plugin, fmt, args);
    va_end(args);
}

bool GameBind() {
    g_base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    for (const Prologue& p : kPrologues) {
        if (memcmp(g_base + p.rva, p.bytes, sizeof(p.bytes)) != 0) {
            LogAt(LogLevel::Error, "bml", "code at rva %llx is not what this build expects, game hooks are off",
                static_cast<unsigned long long>(p.rva));
            return false;
        }
    }
    void** placement = At<void**>(kPlacementVtable);
    void** road = At<void**>(kRoadVtable);
    void** gameVtable = At<void**>(kGameVtable);
    if (At<void**>(kMeshVtable)[0] != g_base + kMeshDelete ||
        placement[kToolUpdateSlot] != g_base + kPlacementUpdate ||
        gameVtable[kGameUpdateSlot] != g_base + kGameUpdate ||
        road[kRoadInitSlot] != g_base + kRoadInit || road[kRoadCreateSlot] != g_base + kRoadCreate ||
        road[kRoadCheckSlot] != g_base + kRoadCheck || road[kAreaUpdateSlot] != g_base + kRoadUpdate ||
        road[kAreaDrawSlot] != g_base + kRoadDraw) {
        LogAt(LogLevel::Error, "bml", "vtables are not what this build expects, game hooks are off");
        return false;
    }
    for (const AreaKind& k : kAreaKinds) {
        void** v = At<void**>(k.vtable);
        if (v[kAreaUpdateSlot] != g_base + k.update || v[kAreaDrawSlot] != g_base + k.draw ||
            v[kAreaCreateSlot] != g_base + k.create || v[kAreaCheckSlot] != g_base + k.check) {
            LogAt(LogLevel::Error, "bml", "area tool vtables are not what this build expects, game hooks are off");
            return false;
        }
    }

    HMODULE runtime = GetModuleHandleW(L"Runtime-steam-x64.dll");
    if (!runtime) {
        LogAt(LogLevel::Error, "bml", "Runtime-steam-x64.dll is not loaded");
        return false;
    }
    bool ok = Bind(runtime, "?Get@Configuration@Input@@SAPEAV12@XZ", g_rt.getConfig) &&
              Bind(runtime, "?GetKeyMap@Configuration@Input@@QEAAPEAVKeyMap@2@XZ", g_rt.getKeyMap) &&
              Bind(runtime, "?Pressed@KeyMap@Input@@QEBA_NHH@Z", g_rt.pressed) &&
              Bind(runtime, "?Released@KeyMap@Input@@QEBA_NHH@Z", g_rt.released) &&
              Bind(runtime, "?GetValue@KeyMap@Input@@QEBAFHH@Z", g_rt.value) &&
              Bind(runtime, "?Get@ExternalFactory@Object@@SAPEAV12@XZ", g_rt.factoryGet) &&
              Bind(runtime, "?Find@ExternalFactory@Object@@QEBAPEBVExternal@2@AEBVFilename@System@@@Z",
                   g_rt.factoryFind) &&
              Bind(runtime, "??0Filename@System@@QEAA@PEBD@Z", g_rt.filenameCtor) &&
              Bind(runtime, "??1Filename@System@@QEAA@XZ", g_rt.filenameDtor) &&
              Bind(runtime, "?IsDerivedFrom@Base@Object@@QEBA_NI@Z", g_rt.isDerivedFrom) &&
              Bind(runtime, "?AsSeconds@Time@System@@QEBAMXZ", g_rt.timeSeconds) &&
              Bind(runtime, "?PausesGame@UserInterface@UI@@QEBA_NXZ", g_rt.pausesGame) &&
              Bind(runtime, "?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z", g_rt.memAlloc) &&
              Bind(runtime, "??0MeshInstance@Graphics@@QEAA@XZ", g_rt.meshCtor) &&
              Bind(runtime, "?SetMesh@MeshInstance@Graphics@@QEAAXV?$ExternalPtr@VGraphicsMesh@Graphics@@@Object@@@Z",
                   g_rt.meshSetMesh) &&
              Bind(runtime, "?UpdateHierarchy@MeshInstance@Graphics@@QEAAXXZ", g_rt.meshUpdate) &&
              Bind(runtime, "?AddObject@Scene@Graphics@@QEAAIPEAVGeometry@2@@Z", g_rt.sceneAdd) &&
              Bind(runtime, "?RemoveObject@Scene@Graphics@@QEAAXI@Z", g_rt.sceneRemove) &&
              Bind(runtime, "?SetTransform@Geometry@Graphics@@QEAAXAEBVTransform@Math@@@Z", g_rt.setTransform) &&
              Bind(runtime, "?FromYawPitchOffset@Transform@Math@@SA?AV12@AEBVScalar@2@0AEBVVector3@2@@Z",
                   g_rt.fromYawPitchOffset) &&
              Bind(runtime, "?Rand@Random@System@@QEAAII@Z", g_rt.rand) &&
              Bind(runtime, "?RandFloat@Random@System@@QEAAMXZ", g_rt.randFloat) &&
              Bind(runtime, "?Get@String@System@@QEBAPEBDXZ", g_rt.stringGet) &&
              Bind(runtime, "?Set@String@System@@QEAAXPEBD@Z", g_rt.stringSet) &&
              Bind(runtime, "?CurrentBuildId@Configuration@System@@SA?AVString@2@XZ", g_rt.buildId);
    if (!ok) return false;
    if (*At<void**>(kBuildIdImport) != reinterpret_cast<void*>(g_rt.buildId)) {
        LogAt(LogLevel::Error, "bml", "the build id import is not where this build expects");
        return false;
    }
    LogAt(LogLevel::Good, "bml", "bound to the game at %p", g_base);
    return true;
}

bool PatchPointer(void** slot, void* value) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

void GameHook(const std::vector<std::string>& plugins) {
    g_menuNote = "   |   Patched for the BML mod loader, " + std::to_string(plugins.size()) +
                 (plugins.size() == 1 ? " native mod running" : " native mods running");
    g_originalBuildId = g_rt.buildId;
    if (!PatchPointer(At<void**>(kBuildIdImport), reinterpret_cast<void*>(HookedBuildId))) {
        LogAt(LogLevel::Error, "bml", "could not patch the main menu build line");
    }
    void** placement = At<void**>(kPlacementVtable);
    g_originalUpdate = reinterpret_cast<ToolUpdateFn>(placement[kToolUpdateSlot]);
    if (!PatchPointer(&placement[kToolUpdateSlot], reinterpret_cast<void*>(HookedPlacementUpdate))) {
        LogAt(LogLevel::Error, "bml", "could not patch the placement tool vtable");
    }
    void* areaUpdates[] = {reinterpret_cast<void*>(HookedAreaUpdate<0>), reinterpret_cast<void*>(HookedAreaUpdate<1>)};
    void* areaDraws[] = {reinterpret_cast<void*>(HookedAreaDraw<0>), reinterpret_cast<void*>(HookedAreaDraw<1>)};
    static_assert(sizeof(areaUpdates) / sizeof(areaUpdates[0]) == kAreaKindCount, "one hook per area kind");
    for (int i = 0; i < kAreaKindCount; ++i) {
        void** v = At<void**>(kAreaKinds[i].vtable);
        g_originalAreaUpdate[i] = reinterpret_cast<ToolUpdateFn>(v[kAreaUpdateSlot]);
        g_originalAreaDraw[i] = reinterpret_cast<AreaDrawFn>(v[kAreaDrawSlot]);
        if (!PatchPointer(&v[kAreaUpdateSlot], areaUpdates[i]) || !PatchPointer(&v[kAreaDrawSlot], areaDraws[i])) {
            LogAt(LogLevel::Error, "bml", "could not patch an area tool vtable");
        }
    }
    void** road = At<void**>(kRoadVtable);
    g_originalRoadUpdate = reinterpret_cast<ToolUpdateFn>(road[kAreaUpdateSlot]);
    g_originalRoadDraw = reinterpret_cast<AreaDrawFn>(road[kAreaDrawSlot]);
    if (!PatchPointer(&road[kAreaUpdateSlot], reinterpret_cast<void*>(HookedRoadUpdate)) ||
        !PatchPointer(&road[kAreaDrawSlot], reinterpret_cast<void*>(HookedRoadDraw))) {
        LogAt(LogLevel::Error, "bml", "could not patch the road tool vtable");
    }
    void** game = At<void**>(kGameVtable);
    g_originalGameUpdate = reinterpret_cast<GameUpdateFn>(game[kGameUpdateSlot]);
    if (!PatchPointer(&game[kGameUpdateSlot], reinterpret_cast<void*>(HookedGameUpdate))) {
        LogAt(LogLevel::Error, "bml", "could not patch the game update");
    }
}

const BmlApi* GameApi() {
    return &kApi;
}
