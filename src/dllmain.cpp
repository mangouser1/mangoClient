// Bedrock Client v0.2  -  FPS overlay only (stability first)
//  - ImGuiのWin32バックエンド/WndProcフックは使わない（クラッシュ要因を排除）
//  - 入力は GetAsyncKeyState のポーリングのみ（Lで設定、↑↓←→で操作）
//  - 毎フレームRTVを作って解放、描画ステート(RT)を保存/復元、例外は握りつぶして自動停止
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <MinHook.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cstdio>

using Clock = std::chrono::steady_clock;

using PresentFn  = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(__stdcall*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
static PresentFn  oPresent  = nullptr;
static Present1Fn oPresent1 = nullptr;

static volatile bool g_disabled = false;
static int  g_failures = 0;
static thread_local bool t_in = false;

static bool g_imguiReady = false, g_dxReady = false, g_started = false;
static ID3D11Device* g_dev = nullptr;
static Clock::time_point g_start, g_lastFrame;
static float g_fps = 0.f;

// ---------- 設定 ----------
struct Settings { bool fpsOn = true; int corner = 0; int scaleIdx = 1; };
static Settings g_set;
static const float kScales[]  = { 0.8f, 1.0f, 1.3f, 1.7f, 2.2f };
static const char* kCorners[] = { "Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right" };
static bool g_panel = false;
static int  g_sel = 0;

// ---------- 入力 ----------
static bool gameFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}
static bool pressed(int vk) {              // 毎フレーム全キーで呼ぶこと
    static bool prev[256];
    bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool r = d && !prev[vk];
    prev[vk] = d;
    return r;
}
static void handleInput() {
    bool f  = gameFocused();
    bool l  = pressed('L') && f,        up = pressed(VK_UP) && f,   dn = pressed(VK_DOWN) && f;
    bool lf = pressed(VK_LEFT) && f,    rt = pressed(VK_RIGHT) && f, en = pressed(VK_RETURN) && f;
    if (l) g_panel = !g_panel;
    if (!g_panel) return;
    if (up) g_sel = (g_sel + 2) % 3;
    if (dn) g_sel = (g_sel + 1) % 3;
    int d = (rt || en) ? 1 : (lf ? -1 : 0);
    if (!d) return;
    if (g_sel == 0) g_set.fpsOn = !g_set.fpsOn;
    else if (g_sel == 1) g_set.corner = (g_set.corner + d + 4) % 4;
    else g_set.scaleIdx = std::clamp(g_set.scaleIdx + d, 0, 4);
}

// ---------- 描画 ----------
static ImVec2 measure(float size, const char* s) { return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.f, s); }
static void   text(ImDrawList* dl, float size, ImVec2 p, ImU32 col, const char* s) { dl->AddText(ImGui::GetFont(), size, p, col, s); }

static void drawPanel(ImDrawList* dl, ImVec2 ds, float ui) {
    const float sz = 22 * ui, pad = 14 * ui, lh = sz + 10 * ui, gut = 22 * ui;
    char rows[3][64];
    snprintf(rows[0], 64, "FPS Display  : %s", g_set.fpsOn ? "ON" : "OFF");
    snprintf(rows[1], 64, "FPS Position : %s", kCorners[g_set.corner]);
    snprintf(rows[2], 64, "FPS Size     : x%.1f", kScales[g_set.scaleIdx]);
    const char* title = "Settings   [L] close";
    const char* foot  = "Up/Down: select   Left/Right: change";
    float w = std::max(measure(sz, title).x, measure(sz * 0.8f, foot).x);
    for (auto& r : rows) w = std::max(w, measure(sz, r).x + gut);
    w += 2 * pad;
    float h = pad * 2 + lh * 4 + sz * 0.8f;
    ImVec2 p((ds.x - w) / 2, (ds.y - h) / 2);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(15, 15, 20, 215), 8 * ui);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(120, 255, 140, 200), 8 * ui, 0, 2 * ui);
    float y = p.y + pad;
    text(dl, sz, ImVec2(p.x + pad, y), IM_COL32(120, 255, 140, 255), title); y += lh;
    for (int i = 0; i < 3; i++, y += lh) {
        if (i == g_sel) {
            dl->AddRectFilled(ImVec2(p.x + pad * 0.5f, y - 3 * ui), ImVec2(p.x + w - pad * 0.5f, y + sz + 3 * ui), IM_COL32(60, 120, 255, 130), 4 * ui);
            text(dl, sz, ImVec2(p.x + pad, y), IM_COL32_WHITE, ">");
        }
        text(dl, sz, ImVec2(p.x + pad + gut, y), IM_COL32_WHITE, rows[i]);
    }
    text(dl, sz * 0.8f, ImVec2(p.x + pad, y + 2 * ui), IM_COL32(200, 200, 200, 255), foot);
}

