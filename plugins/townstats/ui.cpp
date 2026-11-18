// what the overlay looks like: tabs for five pages, cards with the town's numbers and charts of
// its records. Every page is laid out by hand from the room it has, so nothing is cut off, and
// what is made and used is averaged over a year unless the player switches that off
#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "icons.h"
#include "imgui.h"
#include "implot.h"
#include "townstats.h"

namespace townstats {
namespace {

ImVec4 Hex(unsigned rgb, float alpha = 1) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
}

ImU32 U32(const ImVec4& color, float alpha = 1) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
}

const ImVec4 kWindow = Hex(0x15171a, 0.985f);
const ImVec4 kCard = Hex(0x1f2226);
const ImVec4 kCardHi = Hex(0x2b2f35);
const ImVec4 kBorder = Hex(0x343940);
const ImVec4 kTrack = Hex(0x30353c);
const ImVec4 kText = Hex(0xf3f1ec);
const ImVec4 kMuted = Hex(0xb9b6ae);
const ImVec4 kFaint = Hex(0x8d8b85);
const ImVec4 kAccent = Hex(0xf2b24a);
const ImVec4 kOnAccent = Hex(0x1a1408);
const ImVec4 kGood = Hex(0x7dd28b);
const ImVec4 kWarn = Hex(0xf2b24a);
const ImVec4 kBad = Hex(0xf4776a);
const ImVec4 kMade = Hex(0x7dd28b);
const ImVec4 kUsed = Hex(0xf0935a);
const ImVec4 kBlue = Hex(0x7ab6f6);
const ImVec4 kPink = Hex(0xe893b8);
const ImVec4 kViolet = Hex(0xb69cf0);

constexpr float kSmall = 16;
constexpr float kTick = 15;
constexpr float kBody = 17;
constexpr float kHeading = 18;
constexpr float kValue = 28;
constexpr float kTitle = 26;
constexpr float kPad = 18;  // inside a card
constexpr float kGap = 14;  // between cards
constexpr float kKpiHeight = 112;
constexpr float kOverviewTile = 134;
constexpr float kChartMin = 230;

ImFont* g_regular;
ImFont* g_semibold;

struct Goods {
    const char* name;
    const char* sprite;  // on the game's sprite sheet
    int stock, used, made;
    ImVec4 color;
};

const Goods kGoods[] = {
    {"Food", "Apple", 23, 24, 25, Hex(0xf2b24a)},
    {"Firewood", "Firewood", 17, 18, 19, Hex(0xf0935a)},
    {"Logs", "Log", 8, 9, 10, Hex(0xcf9a66)},
    {"Stone", "Stone", 11, 12, 13, Hex(0xc3c7ce)},
    {"Iron", "Iron", 14, 15, 16, Hex(0x8fb0e0)},
    {"Coal", "Coal", 35, 36, 37, Hex(0xa3a3ad)},
    {"Tools", "Tool", 20, 21, 22, Hex(0x6fcbbd)},
    {"Clothing", "WoolCoat", 29, 30, 31, Hex(0xb69cf0)},
    {"Herbs", "Herb", 26, 27, 28, Hex(0x8fd676)},
    {"Alcohol", "Ale", 32, 33, 34, Hex(0xf2d76b)},
    {"Textiles", "Wool", 38, 39, 40, Hex(0xe893b8)},
};
constexpr int kGoodsCount = sizeof(kGoods) / sizeof(kGoods[0]);

enum Page { kOverview, kFood, kGoodsPage, kPeople, kCharts, kPageCount };
const char* const kPages[] = {"Overview", "Food", "Goods", "People", "Charts"};
const char* const kPageIcons[] = {ICON_LAYOUT_DASHBOARD, ICON_WHEAT, ICON_PACKAGE, ICON_USERS, ICON_CHART_LINE};
const char* const kPageNotes[] = {
    "The town at a glance. Click a row for its charts.",
    "What the town harvests and eats, and how long its food lasts.",
    "What is in stock, made and used of one kind of goods.",
    "Who lives in the town and how they are doing.",
    "Pick the goods to follow side by side.",
};
const int kRangeYears[] = {1, 2, 5, 10, 25, 0};
const char* const kRangeNames[] = {"1y", "2y", "5y", "10y", "25y", "All"};
constexpr int kRangeCount = 6;
const char* const kShowNames[] = {"Made and used", "In stock", "Net"};
const char* const kSeasons[] = {"Spring", "Summer", "Autumn", "Winter"};
const char* const kThirds[] = {"Early", "Mid", "Late"};
const char* const kDash = "\xe2\x80\x93";

// records

int Samples() {
    return g_records.series.empty() ? 0 : static_cast<int>(g_records.series[0].size());
}

float Latest(int s) {
    const auto& v = g_records.series[s];
    return v.empty() ? 0.0f : v.back();
}

float Ago(int s, int samples) {
    const auto& v = g_records.series[s];
    if (v.empty()) return 0;
    return v[std::max(0, static_cast<int>(v.size()) - 1 - samples)];
}

// the sum of a rate, or the mean of a stock, over the last year
double LastYear(int s, bool mean) {
    const auto& v = g_records.series[s];
    int n = std::min(static_cast<int>(v.size()), kPerYear);
    double sum = 0;
    for (int i = static_cast<int>(v.size()) - n; i < static_cast<int>(v.size()); ++i) sum += v[i];
    if (n == 0) return 0;
    return mean ? sum / n : sum * kPerYear / n;
}

// rates are per sample, this is what a season brought on average over the last year
double SeasonRate(int s) {
    return LastYear(s, false) / 4;
}

int FirstSample() {
    int years = kRangeYears[g_settings.range];
    return years > 0 ? std::max(0, Samples() - years * kPerYear) : 0;
}

// a stock, averaged over the season before each sample when smoothing
std::vector<float> StockSeries(int s, int first) {
    const auto& v = g_records.series[s];
    int window = g_settings.smooth ? kPerSeason : 1;
    std::vector<float> out;
    double sum = 0;
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        sum += v[i];
        if (i >= window) sum -= v[i - window];
        if (i >= first) out.push_back(static_cast<float>(sum / std::min(i + 1, window)));
    }
    return out;
}

// a rate as an amount a season: over the last season, or the last year's average when smoothing
std::vector<float> SeasonSeries(int s, int first) {
    const auto& v = g_records.series[s];
    int window = g_settings.smooth ? kPerYear : kPerSeason;
    std::vector<float> out;
    double sum = 0;
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        sum += v[i];
        if (i >= window) sum -= v[i - window];
        if (i >= first) out.push_back(static_cast<float>(sum * kPerSeason / std::min(i + 1, window)));
    }
    return out;
}

struct Flow {
    double stock, made, used, net;
    double reserve;  // months the stock covers what is used, negative when nothing is used
    double runsOut;  // months until the stock is gone at last year's net, negative while it is not shrinking
};

Flow FlowOf(const Goods& g) {
    Flow f;
    f.stock = Latest(g.stock);
    f.made = SeasonRate(g.made);
    f.used = SeasonRate(g.used);
    f.net = f.made - f.used;
    f.reserve = f.used > 0.01 ? f.stock / (f.used / 3) : -1;
    f.runsOut = f.net < -0.01 ? f.stock / (-f.net / 3) : -1;
    return f;
}

// text

std::string Number(double value) {
    char raw[32];
    snprintf(raw, sizeof(raw), "%.0f", std::fabs(value));
    std::string digits(raw), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (value <= -0.5 ? "-" : "") + out;
}

std::string Signed(double value) {
    return (value >= 0.5 ? "+" : "") + Number(value);
}

