#include "menu.h"

#include <windows.h>

#include <cstdint>
#include <string>

#include "game.h"

namespace {

// RVAs in Application-steam-x64.exe 1.0.7
constexpr uintptr_t kMenuVtable = 0x165960;   // MainMenuDialog, both the main menu and the pause menu
constexpr int kOpenedSlot = 4;                // DialogInstance::OnOpened
constexpr int kEventSlot = 8;                 // DialogInstance::OnElementEvent
constexpr uintptr_t kMenuOpened = 0x0B91A0;
constexpr uintptr_t kMenuEvent = 0x0B9020;
constexpr int kInstanceSlots = 10;

constexpr int kClicked = 0x12;
constexpr int kBmlButtonId = 0x4D42;         // the menu's own buttons are 0 to 12
constexpr int kCloseId = 1;
constexpr uint32_t kHidden = 1;
constexpr size_t kInstanceSize = 0xB8;
constexpr size_t kInstanceUi = 0x10;
constexpr size_t kElementParent = 0x98;
constexpr size_t kElementChildren = 0xA0;
constexpr size_t kElementChildCount = 0xA8;
constexpr size_t kModName = 0x00;             // IO::Modification::Mod, System::Strings
constexpr size_t kModDescription = 0x10;
constexpr size_t kModEnabled = 0x50;          // bool

// System::String is one pointer, UI::String a string table and an entry in it
struct SysString {
    void* text;
};
struct UiString {
    void* table;
    intptr_t entry;
};

using OpenedFn = void (*)(void* self);
using EventFn = void (*)(void* self, void* element, int event, int id, int a, int b, int c, void* user);
using StringCtorFn = SysString* (*)(SysString* self, const char* text);
using StringDtorFn = void (*)(SysString* self);
using UiStringCtorFn = UiString* (*)(UiString* self, const void* table, const SysString* name);
using FilenameFn = void* (*)(void* self, const char* path);
using FilenameDtorFn = void (*)(void* self);
using FindOrLoadFn = const void* (*)(void* factory, const void* filename);
using IsDerivedFromFn = bool (*)(const void* object, uint32_t type);
using CreateElementFn = void** (*)(const void* dialog, void** out, const void* description);
using FindElementFn = void** (*)(void* instance, void** out, const SysString* name);
using FindElementTypeFn = void** (*)(void* element, void** out, uint32_t type);
using TypeIdFn = uint32_t (*)();
using SetTextFn = void (*)(void* text, UiString* value);
using SetParameterFn = void (*)(void* text, int index, const SysString* value);
using SetDocumentFn = void (*)(void* document, const SysString* value);
using SetIdFn = void (*)(void* element, int id);
using SetFlagsFn = void (*)(void* element, uint32_t on, uint32_t off);
using AddItemFn = void (*)(void* ribbon, void** item);
using InstanceCtorFn = void* (*)(void* self, void* ui, const void* dialog, void** parent, void* owner, void* user,
                                 int flags);
using UiDialogFn = void (*)(void* ui, void** instance);
using MemAllocFn = void* (*)(size_t size, int align);
using ModsFn = void* (*)();
using ModCountFn = int (*)(const void* mods);
using ModGetFn = const void* (*)(const void* mods, int index);

struct Runtime {
    StringCtorFn stringCtor;
    StringDtorFn stringDtor;
    UiStringCtorFn uiStringCtor;
    FilenameFn filenameCtor;
    FilenameDtorFn filenameDtor;
    void** factory;
    FindOrLoadFn findOrLoad;
    IsDerivedFromFn isDerivedFrom;
    uint32_t* dialogType;
    uint32_t* descriptionType;
    uint32_t* ribbonType;
    uint32_t* stringTableType;
    CreateElementFn createElement;
    FindElementFn findElement;
    FindElementTypeFn findElementType;
    TypeIdFn labelTypeId;
    SetTextFn setText;
    SetParameterFn setParameter;
    SetDocumentFn setDocument;
    SetIdFn setId;
    SetFlagsFn setFlags;
    AddItemFn addItem;
    InstanceCtorFn instanceCtor;
    void** instanceVtable;
    UiDialogFn addDialog;
    UiDialogFn closeDialog;
    MemAllocFn memAlloc;
    ModsFn mods;
    ModCountFn modCount;
    ModGetFn modGet;
} g_rt;

OpenedFn g_menuOpened;
EventFn g_menuEvent;
EventFn g_instanceEvent;
void* g_screenVtable[kInstanceSlots];
std::vector<PluginEntry> g_plugins;
std::string g_folder;

template <typename T>
T& Field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// a loaded resource such as "Dialog/MainMenu.rsc:buttonMod", null unless it is of that type
const void* Load(const char* path, uint32_t type) {
    alignas(16) uint8_t filename[64];
    g_rt.filenameCtor(filename, path);
    const void* found = g_rt.findOrLoad(*g_rt.factory, filename);
    g_rt.filenameDtor(filename);
    return found && g_rt.isDerivedFrom(found, type) ? found : nullptr;
}

void* Find(void* instance, const char* name) {
    SysString text;
    g_rt.stringCtor(&text, name);
    void* found = nullptr;
    g_rt.findElement(instance, &found, &text);
    g_rt.stringDtor(&text);
    return found;
}

void SetParameter(void* element, const std::string& value) {
    SysString text;
    g_rt.stringCtor(&text, value.c_str());
    g_rt.setParameter(element, 0, &text);
    g_rt.stringDtor(&text);
}

// any text element shows this, through the string table's one word "@0" entry
void SetPlainText(void* element, const std::string& value) {
    const void* general = Load("Dialog/StringTable.rsc:general", *g_rt.stringTableType);
    if (!general) return;
    SysString name;
    g_rt.stringCtor(&name, "Parameter");
    UiString entry;
    g_rt.uiStringCtor(&entry, general, &name);
    g_rt.stringDtor(&name);
    g_rt.setText(element, &entry);
    SetParameter(element, value);
}

void SetDocument(void* element, const std::string& value) {
    SysString text;
    g_rt.stringCtor(&text, value.c_str());
    g_rt.setDocument(element, &text);
    g_rt.stringDtor(&text);
}

void Hide(void* instance, const char* name) {
    if (void* element = Find(instance, name)) g_rt.setFlags(element, kHidden, 0);
}

// a copy of the Mods button, put right under it
void AddButton(void* menu) {
    void* mods = Find(menu, "buttonMod");
    if (!mods) return;
    void* ribbon = Field<void*>(mods, kElementParent);
    const void* dialog = Load("Dialog/MainMenu.rsc", *g_rt.dialogType);
    const void* description = Load("Dialog/MainMenu.rsc:buttonMod", *g_rt.descriptionType);
    if (!ribbon || !g_rt.isDerivedFrom(ribbon, *g_rt.ribbonType) || !dialog || !description) {
        LogAt(LogLevel::Warn, "bml", "the menu is not laid out the way BML expects, no BML button");
        return;
    }
    void* button = nullptr;
    g_rt.createElement(dialog, &button, description);
    if (!button) return;
    g_rt.setId(button, kBmlButtonId);
    void* label = nullptr;
    g_rt.findElementType(button, &label, g_rt.labelTypeId());
    if (label) SetPlainText(label, "BML");
    g_rt.addItem(ribbon, &button);

    void** children = Field<void**>(ribbon, kElementChildren);
    int count = Field<int>(ribbon, kElementChildCount);
    int at = count - 1;
    while (at > 0 && children[at - 1] != mods) {
        children[at] = children[at - 1];
        --at;
    }
    children[at] = button;
}

// a System::String is a pointer to its characters
std::string ModText(const void* mod, size_t offset) {
    const char* text = *reinterpret_cast<const char* const*>(static_cast<const uint8_t*>(mod) + offset);
    return text ? text : "";
}

std::string Entry(const std::string& title, const std::string& description) {
    std::string text = "^f1^c1" + title + "^c0^f0^n";
    if (!description.empty()) text += description + "^n";
    return text + "^n";
}

// the mods ticked in the game's Mods list, in their load order, then the plugins
std::string ScreenText(int* active) {
    std::string mods;
    const void* list = g_rt.mods();
    int count = list ? g_rt.modCount(list) : 0;
    for (int i = 0; i < count; ++i) {
        const void* mod = g_rt.modGet(list, i);
        if (!*(static_cast<const uint8_t*>(mod) + kModEnabled)) continue;
        mods += Entry(ModText(mod, kModName), ModText(mod, kModDescription));
        ++*active;
    }
    std::string text = "^f1^c2Mods^c0^f0^n^n";
    text += mods.empty() ? "None are enabled.^n^n" : mods;
    text += "^f1^c2BML plugins^c0^f0^n^n";
    for (const PluginEntry& p : g_plugins) text += Entry(p.title, p.description);
    return text;
}

void ScreenEvent(void* self, void* element, int event, int id, int a, int b, int c, void* user) {
    if (event == kClicked && id == kCloseId) {
        void* instance = self;
        g_rt.closeDialog(Field<void*>(self, kInstanceUi), &instance);
        return;
    }
    g_instanceEvent(self, element, event, id, a, b, c, user);
}

// the game's own mod details window, filled with the active mods and the plugins
void OpenScreen(void* menu) {
    const void* dialog = Load("Dialog/ModDetails.rsc", *g_rt.dialogType);
    if (!dialog) {
        LogAt(LogLevel::Warn, "bml", "Dialog/ModDetails.rsc did not load, no BML screen");
        return;
    }
    void* ui = Field<void*>(menu, kInstanceUi);
    // no parent, the way the menu opens the game's own Mods window. A modal window under the
    // menu never got the input, so it could not be scrolled or closed
    void* parent = nullptr;
    void* screen = g_rt.instanceCtor(g_rt.memAlloc(kInstanceSize, 8), ui, dialog, &parent, nullptr, nullptr, 0);
    *static_cast<void**>(screen) = g_screenVtable;

    if (void* name = Find(screen, "textName")) SetParameter(name, "BML");
    if (void* path = Find(screen, "textPath")) SetParameter(path, g_folder);
    if (void* about = Find(screen, "textDescription")) {
        SetParameter(about, "The mods enabled in the Mods list, and the plugins BML loaded from the NativeMods "
                            "folder. Plugins change the game's code itself, so they run with every save.");
    }
    int active = 0;
    std::string list = ScreenText(&active);
    if (void* title = Find(screen, "textConflictTitle")) {
        SetDocument(title, "^jl^f1^c0Active Mods^f0^n " + std::to_string(active) + " from the Mods list and " +
                               std::to_string(g_plugins.size()) + " BML plugins.");
    }
    if (void* body = Find(screen, "textConflictList")) SetDocument(body, list);
    for (const char* unused : {"imageIcon", "textAuthor", "textVersion", "textResourceTitle", "textResourceList",
                               "buttonCreate", "buttonUpdate"}) {
        Hide(screen, unused);
    }
    // without its own id for the Ok button the window could never be closed, so it is not shown
    void* ok = Find(screen, "buttonOk");
    if (!ok) {
        LogAt(LogLevel::Warn, "bml", "the mod details window has no Ok button, no BML screen");
        return;
    }
    g_rt.setId(ok, kCloseId);
    g_rt.addDialog(ui, &screen);
}

void MenuOpened(void* self) {
    g_menuOpened(self);
    AddButton(self);
}

void MenuEvent(void* self, void* element, int event, int id, int a, int b, int c, void* user) {
    if (event == kClicked && id == kBmlButtonId) {
        OpenScreen(self);
        return;
    }
    g_menuEvent(self, element, event, id, a, b, c, user);
}

bool BindRuntime() {
    HMODULE r = GetModuleHandleW(L"Runtime-steam-x64.dll");
    return r && Bind(r, "??0String@System@@QEAA@PEBD@Z", g_rt.stringCtor) &&
           Bind(r, "??1String@System@@QEAA@XZ", g_rt.stringDtor) &&
           Bind(r, "??0String@UI@@QEAA@V?$ExternalPtr@VStringTable@UI@@@Object@@AEBV0System@@@Z", g_rt.uiStringCtor) &&
           Bind(r, "??0Filename@System@@QEAA@PEBD@Z", g_rt.filenameCtor) &&
           Bind(r, "??1Filename@System@@QEAA@XZ", g_rt.filenameDtor) &&
           Bind(r, "?_singleton@ExternalFactory@Object@@0PEAV12@EA", g_rt.factory) &&
           Bind(r, "?FindOrLoad@ExternalFactory@Object@@QEAAPEBVExternal@2@AEBVFilename@System@@@Z", g_rt.findOrLoad) &&
           Bind(r, "?IsDerivedFrom@Base@Object@@QEBA_NI@Z", g_rt.isDerivedFrom) &&
           Bind(r, "?_typeInfo@Dialog@UI@@0VBaseTypeInfo@Object@@A", g_rt.dialogType) &&
           Bind(r, "?_typeInfo@ElementDescription@UI@@0VBaseTypeInfo@Object@@A", g_rt.descriptionType) &&
           Bind(r, "?_typeInfo@RibbonElement@UI@@0VBaseTypeInfo@Object@@A", g_rt.ribbonType) &&
           Bind(r, "?_typeInfo@StringTable@UI@@0VBaseTypeInfo@Object@@A", g_rt.stringTableType) &&
           Bind(r, "?CreateElement@Dialog@UI@@QEBA?AV?$InternalPtr@VElement@UI@@@Object@@V?$ExternalPtr@VElementDescription@UI@@@4@@Z",
                g_rt.createElement) &&
           Bind(r, "?FindElement@DialogInstance@UI@@QEAA?AV?$InternalPtr@VElement@UI@@@Object@@AEBVString@System@@@Z",
                g_rt.findElement) &&
           Bind(r, "?FindElementType@Element@UI@@QEAA?AV?$InternalPtr@VElement@UI@@@Object@@I@Z", g_rt.findElementType) &&
           Bind(r, "?TypeId@LabelElement@UI@@SAIXZ", g_rt.labelTypeId) &&
           Bind(r, "?SetText@TextElement@UI@@QEAAXVString@2@@Z", g_rt.setText) &&
           Bind(r, "?SetParameter@TextElement@UI@@QEAAXHAEBVString@System@@@Z", g_rt.setParameter) &&
           Bind(r, "?SetText@DocumentTextElement@UI@@QEAAXAEBVString@System@@@Z", g_rt.setDocument) &&
           Bind(r, "?SetElementId@Element@UI@@QEAAXH@Z", g_rt.setId) &&
           Bind(r, "?SetFlagsOnOff@Element@UI@@QEAAXII@Z", g_rt.setFlags) &&
           Bind(r, "?AddItem@RibbonElement@UI@@QEAAXV?$InternalPtr@VElement@UI@@@Object@@@Z", g_rt.addItem) &&
           Bind(r, "??0DialogInstance@UI@@QEAA@PEAVUserInterface@1@PEBVDialog@1@V?$InternalPtr@VDialogInstance@UI@@@Object@@PEAVBase@5@PEAXH@Z",
                g_rt.instanceCtor) &&
           Bind(r, "??_7DialogInstance@UI@@6B@", g_rt.instanceVtable) &&
           Bind(r, "?AddDialog@UserInterface@UI@@QEAAXV?$InternalPtr@VDialogInstance@UI@@@Object@@@Z", g_rt.addDialog) &&
           Bind(r, "?CloseDialog@UserInterface@UI@@QEAAXV?$InternalPtr@VDialogInstance@UI@@@Object@@@Z", g_rt.closeDialog) &&
           Bind(r, "?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z", g_rt.memAlloc) &&
           Bind(r, "?Get@Modification@IO@@SAPEAV12@XZ", g_rt.mods) &&
           Bind(r, "?GetCount@Modification@IO@@QEBAHXZ", g_rt.modCount) &&
           Bind(r, "?GetModification@Modification@IO@@QEBAAEBUMod@12@H@Z", g_rt.modGet);
}

}  // namespace

