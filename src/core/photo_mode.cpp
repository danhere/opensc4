// Photo mode (photo_mode.h; docs/photo-mode.md).
#include "core/photo_mode.h"

#include "core/gd/gd.h"
#include "core/log.h"
#include "core/settings.h"

#include <GZServPtrs.h>
#include <cIGZWin.h>
#include <cISC43DRender.h>
#include <cISC4App.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <opensc4/addresses.h>

#include <objbase.h>
#include <wincodec.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace opensc4::photo_mode {
namespace {

constexpr uint32_t kWinSC4App = 0x6104489A;
constexpr uint32_t kWinView3D = 0x9A47B417;
constexpr std::size_t kFineZoom = 0xF0; // cSC4CameraControl (docs/renderer.md, "The camera control")

bool g_active = false;
bool g_swallowChar = false;          // the WM_CHAR of a key we took
std::vector<cIGZWin*> g_hidden;      // referenced while hidden

cIGZWin* CityParent(cISC4App* app)
{
    cIGZWin* main = app ? app->GetMainWindow() : nullptr;
    return main ? main->GetChildWindowFromID(kWinSC4App) : nullptr;
}

void* CameraControl()
{
    cISC4AppPtr app;
    cIGZWin* parent = CityParent(app);
    cRZAutoRefCount<cISC4View3DWin> view;
    if (!parent || !parent->GetChildAs(kWinView3D, kGZIID_cISC4View3DWin, view.AsPPVoid()) || !view)
        return nullptr;
    cISC43DRender* renderer = view->GetRenderer();
    return renderer ? renderer->GetCameraControl() : nullptr;
}

float FineZoom(void* control) { return *reinterpret_cast<float*>(static_cast<char*>(control) + kFineZoom); }

void SetFineZoom(void* control, float factor)
{
    using Fn = void(__thiscall*)(void*, float);
    reinterpret_cast<Fn>(addr::cSC4CameraControl_SetZoomFactor)(control, factor);
}

// Every visible window but the app frame and the city view: the app's and
// the city view's children (the control panel, the pause frame).
void HideInterface()
{
    cISC4AppPtr app;
    cIGZWin* main = app ? app->GetMainWindow() : nullptr;
    cIGZWin* parent = CityParent(app);
    const auto hide = [](cIGZWin*, uint32_t id, void* child, void*) -> bool {
        auto* win = static_cast<cIGZWin*>(child);
        if (id != kWinSC4App && id != kWinView3D && win->IsVisible()) {
            win->AddRef();
            g_hidden.push_back(win);
            win->HideWindow();
        }
        return true;
    };
    if (main)
        main->EnumChildren(GZIID_cIGZWin, hide, nullptr);
    if (parent) {
        parent->EnumChildren(GZIID_cIGZWin, hide, nullptr);
        if (cIGZWin* view = parent->GetChildWindowFromID(kWinView3D))
            view->EnumChildren(GZIID_cIGZWin, hide, nullptr);
    }
}

void ShowInterface()
{
    for (cIGZWin* win : g_hidden) {
        win->ShowWindow();
        win->Release();
    }
    g_hidden.clear();
}

std::wstring Folder()
{
    const std::string configured = settings::Get("photo", "folder", "");
    if (!configured.empty()) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, configured.c_str(), -1, nullptr, 0);
        std::wstring wide(n > 0 ? n - 1 : 0, L'\0');
        if (n > 0)
            MultiByteToWideChar(CP_UTF8, 0, configured.c_str(), -1, wide.data(), n);
        return wide;
    }
    wchar_t profile[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return L".";
    return std::wstring(profile) + L"\\Pictures\\OpenSC4";
}

// Creates `folder` and its missing parents.
void MakeFolder(const std::wstring& folder)
{
    for (std::size_t at = folder.find(L'\\', 3); at != std::wstring::npos; at = folder.find(L'\\', at + 1))
        CreateDirectoryW(folder.substr(0, at).c_str(), nullptr);
    CreateDirectoryW(folder.c_str(), nullptr);
}

// The WIC objects, from DLLs loaded at run time (core.dll imports only KERNEL32).
constexpr CLSID kWicFactory = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
constexpr GUID kPngFormat = {0x1b7cfaf4, 0x713f, 0x473c, {0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf}};
constexpr GUID kBgr24 = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0c}};

template <typename T>
void Release(T*& p)
{
    if (p)
        p->Release();
    p = nullptr;
}

