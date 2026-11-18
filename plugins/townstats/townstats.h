#pragma once

#include <atomic>
#include <mutex>
#include <vector>

#include "bml.h"

namespace townstats {

constexpr int kPerYear = 48;  // the town records four samples a month
constexpr int kPerMonth = 4;
constexpr int kPerSeason = 12;
constexpr int kSeriesUsed = 41;

// the town's records, copied on the game thread after an update and drawn from here
struct Records {
    std::mutex lock;
    std::vector<std::vector<float>> series;
    int samples = 0;
    int frames = 0;
};

struct Settings {
    int page = 0;
    int range = 2;
    bool smooth = true;
    int goods = 1;
    int chartGoods = 0x47;  // a bit per kind of goods on the Charts page
    int chartShow = 0;
};

extern const BmlApi* g_api;
extern Records g_records;
extern Settings g_settings;
extern std::atomic<bool> g_open;

void SaveSettings();

// ui.cpp, called with the ImGui context current
void UiSetup();
void UiWindow();

}  // namespace townstats
