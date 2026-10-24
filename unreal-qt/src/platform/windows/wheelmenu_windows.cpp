#include "platform/windows/wheelmenu_windows.h"

#include <QtGlobal>

#ifdef Q_OS_WIN

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <inspectable.h>

/// region <The ABI of Windows.UI.Input (windows.ui.input.h and RadialControllerInterop.h, Windows SDK 10.0.22621)>
/// Only what a hold calls; a slot never called keeps its place in the vtable under a name of its own (MSVC groups
/// overloads, so no two slots share a name). A named namespace, not an anonymous one: GCC sees every class derived
/// from a type with internal linkage and, with no implementation of these interfaces in this file, takes a call
/// through one for unreachable (-O2 devirtualization; the MinGW build crashed)
namespace WheelMenuAbi
{

// clang-format off
constexpr IID kIidRadialControllerInterop = {0x1b0535c9, 0x57ad, 0x45c1, {0x9d, 0x79, 0xad, 0x5c, 0x34, 0x36, 0x05, 0x13}};
constexpr IID kIidConfigurationInterop = {0x787cdaac, 0x3186, 0x476d, {0x87, 0xe4, 0xb9, 0x37, 0x4a, 0x7b, 0x99, 0x70}};
constexpr IID kIidRadialController = {0x3055d1c8, 0xdf51, 0x43d4, {0xb2, 0x3b, 0x0e, 0x10, 0x37, 0x46, 0x7a, 0x09}};
constexpr IID kIidConfiguration = {0xa6b79ecb, 0x6a52, 0x4430, {0x91, 0x0c, 0x56, 0x37, 0x0a, 0x9d, 0x6b, 0x42}};
constexpr IID kIidConfiguration2 = {0x3d577ef7, 0x3cee, 0x11e6, {0xb5, 0x35, 0x00, 0x1b, 0xdc, 0x06, 0xab, 0x3b}};
constexpr IID kIidMenuItemStatics = {0x249e0887, 0xd842, 0x4524, {0x9d, 0xf8, 0xe0, 0xd6, 0x47, 0xed, 0xc8, 0x87}};
/// IIterable<RadialControllerSystemMenuItemKind>, IIterator<RadialControllerSystemMenuItemKind>
constexpr IID kIidKindIterable = {0x4516010f, 0xfd98, 0x5e1d, {0xbf, 0x3f, 0xae, 0xaf, 0x79, 0xf1, 0xf3, 0xda}};
constexpr IID kIidKindIterator = {0x3fbc1858, 0xa43e, 0x54dc, {0xb0, 0xe2, 0x8b, 0x09, 0x8b, 0xdd, 0xac, 0xf6}};
constexpr IID kIidAgileObject = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
constexpr IID kIidUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr IID kIidInspectable = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
// clang-format on

constexpr int kKnownIconNextPreviousTrack = 4;  ///< RadialControllerMenuKnownIcon_NextPreviousTrack
constexpr HRESULT kBounds = static_cast<HRESULT>(0x8000000BL);  ///< E_BOUNDS

struct IRadialControllerMenuItem : IInspectable
{
};

/// IVector<RadialControllerMenuItem>
struct IMenuItemVector : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE GetAt(unsigned index, IRadialControllerMenuItem** item) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(unsigned* size) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetView(void** view) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(IRadialControllerMenuItem* value, unsigned* index, boolean* found) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAt(unsigned index, IRadialControllerMenuItem* item) = 0;
    virtual HRESULT STDMETHODCALLTYPE InsertAt(unsigned index, IRadialControllerMenuItem* item) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveAt(unsigned index) = 0;
    virtual HRESULT STDMETHODCALLTYPE Append(IRadialControllerMenuItem* item) = 0;
};