static void drawHud(ImVec2 ds) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float ui = std::max(0.6f, ds.y / 1080.f), m = 14 * ui;

    // 読み込み完了バナー（右下・10秒、最後の2秒でフェード）
    double t = std::chrono::duration<double>(Clock::now() - g_start).count();
    float bannerH = 0;
    if (t < 10.0) {
        float a = t < 8.0 ? 1.f : (float)(1.0 - (t - 8.0) / 2.0);
        const char* s = "BedrockClient v0.2 loaded!  Press L for settings";
        float sz = 20 * ui, pad = 8 * ui; ImVec2 ts = measure(sz, s);
        ImVec2 p(ds.x - ts.x - 2 * pad - m, ds.y - ts.y - 2 * pad - m);
        dl->AddRectFilled(p, ImVec2(p.x + ts.x + 2 * pad, p.y + ts.y + 2 * pad), IM_COL32(0, 0, 0, (int)(180 * a)), 6 * ui);
        text(dl, sz, ImVec2(p.x + pad, p.y + pad), IM_COL32(120, 255, 140, (int)(255 * a)), s);
        bannerH = ts.y + 2 * pad + 8 * ui;
    }

    // FPS表示
    if (g_set.fpsOn) {
        char buf[32]; snprintf(buf, sizeof buf, "%d FPS", (int)(g_fps + 0.5f));
        float sc = kScales[g_set.scaleIdx], sz = 20 * ui * sc, pad = 7 * ui * sc;
        ImVec2 ts = measure(sz, buf); float w = ts.x + 2 * pad, h = ts.y + 2 * pad;
        float x = (g_set.corner == 1 || g_set.corner == 3) ? ds.x - w - m : m;
        float y = (g_set.corner >= 2) ? ds.y - h - m - (g_set.corner == 3 ? bannerH : 0) : m;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0, 0, 0, 140), 5 * ui);
        text(dl, sz, ImVec2(x + pad, y + pad), IM_COL32_WHITE, buf);
    }
    if (g_panel) drawPanel(dl, ds, ui);
}

static DXGI_FORMAT rtvFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:    return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:    return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

static void renderFrame(IDXGISwapChain* sc) {
    ID3D11Device* dev = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev)) || !dev) {
        g_disabled = true;                       // DX12描画など：非対応なので何もしない
        return;
    }
    ID3D11DeviceContext* ctx = nullptr; dev->GetImmediateContext(&ctx);
    ID3D11Texture2D* bb = nullptr;
    if (!ctx || FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) {
        if (ctx) ctx->Release();
        dev->Release(); return;
    }
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
    if (td.SampleDesc.Count > 1) { bb->Release(); ctx->Release(); dev->Release(); return; }

    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = rtvFormat(td.Format); rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ID3D11RenderTargetView* rtv = nullptr;
    HRESULT hr = dev->CreateRenderTargetView(bb, &rd, &rtv);
    bb->Release();
    if (FAILED(hr) || !rtv) { ctx->Release(); dev->Release(); return; }

    if (!g_imguiReady) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        g_imguiReady = true;
    }
    if (dev != g_dev) {                           // デバイスが変わったら作り直す
        if (g_dxReady) ImGui_ImplDX11_Shutdown();
        g_dxReady = ImGui_ImplDX11_Init(dev, ctx);
        g_dev = dev;
    }
    if (g_dxReady) {
        auto now = Clock::now();
        if (!g_started) { g_started = true; g_start = g_lastFrame = now; }
        float dt = std::chrono::duration<float>(now - g_lastFrame).count();
        g_lastFrame = now;
        if (dt > 0.f) { float inst = 1.f / dt; g_fps = g_fps <= 0.f ? inst : g_fps + (inst - g_fps) * 0.05f; }
        dt = std::clamp(dt, 0.001f, 0.1f);

        handleInput();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)td.Width, (float)td.Height);
        io.DeltaTime = dt;

        ID3D11RenderTargetView* oldRtv[1] = { nullptr }; ID3D11DepthStencilView* oldDsv = nullptr;
        ctx->OMGetRenderTargets(1, oldRtv, &oldDsv);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);

        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        drawHud(io.DisplaySize);
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        ctx->OMSetRenderTargets(1, oldRtv, oldDsv);
        if (oldRtv[0]) oldRtv[0]->Release();
        if (oldDsv) oldDsv->Release();
    }
    rtv->Release(); ctx->Release(); dev->Release();
}

// __try/__except専用の薄いラッパー（アンワインド対象オブジェクトを持たない）
static void safeRender(IDXGISwapChain* sc) {
#ifdef _MSC_VER
    __try { renderFrame(sc); }
    __except (EXCEPTION_EXECUTE_HANDLER) { if (++g_failures >= 3) g_disabled = true; }
#else
    renderFrame(sc);
#endif
}

static HRESULT __stdcall hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (t_in || g_disabled) return oPresent(sc, sync, flags);
    t_in = true;
    safeRender(sc);
    HRESULT r = oPresent(sc, sync, flags);
    t_in = false;
    return r;
}
static HRESULT __stdcall hkPresent1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* pp) {
    if (t_in || g_disabled) return oPresent1(sc, sync, flags, pp);
    t_in = true;
    safeRender(sc);
    HRESULT r = oPresent1(sc, sync, flags, pp);
    t_in = false;
    return r;
}

// ---------- フック設置 ----------
static bool getVTable(void** vt, bool* hasP1) {
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
    *hasP1 = false;
    if (ok) {
        memcpy(vt, *(void***)sc, sizeof(void*) * 18);
        IDXGISwapChain1* sc1 = nullptr;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1)) && sc1) {
            vt[18] = (*(void***)sc1)[22];       // Present1
            *hasP1 = true; sc1->Release();
        }
    }
    if (sc) sc->Release();
    if (c) c->Release();
    if (d) d->Release();
    DestroyWindow(w); UnregisterClassW(L"bc_dummy", wc.hInstance);
    return ok;
}

static DWORD WINAPI initThread(LPVOID) {
    Sleep(1500);                                 // ゲーム側の初期化を待つ
    void* vt[19] = {};
    bool p1 = false;
    if (!getVTable(vt, &p1) || MH_Initialize() != MH_OK) return 0;
    if (MH_CreateHook(vt[8], (void*)&hkPresent, (void**)&oPresent) == MH_OK) MH_EnableHook(vt[8]);
    if (p1 && MH_CreateHook(vt[18], (void*)&hkPresent1, (void**)&oPresent1) == MH_OK) MH_EnableHook(vt[18]);
    return 0;                                    // アンロードはしない（ゲーム再起動で解除）
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        HANDLE t = CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
