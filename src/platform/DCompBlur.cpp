// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  DCompBlur.cpp — Реализация стеклянного размытия через Windows DWM         ║
// ║                                                                              ║
// ║  Этот файл реализует функции из DCompBlur.h.                               ║
// ║  Создаёт два невидимых Win32-окна (левое и правое) с эффектом              ║
// ║  Acrylic Blur от Windows. Они позиционируются точно под колонками SmartClip.║
// ║                                                                              ║
// ║  Использует недокументированный API Windows:                                ║
// ║  SetWindowCompositionAttribute (из user32.dll)                              ║
// ║  — он не объявлен в SDK, поэтому загружаем его вручную через GetProcAddress.║
// ╚══════════════════════════════════════════════════════════════════════════════╝

// WIN32_LEAN_AND_MEAN — просим Windows.h не тянуть лишние заголовки (COM, Winsock и пр.)
// Ускоряет компиляцию.
#define WIN32_LEAN_AND_MEAN
// NOMINMAX — запрещаем Windows.h определять макросы min/max которые конфликтуют с C++ std
#define NOMINMAX
#include <windows.h>
#include "DCompBlur.h"

// ═════════════════════════════════════════════════════════════════════════════
//  Недокументированный Windows API: SetWindowCompositionAttribute
//
//  Эта функция существует в Windows 10/11 но не объявлена в публичном SDK.
//  Она позволяет применить эффект Acrylic Blur (матовое стекло) к любому окну.
//  Мы загружаем её адрес из user32.dll вручную во время выполнения программы.
// ═════════════════════════════════════════════════════════════════════════════

// Перечисление атрибутов окна. Нас интересует только WCA_ACCENT_POLICY = 19.
typedef enum { WCA_ACCENT_POLICY = 19 } WINDOWCOMPOSITIONATTRIB;

// Типы состояния акцента (эффекта) окна:
typedef enum {
    ACCENT_DISABLED                   = 0,  // Эффект выключен
    ACCENT_ENABLE_GRADIENT            = 1,  // Градиент
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,  // Прозрачный градиент
    ACCENT_ENABLE_BLURBEHIND          = 3,  // Простое размытие
    ACCENT_ENABLE_ACRYLICBLURBEHIND   = 4,  // Acrylic (матовое стекло) — нам нужен этот
    ACCENT_INVALID_STATE              = 5
} ACCENT_STATE;

// Политика акцента: передаётся в API чтобы указать тип и настройки эффекта.
struct ACCENT_POLICY {
    ACCENT_STATE AccentState;    // Какой эффект применить
    DWORD        AccentFlags;    // Дополнительные флаги (обычно 0)
    DWORD        GradientColor;  // Цвет тинта поверх размытия. Формат: AABBGGRR (не ARGB!)
                                  // 0x20101010 ≈ почти чёрный, 12.5% непрозрачности
    DWORD        AnimationId;    // ID анимации (0 = без анимации)
};

// Структура для вызова SetWindowCompositionAttribute.
struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;  // Какой атрибут меняем (WCA_ACCENT_POLICY)
    PVOID                   pvData;  // Указатель на данные атрибута (ACCENT_POLICY)
    SIZE_T                  cbData;  // Размер данных в байтах
};

// Тип указателя на функцию SetWindowCompositionAttribute.
// WINAPI = __stdcall (соглашение о вызове Windows API).
typedef BOOL (WINAPI *pfnSetWCA)(HWND, WINDOWCOMPOSITIONATTRIBDATA *);

// Указатель на загруженную функцию (nullptr пока не загружена).
static pfnSetWCA g_SetWCA = nullptr;

// Загрузить SetWindowCompositionAttribute из user32.dll.
// Вызывается один раз, результат кешируется в g_SetWCA.
// Возвращает false если функция недоступна (старый Windows или API изменился).
static bool loadSetWCA()
{
    if (g_SetWCA) return true;  // Уже загружена — сразу возвращаем успех
    // GetModuleHandleW — получить дескриптор уже загруженной DLL (user32.dll всегда загружена).
    HMODULE h = GetModuleHandleW(L"user32.dll");
    if (h)
        // GetProcAddress — найти адрес функции по имени внутри DLL.
        // reinterpret_cast<pfnSetWCA> — говорим компилятору «это указатель на нужную функцию».
        g_SetWCA = reinterpret_cast<pfnSetWCA>(
            GetProcAddress(h, "SetWindowCompositionAttribute"));
    return g_SetWCA != nullptr;
}

// Применить Acrylic Blur к указанному HWND.
// gradientColor — AABBGGRR цвет тинта (лёгкий тёмный оттенок по умолчанию).
static void enableAcrylic(HWND hwnd, DWORD gradientColor = 0x20101010)
{
    if (!loadSetWCA()) return;  // API недоступен — ничего не делаем

    // Заполняем структуру ACCENT_POLICY с нужными параметрами.
    // {} — инициализация нулями (C++11).
    ACCENT_POLICY policy {};
    policy.AccentState   = ACCENT_ENABLE_ACRYLICBLURBEHIND;  // Acrylic эффект
    policy.AccentFlags   = 0;
    policy.GradientColor = gradientColor;
    policy.AnimationId   = 0;

    WINDOWCOMPOSITIONATTRIBDATA data {};
    data.Attrib  = WCA_ACCENT_POLICY;
    data.pvData  = &policy;            // Указатель на нашу структуру
    data.cbData  = sizeof(policy);     // Размер структуры в байтах

    g_SetWCA(hwnd, &data);  // Применяем эффект к окну
}

