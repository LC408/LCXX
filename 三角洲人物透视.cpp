// DeltaESP.cpp — 三角洲行动 透视（外部读取 + 叠加绘制）
// 编译：VS2022 x64，链接 dwmapi.lib gdi32.lib user32.lib
// 偏移必须用 CE 按当前游戏版本替换 Offsets 里的 0x0 项

#include <windows.h>
#include <TlHelp32.h>
#include <dwmapi.h>
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <chrono>
#include <thread>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// ============================================================
// Offsets.hpp
// ============================================================
namespace Offsets {
    constexpr uintptr_t GWorld         = 0x0;   // ← CE 找 UWorld 全局指针
    constexpr uintptr_t LevelOffset    = 0x30;  // UWorld -> PersistentLevel
    constexpr uintptr_t ActorsOffset   = 0xA0;  // ULevel -> Actors TArray data
    constexpr uintptr_t ActorCount     = 0xA8;  // ULevel -> Actors count
    constexpr uintptr_t RootComp       = 0x130; // AActor -> RootComponent
    constexpr uintptr_t Location       = 0x158; // USceneComponent -> RelativeLocation
    constexpr uintptr_t LocalPlayer    = 0x0;   // ← UWorld -> LocalPlayer
    constexpr uintptr_t CameraManager  = 0x0;   // ← PlayerController -> CameraManager
    constexpr uintptr_t CameraCache    = 0x0;   // ← CameraManager -> CameraCache.POV
    constexpr uintptr_t TeamIdOffset   = 0x0;   // ← AActor -> TeamId（可选，做阵营过滤）
    constexpr uintptr_t HeadOffsetZ    = 70.0f; // 身高近似，按游戏单位调
    constexpr float     DistDivisor    = 100.0f;// cm -> m，按需调
}

// ============================================================
// Math.hpp
// ============================================================
struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

struct Matrix4x4 {
    float m[4][4];
};

inline bool WorldToScreen(const Vec3& world, Vec2& screen,
                          const Matrix4x4& vp, int width, int height) {
    float clipX = world.x * vp.m[0][0] + world.y * vp.m[1][0] +
                  world.z * vp.m[2][0] + vp.m[3][0];
    float clipY = world.x * vp.m[0][1] + world.y * vp.m[1][1] +
                  world.z * vp.m[2][1] + vp.m[3][1];
    float clipW = world.x * vp.m[0][3] + world.y * vp.m[1][3] +
                  world.z * vp.m[2][3] + vp.m[3][3];

    if (clipW < 0.001f) return false;

    float ndcX = clipX / clipW;
    float ndcY = clipY / clipW;

    screen.x = (width  * 0.5f) * (ndcX + 1.0f);
    screen.y = (height * 0.5f) * (1.0f - ndcY);

    return screen.x >= 0 && screen.x <= width &&
           screen.y >= 0 && screen.y <= height;
}

// ============================================================
// Memory.hpp
// ============================================================
class Memory {
public:
    bool Attach(const std::wstring& procName) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;

        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                if (procName == pe.szExeFile) { pid = pe.th32ProcessID; break; }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        if (!pid) return false;

        hProc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                            FALSE, pid);
        return hProc != nullptr;
    }

    template<typename T>
    T Read(uintptr_t addr) const {
        T out{};
        if (!hProc) return out;
        SIZE_T bytes = 0;
        ReadProcessMemory(hProc, reinterpret_cast<LPCVOID>(addr),
                          &out, sizeof(T), &bytes);
        return out;
    }

    bool ReadBuffer(uintptr_t addr, void* buf, size_t size) const {
        if (!hProc) return false;
        SIZE_T bytes = 0;
        return ReadProcessMemory(hProc, reinterpret_cast<LPCVOID>(addr),
                                 buf, size, &bytes) && bytes == size;
    }

    uintptr_t GetModuleBase(const std::wstring& modName) const {
        HANDLE snap = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE) return 0;

        MODULEENTRY32W me{ sizeof(me) };
        uintptr_t base = 0;
        if (Module32FirstW(snap, &me)) {
            do {
                if (modName == me.szModule) {
                    base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                    break;
                }
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
        return base;
    }

    bool IsOpen() const { return hProc != nullptr; }
    ~Memory() { if (hProc) CloseHandle(hProc); }

private:
    HANDLE hProc = nullptr;
    DWORD  pid   = 0;
};

