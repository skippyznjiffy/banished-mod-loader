// Growth Sync: a plant draws its lifespan from its template once, when it appears, and keeps it
// in the save, so a mod that shortens a lifespan only reaches new plants. This brings every
// loaded plant down to what its template allows now. It never lengthens one.
#include <cstdint>
#include <vector>

#include "bml.h"

namespace {

// RVAs in Application-steam-x64.exe 1.0.7
constexpr uint32_t kSerializeSlot = 0x1699D8;  // GrowthComponent vtable slot 3, load and save
constexpr uint32_t kSerialize = 0x125C90;

// offsets into game objects, as GrowthComponent reads them when it appears
constexpr size_t kStreamMode = 8;         // 0 while a stream is read
constexpr size_t kComponentDesc = 24;
constexpr size_t kLifespan = 0x4C;        // float, below 0 for a plant that never dies
constexpr size_t kDescLifespan = 0x20;    // GrowthDescription._maxGrowth
constexpr size_t kDescTolerance = 0x24;   // _maxGrowthTolerance, the most a plant draws on top

using SerializeFn = void* (*)(void* component, void* stream);

const BmlApi* g_api;
SerializeFn g_serialize;
std::vector<void*> g_loaded;

template <typename T>
T& Field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// the description is not hooked up until the load is done, so lifespans are fixed on the next frame
void* HookedSerialize(void* growth, void* stream) {
    void* result = g_serialize(growth, stream);
    if (Field<int>(stream, kStreamMode) == 0) g_loaded.push_back(growth);
    return result;
}

void OnFrame(float, void*) {
    if (g_loaded.empty()) return;
    int shortened = 0;
    for (void* growth : g_loaded) {
        void* desc = Field<void*>(growth, kComponentDesc);
        if (!desc) continue;
        float lifespan = Field<float>(desc, kDescLifespan);
        if (lifespan <= 0) continue;
        float longest = lifespan + Field<float>(desc, kDescTolerance);
        float& current = Field<float>(growth, kLifespan);
        if (current < 0 || current > longest) {
            current = longest;
            ++shortened;
        }
    }
    if (shortened > 0) g_api->log("growthsync", "%d of %d plants cut back to their template lifespan", shortened,
                                  static_cast<int>(g_loaded.size()));
    g_loaded.clear();
}

}  // namespace

extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api) {
    if (api->version < 4) return false;
    g_api = api;
    void* original = api->exe_base() + kSerialize;
    void* hook = reinterpret_cast<void*>(&HookedSerialize);
    if (!api->patch_code(kSerializeSlot, &original, sizeof(original), &hook, sizeof(hook))) return false;
    g_serialize = reinterpret_cast<SerializeFn>(original);
    return api->on_frame(OnFrame, nullptr);
}

extern "C" __declspec(dllexport) const BmlPluginInfo* BmlPluginAbout() {
    static const BmlPluginInfo info = {
        "Growth Sync",
        "Trees, herbs and other plants keep the lifespan they had when they appeared. When a game loads, "
        "any that would outlive what their template allows now are cut back to it. None is ever made to "
        "live longer.",
    };
    return &info;
}
