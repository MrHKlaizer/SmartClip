#include "HotkeyManager.h"
#include "AppSettings.h"
#include <QDebug>

// Статические данные для LL-хука (хук — глобальный колбек без контекста)
HotkeyManager *HotkeyManager::s_instance = nullptr;
bool           HotkeyManager::s_winDown   = false;

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
}

HotkeyManager::~HotkeyManager()
{
    stop();
}

bool HotkeyManager::start()
{
    WNDCLASS wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.lpszClassName = L"SmartClipHotkeyWindow";
    RegisterClass(&wc);

    m_hwnd = CreateWindowEx(
        0, L"SmartClipHotkeyWindow", L"SmartClip Hotkey",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr
    );

    if (!m_hwnd) {
        qDebug() << "Ошибка создания окна хоткея, код:" << GetLastError();
        return false;
    }

    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Читаем горячую клавишу из настроек (по умолчанию Ctrl+Shift+V)
    QString hkStr = AppSettings::get().mainHotkey();
    UINT mod = 0, vk = 0;
    if (!parseHotkey(hkStr, mod, vk)) {
        // fallback — Ctrl+Shift+V если строка не распозналась
        mod = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
        vk  = 'V';
        qDebug() << "Не распознан хоткей из настроек, используем Ctrl+Shift+V";
    }

    if (mod & MOD_WIN && !RegisterHotKey(m_hwnd, HOTKEY_ID, mod, vk)) {
        // Win+key и RegisterHotKey не удался (Win+V зарезервирован системой,
        // DisabledHotkeys ещё не установлен) → используем LL hook
        qDebug() << "RegisterHotKey не удался для Win+key, используем LL hook:" << hkStr;
        installLLHook(vk);
    } else if (!(mod & MOD_WIN) && !RegisterHotKey(m_hwnd, HOTKEY_ID, mod, vk)) {
        qDebug() << "RegisterHotKey не удался (код" << GetLastError() << "):" << hkStr;
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    qDebug() << "HotkeyManager запущен (" << hkStr << ")";
    return true;
}

void HotkeyManager::stop()
{
    removeLLHook();
    if (m_hwnd) {
        UnregisterHotKey(m_hwnd, HOTKEY_ID);
        for (int id : m_profileIds.keys())
            UnregisterHotKey(m_hwnd, id);
        m_profileIds.clear();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void HotkeyManager::setMainHotkey(const QString &hotkey)
{
    if (!m_hwnd) return;

    // Снимаем старый хоткей (RegisterHotKey или LL-хук)
    UnregisterHotKey(m_hwnd, HOTKEY_ID);
    removeLLHook();

    UINT mod = 0, vk = 0;
    if (!parseHotkey(hotkey, mod, vk)) {
        qDebug() << "Не распознан новый хоткей:" << hotkey;
        return;
    }

    if (RegisterHotKey(m_hwnd, HOTKEY_ID, mod, vk)) {
        qDebug() << "Основной хоткей перерегистрирован:" << hotkey;
    } else if (mod & MOD_WIN) {
        // Win+key — пробуем LL hook (Win+V зарезервирован, если без installer)
        qDebug() << "RegisterHotKey не удался, используем LL hook для Win+key:" << hotkey;
        installLLHook(vk);
    } else {
        qDebug() << "RegisterHotKey не удался (код" << GetLastError() << "):" << hotkey;
    }
}

void HotkeyManager::setProfileHotkeys(const QMap<int, QString> &hotkeys)
{
    if (!m_hwnd) return;

    // Снимаем все старые профильные хоткеи
    for (int id : m_profileIds.keys())
        UnregisterHotKey(m_hwnd, id);
    m_profileIds.clear();

    // Регистрируем новые
    int winId = PROFILE_ID_BASE;
    for (auto it = hotkeys.constBegin(); it != hotkeys.constEnd(); ++it) {
        int profileId         = it.key();
        const QString &hkStr  = it.value();
        if (hkStr.trimmed().isEmpty()) continue;

        UINT mod = 0, vk = 0;
        if (!parseHotkey(hkStr, mod, vk)) {
            qDebug() << "Не распознан хоткей профиля" << profileId << ":" << hkStr;
            continue;
        }

        if (RegisterHotKey(m_hwnd, winId, mod, vk)) {
            m_profileIds[winId] = profileId;
            qDebug() << "Зарегистрирован хоткей профиля" << profileId << ":" << hkStr;
            ++winId;
        } else {
            qDebug() << "Ошибка регистрации хоткея профиля" << profileId
                     << ":" << hkStr << ", код:" << GetLastError();
        }
    }
}

bool HotkeyManager::parseHotkey(const QString &str, UINT &modifiers, UINT &vk)
{
    modifiers = MOD_NOREPEAT;
    vk = 0;

    const QStringList parts = str.toUpper().split('+', Qt::SkipEmptyParts);
    for (const QString &raw : parts) {
        QString p = raw.trimmed();
        if      (p == "CTRL")  modifiers |= MOD_CONTROL;
        else if (p == "SHIFT") modifiers |= MOD_SHIFT;
        else if (p == "ALT")   modifiers |= MOD_ALT;
        else if (p == "WIN")   modifiers |= MOD_WIN;
        else if (p.length() == 1 && p[0].isLetter())
            vk = static_cast<UINT>(p[0].toLatin1()); // A-Z
        else if (p.length() == 1 && p[0].isDigit())
            vk = static_cast<UINT>(p[0].toLatin1()); // 0-9
        else if (p.startsWith('F')) {
            bool ok = false;
            int n = p.mid(1).toInt(&ok);
            if (ok && n >= 1 && n <= 12) vk = VK_F1 + n - 1;
        }
    }
    return vk != 0;
}

// ── LL keyboard hook ─────────────────────────────────────────────────────────
// Перехватывает Win+<vk> на уровне ядра, раньше системы.
// Нужен для Win+V и других зарезервированных Windows комбинаций.

void HotkeyManager::installLLHook(UINT vk)
{
    removeLLHook(); // на случай повторного вызова
    m_llVk     = vk;
    s_instance = this;
    // Для WH_KEYBOARD_LL хук не инжектируется в другие процессы →
    // по MSDN hMod должен быть NULL когда proc в нашем процессе
    m_llHook   = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyboardProc, nullptr, 0);
    if (!m_llHook)
        qDebug() << "SetWindowsHookEx не удался, код:" << GetLastError();
}

void HotkeyManager::removeLLHook()
{
    if (m_llHook) {
        UnhookWindowsHookEx(m_llHook);
        m_llHook = nullptr;
        m_llVk   = 0;
        if (s_instance == this)
            s_instance = nullptr;
    }
}

LRESULT CALLBACK HotkeyManager::LLKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION) {
        auto *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
        const UINT vk       = kb->vkCode;
        const bool injected = (kb->flags & LLKHF_INJECTED) != 0;

        // Отслеживаем Win-клавишу сами.
        // Игнорируем инжектированные события — они наши собственные SendInput.
        if (!injected && (vk == VK_LWIN || vk == VK_RWIN)) {
            s_winDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        }

        if (!injected && s_instance && s_instance->m_llVk != 0
            && vk == s_instance->m_llVk && s_winDown)
        {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                // ── Подавляем меню Пуск ──────────────────────────────────────
                // Windows открывает Пуск если Win был нажат и отпущен БЕЗ других
                // клавиш. Мы проглотили V → с точки зрения системы Win был один.
                // Фикс: инжектируем DOWN+UP виртуальной клавиши 0x88.
                // Диапазон 0x88-0x8F — "Unassigned" по MSDN (не F-клавиши!),
                // ни один OEM-драйвер их не маппит → никаких побочных эффектов.
                // Система видит "Win + 0x88 нажато" → Пуск отменяется.
                INPUT cancel[2]      = {};
                cancel[0].type       = INPUT_KEYBOARD;
                cancel[0].ki.wVk     = 0x88;                  // DOWN  (unassigned VK)
                cancel[1].type       = INPUT_KEYBOARD;
                cancel[1].ki.wVk     = 0x88;
                cancel[1].ki.dwFlags = KEYEVENTF_KEYUP;        // UP
                SendInput(2, cancel, sizeof(INPUT));

                // Показываем SmartClip
                PostMessage(s_instance->m_hwnd, WM_HOTKEY, HOTKEY_ID, 0);
            }

            // Проглатываем и keydown и keyup целевой клавиши —
            // Windows Clipboard History не получает ни одного события
            return 1;
        }
    }
    return CallNextHookEx(s_instance ? s_instance->m_llHook : nullptr,
                          code, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK HotkeyManager::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    HotkeyManager *self = reinterpret_cast<HotkeyManager*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA)
    );

    if (msg == WM_HOTKEY && self) {
        int id = static_cast<int>(wParam);
        if (id == HOTKEY_ID) {
            emit self->hotkeyPressed();
        } else if (self->m_profileIds.contains(id)) {
            emit self->profileHotkeyPressed(self->m_profileIds[id]);
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
