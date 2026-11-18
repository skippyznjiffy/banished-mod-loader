// Runs the Town Stats overlay without the game: the plugin's ui.cpp over a backdrop, with the
// records of a real town from a file and the goods icons from the mod kit's PNGs. Opens a window,
// or with --shot renders one frame offscreen and writes it as a PNG.
#include <windows.h>

#include <d3d11.h>
#include <wincodec.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

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
std::atomic<bool> g_open{true};

void SaveSettings() {}

}  // namespace townstats

namespace {

using namespace townstats;

struct Options {
    std::string records = "records.bin";
    std::string sprites = "E:\\BanishedKit\\resource\\Dialog\\Sprite";
    std::string backdrop;
    std::string shot;
    int width = 1920, height = 1080;
    float windowWidth = 0, windowHeight = 0;
    float mouseX = -1, mouseY = -1;
    bool click = false;
};

ID3D11Device* g_device;
ID3D11DeviceContext* g_context;
IWICImagingFactory* g_wic;
Options g_options;

struct Picture {
    ID3D11ShaderResourceView* view = nullptr;
    int width = 0, height = 0;
};

Picture LoadPicture(const std::wstring& path) {
    Picture picture;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    if (SUCCEEDED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                   &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(g_wic->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeCustom))) {
        UINT width = 0, height = 0;
        converter->GetSize(&width, &height);
        std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
        converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data = {pixels.data(), width * 4, 0};
        ID3D11Texture2D* texture = nullptr;
        if (SUCCEEDED(g_device->CreateTexture2D(&desc, &data, &texture))) {
            g_device->CreateShaderResourceView(texture, nullptr, &picture.view);
            texture->Release();
            picture.width = static_cast<int>(width);
            picture.height = static_cast<int>(height);
        }
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    return picture;
}

// the game finds a sprite by name on its sheet, here it is the PNG of that name in the kit
bool UiSprite(const char* name, BmlSprite* out) {
    struct Known {
        std::string name;
        Picture picture;
    };
    static std::deque<Known> known;
    const Picture* picture = nullptr;
    for (const Known& k : known) {
        if (k.name == name) picture = &k.picture;
    }
    if (!picture) {
        Known& k = known.emplace_back(Known{name, {}});
        std::error_code error;
        std::string file = std::string(name) + ".png";
        for (const auto& entry : std::filesystem::recursive_directory_iterator(g_options.sprites, error)) {
            if (entry.path().filename() == file && entry.path().parent_path().filename() != "Small") {
                k.picture = LoadPicture(entry.path().wstring());
                break;
            }
        }
        picture = &k.picture;
    }
    if (!picture->view) return false;
    *out = BmlSprite{picture->view, 0, 0, 1, 1, picture->width, picture->height};
    return true;
}

// int32 series count, int32 samples, then every series' samples as int32
bool LoadRecords(const std::string& path) {
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || !file) return false;
    int header[2] = {};
    bool ok = fread(header, sizeof(int), 2, file) == 2 && header[0] >= kSeriesUsed && header[1] > 0;
    if (ok) {
        std::vector<int> values(header[1]);
        g_records.series.resize(kSeriesUsed);
        for (int s = 0; s < kSeriesUsed && ok; ++s) {
            ok = fread(values.data(), sizeof(int), values.size(), file) == values.size();
            for (int value : values) g_records.series[s].push_back(static_cast<float>(value));
        }
        g_records.samples = header[1];
    }
    fclose(file);
    return ok;
}

