#include "menu.h"

#include <windows.h>

#include <cstdint>
#include <string>

#include "game.h"
#include "settings.h"
#include "update.h"
#include "version.h"

namespace {

// RVAs in Application-steam-x64.exe 1.0.7
constexpr uintptr_t kMenuVtable = 0x165960;   // MainMenuDialog, both the main menu and the pause menu
constexpr int kOpenedSlot = 4;                // DialogInstance::OnOpened
constexpr int kClosedSlot = 5;                // DialogInstance::OnClosed
constexpr int kUpdateSlot = 7;                // DialogInstance::OnUpdate, every frame while open
constexpr int kEventSlot = 8;                 // DialogInstance::OnElementEvent
constexpr uintptr_t kMenuOpened = 0x0B91A0;
constexpr uintptr_t kMenuEvent = 0x0B9020;
constexpr int kInstanceSlots = 10;

constexpr int kClicked = 0x12;                // a checkbox sends it too, after it flipped
constexpr int kBmlButtonId = 0x4D42;          // the menu's own buttons are 0 to 12
constexpr int kOkId = 1;
constexpr int kCancelId = 2;
constexpr int kUpdateId = 3;
constexpr int kCheckId = 0x4D50;              // plus the plugin's index
constexpr uint32_t kHidden = 1;
constexpr size_t kInstanceSize = 0xB8;
constexpr size_t kInstanceUi = 0x10;
constexpr size_t kElementParent = 0x98;
constexpr size_t kElementChildren = 0xA0;
constexpr size_t kElementChildCount = 0xA8;

// System::String is one pointer, UI::String a string table and an entry in it
struct SysString {
    void* text;
};
struct UiString {
    void* table;
    intptr_t entry;
};

using OpenedFn = void (*)(void* self);
using ClosedFn = void (*)(void* self);
using UpdateFn = void (*)(void* self, const void* time);
using EventFn = void (*)(void* self, void* element, int event, int id, int a, int b, int c, void* user);
using StringCtorFn = SysString* (*)(SysString* self, const char* text);
using StringDtorFn = void (*)(SysString* self);
using UiStringCtorFn = UiString* (*)(UiString* self, const void* table, const SysString* name);
using FilenameFn = void* (*)(void* self, const char* path);
using FilenameDtorFn = void (*)(void* self);
using FindFn = const void* (*)(const void* factory, const void* filename);
using IsDerivedFromFn = bool (*)(const void* object, uint32_t type);
using GetDescriptionFn = const void** (*)(const void* element, const void** out);
using GetDialogFn = const void* (*)(const void* instance);
using CreateElementFn = void** (*)(const void* dialog, void** out, const void* description);
using FindElementFn = void** (*)(void* instance, void** out, const SysString* name);
using FindElementTypeFn = void** (*)(void* element, void** out, uint32_t type);
using TypeIdFn = uint32_t (*)();
using SetTextFn = void (*)(void* text, UiString* value);
using SetParameterFn = void (*)(void* text, int index, const SysString* value);
using SetIdFn = void (*)(void* element, int id);
using SetFlagsFn = void (*)(void* element, uint32_t on, uint32_t off);
using AddItemFn = void (*)(void* ribbon, void** item);
using EnableCheckFn = void (*)(void* check, bool on);
using IsCheckedFn = bool (*)(const void* check);
using InstanceCtorFn = void* (*)(void* self, void* ui, const void* dialog, void** parent, void* owner, void* user,
                                 int flags);
using UiDialogFn = void (*)(void* ui, void** instance);
using MemAllocFn = void* (*)(size_t size, int align);

struct Runtime {
    StringCtorFn stringCtor;
    StringDtorFn stringDtor;
    UiStringCtorFn uiStringCtor;
    FilenameFn filenameCtor;
    FilenameDtorFn filenameDtor;
    void** factory;
    FindFn find;
    IsDerivedFromFn isDerivedFrom;
    uint32_t* dialogType;
    uint32_t* descriptionType;
    uint32_t* ribbonType;
    uint32_t* stringTableType;
    GetDescriptionFn getDescription;
    GetDialogFn getDialog;
    CreateElementFn createElement;
    FindElementFn findElement;
    FindElementFn findChild;
    FindElementTypeFn findElementType;
    TypeIdFn labelTypeId;
    SetTextFn setText;
    SetParameterFn setParameter;
    SetIdFn setId;
    SetFlagsFn setFlags;
    AddItemFn addItem;
    EnableCheckFn enableCheck;
    IsCheckedFn isChecked;
    InstanceCtorFn instanceCtor;
    void** instanceVtable;
    UiDialogFn addDialog;
    UiDialogFn closeDialog;
    MemAllocFn memAlloc;
} g_rt;

// one line of the list, valid while the screen is open
struct Row {
    void* check;
    void* status;
};

OpenedFn g_menuOpened;
EventFn g_menuEvent;
EventFn g_instanceEvent;
ClosedFn g_instanceClosed;
UpdateFn g_instanceUpdate;
void* g_screenVtable[kInstanceSlots];
void* g_screen;           // the open BML screen
std::vector<Row> g_rows;
std::vector<bool> g_ticked;  // what the checkboxes say, saved on Ok
bool g_screenDirty;
int g_shownUpdate;        // the UpdateState generation the screen shows
std::vector<PluginEntry> g_plugins;

template <typename T>
T& Field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// a resource the game already has loaded, such as "Dialog/Mod.rsc:modLine", null unless it is of
// that type. Never loads one: a resource loaded on the side during a game is missing from the
// game's lists, its references are freed at quit to menu and the next load of it crashed
const void* Loaded(const char* path, uint32_t type) {
    alignas(16) uint8_t filename[64];
    g_rt.filenameCtor(filename, path);
    const void* found = g_rt.find(*g_rt.factory, filename);
    g_rt.filenameDtor(filename);
    return found && g_rt.isDerivedFrom(found, type) ? found : nullptr;
}

// an element by its description's name, in a whole dialog or under one element
void* FindWith(FindElementFn find, void* in, const char* name) {
    SysString text;
    g_rt.stringCtor(&text, name);
    void* found = nullptr;
    find(in, &found, &text);
    g_rt.stringDtor(&text);
    return found;
}

void* Find(void* instance, const char* name) {
    return FindWith(g_rt.findElement, instance, name);
}

void* FindIn(void* element, const char* name) {
    return FindWith(g_rt.findChild, element, name);
}

void SetParameter(void* element, const std::string& value) {
    SysString text;
    g_rt.stringCtor(&text, value.c_str());
    g_rt.setParameter(element, 0, &text);
    g_rt.stringDtor(&text);
}

// any text element shows this, through the string table's one word "@0" entry
void SetPlainText(void* element, const std::string& value) {
    if (!element) return;
    const void* general = Loaded("Dialog/StringTable.rsc:general", *g_rt.stringTableType);
    if (!general) return;
    SysString name;
    g_rt.stringCtor(&name, "Parameter");
    UiString entry;
    g_rt.uiStringCtor(&entry, general, &name);
    g_rt.stringDtor(&name);
    g_rt.setText(element, &entry);
    SetParameter(element, value);
}

void Show(void* element, bool shown) {
    if (element) g_rt.setFlags(element, shown ? 0 : kHidden, shown ? kHidden : 0);
}

// a copy of the Mods button, put right under it
void AddButton(void* menu) {
    void* mods = Find(menu, "buttonMod");
    if (!mods) return;
    void* ribbon = Field<void*>(mods, kElementParent);
    // the open menu's own dialog, MainMenu.rsc or Pause.rsc, and the Mods button's description
    const void* dialog = g_rt.getDialog(menu);
    const void* description = nullptr;
    g_rt.getDescription(mods, &description);
    if (!ribbon || !g_rt.isDerivedFrom(ribbon, *g_rt.ribbonType) || !dialog || !description ||
        !g_rt.isDerivedFrom(description, *g_rt.descriptionType)) {
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

std::string Status(size_t i) {
    bool on = g_ticked[i];
    switch (g_plugins[i].state) {
        case PluginEntry::State::Running: return on ? "Running" : "Stops when you restart the game";
        case PluginEntry::State::Off: return on ? "Starts when you restart the game" : "Off";
        case PluginEntry::State::Failed: return on ? "Did not start, bml.log says why" : "Off";
    }
    return "";
}

// the game's own update corner of the window: the update button, or how it went
void ShowUpdate(void* screen, const UpdateState& u) {
    Show(Find(screen, "groupProgress"), u.kind != UpdateState::None);
    Show(Find(screen, "ribbonUpdateAvailable"), u.kind == UpdateState::Available);
    Show(Find(screen, "ribbonProgress"), u.kind != UpdateState::None && u.kind != UpdateState::Available);
    if (u.kind == UpdateState::Available) {
        SetPlainText(Find(screen, "textUpdateAvailable"), "BML " + u.version + " is out, click to update");
        return;
    }
    std::string line;
    if (u.kind == UpdateState::Installing) line = "Downloading BML " + u.version + "...";
    if (u.kind == UpdateState::Installed) line = "BML " + u.version + " is installed, restart the game to use it";
    if (u.kind == UpdateState::Failed) line = "The update failed, " + u.error;
    SetPlainText(Find(screen, "textUpdate"), line);
}

void Refresh(void* screen) {
    for (size_t i = 0; i < g_rows.size(); ++i) SetPlainText(g_rows[i].status, Status(i));
    UpdateState update = UpdateGet();
    ShowUpdate(screen, update);
    g_shownUpdate = update.generation;
    g_screenDirty = false;
}

void Close(void* screen) {
    void* instance = screen;
    g_rt.closeDialog(Field<void*>(screen, kInstanceUi), &instance);
}

void ScreenEvent(void* self, void* element, int event, int id, int a, int b, int c, void* user) {
    if (event == kClicked && id == kOkId) {
        for (size_t i = 0; i < g_plugins.size(); ++i) {
            if (g_ticked[i] != PluginEnabled(g_plugins[i].file)) SetPluginEnabled(g_plugins[i].file, g_ticked[i]);
        }
        Close(self);
        return;
    }
    if (event == kClicked && id == kCancelId) {
        Close(self);
        return;
    }
    if (event == kClicked && id == kUpdateId) {
        UpdateInstall();
        g_screenDirty = true;
        return;
    }
    if (event == kClicked && id >= kCheckId && id < kCheckId + static_cast<int>(g_rows.size())) {
        size_t i = id - kCheckId;
        g_ticked[i] = g_rt.isChecked(g_rows[i].check);
        // the status is redrawn in the next update, not from inside the checkbox's own update
        g_screenDirty = true;
        return;
    }
    g_instanceEvent(self, element, event, id, a, b, c, user);
}

void ScreenUpdate(void* self, const void* time) {
    g_instanceUpdate(self, time);
    if (self == g_screen && (g_screenDirty || UpdateGet().generation != g_shownUpdate)) Refresh(self);
}

void ScreenClosed(void* self) {
    if (self == g_screen) {
        g_screen = nullptr;
        g_rows.clear();
    }
    g_instanceClosed(self);
}

// a line from the game's Mods list for one plugin, with its own checkbox
void AddRow(void* table, const void* dialog, const void* line, size_t i) {
    void* row = nullptr;
    g_rt.createElement(dialog, &row, line);
    void* check = row ? FindIn(row, "checkLoaded") : nullptr;
    if (!check) return;
    const PluginEntry& p = g_plugins[i];
    if (void* name = FindIn(row, "textModName")) SetParameter(name, p.title);
    if (void* description = FindIn(row, "textModDescription")) SetParameter(description, p.description);
    SetPlainText(FindIn(row, "textModVersion"), p.file + ".dll");
    for (const char* unused : {"imageIcon", "imageConflict", "buttonDetails", "buttonTop", "buttonUp", "buttonDown"}) {
        Show(FindIn(row, unused), false);
    }
    g_rt.setId(check, kCheckId + static_cast<int>(i));
    g_rt.enableCheck(check, g_ticked[i]);
    g_rows.push_back({check, FindIn(row, "textModAuthor")});
    g_rt.addItem(table, &row);
}

// the game's own Mods list window, listing the plugins instead
void OpenScreen(void* menu) {
    const void* dialog = Loaded("Dialog/Mod.rsc", *g_rt.dialogType);
    const void* line = Loaded("Dialog/Mod.rsc:modLine", *g_rt.descriptionType);
    if (!dialog || !line) {
        LogAt(LogLevel::Warn, "bml", "Dialog/Mod.rsc is not loaded, no BML screen");
        return;
    }
    void* ui = Field<void*>(menu, kInstanceUi);
    // no parent, the way the menu opens the game's own Mods window. A modal window under the
    // menu never got the input, so it could not be scrolled or closed
    void* parent = nullptr;
    void* screen = g_rt.instanceCtor(g_rt.memAlloc(kInstanceSize, 8), ui, dialog, &parent, nullptr, nullptr, 0);
    *static_cast<void**>(screen) = g_screenVtable;

    void* table = Find(screen, "tableMods");
    void* ok = Find(screen, "buttonOk");
    void* cancel = Find(screen, "buttonCancel");
    // without its own ids for Ok and Cancel the window could never be closed, so it is not shown
    if (!table || !ok || !cancel) {
        LogAt(LogLevel::Warn, "bml", "the Mods list window is not laid out the way BML expects, no BML screen");
        return;
    }
    g_rt.setId(ok, kOkId);
    g_rt.setId(cancel, kCancelId);
    if (void* update = Find(screen, "buttonUpdateReload")) g_rt.setId(update, kUpdateId);
    SetPlainText(Find(screen, "labelTitle"), "BML " + std::string(kBmlVersion));
    SetPlainText(Find(screen, "labelMod"), "Plugins");
    SetPlainText(Find(screen, "textName"), "Name");
    for (const char* unused : {"buttonBrowse", "textOrder", "progressUpdate"}) Show(Find(screen, unused), false);

    g_rows.clear();
    g_ticked.clear();
    for (const PluginEntry& p : g_plugins) g_ticked.push_back(PluginEnabled(p.file));
    for (size_t i = 0; i < g_plugins.size(); ++i) AddRow(table, dialog, line, i);
    Refresh(screen);
    g_screen = screen;
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
           Bind(r, "?Find@ExternalFactory@Object@@QEBAPEBVExternal@2@AEBVFilename@System@@@Z", g_rt.find) &&
           Bind(r, "?IsDerivedFrom@Base@Object@@QEBA_NI@Z", g_rt.isDerivedFrom) &&
           Bind(r, "?_typeInfo@Dialog@UI@@0VBaseTypeInfo@Object@@A", g_rt.dialogType) &&
           Bind(r, "?_typeInfo@ElementDescription@UI@@0VBaseTypeInfo@Object@@A", g_rt.descriptionType) &&
           Bind(r, "?_typeInfo@RibbonElement@UI@@0VBaseTypeInfo@Object@@A", g_rt.ribbonType) &&
           Bind(r, "?_typeInfo@StringTable@UI@@0VBaseTypeInfo@Object@@A", g_rt.stringTableType) &&
           Bind(r, "?GetDescription@Element@UI@@QEBA?AV?$ExternalPtr@VElementDescription@UI@@@Object@@XZ",
                g_rt.getDescription) &&
           Bind(r, "?GetDialog@DialogInstance@UI@@QEBAPEBVDialog@2@XZ", g_rt.getDialog) &&
           Bind(r, "?CreateElement@Dialog@UI@@QEBA?AV?$InternalPtr@VElement@UI@@@Object@@V?$ExternalPtr@VElementDescription@UI@@@4@@Z",
                g_rt.createElement) &&
           Bind(r, "?FindElement@DialogInstance@UI@@QEAA?AV?$InternalPtr@VElement@UI@@@Object@@AEBVString@System@@@Z",
                g_rt.findElement) &&
           Bind(r, "?FindElement@Element@UI@@QEAA?AV?$InternalPtr@VElement@UI@@@Object@@AEBVString@System@@@Z",
                g_rt.findChild) &&
           Bind(r, "?FindElementType@Element@UI@@QEAA?AV?$InternalPtr@VElement@UI@@@Object@@I@Z", g_rt.findElementType) &&
           Bind(r, "?TypeId@LabelElement@UI@@SAIXZ", g_rt.labelTypeId) &&
           Bind(r, "?SetText@TextElement@UI@@QEAAXVString@2@@Z", g_rt.setText) &&
           Bind(r, "?SetParameter@TextElement@UI@@QEAAXHAEBVString@System@@@Z", g_rt.setParameter) &&
           Bind(r, "?SetElementId@Element@UI@@QEAAXH@Z", g_rt.setId) &&
           Bind(r, "?SetFlagsOnOff@Element@UI@@QEAAXII@Z", g_rt.setFlags) &&
           Bind(r, "?AddItem@RibbonElement@UI@@QEAAXV?$InternalPtr@VElement@UI@@@Object@@@Z", g_rt.addItem) &&
           Bind(r, "?EnableCheck@CheckElement@UI@@QEAAX_N@Z", g_rt.enableCheck) &&
           Bind(r, "?IsChecked@CheckElement@UI@@QEBA_NXZ", g_rt.isChecked) &&
           Bind(r, "??0DialogInstance@UI@@QEAA@PEAVUserInterface@1@PEBVDialog@1@V?$InternalPtr@VDialogInstance@UI@@@Object@@PEAVBase@5@PEAXH@Z",
                g_rt.instanceCtor) &&
           Bind(r, "??_7DialogInstance@UI@@6B@", g_rt.instanceVtable) &&
           Bind(r, "?AddDialog@UserInterface@UI@@QEAAXV?$InternalPtr@VDialogInstance@UI@@@Object@@@Z", g_rt.addDialog) &&
           Bind(r, "?CloseDialog@UserInterface@UI@@QEAAXV?$InternalPtr@VDialogInstance@UI@@@Object@@@Z", g_rt.closeDialog) &&
           Bind(r, "?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z", g_rt.memAlloc);
}

}  // namespace

void MenuHook(const std::vector<PluginEntry>& plugins) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    void** menu = reinterpret_cast<void**>(base + kMenuVtable);
    if (menu[kOpenedSlot] != base + kMenuOpened || menu[kEventSlot] != base + kMenuEvent || !BindRuntime()) {
        LogAt(LogLevel::Error, "bml", "the main menu is not what this build expects, no BML button");
        return;
    }
    g_plugins = plugins;
    for (int i = 0; i < kInstanceSlots; ++i) g_screenVtable[i] = g_rt.instanceVtable[i];
    g_instanceEvent = reinterpret_cast<EventFn>(g_screenVtable[kEventSlot]);
    g_screenVtable[kEventSlot] = reinterpret_cast<void*>(ScreenEvent);
    g_instanceClosed = reinterpret_cast<ClosedFn>(g_screenVtable[kClosedSlot]);
    g_screenVtable[kClosedSlot] = reinterpret_cast<void*>(ScreenClosed);
    g_instanceUpdate = reinterpret_cast<UpdateFn>(g_screenVtable[kUpdateSlot]);
    g_screenVtable[kUpdateSlot] = reinterpret_cast<void*>(ScreenUpdate);
    g_menuOpened = reinterpret_cast<OpenedFn>(menu[kOpenedSlot]);
    g_menuEvent = reinterpret_cast<EventFn>(menu[kEventSlot]);
    if (!PatchPointer(&menu[kOpenedSlot], reinterpret_cast<void*>(MenuOpened)) ||
        !PatchPointer(&menu[kEventSlot], reinterpret_cast<void*>(MenuEvent))) {
        LogAt(LogLevel::Error, "bml", "could not patch the main menu");
    }
}