struct IRadialControllerMenu : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Items(IMenuItemVector** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSelectedMenuItem(IRadialControllerMenuItem** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE SelectMenuItem(IRadialControllerMenuItem* menuItem) = 0;
};

struct IRadialController : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Menu(IRadialControllerMenu** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RotationResolutionInDegrees(double* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_RotationResolutionInDegrees(double value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_UseAutomaticHapticFeedback(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_UseAutomaticHapticFeedback(boolean value) = 0;
};

struct IRadialControllerMenuItemStatics : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE CreateFromIcon(HSTRING displayText, IUnknown* icon,
                                                     IRadialControllerMenuItem** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromKnownIcon(HSTRING displayText, int value,
                                                          IRadialControllerMenuItem** result) = 0;
};

/// IIterator<RadialControllerSystemMenuItemKind>
struct IKindIterator : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Current(int* current) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* hasCurrent) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveNext(boolean* hasCurrent) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(unsigned capacity, int* value, unsigned* actual) = 0;
};

/// IIterable<RadialControllerSystemMenuItemKind>
struct IKindIterable : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE First(IKindIterator** first) = 0;
};

struct IRadialControllerConfiguration : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE SetDefaultMenuItems(IKindIterable* buttons) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetToDefaultMenuItems() = 0;
};

struct IRadialControllerConfiguration2 : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE put_ActiveControllerWhenMenuIsSuppressed(IRadialController* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ActiveControllerWhenMenuIsSuppressed(IRadialController** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsMenuSuppressed(boolean value) = 0;
};

struct IRadialControllerInterop : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE CreateForWindow(HWND hwnd, REFIID riid, void** ppv) = 0;
};

struct IRadialControllerConfigurationInterop : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE GetForWindow(HWND hwnd, REFIID riid, void** ppv) = 0;
};
}  // namespace WheelMenuAbi
/// endregion </The ABI>