bool SavePng(ID3D11Texture2D* texture, const std::string& path) {
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging))) return false;
    g_context->CopyResource(staging, texture);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        staging->Release();
        return false;
    }
    std::vector<BYTE> pixels(static_cast<size_t>(desc.Width) * desc.Height * 3);
    for (UINT y = 0; y < desc.Height; ++y) {
        const BYTE* row = static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        BYTE* out = pixels.data() + static_cast<size_t>(y) * desc.Width * 3;
        for (UINT x = 0; x < desc.Width; ++x) {
            out[x * 3] = row[x * 4 + 2];
            out[x * 3 + 1] = row[x * 4 + 1];
            out[x * 3 + 2] = row[x * 4];
        }
    }
    g_context->Unmap(staging, 0);
    staging->Release();

    std::wstring wide = std::filesystem::path(path).wstring();
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    bool ok = SUCCEEDED(g_wic->CreateStream(&stream)) &&
              SUCCEEDED(stream->InitializeFromFilename(wide.c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
              SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
              SUCCEEDED(frame->SetSize(desc.Width, desc.Height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
              SUCCEEDED(frame->WritePixels(desc.Height, desc.Width * 3, static_cast<UINT>(pixels.size()),
                                           pixels.data())) &&
              SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    return ok;
}

// what the overlay would be drawn over: a screenshot of the game, or the grey of a snowed in map
void Backdrop(const Picture& picture) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImVec2 size = ImGui::GetIO().DisplaySize;
    if (picture.view) {
        draw->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(picture.view))), ImVec2(0, 0),
                       size);
    } else {
        draw->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(196, 199, 203, 255), IM_COL32(178, 182, 187, 255),
                                      IM_COL32(150, 155, 161, 255), IM_COL32(168, 172, 177, 255));
    }
}

void Frame(const Picture& backdrop, int frame) {
    if (frame > 0 && g_options.windowWidth > 0) {
        ImGui::SetWindowSize("Town Stats##overlay", ImVec2(g_options.windowWidth, g_options.windowHeight));
    }
    Backdrop(backdrop);
    if (g_open) UiWindow();
}

void Render(ID3D11RenderTargetView* target) {
    ImGui::Render();
    const float clear[4] = {0, 0, 0, 1};
    g_context->OMSetRenderTargets(1, &target, nullptr);
    g_context->ClearRenderTargetView(target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

int Shot(const Picture& backdrop) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = g_options.width;
    desc.Height = g_options.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* target = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &texture)) ||
        FAILED(g_device->CreateRenderTargetView(texture, nullptr, &target))) {
        return 1;
    }
    D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
    g_context->RSSetViewports(1, &viewport);
    ImGuiIO& io = ImGui::GetIO();
    // a few frames, so sizes settle and a click has gone down and up before the one that is kept
    constexpr int kFrames = 8;
    for (int frame = 0; frame < kFrames; ++frame) {
        io.DisplaySize = ImVec2(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
        io.DeltaTime = 1.0f / 60;
        if (g_options.mouseX >= 0 && frame >= 2) {
            io.AddMousePosEvent(g_options.mouseX, g_options.mouseY);
            if (g_options.click && frame == 3) io.AddMouseButtonEvent(0, true);
            if (g_options.click && frame == 4) io.AddMouseButtonEvent(0, false);
        }
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        Frame(backdrop, frame);
        Render(target);
    }
    g_context->Flush();
    bool saved = SavePng(texture, g_options.shot);
    target->Release();
    texture->Release();
    if (!saved) fprintf(stderr, "could not write %s\n", g_options.shot.c_str());
    return saved ? 0 : 1;
}

IDXGISwapChain* g_chain;
ID3D11RenderTargetView* g_target;
UINT g_resizeWidth, g_resizeHeight;

LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(window, msg, wParam, lParam)) return 1;
    if (msg == WM_SIZE && wParam != SIZE_MINIMIZED) {
        g_resizeWidth = LOWORD(lParam);
        g_resizeHeight = HIWORD(lParam);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, msg, wParam, lParam);
}

void CreateTarget() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
        g_device->CreateRenderTargetView(back, nullptr, &g_target);
        back->Release();
    }
}

