#include "input.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <atomic>
#include <mutex>

#include "game.h"

namespace {

// IDirectInput8 and IDirectInputDevice8, the A and W versions share the layout
constexpr int kCreateDeviceSlot = 3;
constexpr int kGetDeviceStateSlot = 9;
constexpr int kGetDeviceDataSlot = 10;

using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(void* self, REFGUID guid, void** device, IUnknown* outer);
using GetStateFn = HRESULT(STDMETHODCALLTYPE*)(void* self, DWORD size, void* data);
using GetDataFn = HRESULT(STDMETHODCALLTYPE*)(void* self, DWORD size, DIDEVICEOBJECTDATA* data, DWORD* count,
                                             DWORD flags);

std::mutex g_lock;
CreateDeviceFn g_createDevice;
GetStateFn g_getState;
GetDataFn g_getData;
void* g_mouse;
void* g_keyboard;
std::atomic<bool> g_takeMouse;
std::atomic<bool> g_takeKeyboard;

void** Vtable(void* object) {
    return *static_cast<void***>(object);
}

bool Taken(void* device) {
    return (device == g_mouse && g_takeMouse) || (device == g_keyboard && g_takeKeyboard);
}

HRESULT STDMETHODCALLTYPE HookedGetState(void* self, DWORD size, void* data) {
    HRESULT result = g_getState(self, size, data);
    if (SUCCEEDED(result) && data && Taken(self)) memset(data, 0, size);
    return result;
}

// the buffered events are read and dropped, so they don't arrive all at once later
HRESULT STDMETHODCALLTYPE HookedGetData(void* self, DWORD size, DIDEVICEOBJECTDATA* data, DWORD* count,
                                        DWORD flags) {
    HRESULT result = g_getData(self, size, data, count, flags);
    if (SUCCEEDED(result) && count && Taken(self)) *count = 0;
    return result;
}

HRESULT STDMETHODCALLTYPE HookedCreateDevice(void* self, REFGUID guid, void** device, IUnknown* outer) {
    HRESULT result = g_createDevice(self, guid, device, outer);
    if (FAILED(result) || !device || !*device) return result;
    std::lock_guard<std::mutex> lock(g_lock);
    if (IsEqualGUID(guid, GUID_SysMouse)) g_mouse = *device;
    if (IsEqualGUID(guid, GUID_SysKeyboard)) g_keyboard = *device;
    // every device the dll makes shares one vtable
    void** v = Vtable(*device);
    if (!g_getState) {
        g_getState = reinterpret_cast<GetStateFn>(v[kGetDeviceStateSlot]);
        g_getData = reinterpret_cast<GetDataFn>(v[kGetDeviceDataSlot]);
        if (!PatchPointer(&v[kGetDeviceStateSlot], reinterpret_cast<void*>(HookedGetState)) ||
            !PatchPointer(&v[kGetDeviceDataSlot], reinterpret_cast<void*>(HookedGetData))) {
            LogAt(LogLevel::Error, "bml", "could not hook the input devices, overlays can't hold the mouse");
        }
    }
    return result;
}

}  // namespace

void InputWrap(void* directInput) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (!directInput || g_createDevice) return;
    void** v = Vtable(directInput);
    g_createDevice = reinterpret_cast<CreateDeviceFn>(v[kCreateDeviceSlot]);
    if (!PatchPointer(&v[kCreateDeviceSlot], reinterpret_cast<void*>(HookedCreateDevice))) {
        LogAt(LogLevel::Error, "bml", "could not hook DirectInput, overlays can't hold the mouse");
    }
}

void InputCapture(bool mouse, bool keyboard) {
    g_takeMouse = mouse;
    g_takeKeyboard = keyboard;
}
