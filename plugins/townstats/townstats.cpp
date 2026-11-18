// Town Stats: an overlay from the gear menu with the town's numbers and charts of everything the
// game records, back to the first year of the save. Drawn with Dear ImGui and ImPlot on the game's
// own DirectX 11 swap chain. This file hooks the swap chain and keeps the records, ui.cpp draws.
#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "implot.h"
#include "townstats.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT msg, WPARAM wParam, LPARAM lParam);

namespace townstats {

const BmlApi* g_api;
Records g_records;
Settings g_settings;
std::atomic<bool> g_open;

namespace {

constexpr int kPresentSlot = 8;
constexpr int kResizeBuffersSlot = 13;

std::string g_dir;  // NativeMods, with a trailing slash
std::string g_layoutPath;
std::atomic<bool> g_wasOpen;

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* chain, UINT sync, UINT flags);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* chain, UINT count, UINT width, UINT height,
                                            DXGI_FORMAT format, UINT flags);
PresentFn g_present;
ResizeFn g_resize;

struct Overlay {
    bool ready = false;
    bool failed = false;
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* target = nullptr;
    HWND window = nullptr;
    WNDPROC windowProc = nullptr;
} g_overlay;

void LoadSettings() {
    FILE* file = nullptr;
    if (fopen_s(&file, (g_dir + "townstats.ini").c_str(), "r") != 0 || !file) return;
    char line[256];
    int value;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf_s(line, "page = %d", &value) == 1) g_settings.page = std::clamp(value, 0, 4);
        if (sscanf_s(line, "range = %d", &value) == 1) g_settings.range = std::clamp(value, 0, 5);
        if (sscanf_s(line, "smooth = %d", &value) == 1) g_settings.smooth = value != 0;
        if (sscanf_s(line, "goods = %d", &value) == 1) g_settings.goods = std::clamp(value, 1, 10);
        if (sscanf_s(line, "chart_goods = %d", &value) == 1) g_settings.chartGoods = value & 0x7FF;
        if (sscanf_s(line, "chart_show = %d", &value) == 1) g_settings.chartShow = std::clamp(value, 0, 2);
    }
    fclose(file);
}

bool HookSwapChains();

void OnFrame(float, void*) {
    // the swap chain is hooked once the game and the Steam overlay have made theirs. Hooked any
    // earlier, the Steam overlay's own Present hook calls itself until the stack runs out
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        if (!HookSwapChains()) {
            g_api->log_warn("townstats", "could not hook DirectX 11, the overlay needs the DirectX 11 renderer");
        }
    }
    const int* values = nullptr;
    int samples = g_api->history(0, &values);
    if (samples == g_records.samples && ++g_records.frames < 120) return;
    std::lock_guard<std::mutex> lock(g_records.lock);
    g_records.frames = 0;
    g_records.samples = samples;
    g_records.series.clear();
    if (g_api->history_series() < kSeriesUsed) return;
    g_records.series.resize(kSeriesUsed);
    for (int s = 0; s < kSeriesUsed; ++s) {
        int n = g_api->history(s, &values);
        if (!values) continue;
        std::vector<float>& out = g_records.series[s];
        out.resize(std::min(n, samples));
        for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<float>(values[i]);
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_open && g_overlay.ready && ImGui_ImplWin32_WndProcHandler(window, msg, wParam, lParam)) return 1;
    return CallWindowProcW(g_overlay.windowProc, window, msg, wParam, lParam);
}

bool Start(IDXGISwapChain* chain) {
    DXGI_SWAP_CHAIN_DESC desc;
    if (FAILED(chain->GetDesc(&desc)) ||
        FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_overlay.device)))) {
        return false;
    }
    g_overlay.device->GetImmediateContext(&g_overlay.context);
    g_overlay.chain = chain;
    g_overlay.window = desc.OutputWindow;
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = g_layoutPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    UiSetup();
    ImGui_ImplWin32_Init(g_overlay.window);
    ImGui_ImplDX11_Init(g_overlay.device, g_overlay.context);
    g_overlay.windowProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_overlay.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc)));
    g_api->log("townstats", "overlay ready");
    return true;
}

