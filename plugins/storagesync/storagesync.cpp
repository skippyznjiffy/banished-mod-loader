// Storage Sync: a storage building keeps the capacity it was built with in the save, so a
// mod that makes barns, stockpiles, markets or taverns bigger only reaches new ones. This
// raises every loaded storage to what its template gives now. It never lowers one.
#include <cstdint>
#include <cstring>
#include <vector>

#include "bml.h"

namespace {

// RVAs in Application-steam-x64.exe 1.0.7
constexpr uint32_t kSerializeSlot = 0x16AB68;  // StorageComponent vtable slot 3, load and save
constexpr uint32_t kSerialize = 0x131C00;
constexpr uint32_t kGetFootprint = 0x004680;   // entity -> the component with its size in tiles
const uint8_t kGetFootprintPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89};

// offsets into game objects, as StorageComponent reads them when construction finishes
constexpr size_t kStreamMode = 8;         // 0 while a stream is read
constexpr size_t kComponentEntity = 16;
constexpr size_t kComponentDesc = 24;
constexpr size_t kStorageCapacity = 56;   // 0 until the building is finished
constexpr size_t kDescAreaBased = 32;     // StorageDescription._areaBasedLimit
constexpr size_t kDescVolume = 36;        // _volumeLimit, per tile when area based
constexpr size_t kFootprintWidth = 44;    // int16
constexpr size_t kFootprintHeight = 46;

using SerializeFn = void* (*)(void* component, void* stream);
using GetComponentFn = void** (*)(void* entity, void** out);

const BmlApi* g_api;
SerializeFn g_serialize;
std::vector<void*> g_loaded;

template <typename T>
T& Field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

int TemplateCapacity(void* storage) {
    void* desc = Field<void*>(storage, kComponentDesc);
    int area = 1;
    if (Field<uint8_t>(desc, kDescAreaBased)) {
        void* out = nullptr;
        auto getFootprint = reinterpret_cast<GetComponentFn>(g_api->exe_base() + kGetFootprint);
        void* footprint = *getFootprint(Field<void*>(storage, kComponentEntity), &out);
        if (!footprint) return 0;
        area = Field<int16_t>(footprint, kFootprintWidth) * Field<int16_t>(footprint, kFootprintHeight);
    }
    return Field<int>(desc, kDescVolume) * area;
}

// the entity is not whole until the load is done, so the sizes are fixed on the next frame
void* HookedSerialize(void* storage, void* stream) {
    void* result = g_serialize(storage, stream);
    if (Field<int>(stream, kStreamMode) == 0) g_loaded.push_back(storage);
    return result;
}

void OnFrame(float, void*) {
    if (g_loaded.empty()) return;
    int raised = 0;
    for (void* storage : g_loaded) {
        int current = Field<int>(storage, kStorageCapacity);
        if (current <= 0) continue;
        int capacity = TemplateCapacity(storage);
        if (capacity > current) {
            Field<int>(storage, kStorageCapacity) = capacity;
            ++raised;
        }
    }
    if (raised > 0) g_api->log("storagesync", "%d of %d storage buildings grown to their template size", raised,
                               static_cast<int>(g_loaded.size()));
    g_loaded.clear();
}

}  // namespace

extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api) {
    if (api->version < 4) return false;
    g_api = api;
    uint8_t* base = api->exe_base();
    if (memcmp(base + kGetFootprint, kGetFootprintPrologue, sizeof(kGetFootprintPrologue)) != 0) {
        api->log_warn("storagesync", "the footprint lookup is not where this build expects, nothing changed");
        return false;
    }
    void* original = base + kSerialize;
    void* hook = reinterpret_cast<void*>(&HookedSerialize);
    if (!api->patch_code(kSerializeSlot, &original, sizeof(original), &hook, sizeof(hook))) return false;
    g_serialize = reinterpret_cast<SerializeFn>(original);
    return api->on_frame(OnFrame, nullptr);
}

extern "C" __declspec(dllexport) const BmlPluginInfo* BmlPluginAbout() {
    static const BmlPluginInfo info = {
        "Storage Sync",
        "Barns, stockpiles, markets and taverns built before a mod made storage bigger are grown to the "
        "new size when a game loads. None is ever made smaller.",
    };
    return &info;
}