std::string Compact(double value) {
    char text[32];
    double a = std::fabs(value);
    if (a < 0.5) return "0";
    if (a >= 1e6) snprintf(text, sizeof(text), "%.1fm", value / 1e6);
    else if (a >= 1e4) snprintf(text, sizeof(text), "%.0fk", value / 1e3);
    else if (a >= 1e3) snprintf(text, sizeof(text), "%.1fk", value / 1e3);
    else snprintf(text, sizeof(text), "%.0f", value);
    std::string out(text);
    size_t zero = out.find(".0");
    if (zero != std::string::npos) out.erase(zero, 2);
    return out;
}

std::string Duration(double months) {
    char text[48];
    if (months >= 120) snprintf(text, sizeof(text), "%.0f years", months / 12);
    else if (months >= 24) snprintf(text, sizeof(text), "%.1f years", months / 12);
    else if (months >= 1.5) snprintf(text, sizeof(text), "%.0f months", months);
    else snprintf(text, sizeof(text), "under a month");
    return text;
}

// the records start with the game, in year 1
std::string Date(int sample) {
    char text[48];
    int inYear = sample % kPerYear;
    snprintf(text, sizeof(text), "%s %s, year %d", kThirds[(inYear / kPerMonth) % 3], kSeasons[inYear / kPerSeason],
             sample / kPerYear + 1);
    return text;
}

// drawing

struct Box {
    float x, y, w, h;
    float Right() const { return x + w; }
    float Bottom() const { return y + h; }
    ImVec2 Min() const { return ImVec2(x, y); }
    ImVec2 Max() const { return ImVec2(x + w, y + h); }
};

enum class Align { Left, Center, Right };

float Width(ImFont* font, float size, const std::string& text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str()).x;
}

// one line of text with its left end, middle or right end at x, cut short with dots past maxWidth
float Text(ImFont* font, float size, float x, float y, const ImVec4& color, const std::string& text,
           Align align = Align::Left, float maxWidth = 0) {
    std::string shown = text;
    float width = Width(font, size, shown);
    if (maxWidth > 0 && width > maxWidth) {
        while (!shown.empty() && width > maxWidth) {
            do shown.pop_back();
            while (!shown.empty() && (shown.back() & 0xC0) == 0x80);
            width = Width(font, size, shown + "...");
        }
        shown += "...";
    }
    if (align == Align::Center) x -= width / 2;
    if (align == Align::Right) x -= width;
    ImGui::GetWindowDrawList()->AddText(font, size, ImVec2(std::floor(x), std::floor(y)), U32(color), shown.c_str());
    return width;
}

void Card(const Box& b, const ImVec4& fill = kCard) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(b.Min(), b.Max(), U32(fill), 12);
    draw->AddRect(b.Min(), b.Max(), U32(kBorder), 12);
}

void Dot(float x, float y, const ImVec4& color) {
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(x, y), 4.5f, U32(color), 16);
}

// a rounded bar filled up to a share of its width
void Meter(const Box& b, double share, const ImVec4& color) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(b.Min(), b.Max(), U32(kTrack), b.h / 2);
    float filled = b.w * static_cast<float>(std::clamp(share, 0.0, 1.0));
    if (filled >= b.h) draw->AddRectFilled(b.Min(), ImVec2(b.x + filled, b.Bottom()), U32(color), b.h / 2);
}

bool Button(const char* id, const Box& b, bool* hovered = nullptr) {
    ImGui::SetCursorScreenPos(b.Min());
    bool clicked = ImGui::InvisibleButton(id, ImVec2(b.w, b.h));
    if (hovered) *hovered = ImGui::IsItemHovered();
    return clicked;
}

// the game's own picture for a kind of goods, from its sprite sheet
struct Sprite {
    std::string name;
    BmlSprite sprite;
    bool found;
};
std::deque<Sprite> g_sprites;

const BmlSprite* FindSprite(const char* name) {
    for (const Sprite& s : g_sprites) {
        if (s.name == name) return s.found ? &s.sprite : nullptr;
    }
    Sprite& s = g_sprites.emplace_back(Sprite{name, {}, false});
    s.found = g_api->ui_sprite(name, &s.sprite);
    return s.found ? &s.sprite : nullptr;
}

void GameIcon(const char* name, float x, float y, float size) {
    if (const BmlSprite* found = FindSprite(name)) {
        ImGui::GetWindowDrawList()->AddImage(
            ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(found->texture))), ImVec2(x, y),
            ImVec2(x + size, y + size), ImVec2(found->u0, found->v0), ImVec2(found->u1, found->v1));
    }
}

void BeginTip() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
    ImGui::BeginTooltip();
}

void EndTip() {
    ImGui::EndTooltip();
    ImGui::PopStyleVar(2);
}

// how many of count cards go side by side in the width, with rows as even as they get
int Columns(float width, float minWidth, int count) {
    int fit = std::clamp(static_cast<int>((width + kGap) / (minWidth + kGap)), 1, std::max(1, count));
    int rows = (count + fit - 1) / fit;
    return (count + rows - 1) / rows;
}

Box Cell(float x, float y, float width, int columns, int index, float height) {
    float w = (width - kGap * (columns - 1)) / columns;
    return Box{x + (w + kGap) * (index % columns), y + (height + kGap) * (index / columns), w, height};
}

float GridHeight(int columns, int count, float height) {
    int rows = (count + columns - 1) / columns;
    return rows * height + (rows - 1) * kGap;
}

struct Kpi {
    const char* icon;
    ImVec4 tint;
    std::string label;
    std::string value;
    ImVec4 valueColor;
    std::string note;
    ImVec4 noteColor;
};

float KpiRow(float x, float y, float width, const std::vector<Kpi>& kpis) {
    int count = static_cast<int>(kpis.size());
    int columns = Columns(width, 190, count);
    for (int i = 0; i < count; ++i) {
        const Kpi& k = kpis[i];
        Box b = Cell(x, y, width, columns, i, kKpiHeight);
        float inner = b.w - 2 * kPad;
        Card(b);
        Text(g_regular, kSmall, b.x + kPad, b.y + 14, kMuted, k.label, Align::Left, inner - 26);
        Text(g_regular, 18, b.Right() - kPad, b.y + 13, k.tint, k.icon, Align::Right);
        Text(g_semibold, kValue, b.x + kPad, b.y + 35, k.valueColor, k.value, Align::Left, inner);
        Text(g_regular, kSmall, b.x + kPad, b.y + 79, k.noteColor, k.note, Align::Left, inner);
    }
    return GridHeight(columns, count, kKpiHeight);
}

// charts

struct Line {
    std::string name;
    ImVec4 color;
    std::vector<float> ys;
    bool fill = false;
    std::vector<float> lower;  // what a stacked line stands on
};

struct ChartOptions {
    bool net = false;    // the tooltip adds the first line minus the second
    bool total = false;  // the tooltip adds the sum of stacked lines
    double top = 0;      // a fixed top of the scale
    const char* sprite = nullptr;
};

double NiceStep(double span, int steps) {
    double raw = std::max(span, 1.0) / steps;
    double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    double f = raw / magnitude;
    double nice = f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10;
    return std::max(1.0, nice * magnitude);
}

struct Ticks {
    std::vector<double> at;
    std::vector<std::string> text;
    std::vector<const char*> labels;

    void Add(double value, const std::string& label) {
        at.push_back(value);
        text.push_back(label);
    }
    void Finish() {
        for (const std::string& t : text) labels.push_back(t.c_str());
    }
};