// ═════════════════════════════════════════════════════════════════════════════
//  Состояние модуля (анонимный namespace)
//
//  namespace {} — скрывает переменные и функции внутри этого .cpp файла.
//  Другие файлы не могут обратиться к g_hwndLeft/g_hwndRight напрямую.
// ═════════════════════════════════════════════════════════════════════════════
namespace {

// Имя класса Win32-окна (строка в кодировке UTF-16).
const wchar_t kClass[] = L"SmartClipBlurWnd";

// Дескрипторы двух служебных окон с размытием.
// nullptr = ещё не созданы / уже уничтожены.
HWND g_hwndLeft  = nullptr;  // Размытие под левой колонкой
HWND g_hwndRight = nullptr;  // Размытие под правой колонкой

// Минимальная оконная процедура для служебных окон.
// Эти окна ничего не рисуют сами — DWM рисует Acrylic за них.
// Нам нужно лишь правильно обрабатывать WM_PAINT и WM_ERASEBKGND.
LRESULT CALLBACK BlurWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        // Обязательно вызвать Begin/EndPaint — иначе Windows будет бесконечно слать WM_PAINT.
        // Сами ничего не рисуем — DWM нарисует размытие.
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;  // Говорим «фон стёрт» — не нужно стирать, DWM сам управляет

    // Все остальные сообщения — стандартная обработка Windows.
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Создать одно служебное окно с Acrylic размытием.
// Окно невидимо для пользователя: без рамки, без кнопки в таскбаре, клики сквозь него.
HWND createBlurHwnd(int x, int y, int w, int h)
{
    // Регистрируем класс окна один раз (static bool гарантирует это).
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = BlurWndProc;              // Наша оконная процедура
        wc.hInstance     = GetModuleHandleW(nullptr); // Дескриптор текущего .exe
        wc.lpszClassName = kClass;
        wc.hbrBackground = nullptr;  // Нет кисти фона — DWM рисует Acrylic
        RegisterClassExW(&wc);
        registered = true;
    }

    // CreateWindowExW — создаёт Win32-окно с расширенными стилями.
    HWND hwnd = CreateWindowExW(
        // Расширенные стили (WS_EX_*):
        WS_EX_NOREDIRECTIONBITMAP |  // DWM рендерит напрямую, без GDI-буфера
        WS_EX_TRANSPARENT          |  // Клики «проваливаются» сквозь это окно
        WS_EX_TOOLWINDOW           |  // Не показывать кнопку в панели задач
        WS_EX_TOPMOST,               // Поверх обычных окон (но под нашим SmartClip)
        kClass, nullptr,
        WS_POPUP,         // Стиль: всплывающее окно без рамки и заголовка
        x, y, w, h,       // Позиция и размер
        nullptr, nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (hwnd)
        enableAcrylic(hwnd);  // Применяем Acrylic эффект сразу после создания
    return hwnd;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
//  Публичный API (объявлен в DCompBlur.h)
// ═════════════════════════════════════════════════════════════════════════════

// Создать оба служебных окна. Вызывается один раз при запуске SmartClip.
// mainHwnd — дескриптор главного окна SmartClip (не используется здесь, передаётся для Z-order позже).
bool dcompBlurInit(HWND /*mainHwnd*/, const BlurRects &r)
{
    // Создаём два окна по координатам из BlurRects.
    g_hwndLeft  = createBlurHwnd(r.leftX,  r.topY, r.leftW,  r.colH);
    g_hwndRight = createBlurHwnd(r.rightX, r.topY, r.rightW, r.colH);
    return g_hwndLeft != nullptr && g_hwndRight != nullptr;
}

// Показать размытие и обновить позицию/размер окон.
// Вызывается каждый раз когда SmartClip открывается или меняет размер.
void dcompBlurShow(HWND mainHwnd, const BlurRects &r)
{
    if (!g_hwndLeft || !g_hwndRight) return;

    // Обновляем позицию и размер (окна могли сдвинуться при смене разрешения).
    // SWP_NOACTIVATE — не переключать фокус на служебные окна.
    // SWP_NOZORDER   — не менять Z-порядок пока (сделаем это ниже).
    SetWindowPos(g_hwndLeft,  nullptr,
                 r.leftX,  r.topY, r.leftW,  r.colH,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    SetWindowPos(g_hwndRight, nullptr,
                 r.rightX, r.topY, r.rightW, r.colH,
                 SWP_NOACTIVATE | SWP_NOZORDER);

    // Показываем окна (SW_SHOWNA = показать без активации).
    ShowWindow(g_hwndLeft,  SW_SHOWNA);
    ShowWindow(g_hwndRight, SW_SHOWNA);

    // Выстраиваем Z-порядок: blur-окна должны быть НЕПОСРЕДСТВЕННО ПОД главным Qt-окном.
    // SetWindowPos с параметром hWndInsertAfter = mainHwnd означает «поставить меня ПОЗАДИ mainHwnd».
    SetWindowPos(g_hwndLeft,  mainHwnd,   0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    // Правое окно — позади левого (оба позади SmartClip).
    SetWindowPos(g_hwndRight, g_hwndLeft, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// Скрыть размытие (когда SmartClip сворачивается).
void dcompBlurHide()
{
    if (g_hwndLeft)  ShowWindow(g_hwndLeft,  SW_HIDE);
    if (g_hwndRight) ShowWindow(g_hwndRight, SW_HIDE);
}

// Удалить служебные окна и освободить системные ресурсы.
// Вызывается при закрытии программы.
void dcompBlurShutdown()
{
    if (g_hwndLeft)  { DestroyWindow(g_hwndLeft);  g_hwndLeft  = nullptr; }
    if (g_hwndRight) { DestroyWindow(g_hwndRight); g_hwndRight = nullptr; }
}
