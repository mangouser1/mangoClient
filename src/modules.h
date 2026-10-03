// HUDモジュール群。ゲームメモリには一切触れず、オーバーレイ描画のみ行う。
#pragma once
#include <imgui.h>
#include <windows.h>
#include <deque>
#include <chrono>
#include <ctime>
#include <string>
#include <vector>
#include <functional>

struct Module {
    std::string name;
    bool enabled = true;
    ImVec2 pos;
    std::function<void(Module&)> render;
};

namespace mods {
using Clock = std::chrono::steady_clock;
inline std::deque<Clock::time_point> lmb, rmb;

inline void trackClicks(HWND game) {
    static bool pl = false, pr = false;
    bool fg = GetForegroundWindow() == game;
    bool l = fg && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
    bool r = fg && (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
    auto now = Clock::now();
    if (l && !pl) lmb.push_back(now);
    if (r && !pr) rmb.push_back(now);
    pl = l; pr = r;
    for (auto* q : {&lmb, &rmb})
        while (!q->empty() && now - q->front() > std::chrono::seconds(1)) q->pop_front();
}

inline void textBox(Module& m, const std::string& s) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 sz = ImGui::CalcTextSize(s.c_str());
    ImVec2 a = m.pos, b(a.x + sz.x + 16, a.y + sz.y + 10);
    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 140), 4.f);
    dl->AddText(ImVec2(a.x + 8, a.y + 5), IM_COL32_WHITE, s.c_str());
}

inline void keyBox(ImDrawList* dl, ImVec2 p, float w, float h, const char* label, bool down) {
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), down ? IM_COL32(255, 255, 255, 190) : IM_COL32(0, 0, 0, 140), 4.f);
    ImVec2 t = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(p.x + (w - t.x) / 2, p.y + (h - t.y) / 2), down ? IM_COL32_BLACK : IM_COL32_WHITE, label);
}

inline std::vector<Module> create() {
    std::vector<Module> v;
    v.push_back({"FPS", true, ImVec2(10, 10), [](Module& m) {
        textBox(m, std::to_string((int)ImGui::GetIO().Framerate) + " FPS"); }});
    v.push_back({"CPS", true, ImVec2(10, 45), [](Module& m) {
        textBox(m, std::to_string(lmb.size()) + " | " + std::to_string(rmb.size()) + " CPS"); }});
    v.push_back({"Clock", false, ImVec2(10, 80), [](Module& m) {
        time_t t = time(nullptr); tm lt; localtime_s(&lt, &t);
        char buf[16]; strftime(buf, sizeof buf, "%H:%M:%S", &lt); textBox(m, buf); }});
    v.push_back({"Keystrokes", true, ImVec2(10, 120), [](Module& m) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float s = 40, g = 4; ImVec2 o = m.pos;
        auto dn = [](int k) { return (GetAsyncKeyState(k) & 0x8000) != 0; };
        keyBox(dl, ImVec2(o.x + s + g, o.y), s, s, "W", dn('W'));
        keyBox(dl, ImVec2(o.x, o.y + s + g), s, s, "A", dn('A'));
        keyBox(dl, ImVec2(o.x + s + g, o.y + s + g), s, s, "S", dn('S'));
        keyBox(dl, ImVec2(o.x + 2 * (s + g), o.y + s + g), s, s, "D", dn('D'));
        keyBox(dl, ImVec2(o.x, o.y + 2 * (s + g)), s * 1.5f, 30, "LMB", dn(VK_LBUTTON));
        keyBox(dl, ImVec2(o.x + s * 1.5f + g, o.y + 2 * (s + g)), s * 1.5f, 30, "RMB", dn(VK_RBUTTON));
        keyBox(dl, ImVec2(o.x, o.y + 2 * (s + g) + 34), s * 3 + 2 * g, 24, "SPACE", dn(VK_SPACE)); }});
    return v;
}

inline void menu(std::vector<Module>& v) {
    ImGui::SetNextWindowSize(ImVec2(320, 360), ImGuiCond_FirstUseEver);
    ImGui::Begin("Bedrock Client  [INSERT: toggle / END: unload]");
    for (auto& m : v) {
        ImGui::PushID(m.name.c_str());
        ImGui::Checkbox(m.name.c_str(), &m.enabled);
        ImGui::SameLine(140); ImGui::SetNextItemWidth(150);
        ImGui::DragFloat2("##pos", &m.pos.x, 1.f, 0.f, 4000.f, "%.0f");
        ImGui::PopID();
    }
    ImGui::TextDisabled("Drag values to move modules (x, y)");
    ImGui::End();
}
} // namespace mods