// seasons by name while they have room, whole years past that. x is in years, 1 at the start of the save
Ticks TimeTicks(double x0, double x1, float width) {
    Ticks ticks;
    double span = x1 - x0;
    int room = std::max(2, static_cast<int>(width / 72));
    double step = 0.25;
    if (span > 2.01 || span * 4 > room) {
        step = span <= 2.01 && span * 2 <= room ? 0.5 : 1;
        for (double years : {1.0, 2.0, 5.0, 10.0, 20.0, 50.0}) {
            if (step < 1 || span / step <= room) break;
            step = years;
        }
    }
    for (double v = std::ceil(x0 / step - 1e-6) * step; v <= x1 + 1e-6; v += step) {
        int quarter = static_cast<int>(std::lround(v * 4));
        char label[32];
        if (step >= 1) snprintf(label, sizeof(label), "Year %d", quarter / 4);
        else if (quarter % 4 == 0) snprintf(label, sizeof(label), "Spring %d", quarter / 4);
        else snprintf(label, sizeof(label), "%s", kSeasons[quarter % 4]);
        // a label hanging over the right edge would be cut
        float fromRight = static_cast<float>((x1 - v) / span) * width;
        if (fromRight < Width(g_regular, kTick, label) / 2 - 12) continue;
        ticks.Add(v, label);
    }
    ticks.Finish();
    return ticks;
}

struct TipRow {
    ImVec4 color;
    std::string name;
    std::string value;
    ImVec4 valueColor;
};

void ChartTip(const std::string& date, const std::vector<TipRow>& rows) {
    BeginTip();
    float names = 0, values = 0;
    for (const TipRow& r : rows) {
        names = std::max(names, Width(g_regular, kSmall, r.name));
        values = std::max(values, Width(g_semibold, kSmall, r.value));
    }
    float width = std::max(18 + names + 28 + values, Width(g_semibold, kSmall, date));
    ImVec2 pos = ImGui::GetCursorScreenPos();
    Text(g_semibold, kSmall, pos.x, pos.y, kText, date);
    float y = pos.y + 28;
    for (const TipRow& r : rows) {
        if (r.color.w > 0) Dot(pos.x + 5, y + 10, r.color);
        Text(g_regular, kSmall, pos.x + 18, y, kMuted, r.name);
        Text(g_semibold, kSmall, pos.x + width, y, r.valueColor, r.value, Align::Right);
        y += 24;
    }
    ImGui::Dummy(ImVec2(width, y - pos.y - 6));
    EndTip();
}

// the lines over the chosen years, with the values under the mouse in a tooltip
void Plot(const Box& box, const std::vector<Line>& lines, int first, const ChartOptions& options) {
    int count = lines.empty() ? 0 : static_cast<int>(lines[0].ys.size());
    if (count < 2) {
        Text(g_regular, kBody, box.x + box.w / 2, box.y + box.h / 2 - 12, kFaint, "No records yet", Align::Center);
        return;
    }
    double x0 = static_cast<double>(first) / kPerYear + 1;
    double x1 = static_cast<double>(first + count - 1) / kPerYear + 1;
    std::vector<float> xs(count);
    for (int i = 0; i < count; ++i) xs[i] = static_cast<float>(first + i) / kPerYear + 1;

    double low = 0, high = 0;
    for (const Line& line : lines) {
        for (float y : line.ys) {
            low = std::min(low, static_cast<double>(y));
            high = std::max(high, static_cast<double>(y));
        }
    }
    double step = NiceStep(std::max(high, options.top) - low, std::clamp(static_cast<int>(box.h / 44), 2, 6));
    // up to the next step when that is close, or the chart would be half empty
    auto reach = [step](double value) {
        double whole = std::ceil(value / step) * step;
        return whole <= value * 1.25 ? whole : value * 1.1;
    };
    double bottom = low < 0 ? -reach(-low) : 0;
    double top = options.top > 0 ? options.top : high > 0 ? reach(high) : step;
    Ticks yTicks;
    for (double v = std::ceil(bottom / step) * step; v <= top + step / 100; v += step) yTicks.Add(v, Compact(v));
    yTicks.Finish();
    Ticks xTicks = TimeTicks(x0, x1, box.w - 60);
    // a little past both ends, ImPlot leaves out a line that runs along the edge of the plot
    double margin = (top - bottom) * 0.012;
    bottom -= margin;
    top += margin;

    ImVec2 plotPos, plotSize;
    ImGui::SetCursorScreenPos(box.Min());
    ImGui::PushFont(g_regular, kTick);
    if (ImPlot::BeginPlot("##plot", ImVec2(box.w, box.h),
                          ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs | ImPlotFlags_NoFrame)) {
        ImPlotAxisFlags axis = ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight |
                               ImPlotAxisFlags_NoSideSwitch;
        ImPlot::SetupAxes(nullptr, nullptr, axis | ImPlotAxisFlags_NoGridLines, axis);
        ImPlot::SetupAxisLimits(ImAxis_X1, x0, x1, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, bottom, top, ImPlotCond_Always);
        ImPlot::SetupAxisTicks(ImAxis_X1, xTicks.at.data(), static_cast<int>(xTicks.at.size()), xTicks.labels.data());
        ImPlot::SetupAxisTicks(ImAxis_Y1, yTicks.at.data(), static_cast<int>(yTicks.at.size()), yTicks.labels.data());
        for (const Line& line : lines) {
            if (!line.fill) continue;
            ImPlotSpec fill(ImPlotProp_FillColor, U32(line.color), ImPlotProp_FillAlpha,
                            line.lower.empty() ? 0.14f : 0.3f);
            if (line.lower.empty()) ImPlot::PlotShaded(line.name.c_str(), xs.data(), line.ys.data(), count, 0.0, fill);
            else ImPlot::PlotShaded(line.name.c_str(), xs.data(), line.lower.data(), line.ys.data(), count, fill);
        }
        for (const Line& line : lines) {
            ImPlot::PlotLine(line.name.c_str(), xs.data(), line.ys.data(), count,
                             ImPlotSpec(ImPlotProp_LineColor, U32(line.color), ImPlotProp_LineWeight, 2.0f));
        }
        plotPos = ImPlot::GetPlotPos();
        plotSize = ImPlot::GetPlotSize();
        ImPlot::EndPlot();
    }
    ImGui::PopFont();

    ImVec2 mouse = ImGui::GetMousePos();
    bool inside = mouse.x >= plotPos.x && mouse.x <= plotPos.x + plotSize.x && mouse.y >= plotPos.y &&
                  mouse.y <= plotPos.y + plotSize.y;
    if (!inside || plotSize.x < 1 || !ImGui::IsWindowHovered()) return;
    int i = std::clamp(static_cast<int>(std::lround((mouse.x - plotPos.x) / plotSize.x * (count - 1))), 0, count - 1);
    float px = plotPos.x + plotSize.x * i / (count - 1);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(px, plotPos.y), ImVec2(px, plotPos.y + plotSize.y), U32(kText, 0.3f));
    std::vector<TipRow> rows;
    double sum = 0;
    for (const Line& line : lines) {
        float py = plotPos.y + plotSize.y * static_cast<float>(1 - (line.ys[i] - bottom) / (top - bottom));
        draw->AddCircleFilled(ImVec2(px, py), 6, U32(kCard), 20);
        draw->AddCircleFilled(ImVec2(px, py), 4, U32(line.color), 20);
        double value = line.ys[i] - (line.lower.empty() ? 0 : line.lower[i]);
        sum += value;
        rows.push_back({line.color, line.name, Number(value), kText});
    }
    if (options.net && lines.size() == 2) {
        double net = lines[0].ys[i] - lines[1].ys[i];
        rows.push_back({ImVec4(), "Net", Signed(net), net >= 0 ? kGood : kBad});
    }
    if (options.total) rows.push_back({ImVec4(), "Together", Number(sum), kText});
    ChartTip(Date(first + i), rows);
}

