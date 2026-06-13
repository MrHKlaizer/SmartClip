#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "DCompBlur.h"

// ═════════════════════════════════════════════════════════════════════════════
//  SetWindowCompositionAttribute — undocumented Win10/11 DWM API
//  Позволяет применить Acrylic Blur к любому окну, в т.ч. к чистому Win32.
//  Загружается динамически из user32.dll (нет в SDK import lib).
// ═════════════════════════════════════════════════════════════════════════════

typedef enum { WCA_ACCENT_POLICY = 19 } WINDOWCOMPOSITIONATTRIB;

typedef enum {
    ACCENT_DISABLED                   = 0,
    ACCENT_ENABLE_GRADIENT            = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND          = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND   = 4,
    ACCENT_INVALID_STATE              = 5
} ACCENT_STATE;

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD        AccentFlags;
    DWORD        GradientColor;   // формат AABBGGRR
    DWORD        AnimationId;
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID                   pvData;
    SIZE_T                  cbData;
};

typedef BOOL (WINAPI *pfnSetWCA)(HWND, WINDOWCOMPOSITIONATTRIBDATA *);
static pfnSetWCA g_SetWCA = nullptr;

static bool loadSetWCA()
{
    if (g_SetWCA) return true;
    HMODULE h = GetModuleHandleW(L"user32.dll");
    if (h) g_SetWCA = reinterpret_cast<pfnSetWCA>(
        GetProcAddress(h, "SetWindowCompositionAttribute"));
    return g_SetWCA != nullptr;
}

// Применяет Acrylic к HWND.
// gradientColor: AABBGGRR — тинт поверх размытия.
// 0x20101010 ≈ слегка тёмный, полупрозрачный (alpha=0x20=12.5%).
static void enableAcrylic(HWND hwnd, DWORD gradientColor = 0x20101010)
{
    if (!loadSetWCA()) return;

    ACCENT_POLICY policy {};
    policy.AccentState   = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    policy.AccentFlags   = 0;
    policy.GradientColor = gradientColor;
    policy.AnimationId   = 0;

    WINDOWCOMPOSITIONATTRIBDATA data {};
    data.Attrib  = WCA_ACCENT_POLICY;
    data.pvData  = &policy;
    data.cbData  = sizeof(policy);

    g_SetWCA(hwnd, &data);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Состояние модуля
// ═════════════════════════════════════════════════════════════════════════════
namespace {

const wchar_t kClass[] = L"SmartClipBlurWnd";
HWND g_hwndLeft  = nullptr;
HWND g_hwndRight = nullptr;

// Минимальный WndProc — не рисуем ничего, DWM рисует Acrylic сам
LRESULT CALLBACK BlurWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1; // не стираем фон — DWM делает всё
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND createBlurHwnd(int x, int y, int w, int h)
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = BlurWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        wc.hbrBackground = nullptr; // нет фона — Acrylic через DWM
        RegisterClassExW(&wc);
        registered = true;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP |   // DWM сам рендерит, без GDI bitmap
        WS_EX_TRANSPARENT          |   // клики насквозь
        WS_EX_TOOLWINDOW           |   // не в таскбаре
        WS_EX_TOPMOST,                 // поверх обычных окон
        kClass, nullptr,
        WS_POPUP,                      // без рамки и заголовка
        x, y, w, h,
        nullptr, nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (hwnd) enableAcrylic(hwnd);
    return hwnd;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
//  Публичный API
// ═════════════════════════════════════════════════════════════════════════════

bool dcompBlurInit(HWND /*mainHwnd*/, const BlurRects &r)
{
    g_hwndLeft  = createBlurHwnd(r.leftX,  r.topY, r.leftW,  r.colH);
    g_hwndRight = createBlurHwnd(r.rightX, r.topY, r.rightW, r.colH);
    return g_hwndLeft != nullptr && g_hwndRight != nullptr;
}

void dcompBlurShow(HWND mainHwnd, const BlurRects &r)
{
    if (!g_hwndLeft || !g_hwndRight) return;

    // Обновляем размер/позицию
    SetWindowPos(g_hwndLeft,  nullptr,
                 r.leftX,  r.topY, r.leftW,  r.colH,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    SetWindowPos(g_hwndRight, nullptr,
                 r.rightX, r.topY, r.rightW, r.colH,
                 SWP_NOACTIVATE | SWP_NOZORDER);

    ShowWindow(g_hwndLeft,  SW_SHOWNA);
    ShowWindow(g_hwndRight, SW_SHOWNA);

    // Z-order: blur окна прямо под главным Qt окном
    SetWindowPos(g_hwndLeft,  mainHwnd,   0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(g_hwndRight, g_hwndLeft, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void dcompBlurHide()
{
    if (g_hwndLeft)  ShowWindow(g_hwndLeft,  SW_HIDE);
    if (g_hwndRight) ShowWindow(g_hwndRight, SW_HIDE);
}

void dcompBlurShutdown()
{
    if (g_hwndLeft)  { DestroyWindow(g_hwndLeft);  g_hwndLeft  = nullptr; }
    if (g_hwndRight) { DestroyWindow(g_hwndRight); g_hwndRight = nullptr; }
}