namespace
{
using namespace WheelMenuAbi;

/// region <An empty IIterable<RadialControllerSystemMenuItemKind>: no built-in tools>
/// IUnknown and IInspectable of a small agile object, freed with its last reference
template <class Derived, class Interface>
class Inspectable : public Interface
{
public:
    explicit Inspectable(const IID& iid) : _iid(iid)
    {
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;
        if (IsEqualIID(riid, _iid) || IsEqualIID(riid, kIidUnknown) || IsEqualIID(riid, kIidInspectable) ||
            IsEqualIID(riid, kIidAgileObject))
        {
            *object = static_cast<Interface*>(this);
            this->AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&_references));
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG references = InterlockedDecrement(&_references);
        if (references == 0)
            delete static_cast<Derived*>(this);
        return static_cast<ULONG>(references);
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** iids) override
    {
        if (count == nullptr || iids == nullptr)
            return E_POINTER;
        *count = 0;
        *iids = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override
    {
        if (name == nullptr)
            return E_POINTER;
        *name = nullptr;  // the empty string
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* level) override
    {
        if (level == nullptr)
            return E_POINTER;
        *level = BaseTrust;
        return S_OK;
    }

private:
    const IID _iid;
    volatile LONG _references = 1;
};

class EmptyKindIterator final : public Inspectable<EmptyKindIterator, IKindIterator>
{
public:
    EmptyKindIterator() : Inspectable(kIidKindIterator)
    {
    }
    HRESULT STDMETHODCALLTYPE get_Current(int* current) override
    {
        if (current != nullptr)
            *current = 0;
        return kBounds;
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* hasCurrent) override
    {
        if (hasCurrent == nullptr)
            return E_POINTER;
        *hasCurrent = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* hasCurrent) override
    {
        if (hasCurrent == nullptr)
            return E_POINTER;
        *hasCurrent = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned, int*, unsigned* actual) override
    {
        if (actual == nullptr)
            return E_POINTER;
        *actual = 0;
        return S_OK;
    }
};

class EmptyKinds final : public Inspectable<EmptyKinds, IKindIterable>
{
public:
    EmptyKinds() : Inspectable(kIidKindIterable)
    {
    }
    HRESULT STDMETHODCALLTYPE First(IKindIterator** first) override
    {
        if (first == nullptr)
            return E_POINTER;
        *first = new EmptyKindIterator();
        return S_OK;
    }
};
/// endregion </An empty IIterable>

/// region <combase.dll, loaded at run time (Windows 8+; no import library for MinGW or MSVC needed)>
using RoInitializeFn = HRESULT(WINAPI*)(int type);  // RO_INIT_TYPE
using RoGetActivationFactoryFn = HRESULT(WINAPI*)(HSTRING classId, REFIID iid, void** factory);
using WindowsCreateStringFn = HRESULT(WINAPI*)(const wchar_t* text, UINT32 length, HSTRING* string);
using WindowsDeleteStringFn = HRESULT(WINAPI*)(HSTRING string);

struct Combase
{
    RoInitializeFn roInitialize = nullptr;
    RoGetActivationFactoryFn roGetActivationFactory = nullptr;
    WindowsCreateStringFn createString = nullptr;
    WindowsDeleteStringFn deleteString = nullptr;
    bool Ok() const
    {
        return roInitialize && roGetActivationFactory && createString && deleteString;
    }
};

template <class Fn>
Fn Proc(HMODULE module, const char* name)
{
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

const Combase& LoadCombase()
{
    static const Combase combase = [] {
        Combase out;
        // Kept loaded for the process: the WinRT objects of a hold live in it
        HMODULE module = LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module == nullptr)
            return out;
        out.roInitialize = Proc<RoInitializeFn>(module, "RoInitialize");
        out.roGetActivationFactory = Proc<RoGetActivationFactoryFn>(module, "RoGetActivationFactory");
        out.createString = Proc<WindowsCreateStringFn>(module, "WindowsCreateString");
        out.deleteString = Proc<WindowsDeleteStringFn>(module, "WindowsDeleteString");
        return out;
    }();
    return combase;
}

/// An HSTRING deleted at the end of the scope
class HString
{
public:
    HString(const Combase& combase, const wchar_t* text) : _combase(combase)
    {
        if (FAILED(_combase.createString(text, static_cast<UINT32>(wcslen(text)), &_string)))
            _string = nullptr;
    }
    ~HString()
    {
        if (_string != nullptr)
            _combase.deleteString(_string);
    }
    HString(const HString&) = delete;
    HString& operator=(const HString&) = delete;
    HSTRING Get() const
    {
        return _string;
    }

private:
    const Combase& _combase;
    HSTRING _string = nullptr;
};

template <class T>
void SafeRelease(T*& object)
{
    if (object != nullptr)
    {
        object->Release();
        object = nullptr;
    }
}

template <class T>
HRESULT Factory(const Combase& combase, const wchar_t* classId, const IID& iid, T** factory)
{
    *factory = nullptr;
    HString name(combase, classId);
    if (name.Get() == nullptr)
        return E_OUTOFMEMORY;
    return combase.roGetActivationFactory(name.Get(), iid, reinterpret_cast<void**>(factory));
}
/// endregion </combase.dll>

struct Hold
{
    IRadialController* controller = nullptr;
    IMenuItemVector* items = nullptr;
    IRadialControllerMenuItem* item = nullptr;
    IRadialControllerConfiguration* configuration = nullptr;
    IRadialControllerConfiguration2* configuration2 = nullptr;
    bool cleared = false;     ///< the built-in tools were cleared for the window
    bool suppressed = false;  ///< the menu is suppressed for the window
};

void Free(Hold* hold)
{
    SafeRelease(hold->configuration2);
    SafeRelease(hold->configuration);
    SafeRelease(hold->item);
    SafeRelease(hold->items);
    SafeRelease(hold->controller);
    delete hold;
}

/// The hold's tool on the window's menu (none of the user's tools: the menu is suppressed, never shown)
bool AddTool(const Combase& combase, Hold& hold)
{
    IRadialControllerMenuItemStatics* statics = nullptr;
    if (FAILED(Factory(combase, L"Windows.UI.Input.RadialControllerMenuItem", kIidMenuItemStatics, &statics)))
        return false;
    HString text(combase, L"Unreal NG");
    const bool created =
        text.Get() != nullptr && SUCCEEDED(statics->CreateFromKnownIcon(text.Get(), kKnownIconNextPreviousTrack, &hold.item));
    SafeRelease(statics);
    if (!created)
        return false;
    IRadialControllerMenu* menu = nullptr;
    if (FAILED(hold.controller->get_Menu(&menu)))
        return false;
    bool added = SUCCEEDED(menu->get_Items(&hold.items)) && SUCCEEDED(hold.items->Append(hold.item));
    if (added)
        menu->SelectMenuItem(hold.item);  // the active tool while the window has the focus
    else
        SafeRelease(hold.items);
    SafeRelease(menu);
    return added;
}
}  // namespace