int Interactive(const Picture& backdrop) {
    WNDCLASSEXW cls = {sizeof(cls)};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = L"TownStatsPreview";
    RegisterClassExW(&cls);
    RECT rect = {0, 0, g_options.width, g_options.height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"Town Stats preview", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                  CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                                  cls.hInstance, nullptr);
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory* factory = nullptr;
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    if (FAILED(g_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))) ||
        FAILED(factory->CreateSwapChain(g_device, &desc, &g_chain))) {
        return 1;
    }
    factory->Release();
    adapter->Release();
    dxgiDevice->Release();
    CreateTarget();
    ShowWindow(window, SW_SHOWDEFAULT);
    ImGui_ImplWin32_Init(window);

    for (int frame = 0;; ++frame) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) return 0;
        }
        if (g_resizeWidth != 0) {
            if (g_target) g_target->Release();
            g_target = nullptr;
            g_chain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeWidth = g_resizeHeight = 0;
            CreateTarget();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        // the gear menu button of the game, any key here
        if (!g_open && ImGui::IsKeyPressed(ImGuiKey_Space)) g_open = true;
        Frame(backdrop, frame);
        Render(g_target);
        g_chain->Present(1, 0);
    }
}

void Usage() {
    printf("townstats-preview [--records file] [--sprites dir] [--backdrop png] [--size W H]\n"
           "                  [--page N] [--range N] [--raw] [--goods N] [--chart-goods MASK] [--chart-show N]\n"
           "                  [--window W H] [--shot png [--mouse X Y [--click]]]\n");
}

bool Parse(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto has = [&](int count) { return i + count < argc; };
        if (arg == "--records" && has(1)) g_options.records = argv[++i];
        else if (arg == "--sprites" && has(1)) g_options.sprites = argv[++i];
        else if (arg == "--backdrop" && has(1)) g_options.backdrop = argv[++i];
        else if (arg == "--shot" && has(1)) g_options.shot = argv[++i];
        else if (arg == "--page" && has(1)) g_settings.page = atoi(argv[++i]);
        else if (arg == "--range" && has(1)) g_settings.range = atoi(argv[++i]);
        else if (arg == "--raw") g_settings.smooth = false;
        else if (arg == "--goods" && has(1)) g_settings.goods = atoi(argv[++i]);
        else if (arg == "--chart-goods" && has(1)) g_settings.chartGoods = strtol(argv[++i], nullptr, 0);
        else if (arg == "--chart-show" && has(1)) g_settings.chartShow = atoi(argv[++i]);
        else if (arg == "--click") g_options.click = true;
        else if (arg == "--size" && has(2)) {
            g_options.width = atoi(argv[++i]);
            g_options.height = atoi(argv[++i]);
        } else if (arg == "--window" && has(2)) {
            g_options.windowWidth = static_cast<float>(atof(argv[++i]));
            g_options.windowHeight = static_cast<float>(atof(argv[++i]));
        } else if (arg == "--mouse" && has(2)) {
            g_options.mouseX = static_cast<float>(atof(argv[++i]));
            g_options.mouseY = static_cast<float>(atof(argv[++i]));
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (!Parse(argc, argv)) {
        Usage();
        return 1;
    }
    if (!LoadRecords(g_options.records)) {
        fprintf(stderr, "no records in %s, the overlay shows its empty state\n", g_options.records.c_str());
    }
    static BmlApi api = {};
    api.version = 6;
    api.ui_sprite = UiSprite;
    g_api = &api;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic)))) {
        return 1;
    }
    for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        if (SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &g_device, nullptr,
                                        &g_context))) {
            break;
        }
    }
    if (!g_device) return 1;

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    UiSetup();
    ImGui_ImplDX11_Init(g_device, g_context);
    Picture backdrop;
    if (!g_options.backdrop.empty()) backdrop = LoadPicture(std::filesystem::path(g_options.backdrop).wstring());
    return g_options.shot.empty() ? Interactive(backdrop) : Shot(backdrop);
}
