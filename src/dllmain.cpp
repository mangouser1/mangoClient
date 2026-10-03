#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include "modules.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn  = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
static PresentFn oPresent; static ResizeFn oResize;
static WNDPROC oWndProc; static HWND g_hwnd;
static ID3D11Device* g_dev; static ID3D11DeviceContext* g_ctx; static ID3D11RenderTargetView* g_rtv;
static bool g_init = false, g_menu = false;
static HMODULE g_mod;
static std::vector<Module> g_modules;

static void makeRTV(IDXGISwapChain* sc) {
    ID3D11Texture2D* bb = nullptr;
    if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) {
        g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
        bb->Release();
    }
}
static void dropRTV() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

static LRESULT CALLBACK hkWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_menu && ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    return CallWindowProcW(oWndProc, h, m, w, l);
}

static HRESULT __stdcall hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!g_init) {
        if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&g_dev)))
            return oPresent(sc, sync, flags);           // DX12 rendering: unsupported
        g_dev->GetImmediateContext(&g_ctx);
        DXGI_SWAP_CHAIN_DESC d; sc->GetDesc(&d); g_hwnd = d.OutputWindow;
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui_ImplWin32_Init(g_hwnd);
        ImGui_ImplDX11_Init(g_dev, g_ctx);
        oWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
        g_modules = mods::create();
        g_init = true;
    }
    if (!g_rtv) makeRTV(sc);

    static bool prevIns = false;
    bool ins = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (ins && !prevIns) g_menu = !g_menu;
    prevIns = ins;

    mods::trackClicks(g_hwnd);
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor = g_menu;
    for (auto& m : g_modules) if (m.enabled) m.render(m);
    if (g_menu) mods::menu(g_modules);
    ImGui::Render();
    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return oPresent(sc, sync, flags);
}

static HRESULT __stdcall hkResize(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl) {
    dropRTV();
    return oResize(sc, n, w, h, f, fl);
}

static bool getVTable(void** vt) {
    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), 0, 0, 0, 0, L"bc_dummy", 0 };
    RegisterClassExW(&wc);
    HWND w = CreateWindowW(L"bc_dummy", L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, wc.hInstance, 0);
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = w;
    sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr; ID3D11Device* d = nullptr; ID3D11DeviceContext* c = nullptr;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &sd, &sc, &d, nullptr, &c);
    bool ok = SUCCEEDED(hr);
    if (ok) memcpy(vt, *(void***)sc, sizeof(void*) * 18);
    if (sc) sc->Release();
    if (c) c->Release();
    if (d) d->Release();
    DestroyWindow(w); UnregisterClassW(L"bc_dummy", wc.hInstance);
    return ok;
}

static DWORD WINAPI mainThread(LPVOID) {
    void* vt[18];
    if (!getVTable(vt) || MH_Initialize() != MH_OK) { FreeLibraryAndExitThread(g_mod, 0); }
    MH_CreateHook(vt[8],  &hkPresent, (void**)&oPresent);
    MH_CreateHook(vt[13], &hkResize,  (void**)&oResize);
    MH_EnableHook(MH_ALL_HOOKS);

    while (!(GetAsyncKeyState(VK_END) & 0x8000)) Sleep(50);   // END to unload

    MH_DisableHook(MH_ALL_HOOKS);
    if (g_init && oWndProc) SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
    Sleep(300);
    MH_Uninitialize();
    FreeLibraryAndExitThread(g_mod, 0);
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_mod = h; DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, mainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