void* WheelMenuWindows::Take(void* hwnd)
{
    const Combase& combase = LoadCombase();
    if (hwnd == nullptr || !combase.Ok())
        return nullptr;
    const HWND window = static_cast<HWND>(hwnd);

    // The GUI thread is an STA already (Qt's OleInitialize): S_FALSE; an MTA thread answers RPC_E_CHANGED_MODE and
    // stays as it is. Never undone: an apartment torn down under the shell's cached wheel proxies fails the next
    // GetForWindow with 0x80070776 (measured on the Windows host with a thread that had no apartment of its own)
    const HRESULT init = combase.roInitialize(0 /* RO_INIT_SINGLETHREADED */);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE)
        return nullptr;
    Hold* hold = new Hold();

    // A controller of this window, with one tool of its own, selected: the turns reach that tool, which does nothing
    // (the emulator reads the Dial through HID), and Windows plays no clicks for it
    IRadialControllerInterop* interop = nullptr;
    if (SUCCEEDED(Factory(combase, L"Windows.UI.Input.RadialController", kIidRadialControllerInterop, &interop)))
        interop->CreateForWindow(window, kIidRadialController, reinterpret_cast<void**>(&hold->controller));
    SafeRelease(interop);
    if (hold->controller == nullptr || !AddTool(combase, *hold))
    {
        Free(hold);  // no wheel stack (Windows 10 before 1607, Server)
        return nullptr;
    }
    hold->controller->put_UseAutomaticHapticFeedback(FALSE);

    // The window's menu: none of the built-in tools, then suppressed with this controller active
    IRadialControllerConfigurationInterop* configurations = nullptr;
    HRESULT clear = Factory(combase, L"Windows.UI.Input.RadialControllerConfiguration", kIidConfigurationInterop,
                            &configurations);
    if (SUCCEEDED(clear))
        clear = configurations->GetForWindow(window, kIidConfiguration, reinterpret_cast<void**>(&hold->configuration));
    SafeRelease(configurations);
    HRESULT suppress = clear;
    if (hold->configuration != nullptr)
    {
        EmptyKinds* none = new EmptyKinds();
        clear = hold->configuration->SetDefaultMenuItems(none);
        none->Release();
        hold->cleared = SUCCEEDED(clear);
        // IRadialControllerConfiguration2: Windows 10 1703+
        suppress = hold->configuration->QueryInterface(kIidConfiguration2, reinterpret_cast<void**>(&hold->configuration2));
        if (SUCCEEDED(suppress))
            suppress = hold->configuration2->put_ActiveControllerWhenMenuIsSuppressed(hold->controller);
        if (SUCCEEDED(suppress))
            suppress = hold->configuration2->put_IsMenuSuppressed(TRUE);
        hold->suppressed = SUCCEEDED(suppress);
    }
    if (!hold->cleared || !hold->suppressed)
        qWarning("controllers: the wheel menu of window %p: built-in tools %s (0x%08lx), menu %s (0x%08lx)", hwnd,
                 hold->cleared ? "cleared" : "kept", static_cast<unsigned long>(clear),
                 hold->suppressed ? "suppressed" : "not suppressed", static_cast<unsigned long>(suppress));
    return hold;
}

void WheelMenuWindows::Release(void* handle)
{
    Hold* hold = static_cast<Hold*>(handle);
    if (hold == nullptr)
        return;
    // The window may be gone already: every call may fail, each is tried
    if (hold->configuration2 != nullptr)
    {
        hold->configuration2->put_IsMenuSuppressed(FALSE);
        hold->configuration2->put_ActiveControllerWhenMenuIsSuppressed(nullptr);
    }
    if (hold->configuration != nullptr && hold->cleared)
        hold->configuration->ResetToDefaultMenuItems();
    if (hold->items != nullptr && hold->item != nullptr)
    {
        unsigned index = 0;
        boolean found = FALSE;
        if (SUCCEEDED(hold->items->IndexOf(hold->item, &index, &found)) && found)
            hold->items->RemoveAt(index);
    }
    Free(hold);
}

#else

void* WheelMenuWindows::Take(void*)
{
    return nullptr;
}

void WheelMenuWindows::Release(void*)
{
}

#endif
