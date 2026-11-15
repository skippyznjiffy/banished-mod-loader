// Auto Rebuild: a building damaged by fire or a tornado is rebuilt on its own, as if the
// Rebuild button in its damage window had been pressed. Builders still do the work.
#include <cstdint>
#include <vector>

#include "bml.h"

namespace {

// RVAs in Application-steam-x64.exe 1.0.7
constexpr uint32_t kStates = 0x1DFAE0;      // DestroyComponent's state table, filled by a static initializer
constexpr uint32_t kStateCount = 0x1E0A60;
constexpr uint32_t kRebuild = 0x122770;     // the damaged state's rebuild event

// a state table entry
constexpr size_t kEntrySize = 0xF8;
constexpr size_t kEntryUpdate = 0x10;       // two update callbacks, the state machine calls one or the other
constexpr size_t kEntryUpdateToo = 0x18;
constexpr size_t kEntryEvents = 0xB0;       // 16 bytes each, the id then the handler
constexpr size_t kEntryEventCount = 0xF0;

// the state machine a StateMachineComponent keeps at +0x48
constexpr size_t kMachineState = 8;
constexpr size_t kMachineIndex = 9;         // the state's entry in its machine's table
constexpr size_t kMachinePending = 10;      // the next state, negative when none
constexpr int kDamaged = 7;                 // burnt out or hit, waiting for Rebuild or Destroy
constexpr int kRebuildEvent = 0;
constexpr size_t kSendEventSlot = 2;

using UpdateFn = bool (*)(void* owner, uint8_t* machine, void* step);
using SendEventFn = bool (*)(void* table, uint8_t* machine, int index, int event);

const BmlApi* g_api;
std::vector<uint8_t*> g_damaged;
bool g_hooked;

template <typename T>
T& Field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// runs every tick for each damaged building. Rebuilding in the middle of a tick crashed the
// game, a citizen later in the same tick still looked for the ruin's job, so it waits for the
// end of the tick, where the Rebuild button acts too
bool DamagedUpdate(void*, uint8_t* machine, void*) {
    g_damaged.push_back(machine);
    return false;
}

// the same call the Rebuild button makes
bool Rebuild(uint8_t* machine) {
    if (machine[kMachineState] != kDamaged || static_cast<int8_t>(machine[kMachinePending]) >= 0) return false;
    void* table = Field<void*>(machine, 0);
    auto send = (*reinterpret_cast<SendEventFn**>(table))[kSendEventSlot];
    return send(table, machine, static_cast<int8_t>(machine[kMachineIndex]), kRebuildEvent);
}

uint8_t* DamagedEntry() {
    uint8_t* base = g_api->exe_base();
    int count = Field<int>(base, kStateCount);
    for (int i = 0; i < count; ++i) {
        uint8_t* entry = base + kStates + kEntrySize * i;
        if (Field<int>(entry, 0) == kDamaged) return entry;
    }
    return nullptr;
}

bool RebuildsOnEvent(uint8_t* entry) {
    int events = Field<int>(entry, kEntryEventCount);
    for (int i = 0; i < events; ++i) {
        uint8_t* event = entry + kEntryEvents + 16 * i;
        if (Field<int>(event, 0) == kRebuildEvent) return Field<uint8_t*>(event, 8) == g_api->exe_base() + kRebuild;
    }
    return false;
}

// the table is only filled once the game's own startup has run, after plugins load
void OnFrame(float, void*) {
    if (!g_hooked) {
        g_hooked = true;
        uint8_t* entry = DamagedEntry();
        if (!entry || !RebuildsOnEvent(entry) || Field<void*>(entry, kEntryUpdate) ||
            Field<void*>(entry, kEntryUpdateToo)) {
            g_api->log_warn("autorebuild", "the damaged building state is not what this build expects, nothing changed");
            return;
        }
        Field<UpdateFn>(entry, kEntryUpdate) = DamagedUpdate;
        Field<UpdateFn>(entry, kEntryUpdateToo) = DamagedUpdate;
        g_api->log("autorebuild", "damaged buildings rebuild on their own");
    }
    if (g_damaged.empty()) return;
    int rebuilt = 0;
    for (uint8_t* machine : g_damaged) rebuilt += Rebuild(machine);
    g_damaged.clear();
    if (rebuilt > 0) g_api->log("autorebuild", "%d damaged buildings set to rebuild", rebuilt);
}

}  // namespace

extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api) {
    if (api->version < 4) return false;
    g_api = api;
    return api->on_frame(OnFrame, nullptr);
}

extern "C" __declspec(dllexport) const BmlPluginInfo* BmlPluginAbout() {
    static const BmlPluginInfo info = {
        "Auto Rebuild",
        "A building damaged by fire or a tornado is rebuilt on its own, as if its Rebuild button had been "
        "pressed. Builders still do the work and the materials still have to be there.",
    };
    return &info;
}