// 24-bit PNG from BGRA rows (alpha dropped).
bool WritePng(const std::wstring& path, const std::vector<uint8_t>& bgra, uint32_t width, uint32_t height)
{
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    if (!ole)
        return false;
    using InitFn = HRESULT(WINAPI*)(void*, DWORD);
    using UninitFn = void(WINAPI*)();
    using CreateFn = HRESULT(WINAPI*)(REFCLSID, IUnknown*, DWORD, REFIID, void**);
    const auto init = reinterpret_cast<InitFn>(GetProcAddress(ole, "CoInitializeEx"));
    const auto uninit = reinterpret_cast<UninitFn>(GetProcAddress(ole, "CoUninitialize"));
    const auto create = reinterpret_cast<CreateFn>(GetProcAddress(ole, "CoCreateInstance"));
    if (!init || !uninit || !create)
        return false;
    const HRESULT initialised = init(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<uint8_t> bgr(static_cast<std::size_t>(width) * height * 3);
    for (std::size_t i = 0, n = static_cast<std::size_t>(width) * height; i < n; ++i) {
        bgr[i * 3] = bgra[i * 4];
        bgr[i * 3 + 1] = bgra[i * 4 + 1];
        bgr[i * 3 + 2] = bgra[i * 4 + 2];
    }
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    WICPixelFormatGUID format = kBgr24;
    bool ok = SUCCEEDED(create(kWicFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
                               reinterpret_cast<void**>(&factory))) &&
              SUCCEEDED(factory->CreateStream(&stream)) &&
              SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(factory->CreateEncoder(kPngFormat, nullptr, &encoder)) &&
              SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
              SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
              format == kBgr24 &&
              SUCCEEDED(frame->WritePixels(height, width * 3, static_cast<UINT>(bgr.size()), bgr.data())) &&
              SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    Release(frame);
    Release(encoder);
    Release(stream);
    Release(factory);
    if (SUCCEEDED(initialised))
        uninit();
    return ok;
}

std::string Narrow(const std::wstring& wide)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string text(n > 0 ? n - 1 : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, text.data(), n, nullptr, nullptr);
    return text;
}

} // namespace

bool Active() { return g_active; }

void Enter()
{
    if (g_active || !CameraControl())
        return;
    g_active = true;
    HideInterface();
    gd::PresentWhileActive();
    log::Write("photo mode: on (%zu interface windows hidden)", g_hidden.size());
}

void Exit()
{
    if (!g_active)
        return;
    g_active = false;
    if (void* control = CameraControl(); control && FineZoom(control) != 1.0f)
        SetFineZoom(control, 1.0f);
    ShowInterface();
    log::Write("photo mode: off");
}

void Zoom(float step)
{
    void* control = CameraControl();
    if (!control)
        return;
    float factor = FineZoom(control) * step;
    factor = factor < 0.25f ? 0.25f : factor > 8.0f ? 8.0f : factor;
    SetFineZoom(control, factor);
}

std::wstring Capture()
{
    std::vector<uint8_t> pixels;
    uint32_t width = 0, height = 0;
    if (!gd::ReadFrame(pixels, width, height)) {
        log::Write("photo mode: no frame to capture (is the D3D11 driver on?)");
        return {};
    }
    const std::wstring folder = Folder();
    MakeFolder(folder);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    std::wstring path;
    for (int n = 0; n < 100; ++n) {
        std::swprintf(name, 64, n ? L"\\photo-%04u%02u%02u-%02u%02u%02u-%d.png" : L"\\photo-%04u%02u%02u-%02u%02u%02u.png",
                      t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, n);
        path = folder + name;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            break;
    }
    if (!WritePng(path, pixels, width, height)) {
        log::Write("photo mode: could not write %s", Narrow(path).c_str());
        return {};
    }
    log::Write("photo mode: %ux%u saved to %s", width, height, Narrow(path).c_str());
    return path;
}

bool OnKey(UINT message, WPARAM key)
{
    if (message == WM_CHAR || message == WM_SYSCHAR) {
        const bool swallow = g_swallowChar || g_active;
        g_swallowChar = false;
        return swallow && message == WM_CHAR;
    }
    if (message != WM_KEYDOWN && message != WM_KEYUP)
        return false;
    const bool down = message == WM_KEYDOWN;
    const bool control = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (key == 'P' && control && shift) {
        if (down)
            g_active ? Exit() : Enter();
        g_swallowChar = down;
        return true;
    }
    if (!g_active)
        return false;
    g_swallowChar = down;
    switch (key) {
    case VK_PRIOR: if (down) Zoom(1.125f); return true;
    case VK_NEXT: if (down) Zoom(1.0f / 1.125f); return true;
    case VK_HOME:
        if (down)
            if (void* c = CameraControl())
                SetFineZoom(c, 1.0f);
        return true;
    case VK_RETURN:
    case 'P':
        if (down)
            Capture();
        return true;
    case VK_ESCAPE:
        if (down)
            Exit();
        return true;
    default:
        g_swallowChar = false;
        return false; // the game's keys (arrows scroll, the zoom and rotation keys) still work
    }
}

} // namespace opensc4::photo_mode