// the lines by name in the card's top right corner, left out when the title leaves no room.
// Returns where the room for the title ends
float Legend(const Box& b, float left, const std::vector<Line>& lines, const std::vector<std::string>& values) {
    float width = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        width += 16 + Width(g_regular, kSmall, lines[i].name) + 18;
        if (!values.empty()) width += 6 + Width(g_semibold, kSmall, values[i]);
    }
    float start = b.Right() - kPad - width + 18;
    if (start < left + 16) return b.Right() - kPad;
    float x = start;
    for (size_t i = 0; i < lines.size(); ++i) {
        Dot(x + 5, b.y + 26, lines[i].color);
        x += 16 + Text(g_regular, kSmall, x + 16, b.y + 16, kMuted, lines[i].name);
        if (!values.empty()) x += 6 + Text(g_semibold, kSmall, x + 6, b.y + 16, kText, values[i]);
        x += 18;
    }
    return start - 8;
}

void ChartCard(const char* id, const Box& b, const std::string& title, const std::string& note,
               const std::vector<Line>& lines, int first, const ChartOptions& options = {},
               const std::vector<std::string>& values = {}) {
    Card(b);
    float x = b.x + kPad;
    if (options.sprite) {
        GameIcon(options.sprite, x, b.y + 11, 28);
        x += 38;
    }
    x += Text(g_semibold, kHeading, x, b.y + 13, kText, title);
    float room = b.Right() - kPad;
    if (lines.size() > 1 || !values.empty()) room = Legend(b, x, lines, values);
    if (!note.empty() && x + 12 + Width(g_regular, kSmall, note) < room) {
        Text(g_regular, kSmall, x + 12, b.y + 16, kFaint, note);
    }
    ImGui::PushID(id);
    Plot(Box{b.x + 8, b.y + 48, b.w - 16, b.h - 56}, lines, first, options);
    ImGui::PopID();
}

