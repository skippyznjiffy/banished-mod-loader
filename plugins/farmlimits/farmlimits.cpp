// Farm Limits: wheat fields go on harvesting and pastures go on culling when the town is
// over its food limit. Fields of every other crop still stop at the limit. Pairs with the
// Wheat for Ale data mod, which takes wheat out of the food count.
#include <climits>
#include <cstdint>
#include <cstring>

#include "bml.h"

namespace {

// RVAs in Application-steam-x64.exe 1.0.7
constexpr uint32_t kGame = 0x1CBDD8;
constexpr uint32_t kHarvestSlot = 0x1678E0;   // HarvestArea vtable slot 4, finds a farmer harvest work
constexpr uint32_t kHarvest = 0x0F21A0;
constexpr uint32_t kGetCropField = 0x005380;  // entity -> its CropFieldComponent
const uint8_t kGetCropFieldPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89};

// herders culling: cmp [rdx+rcx], eax / mov rcx, r12 / setnl dil, the setnl becomes
// xor dil, dil so the limit never reads as reached. Eggs and wool keep their own limit.
constexpr uint32_t kCullLimit = 0x0F7946;
const uint8_t kCullLimitCode[] = {0x39, 0x04, 0x0a, 0x49, 0x8b, 0xcc, 0x40, 0x0f, 0x9d, 0xc7};
const uint8_t kCullNoLimitCode[] = {0x39, 0x04, 0x0a, 0x49, 0x8b, 0xcc, 0x40, 0x30, 0xff, 0x90};

// offsets into game objects
constexpr size_t kGameLimits = 240;   // its limit table at +368, 32 byte entries
constexpr size_t kLimitTable = 368;
constexpr size_t kLimitStock = 0;
constexpr size_t kLimitMax = 8;
constexpr size_t kGameMap = 248;
constexpr size_t kMapTiles = 128;
constexpr size_t kTileSize = 48;
constexpr size_t kTileResource = 8;
constexpr size_t kAreaEntity = 16;
constexpr size_t kAreaTileCount = 96;
constexpr size_t kFieldDesc = 24;
constexpr size_t kFieldTiles = 88;    // 32 byte entries, the map tile first
constexpr size_t kDescLimit = 32;     // CropFieldDescription._resourceLimit
constexpr size_t kEntityTemplate = 40;

using HarvestFn = int64_t (*)(void* area, void* worker);
using GetComponentFn = void** (*)(void* entity, void** out);

const BmlApi* g_api;
HarvestFn g_harvest;
BmlTemplate* g_wheat;
bool g_announced;

template <typename T>
T& Field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// a field holds one crop, so the first plant says what it grows
bool GrowsWheat(void* field, int tiles, uint8_t* mapTiles) {
    uint8_t* entries = Field<uint8_t*>(field, kFieldTiles);
    for (int i = 0; i < tiles; ++i) {
        int tile = Field<int>(entries, 32 * static_cast<size_t>(i));
        void* plant = Field<void*>(mapTiles, kTileSize * tile + kTileResource);
        if (plant) return Field<void*>(plant, kEntityTemplate) == g_wheat;
    }
    return false;
}

// the limit the field obeys, when it is reached and the field grows wheat
int* WheatOverLimit(void* area) {
    void* game = *reinterpret_cast<void**>(g_api->exe_base() + kGame);
    void* entity = Field<void*>(area, kAreaEntity);
    if (!game || !entity) return nullptr;
    void* out = nullptr;
    void* field = *reinterpret_cast<GetComponentFn>(g_api->exe_base() + kGetCropField)(entity, &out);
    if (!field) return nullptr;
    int kind = Field<int>(Field<void*>(field, kFieldDesc), kDescLimit);
    uint8_t* limit = Field<uint8_t*>(Field<void*>(game, kGameLimits), kLimitTable) + 32 * static_cast<size_t>(kind);
    if (Field<int>(limit, kLimitStock) < Field<int>(limit, kLimitMax)) return nullptr;
    if (!g_wheat) g_wheat = g_api->find_template("Template/NaturalResourceWheat.rsc");
    if (!g_wheat) return nullptr;
    uint8_t* mapTiles = Field<uint8_t*>(Field<void*>(game, kGameMap), kMapTiles);
    if (!GrowsWheat(field, Field<int>(area, kAreaTileCount), mapTiles)) return nullptr;
    return &Field<int>(limit, kLimitMax);
}

struct RaisedLimit {
    int* limit;
    int saved;
    explicit RaisedLimit(int* l) : limit(l), saved(*l) { *limit = INT_MAX; }
    ~RaisedLimit() { *limit = saved; }
};

// the harvest only reads the limit once, before it looks for work, so lifting it for
// the call is all it takes
int64_t HookedHarvest(void* area, void* worker) {
    int* limit = WheatOverLimit(area);
    if (!limit) return g_harvest(area, worker);
    if (!g_announced) {
        g_api->log("farmlimits", "a wheat field is harvesting past the food limit");
        g_announced = true;
    }
    RaisedLimit raised(limit);
    return g_harvest(area, worker);
}

bool HookWheatHarvest() {
    uint8_t* base = g_api->exe_base();
    if (memcmp(base + kGetCropField, kGetCropFieldPrologue, sizeof(kGetCropFieldPrologue)) != 0) {
        g_api->log_warn("farmlimits", "the crop field lookup is not where this build expects, wheat fields unchanged");
        return false;
    }
    void* original = base + kHarvest;
    void* hook = reinterpret_cast<void*>(&HookedHarvest);
    if (!g_api->patch_code(kHarvestSlot, &original, sizeof(original), &hook, sizeof(hook))) return false;
    g_harvest = reinterpret_cast<HarvestFn>(original);
    g_api->log("farmlimits", "wheat fields ignore the food limit");
    return true;
}

}  // namespace

extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api) {
    if (api->version < 4) return false;
    g_api = api;
    bool wheat = HookWheatHarvest();
    bool culling = api->patch_code(kCullLimit, kCullLimitCode, sizeof(kCullLimitCode), kCullNoLimitCode,
                                   sizeof(kCullNoLimitCode));
    if (culling) api->log("farmlimits", "pastures ignore the food limit for culling");
    return wheat || culling;
}

extern "C" __declspec(dllexport) const BmlPluginInfo* BmlPluginAbout() {
    static const BmlPluginInfo info = {
        "Farm Limits",
        "Wheat fields keep harvesting and pastures keep culling past the food limit. Fields of every "
        "other crop still stop at it, and eggs and wool keep their own limit.",
    };
    return &info;
}