void MenuHook(const std::vector<PluginEntry>& plugins, const std::string& folder) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    void** menu = reinterpret_cast<void**>(base + kMenuVtable);
    if (menu[kOpenedSlot] != base + kMenuOpened || menu[kEventSlot] != base + kMenuEvent || !BindRuntime()) {
        LogAt(LogLevel::Error, "bml", "the main menu is not what this build expects, no BML button");
        return;
    }
    g_plugins = plugins;
    g_folder = folder;
    for (int i = 0; i < kInstanceSlots; ++i) g_screenVtable[i] = g_rt.instanceVtable[i];
    g_instanceEvent = reinterpret_cast<EventFn>(g_screenVtable[kEventSlot]);
    g_screenVtable[kEventSlot] = reinterpret_cast<void*>(ScreenEvent);
    g_menuOpened = reinterpret_cast<OpenedFn>(menu[kOpenedSlot]);
    g_menuEvent = reinterpret_cast<EventFn>(menu[kEventSlot]);
    if (!PatchPointer(&menu[kOpenedSlot], reinterpret_cast<void*>(MenuOpened)) ||
        !PatchPointer(&menu[kEventSlot], reinterpret_cast<void*>(MenuEvent))) {
        LogAt(LogLevel::Error, "bml", "could not patch the main menu");
    }
}