// a small line of a stock over the chosen years
void Sparkline(const Box& b, const std::vector<float>& ys, const ImVec4& color) {
    int n = static_cast<int>(ys.size());
    int points = std::min(n, static_cast<int>(b.w / 2));
    if (points < 2) return;
    float high = *std::max_element(ys.begin(), ys.end());
    std::vector<ImVec2> path(points);
    for (int p = 0; p < points; ++p) {
        int from = static_cast<int>(static_cast<long long>(p) * n / points);
        int to = std::max(from + 1, static_cast<int>(static_cast<long long>(p + 1) * n / points));
        double sum = 0;
        for (int i = from; i < to; ++i) sum += ys[i];
        float share = high > 0 ? static_cast<float>(sum / (to - from)) / high : 0;
        path[p] = ImVec2(b.x + b.w * p / (points - 1), b.Bottom() - 1 - (b.h - 2) * share);
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // without the fringe of anti-aliasing, or the strips of the fill show as stripes
    ImDrawListFlags flags = draw->Flags;
    draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (int p = 0; p + 1 < points; ++p) {
        draw->AddQuadFilled(path[p], path[p + 1], ImVec2(path[p + 1].x, b.Bottom()), ImVec2(path[p].x, b.Bottom()),
                            U32(color, 0.13f));
    }
    draw->Flags = flags;
    draw->AddPolyline(path.data(), points, U32(color), ImDrawFlags_None, 1.8f);
}

// what a season of the year brings and takes, averaged over the chosen years
void SeasonBars(const Box& b, const Goods& g, int first, const char* madeName, const char* usedName) {
    double made[4] = {}, used[4] = {};
    int counted[4] = {};
    for (int i = first; i < Samples(); ++i) {
        int season = (i % kPerYear) / kPerSeason;
        made[season] += g_records.series[g.made][i];
        used[season] += g_records.series[g.used][i];
        ++counted[season];
    }
    double high = 1;
    for (int s = 0; s < 4; ++s) {
        made[s] = counted[s] ? made[s] * kPerSeason / counted[s] : 0;
        used[s] = counted[s] ? used[s] * kPerSeason / counted[s] : 0;
        high = std::max({high, made[s], used[s]});
    }
    int years = std::max(1, (Samples() - first + kPerYear / 2) / kPerYear);
    Card(b);
    float x = b.x + kPad;
    x += Text(g_semibold, kHeading, x, b.y + 13, kText, "By season");
    std::vector<Line> names = {{madeName, kMade, {}}, {usedName, kUsed, {}}};
    float room = Legend(b, x, names, {});
    std::string note = years == 1 ? "the last year" : "average of " + std::to_string(years) + " years";
    if (x + 12 + Width(g_regular, kSmall, note) < room) {
        Text(g_regular, kSmall, x + 12, b.y + 16, kFaint, note);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    Box area{b.x + kPad, b.y + 60, b.w - 2 * kPad, b.h - 60 - 44};
    float group = area.w / 4;
    float bar = std::min(44.0f, group * 0.3f);
    draw->AddLine(ImVec2(area.x, area.Bottom()), ImVec2(area.Right(), area.Bottom()), U32(kBorder));
    for (int s = 0; s < 4; ++s) {
        float middle = area.x + group * (s + 0.5f);
        const double values[2] = {made[s], used[s]};
        for (int k = 0; k < 2; ++k) {
            float left = middle + (k == 0 ? -bar - 3 : 3);
            float height = std::max(2.0f, (area.h - 24) * static_cast<float>(values[k] / high));
            draw->AddRectFilled(ImVec2(left, area.Bottom() - height), ImVec2(left + bar, area.Bottom()),
                                U32(k == 0 ? kMade : kUsed), 5, ImDrawFlags_RoundCornersTop);
            Text(g_regular, kSmall, left + bar / 2, area.Bottom() - height - 22, kMuted, Compact(values[k]),
                 Align::Center);
        }
        Text(g_regular, kSmall, middle, area.Bottom() + 12, kMuted, kSeasons[s], Align::Center);
    }
}

// controls

float SegmentedWidth(const char* const* names, int count) {
    float width = 8;
    for (int i = 0; i < count; ++i) width += Width(g_semibold, kSmall, names[i]) + 28;
    return width;
}

void Segmented(const char* id, float x, float y, const char* const* names, int count, int* value) {
    constexpr float kHeight = 38;
    Box all{x, y, SegmentedWidth(names, count), kHeight};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(all.Min(), all.Max(), U32(kCard), 10);
    draw->AddRect(all.Min(), all.Max(), U32(kBorder), 10);
    ImGui::PushID(id);
    float left = x + 4;
    for (int i = 0; i < count; ++i) {
        Box item{left, y + 4, Width(g_semibold, kSmall, names[i]) + 28, kHeight - 8};
        ImGui::PushID(i);
        bool hovered = false;
        if (Button("seg", item, &hovered) && *value != i) {
            *value = i;
            SaveSettings();
        }
        ImGui::PopID();
        bool on = *value == i;
        if (on) draw->AddRectFilled(item.Min(), item.Max(), U32(kAccent), 7);
        else if (hovered) draw->AddRectFilled(item.Min(), item.Max(), U32(kCardHi), 7);
        Text(on ? g_semibold : g_regular, kSmall, item.x + item.w / 2, item.y + 5,
             on ? kOnAccent : hovered ? kText : kMuted, names[i], Align::Center);
        left += item.w;
    }
    ImGui::PopID();
}

constexpr float kSwitchWidth = 44;

float SwitchWidth(const char* label) {
    return Width(g_regular, kSmall, label) + 10 + kSwitchWidth;
}

bool Switch(const char* label, float x, float y, bool* value) {
    Box all{x, y, SwitchWidth(label), 38};
    bool hovered = false;
    if (Button(label, all, &hovered)) {
        *value = !*value;
        SaveSettings();
    }
    Text(g_regular, kSmall, x, y + 9, hovered ? kText : kMuted, label);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    Box track{all.Right() - kSwitchWidth, y + 7, kSwitchWidth, 24};
    draw->AddRectFilled(track.Min(), track.Max(), U32(*value ? kAccent : kTrack), 12);
    float knob = *value ? track.Right() - 12 : track.x + 12;
    draw->AddCircleFilled(ImVec2(knob, track.y + 12), 8.5f, U32(*value ? kOnAccent : kMuted), 24);
    return hovered;
}

// pages

ImVec4 SignColor(double value) {
    return value >= 0.5 ? kGood : value <= -0.5 ? kBad : kMuted;
}

ImVec4 LevelColor(double percent) {
    return percent >= 70 ? kGood : percent >= 40 ? kWarn : kBad;
}

void RunsOut(const Flow& f, std::string* text, ImVec4* color) {
    if (f.stock < 0.5 && f.used > 0.01) {
        *text = "Empty";
        *color = kBad;
    } else if (f.runsOut >= 0) {
        *text = Duration(f.runsOut);
        *color = f.runsOut < 12 ? kBad : f.runsOut < 36 ? kWarn : kMuted;
    } else {
        *text = kDash;
        *color = kFaint;
    }
}

void TileHead(const Box& b, const char* label, const char* icon, const ImVec4& tint) {
    Card(b);
    Text(g_regular, kSmall, b.x + kPad, b.y + 14, kMuted, label);
    Text(g_regular, 18, b.Right() - kPad, b.y + 13, tint, icon, Align::Right);
}

void PopulationTile(const Box& b) {
    TileHead(b, "Population", ICON_USERS, kBlue);
    float x = b.x + kPad, inner = b.w - 2 * kPad;
    float people = Latest(0);
    double change = people - Ago(0, kPerYear);
    float end = x + Text(g_semibold, kValue, x, b.y + 35, kText, Number(people));
    Text(g_regular, kSmall, end + 12, b.y + 50, SignColor(change), Signed(change) + " in a year", Align::Left,
         b.Right() - kPad - end - 12);
    const int series[3] = {1, 2, 3};
    const ImVec4 colors[3] = {kBlue, kPink, kGood};
    const char* const names[3] = {"adults", "students", "children"};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float left = x;
    for (int i = 0; i < 3; ++i) {
        float share = people > 0 ? Latest(series[i]) / people : 0;
        float width = std::max(0.0f, (inner - 6) * share);
        draw->AddRectFilled(ImVec2(left, b.y + 82), ImVec2(left + width, b.y + 90), U32(colors[i]), 4);
        left += width + 3;
        float column = x + inner * i / 3;
        Dot(column + 5, b.y + 111, colors[i]);
        Text(g_regular, kSmall, column + 15, b.y + 101, kMuted, Number(Latest(series[i])) + " " + names[i], Align::Left,
             inner / 3 - 17);
    }
}

void ReserveTile(const Box& b, const Flow& food) {
    TileHead(b, "Food lasts", ICON_HOURGLASS, kAccent);
    float x = b.x + kPad, inner = b.w - 2 * kPad;
    ImVec4 color = food.reserve < 0 ? kText : food.reserve < 6 ? kBad : food.reserve < 12 ? kWarn : kText;
    Text(g_semibold, kValue, x, b.y + 35, color, food.reserve < 0 ? std::string(kDash) : Duration(food.reserve));
    Text(g_regular, kSmall, x, b.y + 79, kMuted, Number(food.stock) + " in stock", Align::Left, inner);
    Text(g_regular, kSmall, x, b.y + 101, kFaint, "if nothing more is harvested", Align::Left, inner);
}

void BalanceTile(const Box& b, const Flow& food) {
    TileHead(b, "Food a season", ICON_SCALE, kAccent);
    float x = b.x + kPad, inner = b.w - 2 * kPad;
    float end = x + Text(g_semibold, kValue, x, b.y + 35, SignColor(food.net), Signed(food.net));
    Text(g_regular, kSmall, end + 12, b.y + 50, kMuted, food.net >= 0 ? "more than eaten" : "less than eaten",
         Align::Left, b.Right() - kPad - end - 12);
    double high = std::max({food.made, food.used, 1.0});
    const char* const names[2] = {"Harvested", "Eaten"};
    const double values[2] = {food.made, food.used};
    const ImVec4 colors[2] = {kMade, kUsed};
    for (int i = 0; i < 2; ++i) {
        float y = b.y + 79 + 22.0f * i;
        Text(g_regular, kSmall, x, y, kMuted, names[i]);
        Text(g_semibold, kSmall, x + inner, y, kText, Number(values[i]), Align::Right);
        Meter(Box{x + 82, y + 7, inner - 82 - 62, 8}, values[i] / high, colors[i]);
    }
}

void WellbeingTile(const Box& b) {
    TileHead(b, "Wellbeing", ICON_HEART_PULSE, kPink);
    const char* const names[4] = {"Health", "Happiness", "Education", "Clothed"};
    const int series[4] = {4, 6, 5, 7};
    float inner = b.w - 2 * kPad;
    float width = (inner - 22) / 2;
    for (int i = 0; i < 4; ++i) {
        float x = b.x + kPad + (width + 22) * (i % 2);
        float y = b.y + 44 + 42.0f * (i / 2);
        float value = Latest(series[i]);
        Text(g_regular, kSmall, x, y, kMuted, names[i]);
        Text(g_semibold, kSmall, x + width, y, kText, Number(value) + "%", Align::Right);
        Meter(Box{x, y + 24, width, 7}, value / 100, LevelColor(value));
    }
}

void OpenGoods(int index) {
    if (index == 0) {
        g_settings.page = kFood;
    } else {
        g_settings.page = kGoodsPage;
        g_settings.goods = index;
    }
    SaveSettings();
}

// every kind of goods on a row: what is there, where it is heading and when it is gone
void GoodsTable(const Box& b, int first) {
    Card(b);
    float end = b.x + kPad + Text(g_semibold, kHeading, b.x + kPad, b.y + 13, kText, "Goods");
    Text(g_regular, kSmall, end + 12, b.y + 16, kFaint, "made, used and net are a season, averaged over the last year",
         Align::Left, b.Right() - kPad - end - 12);

    float left = b.x + kPad, right = b.Right() - kPad;
    float room = right - left - 150;
    bool trend = room >= 600;
    float shares[6] = {0.15f, trend ? 0.24f : 0, 0.14f, 0.14f, 0.16f, 0.17f};
    float total = 0;
    for (float s : shares) total += s;
    float edges[6];  // the right end of every column after the name
    float at = left + 150;
    for (int c = 0; c < 6; ++c) {
        at += room * shares[c] / total;
        edges[c] = at;
    }
    float head = b.y + 52;
    char span[32];
    if (kRangeYears[g_settings.range] == 0) snprintf(span, sizeof(span), "All years");
    else if (kRangeYears[g_settings.range] == 1) snprintf(span, sizeof(span), "Last year");
    else snprintf(span, sizeof(span), "Last %d years", kRangeYears[g_settings.range]);
    const char* const heads[6] = {"In stock", span, "Made", "Used", "Net", "Runs out in"};
    Text(g_regular, kSmall, left, head, kFaint, "Kind");
    for (int c = 0; c < 6; ++c) {
        if (c == 1 && !trend) continue;
        if (c == 1) Text(g_regular, kSmall, edges[0] + 32, head, kFaint, heads[c]);
        else Text(g_regular, kSmall, edges[c], head, kFaint, heads[c], Align::Right);
    }

    float top = head + 28;
    float rowHeight = std::clamp((b.Bottom() - 8 - top) / kGoodsCount, 36.0f, 50.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (int i = 0; i < kGoodsCount; ++i) {
        const Goods& g = kGoods[i];
        Flow f = FlowOf(g);
        Box row{b.x + 6, top + rowHeight * i, b.w - 12, rowHeight};
        ImGui::PushID(i);
        bool hovered = false;
        if (Button("row", row, &hovered)) OpenGoods(i);
        ImGui::PopID();
        if (hovered) draw->AddRectFilled(row.Min(), row.Max(), U32(kCardHi), 8);
        else draw->AddLine(ImVec2(left, row.y), ImVec2(right, row.y), U32(kBorder, 0.6f));
        float y = row.y + (rowHeight - 22) / 2;
        GameIcon(g.sprite, left, row.y + (rowHeight - 28) / 2, 28);
        Text(g_semibold, kBody, left + 40, y - 1, kText, g.name);
        Text(g_semibold, kBody, edges[0], y - 1, kText, Number(f.stock), Align::Right);
        if (trend) {
            Sparkline(Box{edges[0] + 32, row.y + 8, edges[1] - edges[0] - 48, rowHeight - 16},
                      StockSeries(g.stock, first), g.color);
        }
        Text(g_regular, kBody, edges[2], y - 1, f.made > 0.5 ? kText : kFaint, Number(f.made), Align::Right);
        Text(g_regular, kBody, edges[3], y - 1, f.used > 0.5 ? kText : kFaint, Number(f.used), Align::Right);
        float netWidth = Text(g_semibold, kBody, edges[4], y - 1, SignColor(f.net), Signed(f.net), Align::Right);
        if (std::fabs(f.net) >= 0.5) {
            Text(g_regular, kSmall, edges[4] - netWidth - 6, y + 1, SignColor(f.net),
                 f.net > 0 ? ICON_TRENDING_UP : ICON_TRENDING_DOWN, Align::Right);
        }
        std::string runsOut;
        ImVec4 color;
        RunsOut(f, &runsOut, &color);
        Text(g_regular, kBody, edges[5], y - 1, color, runsOut, Align::Right);
    }
}

float OverviewPage(float x, float y, float w, float h) {
    int first = FirstSample();
    Flow food = FlowOf(kGoods[0]);
    int columns = w >= 1260 ? 4 : w >= 620 ? 2 : 1;
    PopulationTile(Cell(x, y, w, columns, 0, kOverviewTile));
    ReserveTile(Cell(x, y, w, columns, 1, kOverviewTile), food);
    BalanceTile(Cell(x, y, w, columns, 2, kOverviewTile), food);
    WellbeingTile(Cell(x, y, w, columns, 3, kOverviewTile));
    float used = GridHeight(columns, 4, kOverviewTile) + kGap;

    float tableHeight = std::max(h - used, 88.0f + 38 * kGoodsCount);
    float side = w >= 1180 ? std::clamp(w * 0.32f, 380.0f, 520.0f) : 0;
    GoodsTable(Box{x, y + used, side > 0 ? w - side - kGap : w, tableHeight}, first);
    if (side > 0) {
        float half = (tableHeight - kGap) / 2;
        float sx = x + w - side;
        ChartCard("people", Box{sx, y + used, side, half}, "Population", "",
                  {{"Population", kBlue, StockSeries(0, first), true}}, first);
        ChartCard("food", Box{sx, y + used + half + kGap, side, half}, "Food in stock", "",
                  {{"Food", kGoods[0].color, StockSeries(kGoods[0].stock, first), true}}, first, {});
    }
    return used + tableHeight;
}

// the numbers and charts of one kind of goods, which is what the Food page is made of too
float GoodsDetail(const Goods& g, bool food, float x, float y, float w, float h) {
    int first = FirstSample();
    Flow f = FlowOf(g);
    double yearAgo = f.stock - Ago(g.stock, kPerYear);
    const char* madeName = food ? "Harvested" : "Made";
    const char* usedName = food ? "Eaten" : "Used";
    std::string runsOut;
    ImVec4 runsOutColor;
    RunsOut(f, &runsOut, &runsOutColor);

    std::vector<Kpi> kpis;
    kpis.push_back({ICON_WAREHOUSE, g.color, "In stock", Number(f.stock), kText, Signed(yearAgo) + " in a year",
                    SignColor(yearAgo)});
    kpis.push_back({ICON_SPROUT, kMade, std::string(madeName) + " a season", Number(f.made), kText,
                    "last year's average", kFaint});
    if (food) {
        double people = LastYear(0, true);
        double each = people > 0 ? f.used / people : 0;
        char note[64];
        snprintf(note, sizeof(note), "%.1f a person", each);
        kpis.push_back({ICON_UTENSILS, kUsed, "Eaten a season", Number(f.used), kText, note, kFaint});
        kpis.push_back({ICON_USERS, kBlue, "Needed a season", Number(each * Latest(0)), kText,
                        "by " + Number(Latest(0)) + " people now", kFaint});
    } else {
        kpis.push_back({ICON_PACKAGE, kUsed, "Used a season", Number(f.used), kText, "last year's average", kFaint});
    }
    std::string heading = f.runsOut >= 0 ? "gone in " + runsOut : "not shrinking";
    if (f.stock < 0.5 && f.used > 0.01) heading = "none left";
    kpis.push_back({ICON_SCALE, kAccent, "Net a season", Signed(f.net), SignColor(f.net), heading,
                    f.runsOut >= 0 ? runsOutColor : kFaint});
    kpis.push_back({ICON_HOURGLASS, kAccent, "Stock lasts", f.reserve < 0 ? std::string(kDash) : Duration(f.reserve),
                    kText, food ? "if nothing is harvested" : "if none is made", kFaint});
    float used = KpiRow(x, y, w, kpis) + kGap;

    float chart = std::max(kChartMin, (h - used - kGap) / 2);
    float bars = w >= 820 ? std::clamp(w * 0.36f, 330.0f, 520.0f) : 0;
    ChartCard("flow", Box{x, y + used, bars > 0 ? w - bars - kGap : w, chart},
              std::string(madeName) + " and " + (food ? "eaten" : "used"),
              g_settings.smooth ? "a season, averaged over a year" : "in the season before",
              {{madeName, kMade, SeasonSeries(g.made, first), true}, {usedName, kUsed, SeasonSeries(g.used, first)}},
              first, {true});
    if (bars > 0) SeasonBars(Box{x + w - bars, y + used, bars, chart}, g, first, madeName, usedName);
    ChartCard("stock", Box{x, y + used + chart + kGap, w, chart}, "In stock", "",
              {{"In stock", g.color, StockSeries(g.stock, first), true}}, first);
    return used + chart * 2 + kGap;
}

float GoodsPage(float x, float y, float w, float h) {
    constexpr float kList = 232;
    constexpr float kRow = 46;
    int chosen = std::clamp(g_settings.goods, 1, kGoodsCount - 1);
    Box list{x, y, kList, 16 + kRow * (kGoodsCount - 1)};
    Card(list);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (int i = 1; i < kGoodsCount; ++i) {
        const Goods& g = kGoods[i];
        Box row{x + 8, y + 8 + kRow * (i - 1), kList - 16, kRow};
        ImGui::PushID(i);
        bool hovered = false;
        if (Button("pick", row, &hovered)) {
            g_settings.goods = i;
            SaveSettings();
        }
        ImGui::PopID();
        bool on = chosen == i;
        if (on || hovered) {
            draw->AddRectFilled(ImVec2(row.x, row.y + 2), ImVec2(row.Right(), row.Bottom() - 2), U32(kCardHi), 8);
        }
        if (on) draw->AddRectFilled(ImVec2(row.x, row.y + 12), ImVec2(row.x + 3, row.Bottom() - 12), U32(kAccent), 2);
        GameIcon(g.sprite, row.x + 12, row.y + 9, 28);
        Text(on ? g_semibold : g_regular, kBody, row.x + 50, row.y + 11, on || hovered ? kText : kMuted, g.name);
        Text(g_regular, kSmall, row.Right() - 12, row.y + 13, on ? kMuted : kFaint, Compact(Latest(g.stock)),
             Align::Right);
    }
    float used = GoodsDetail(kGoods[chosen], false, x + kList + kGap, y, w - kList - kGap, h);
    return std::max(used, list.h);
}

void WellbeingCard(const Box& b) {
    Card(b);
    Text(g_semibold, kHeading, b.x + kPad, b.y + 13, kText, "Wellbeing");
    Text(g_regular, kSmall, b.Right() - kPad, b.y + 16, kFaint, "now", Align::Right);
    const char* const names[4] = {"Health", "Happiness", "Education", "Clothed"};
    const char* const icons[4] = {ICON_HEART_PULSE, ICON_SMILE, ICON_BOOK_OPEN, ICON_SHIRT};
    const int series[4] = {4, 6, 5, 7};
    float room = (b.h - 56 - 10) / 4;
    for (int i = 0; i < 4; ++i) {
        float y = b.y + 56 + room * i + (room - 40) / 2;
        float value = Latest(series[i]);
        double change = value - Ago(series[i], kPerYear);
        ImVec4 color = LevelColor(value);
        Text(g_regular, 18, b.x + kPad, y, color, icons[i]);
        Text(g_regular, kBody, b.x + kPad + 30, y - 1, kText, names[i]);
        float end = b.Right() - kPad;
        end -= Text(g_semibold, kBody, end, y - 1, kText, Number(value) + "%", Align::Right);
        if (std::fabs(change) >= 0.5) {
            Text(g_regular, kSmall, end - 10, y + 1, SignColor(change), Signed(change) + " in a year", Align::Right);
        }
        Meter(Box{b.x + kPad, y + 29, b.w - 2 * kPad, 8}, value / 100, color);
    }
}

float PeoplePage(float x, float y, float w, float h) {
    int first = FirstSample();
    auto kpi = [](const char* icon, const ImVec4& tint, const char* label, int s) {
        double change = Latest(s) - Ago(s, kPerYear);
        return Kpi{icon, tint, label, Number(Latest(s)), kText, Signed(change) + " in a year", SignColor(change)};
    };
    std::vector<Kpi> kpis = {kpi(ICON_USERS, kText, "Population", 0), kpi(ICON_USER, kBlue, "Adults", 1),
                             kpi(ICON_GRADUATION_CAP, kPink, "Students", 2), kpi(ICON_BABY, kGood, "Children", 3)};
    float used = KpiRow(x, y, w, kpis) + kGap;

    float chart = std::max(kChartMin, (h - used - kGap) / 2);
    std::vector<float> adults = StockSeries(1, first);
    std::vector<float> students = StockSeries(2, first);
    std::vector<float> children = StockSeries(3, first);
    for (size_t i = 0; i < adults.size(); ++i) {
        students[i] += adults[i];
        children[i] += students[i];
    }
    float side = w >= 820 ? std::clamp(w * 0.32f, 320.0f, 460.0f) : 0;
    ChartOptions stacked;
    stacked.total = true;
    ChartCard("population", Box{x, y + used, side > 0 ? w - side - kGap : w, chart}, "Who lives here", "",
              {{"Adults", kBlue, adults, true, std::vector<float>(adults.size(), 0.0f)},
               {"Students", kPink, students, true, adults},
               {"Children", kGood, children, true, students}},
              first, stacked);
    if (side > 0) WellbeingCard(Box{x + w - side, y + used, side, chart});
    ChartOptions percent;
    percent.top = 100;
    ChartCard("wellbeing", Box{x, y + used + chart + kGap, w, chart}, "Wellbeing over the years", "percent",
              {{"Health", kBad, StockSeries(4, first)},
               {"Happiness", kAccent, StockSeries(6, first)},
               {"Education", kBlue, StockSeries(5, first)},
               {"Clothed", kViolet, StockSeries(7, first)}},
              first, percent);
    return used + chart * 2 + kGap;
}

// a chart for every kind of goods the player ticked
float ChartsPage(float x, float y, float w, float h) {
    int first = FirstSample();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float cx = x, cy = y;
    for (int i = 0; i < kGoodsCount; ++i) {
        const Goods& g = kGoods[i];
        bool on = (g_settings.chartGoods >> i) & 1;
        Box chip{cx, cy, 14 + 26 + 8 + Width(g_semibold, kSmall, g.name) + 16, 40};
        if (chip.Right() > x + w) {
            chip.x = cx = x;
            chip.y = cy += 40 + 10;
        }
        ImGui::PushID(i);
        bool hovered = false;
        if (Button("chip", chip, &hovered)) {
            g_settings.chartGoods ^= 1 << i;
            SaveSettings();
        }
        ImGui::PopID();
        draw->AddRectFilled(chip.Min(), chip.Max(), on ? U32(g.color, 0.16f) : U32(hovered ? kCardHi : kCard), 20);
        draw->AddRect(chip.Min(), chip.Max(), on ? U32(g.color, 0.85f) : U32(kBorder), 20, 0, on ? 1.5f : 1.0f);
        GameIcon(g.sprite, chip.x + 14, chip.y + 7, 26);
        Text(on ? g_semibold : g_regular, kSmall, chip.x + 48, chip.y + 10, on || hovered ? kText : kMuted, g.name);
        cx = chip.Right() + 10;
    }
    float used = cy + 40 + kGap - y;

    std::vector<int> chosen;
    for (int i = 0; i < kGoodsCount; ++i) {
        if ((g_settings.chartGoods >> i) & 1) chosen.push_back(i);
    }
    int count = static_cast<int>(chosen.size());
    if (count == 0) {
        Box empty{x, y + used, w, std::max(h - used, 200.0f)};
        Card(empty);
        Text(g_regular, kBody, x + w / 2, empty.y + empty.h / 2 - 12, kMuted, "Pick one or more kinds of goods above.",
             Align::Center);
        return used + empty.h;
    }
    int columns = count <= 2 ? 1 : count <= 4 ? 2 : count <= 9 ? 3 : 4;
    columns = std::min(columns, std::max(1, static_cast<int>((w + kGap) / (330 + kGap))));
    int rows = (count + columns - 1) / columns;
    float height = std::max(200.0f, (h - used - kGap * (rows - 1)) / rows);
    for (int n = 0; n < count; ++n) {
        const Goods& g = kGoods[chosen[n]];
        Box b = Cell(x, y + used, w, columns, n, height);
        ChartOptions options;
        options.sprite = g.sprite;
        std::vector<Line> lines;
        std::vector<std::string> values;
        if (g_settings.chartShow == 0) {
            options.net = true;
            lines = {{"Made", kMade, SeasonSeries(g.made, first), true}, {"Used", kUsed, SeasonSeries(g.used, first)}};
            values = {Compact(lines[0].ys.back()), Compact(lines[1].ys.back())};
        } else if (g_settings.chartShow == 1) {
            lines = {{"In stock", g.color, StockSeries(g.stock, first), true}};
            values = {Compact(Latest(g.stock))};
        } else {
            std::vector<float> net = SeasonSeries(g.made, first), taken = SeasonSeries(g.used, first);
            for (size_t i = 0; i < net.size(); ++i) net[i] -= taken[i];
            lines = {{"Net a season", g.color, net, true}};
            values = {Signed(net.back())};
        }
        ImGui::PushID(chosen[n]);
        ChartCard("chart", b, g.name, "", lines, first, options, values);
        ImGui::PopID();
    }
    return used + GridHeight(columns, count, height);
}

// the title of the page with the years to show and the smoothing on its right
float PageHead(float x, float y, float w) {
    const char* smooth = "Smooth seasons";
    float right = x + w;
    float controls = SwitchWidth(smooth) + 20 + SegmentedWidth(kRangeNames, kRangeCount);
    if (g_settings.page == kCharts) controls += 14 + SegmentedWidth(kShowNames, 3);
    bool below = w < controls + 180;
    float cy = below ? y + 48 : y + 2;
    float cx = below ? x : right - controls;
    if (g_settings.page == kCharts) {
        Segmented("show", cx, cy, kShowNames, 3, &g_settings.chartShow);
        cx += SegmentedWidth(kShowNames, 3) + 14;
    }
    Segmented("range", cx, cy, kRangeNames, kRangeCount, &g_settings.range);
    cx += SegmentedWidth(kRangeNames, kRangeCount) + 20;
    if (Switch(smooth, cx, cy, &g_settings.smooth)) {
        BeginTip();
        ImGui::PushTextWrapPos(330);
        ImGui::TextColored(kText, "On: what is made and used is the average of the year before, so one harvest "
                                  "does not make a spike.");
        ImGui::TextColored(kMuted, "Off: the season before, as the game counted it.");
        ImGui::PopTextWrapPos();
        EndTip();
    }

    const char* title = g_settings.page == kGoodsPage ? kGoods[std::clamp(g_settings.goods, 1, kGoodsCount - 1)].name
                                                      : kPages[g_settings.page];
    float room = below ? w : w - controls - 24;
    float end = x + Text(g_semibold, kTitle, x, y, kText, title);
    if (room - (end - x) - 14 > 160) {
        Text(g_regular, kSmall, end + 14, y + 13, kMuted, kPageNotes[g_settings.page], Align::Left,
             room - (end - x) - 14);
    }
    return below ? 100.0f : 56.0f;
}

void Header(int samples) {
    constexpr float kHeight = 64;
    ImVec2 pos = ImGui::GetWindowPos();
    float width = ImGui::GetWindowWidth();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(pos.x, pos.y + kHeight), ImVec2(pos.x + width, pos.y + kHeight), U32(kBorder));
    Text(g_regular, 24, pos.x + 24, pos.y + 19, kAccent, ICON_CHART_SPLINE);
    Text(g_semibold, 21, pos.x + 58, pos.y + 17, kText, "Town Stats");

    float x = pos.x + 196;
    for (int p = 0; p < kPageCount; ++p) {
        Box tab{x, pos.y + 12, 16 + 20 + 9 + Width(g_semibold, kBody, kPages[p]) + 18, 40};
        bool hovered = false;
        if (Button(kPages[p], tab, &hovered) && g_settings.page != p) {
            g_settings.page = p;
            SaveSettings();
        }
        bool on = g_settings.page == p;
        if (on || hovered) draw->AddRectFilled(tab.Min(), tab.Max(), U32(on ? kCardHi : kCard), 10);
        Text(g_regular, 19, tab.x + 16, tab.y + 10, on ? kAccent : hovered ? kText : kMuted, kPageIcons[p]);
        Text(on ? g_semibold : g_regular, kBody, tab.x + 45, tab.y + 8, on || hovered ? kText : kMuted, kPages[p]);
        x = tab.Right() + 6;
    }

    Box close{pos.x + width - 18 - 38, pos.y + 13, 38, 38};
    bool hovered = false;
    if (Button("close", close, &hovered)) g_open = false;
    if (hovered) draw->AddRectFilled(close.Min(), close.Max(), U32(kBad, 0.22f), 10);
    Text(g_regular, 20, close.x + 19, close.y + 9, hovered ? kText : kMuted, ICON_X, Align::Center);
    if (samples > 0) {
        std::string date = Date(samples - 1);
        if (close.x - 18 - Width(g_regular, kSmall, date) > x + 12) {
            Text(g_regular, kSmall, close.x - 18, pos.y + 22, kMuted, date, Align::Right);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + kHeight + 1));
}

void Content(int samples) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::BeginChild("content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 room = ImGui::GetContentRegionAvail();
    float used = 0;
    if (samples < 2) {
        Text(g_semibold, kTitle, origin.x + room.x / 2, origin.y + room.y / 2 - 40, kText, "No records yet",
             Align::Center);
        Text(g_regular, kBody, origin.x + room.x / 2, origin.y + room.y / 2, kMuted,
             "The town writes them down a few weeks into a new game.", Align::Center);
    } else {
        used = PageHead(origin.x, origin.y, room.x);
        float y = origin.y + used, h = room.y - used;
        if (g_settings.page == kOverview) used += OverviewPage(origin.x, y, room.x, h);
        else if (g_settings.page == kFood) used += GoodsDetail(kGoods[0], true, origin.x, y, room.x, h);
        else if (g_settings.page == kGoodsPage) used += GoodsPage(origin.x, y, room.x, h);
        else if (g_settings.page == kPeople) used += PeoplePage(origin.x, y, room.x, h);
        else used += ChartsPage(origin.x, y, room.x, h);
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(room.x, used));
    ImGui::EndChild();
}

}  // namespace

void UiSetup() {
    ImGuiIO& io = ImGui::GetIO();
    char windows[MAX_PATH];
    GetWindowsDirectoryA(windows, MAX_PATH);
    std::string fonts = std::string(windows) + "\\Fonts\\";
    ImFontConfig icons;
    icons.MergeMode = true;
    icons.FontDataOwnedByAtlas = false;
    icons.GlyphOffset = ImVec2(0, 2);
    static const ImWchar range[] = {kIconFirst, kIconLast, 0};
    auto load = [&](const char* file) {
        ImFont* font = io.Fonts->AddFontFromFileTTF((fonts + file).c_str(), kBody);
        if (!font) font = io.Fonts->AddFontDefault();
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kIconFont), sizeof(kIconFont), kBody, &icons, range);
        return font;
    };
    g_regular = load("segoeui.ttf");
    g_semibold = load("seguisb.ttf");
    io.FontDefault = g_regular;

    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = kSmall;
    style.WindowRounding = 16;
    style.ChildRounding = 0;
    style.PopupRounding = 10;
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 0;
    style.PopupBorderSize = 1;
    style.ItemSpacing = ImVec2(8, 6);
    style.ScrollbarSize = 12;
    style.ScrollbarRounding = 6;
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kFaint;
    c[ImGuiCol_WindowBg] = kWindow;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = Hex(0x262a30, 0.985f);
    c[ImGuiCol_Border] = Hex(0x444a53);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = kCardHi;
    c[ImGuiCol_ScrollbarGrabHovered] = kBorder;
    c[ImGuiCol_ScrollbarGrabActive] = kFaint;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.4f);
    c[ImGuiCol_ResizeGripActive] = kAccent;

    ImPlotStyle& plot = ImPlot::GetStyle();
    plot.PlotPadding = ImVec2(10, 8);
    plot.LabelPadding = ImVec2(8, 6);
    plot.PlotBorderSize = 0;
    plot.Colors[ImPlotCol_PlotBg] = ImVec4(0, 0, 0, 0);
    plot.Colors[ImPlotCol_PlotBorder] = ImVec4(0, 0, 0, 0);
    plot.Colors[ImPlotCol_FrameBg] = ImVec4(0, 0, 0, 0);
    plot.Colors[ImPlotCol_AxisBg] = ImVec4(0, 0, 0, 0);
    plot.Colors[ImPlotCol_AxisGrid] = ImVec4(kText.x, kText.y, kText.z, 0.09f);
    plot.Colors[ImPlotCol_AxisTick] = ImVec4(0, 0, 0, 0);
    plot.Colors[ImPlotCol_AxisText] = kFaint;
}

void UiWindow() {
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImVec2 size(std::min(1440.0f, screen.x - 80), std::min(920.0f, screen.y - 80));
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2((screen.x - size.x) / 2, (screen.y - size.y) / 2), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(1000, 640), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool visible = ImGui::Begin("Town Stats##overlay", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (visible) {
        std::lock_guard<std::mutex> lock(g_records.lock);
        int samples = static_cast<int>(g_records.series.size()) == kSeriesUsed ? Samples() : 0;
        Header(samples);
        Content(samples);
    }
    ImGui::End();
}

}  // namespace townstats
