// Idle Work: a citizen with nothing left to do takes construction work, so builders are not the
// only ones putting up buildings. The game's idle state already falls back from a citizen's own
// job to the general jobs and then to the laborers' jobs, this adds the builders' jobs after those.
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "bml.h"

namespace {

// RVAs in Application-steam-x64.exe 1.0.7
constexpr uint32_t kIdleState = 0x1107A0;   // CitizenAIComponent's idle state, it only calls the search below
constexpr uint32_t kGame = 0x1CBDD8;        // the game loop
constexpr uint32_t kWalkBucket = 0x0FCE80;  // offers a profession's open jobs to a citizen, true when one took
constexpr uint32_t kRollJobs = 0x0FCA20;    // tries the general job lists in a random order

// the first 14 bytes of the idle state, replaced by a jump here
constexpr uint8_t kIdleStart[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
                                  0x41, 0x38, 0x48, 0x8D, 0x59, 0x38};

constexpr size_t kGameScheduler = 0x118;
constexpr size_t kGameProfessions = 0xB0;
constexpr size_t kLaborer = 0x30;           // ProfessionList._default
constexpr size_t kBuilder = 0x38;           // ProfessionList._builder
constexpr size_t kChild = 0x40;
constexpr size_t kStudent = 0x48;
constexpr size_t kSchedulerReady = 0x38;    // 0 until the scheduler has any job
constexpr size_t kSchedulerBuckets = 0x120; // a tree of open jobs by profession
constexpr size_t kBucketJobs = 0x08;
constexpr size_t kBucketLeft = 0x20;
constexpr size_t kBucketRight = 0x28;
constexpr size_t kJobWork = 0x08;          // a job set node is keyed by work id, the work follows
constexpr size_t kJobLeft = 0x18;
constexpr size_t kJobRight = 0x20;
constexpr size_t kWorkRejectStep = 0x2C;  // Work: the scheduler pass that last turned someone away
constexpr size_t kSchedulerStep = 0x08;
constexpr size_t kAiCitizen = 0x38;
constexpr size_t kCitizenProfession = 0x1F0;
constexpr size_t kCitizenCharacter = 0x278;
constexpr size_t kCharacterAction = 0x60;   // -1 when the character has no action

using WalkBucketFn = bool (*)(void* scheduler, void** citizen, void** first, void* profession, bool cost,
                              bool all);
using RollJobsFn = bool (*)(void* scheduler, void** citizen);

const BmlApi* g_api;
WalkBucketFn g_walkBucket;
RollJobsFn g_rollJobs;

template <typename T>
T& Field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

void* Game() {
    return Field<void*>(g_api->exe_base(), kGame);
}

// a profession's entry in the scheduler's job tree, null when it has none. The tree is ordered by address
void* FindBucket(void* scheduler, void* profession) {
    auto key = reinterpret_cast<uintptr_t>(profession);
    void* node = Field<void*>(scheduler, kSchedulerBuckets);
    while (node && Field<uintptr_t>(node, 0) != key) {
        node = Field<void*>(node, key < Field<uintptr_t>(node, 0) ? kBucketLeft : kBucketRight);
    }
    return node;
}

// the walk also runs with no open jobs in the bucket: it looks at the citizen's own workplace too,
// which is how builders find the clearing job of the site they are on
bool Offer(void* scheduler, void* citizen, void* profession, void* bucket) {
    void* first = Field<void*>(bucket, kBucketJobs);
    while (first && Field<void*>(first, kJobLeft)) first = Field<void*>(first, kJobLeft);
    return g_walkBucket(scheduler, &citizen, &first, profession, false, false);
}

bool OfferBucket(void* scheduler, void* citizen, void* profession) {
    void* bucket = FindBucket(scheduler, profession);
    return bucket && Offer(scheduler, citizen, profession, bucket);
}

// a job that turns a citizen away is skipped by everyone for the rest of the pass, and a site
// with all its builders turns every other citizen away, so a helper's rejections are taken back
// or the builders never get the job
bool OfferAsHelper(void* scheduler, void* citizen, void* profession) {
    void* bucket = FindBucket(scheduler, profession);
    if (!bucket || !Field<void*>(bucket, kBucketJobs)) return false;
    std::vector<std::pair<void*, int>> stamps;
    std::vector<void*> pending{Field<void*>(bucket, kBucketJobs)};
    while (!pending.empty()) {
        void* node = pending.back();
        pending.pop_back();
        void* work = Field<void*>(node, kJobWork);
        if (work) stamps.emplace_back(work, Field<int>(work, kWorkRejectStep));
        if (void* left = Field<void*>(node, kJobLeft)) pending.push_back(left);
        if (void* right = Field<void*>(node, kJobRight)) pending.push_back(right);
    }
    bool took = Offer(scheduler, citizen, profession, bucket);
    int step = Field<int>(scheduler, kSchedulerStep);
    for (auto& [work, stamp] : stamps) {
        if (Field<int>(work, kWorkRejectStep) == step) Field<int>(work, kWorkRejectStep) = stamp;
    }
    return took;
}

// the game's own search (0xFCD00) with the builders' jobs added at the end
void FindWork(void* scheduler, void* citizen) {
    if (Field<int>(scheduler, kSchedulerReady) == 0) return;
    void* professions = Field<void*>(Game(), kGameProfessions);
    void* laborer = Field<void*>(professions, kLaborer);
    void* builder = Field<void*>(professions, kBuilder);
    void* own = Field<void*>(citizen, kCitizenProfession);
    if (own != laborer && OfferBucket(scheduler, citizen, own)) return;
    if (g_rollJobs(scheduler, &citizen)) return;
    if (OfferBucket(scheduler, citizen, laborer)) return;
    if (own != builder && own != Field<void*>(professions, kChild) && own != Field<void*>(professions, kStudent)) {
        OfferAsHelper(scheduler, citizen, builder);
    }
}

// replaces the idle state, which reports whether the citizen has an action now
bool IdleState(void* ai) {
    void* citizen = Field<void*>(ai, kAiCitizen);
    FindWork(Field<void*>(Game(), kGameScheduler), citizen);
    void* character = Field<void*>(citizen, kCitizenCharacter);
    return Field<int>(character, kCharacterAction) + 1 > 0;
}

}  // namespace

extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api) {
    if (api->version < 4) return false;
    g_api = api;
    g_walkBucket = reinterpret_cast<WalkBucketFn>(api->exe_base() + kWalkBucket);
    g_rollJobs = reinterpret_cast<RollJobsFn>(api->exe_base() + kRollJobs);
    // jmp qword ptr [rip+0] and the address
    uint8_t jump[14] = {0xFF, 0x25, 0, 0, 0, 0};
    void* target = reinterpret_cast<void*>(&IdleState);
    std::memcpy(jump + 6, &target, sizeof(target));
    if (!api->patch_code(kIdleState, kIdleStart, sizeof(kIdleStart), jump, sizeof(jump))) return false;
    api->log("idlework", "idle citizens take construction work");
    return true;
}

extern "C" __declspec(dllexport) const BmlPluginInfo* BmlPluginAbout() {
    static const BmlPluginInfo info = {
        "Idle Work",
        "A citizen with nothing left to do, after their own job and the laborers' jobs, helps build. "
        "Builders are no longer the only ones putting up buildings.",
    };
    return &info;
}