void Draw(IDXGISwapChain* chain) {
    if (!g_open) {
        if (g_wasOpen.exchange(false)) g_api->capture_input(false, false);
        return;
    }
    g_wasOpen = true;
    if (!g_overlay.ready) {
        if (g_overlay.failed) return;
        g_overlay.ready = Start(chain);
        g_overlay.failed = !g_overlay.ready;
        if (g_overlay.failed) {
            g_api->log_warn("townstats", "could not start the overlay on the game's swap chain");
            return;
        }
    }
    if (chain != g_overlay.chain) return;
    if (!g_overlay.target) {
        ID3D11Texture2D* back = nullptr;
        if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return;
        g_overlay.device->CreateRenderTargetView(back, nullptr, &g_overlay.target);
        back->Release();
        if (!g_overlay.target) return;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    UiWindow();
    ImGui::Render();

    ID3D11RenderTargetView* oldTarget = nullptr;
    ID3D11DepthStencilView* oldDepth = nullptr;
    g_overlay.context->OMGetRenderTargets(1, &oldTarget, &oldDepth);
    g_overlay.context->OMSetRenderTargets(1, &g_overlay.target, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_overlay.context->OMSetRenderTargets(1, &oldTarget, oldDepth);
    if (oldTarget) oldTarget->Release();
    if (oldDepth) oldDepth->Release();

    ImGuiIO& io = ImGui::GetIO();
    g_api->capture_input(io.WantCaptureMouse, io.WantTextInput);
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* chain, UINT sync, UINT flags) {
    if (!(flags & DXGI_PRESENT_TEST)) Draw(chain);
    return g_present(chain, sync, flags);
}

HRESULT STDMETHODCALLTYPE HookedResize(IDXGISwapChain* chain, UINT count, UINT width, UINT height,
                                       DXGI_FORMAT format, UINT flags) {
    if (chain == g_overlay.chain && g_overlay.target) {
        g_overlay.target->Release();
        g_overlay.target = nullptr;
    }
    return g_resize(chain, count, width, height, format, flags);
}

bool Patch(void** slot, void* value) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// a throwaway device and swap chain on a hidden window, for the vtable every swap chain shares
bool HookSwapChains() {
    WNDCLASSEXW cls = {sizeof(cls)};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"BmlTownStatsProbe";
    RegisterClassExW(&cls);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 16, 16, nullptr, nullptr,
                                  cls.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    bool hooked = false;
    for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc,
                                                    &chain, &device, nullptr, &context))) {
            break;
        }
    }
    if (chain) {
        void** v = *reinterpret_cast<void***>(chain);
        g_present = reinterpret_cast<PresentFn>(v[kPresentSlot]);
        g_resize = reinterpret_cast<ResizeFn>(v[kResizeBuffersSlot]);
        hooked = Patch(&v[kPresentSlot], reinterpret_cast<void*>(HookedPresent)) &&
                 Patch(&v[kResizeBuffersSlot], reinterpret_cast<void*>(HookedResize));
        chain->Release();
    }
    if (context) context->Release();
    if (device) device->Release();
    if (window) DestroyWindow(window);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    return hooked;
}

void Toggle(void*) {
    g_open = !g_open;
}

std::string PluginDir() {
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<const char*>(&PluginDir), &self);
    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    std::string dir(path);
    return dir.substr(0, dir.find_last_of("\\/") + 1);
}

}  // namespace

void SaveSettings() {
    FILE* file = nullptr;
    if (fopen_s(&file, (g_dir + "townstats.ini").c_str(), "w") != 0 || !file) return;
    fprintf(file, "page = %d\nrange = %d\nsmooth = %d\ngoods = %d\nchart_goods = %d\nchart_show = %d\n",
            g_settings.page, g_settings.range, g_settings.smooth ? 1 : 0, g_settings.goods, g_settings.chartGoods,
            g_settings.chartShow);
    fclose(file);
}

}  // namespace townstats

extern "C" __declspec(dllexport) bool BmlPluginInit(const BmlApi* api) {
    using namespace townstats;
    if (api->version < 6) return false;
    g_api = api;
    g_dir = PluginDir();
    g_layoutPath = g_dir + "townstats-layout.ini";
    LoadSettings();
    if (!api->add_gear_button("Town Stats", "Graphs and numbers", "GraphsTab", Toggle, nullptr)) {
        api->log_warn("townstats", "no gear menu slot left for Town Stats");
        return false;
    }
    return api->on_frame(OnFrame, nullptr);
}

extern "C" __declspec(dllexport) const BmlPluginInfo* BmlPluginAbout() {
    static const BmlPluginInfo info = {
        "Town Stats",
        "A Town Stats button in the gear menu opens an overlay with what your town makes and uses, how "
        "long its food lasts, and charts of everything the game records back to the first year.",
    };
    return &info;
}