// ============================================================
// ESP.hpp
// ============================================================
struct EntityBox {
    Vec2 topLeft, topRight, bottomLeft, bottomRight;
    float distance;
};

class ESP {
public:
    ESP(const Memory& mem, uintptr_t base) : m_mem(mem), m_base(base) {}

    Matrix4x4 GetViewProjection() const {
        uintptr_t gworld = m_mem.Read<uintptr_t>(m_base + Offsets::GWorld);
        if (!gworld) return {};
        uintptr_t lp = m_mem.Read<uintptr_t>(gworld + Offsets::LocalPlayer);
        if (!lp) return {};
        uintptr_t cm = m_mem.Read<uintptr_t>(lp + Offsets::CameraManager);
        if (!cm) return {};

        Matrix4x4 vp{};
        m_mem.ReadBuffer(cm + Offsets::CameraCache, &vp, sizeof(vp));
        return vp;
    }

    Vec3 GetCameraLocation() const {
        uintptr_t gworld = m_mem.Read<uintptr_t>(m_base + Offsets::GWorld);
        if (!gworld) return {};
        uintptr_t lp = m_mem.Read<uintptr_t>(gworld + Offsets::LocalPlayer);
        if (!lp) return {};
        uintptr_t cm = m_mem.Read<uintptr_t>(lp + Offsets::CameraManager);
        if (!cm) return {};

        Vec3 loc{};
        m_mem.ReadBuffer(cm + Offsets::CameraCache, &loc, sizeof(loc));
        return loc;
    }

    std::vector<EntityBox> Collect(int screenW, int screenH) const {
        std::vector<EntityBox> out;
        if (!m_mem.IsOpen()) return out;

        uintptr_t gworld = m_mem.Read<uintptr_t>(m_base + Offsets::GWorld);
        if (!gworld) return out;

        uintptr_t level = m_mem.Read<uintptr_t>(gworld + Offsets::LevelOffset);
        if (!level) return out;

        uintptr_t actors = m_mem.Read<uintptr_t>(level + Offsets::ActorsOffset);
        int count = m_mem.Read<int>(level + Offsets::ActorCount);
        if (!actors || count <= 0 || count > 10000) return out;

        Vec3 camLoc = GetCameraLocation();
        Matrix4x4 vp = GetViewProjection();

        for (int i = 0; i < count; ++i) {
            uintptr_t actor = m_mem.Read<uintptr_t>(actors + i * sizeof(uintptr_t));
            if (!actor) continue;

            uintptr_t root = m_mem.Read<uintptr_t>(actor + Offsets::RootComp);
            if (!root) continue;

            Vec3 world = m_mem.Read<Vec3>(root + Offsets::Location);

            Vec2 origin;
            if (!WorldToScreen(world, origin, vp, screenW, screenH)) continue;

            Vec3 headWorld{ world.x, world.y, world.z + Offsets::HeadOffsetZ };
            Vec2 head;
            if (!WorldToScreen(headWorld, head, vp, screenW, screenH)) continue;

            float h = origin.y - head.y;
            if (h <= 2.0f || h > screenH) continue;
            float w = h * 0.4f;

            EntityBox box{};
            box.bottomLeft  = { origin.x - w * 0.5f, origin.y };
            box.bottomRight = { origin.x + w * 0.5f, origin.y };
            box.topLeft     = { origin.x - w * 0.5f, head.y };
            box.topRight    = { origin.x + w * 0.5f, head.y };

            float dx = world.x - camLoc.x;
            float dy = world.y - camLoc.y;
            float dz = world.z - camLoc.z;
            box.distance = std::sqrt(dx*dx + dy*dy + dz*dz) / Offsets::DistDivisor;

            out.push_back(box);
        }
        return out;
    }

private:
    const Memory& m_mem;
    uintptr_t     m_base;
};

// ============================================================
// Overlay.hpp
// ============================================================
class Overlay {
public:
    bool Create(const std::wstring& title, int w, int h) {
        width = w;
        height = h;

        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"DeltaESPOverlay";
        RegisterClassExW(&wc);

        hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
            wc.lpszClassName, title.c_str(), WS_POPUP,
            0, 0, w, h, nullptr, nullptr, wc.hInstance, nullptr);

        if (!hwnd) return false;

        SetLayeredWindowAttributes(hwnd, RGB(0,0,0), 0, LWA_COLORKEY);
        MARGINS margins = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

    void Clear() const {
        HDC hdc = GetDC(hwnd);
        RECT r{ 0, 0, width, height };
        HBRUSH black = CreateSolidBrush(RGB(0,0,0));
        FillRect(hdc, &r, black);
        DeleteObject(black);
        ReleaseDC(hwnd, hdc);
    }

    void DrawBox(const Vec2& tl, const Vec2& tr,
                 const Vec2& bl, const Vec2& br,
                 COLORREF color, int thickness = 1) const {
        HDC hdc = GetDC(hwnd);
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HGDIOBJ old = SelectObject(hdc, pen);

        MoveToEx(hdc, (int)tl.x, (int)tl.y, nullptr);
        LineTo(hdc, (int)tr.x, (int)tr.y);
        LineTo(hdc, (int)br.x, (int)br.y);
        LineTo(hdc, (int)bl.x, (int)bl.y);
        LineTo(hdc, (int)tl.x, (int)tl.y);

        SelectObject(hdc, old);
        DeleteObject(pen);
        ReleaseDC(hdc, hwnd);
    }

    void DrawText(const Vec2& pos, const std::wstring& text, COLORREF color) const {
        HDC hdc = GetDC(hwnd);
        SetTextColor(hdc, color);
        SetBkMode(hdc, TRANSPARENT);
        TextOutW(hdc, (int)pos.x, (int)pos.y, text.c_str(), (int)text.size());
        ReleaseDC(hwnd, hdc);
    }

    HWND Hwnd()  const { return hwnd; }
    int  Width() const { return width; }
    int  Height() const { return height; }

    ~Overlay() { if (hwnd) DestroyWindow(hwnd); }

private:
    HWND hwnd   = nullptr;
    int  width  = 0;
    int  height = 0;
};

// ============================================================
// main
// ============================================================
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    Memory mem;
    if (!mem.Attach(L"DeltaForceClient-Win64-Shipping.exe")) {
        MessageBoxW(nullptr, L"进程未找到，先启动游戏", L"DeltaESP", MB_OK);
        return 1;
    }

    uintptr_t base = mem.GetModuleBase(L"DeltaForceClient-Win64-Shipping.exe");
    if (!base) {
        MessageBoxW(nullptr, L"模块基址获取失败", L"DeltaESP", MB_OK);
        return 1;
    }

    Overlay overlay;
    if (!overlay.Create(L"DeltaESP",
                        GetSystemMetrics(SM_CXSCREEN),
                        GetSystemMetrics(SM_CYSCREEN))) {
        MessageBoxW(nullptr, L"叠加窗口创建失败", L"DeltaESP", MB_OK);
        return 1;
    }

    ESP esp(mem, base);

    while (mem.IsOpen()) {
        overlay.Clear();

        auto boxes = esp.Collect(overlay.Width(), overlay.Height());
        for (const auto& b : boxes) {
            COLORREF col = (b.distance < 100.0f)
                           ? RGB(255, 40, 40)
                           : RGB(255, 200, 40);
            overlay.DrawBox(b.topLeft, b.topRight,
                            b.bottomLeft, b.bottomRight, col, 2);

            std::wstring distText =
                std::to_wstring((int)b.distance) + L"m";
            overlay.DrawText({ b.topLeft.x, b.topLeft.y - 16 },
                             distText, col);
        }

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}