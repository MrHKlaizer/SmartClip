// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  HotkeyManager.cpp — Реализация глобальных горячих клавиш                 ║
// ║                                                                              ║
// ║  Этот файл реализует два механизма перехвата клавиш:                       ║
// ║  1. RegisterHotKey  — для стандартных Ctrl/Shift/Alt комбинаций            ║
// ║  2. WH_KEYBOARD_LL  — для Win+key (Win+V зарезервирован Windows)           ║
// ║                                                                              ║
// ║  Оба механизма используют одно скрытое «message-only» окно для приёма     ║
// ║  WM_HOTKEY сообщений от Windows.                                            ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "HotkeyManager.h"
#include "AppSettings.h"   // Читаем главный хоткей из настроек
#include <QDebug>          // qDebug() для диагностических сообщений

// ─── Статические члены класса ────────────────────────────────────────────────
// Статические члены нужно определять в .cpp файле (объявление в .h — не определение).
// s_instance — указатель на текущий объект HotkeyManager, доступный из статического хука.
HotkeyManager *HotkeyManager::s_instance = nullptr;
// s_winDown — true пока Win-клавиша зажата (для определения Win+key комбинаций).
bool           HotkeyManager::s_winDown   = false;

// ─── Конструктор/деструктор ───────────────────────────────────────────────────
HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    // Все поля уже инициализированы в .h (= nullptr, = 0, = false)
}

HotkeyManager::~HotkeyManager()
{
    stop();  // Гарантируем снятие всех регистраций при удалении объекта
}

// ─── Запуск ───────────────────────────────────────────────────────────────────
// Создаёт скрытое окно и регистрирует главный хоткей.
bool HotkeyManager::start()
{
    // ── Регистрируем класс окна ───────────────────────────────────────────────
    WNDCLASS wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.lpszClassName = L"SmartClipHotkeyWindow";
    RegisterClass(&wc);

    // ── Создаём скрытое «message-only» окно ───────────────────────────────────
    // HWND_MESSAGE — окно не отображается, только получает сообщения.
    // RegisterHotKey требует HWND — когда хоткей нажат, Windows пошлёт WM_HOTKEY этому окну.
    m_hwnd = CreateWindowEx(
        0, L"SmartClipHotkeyWindow", L"SmartClip Hotkey",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr
    );

    if (!m_hwnd) {
        qDebug() << "Ошибка создания окна хоткея, код:" << GetLastError();
        return false;
    }

    // Сохраняем this в окне — WndProc сможет его получить через GetWindowLongPtr.
    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // ── Регистрируем главный хоткей ───────────────────────────────────────────
    QString hkStr = AppSettings::get().mainHotkey();  // "Ctrl+Shift+V" или "Win+C"
    UINT mod = 0, vk = 0;

    if (!parseHotkey(hkStr, mod, vk)) {
        // Строка не распознана — используем fallback: Ctrl+Shift+V
        mod = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
        vk  = 'V';
        qDebug() << "Не распознан хоткей из настроек, используем Ctrl+Shift+V";
    }

    // Win+key — особый случай.
    // MOD_WIN в mod означает что хоткей содержит Win-клавишу.
    if (mod & MOD_WIN && !RegisterHotKey(m_hwnd, HOTKEY_ID, mod, vk)) {
        // RegisterHotKey для Win+key часто не работает (Win+V зарезервирован системой).
        // Используем низкоуровневый хук — он работает всегда.
        qDebug() << "RegisterHotKey не удался для Win+key, используем LL hook:" << hkStr;
        installLLHook(vk);
    } else if (!(mod & MOD_WIN) && !RegisterHotKey(m_hwnd, HOTKEY_ID, mod, vk)) {
        // Обычный хоткей (не Win+key) — если не зарегистрировался, значит кто-то занял его.
        qDebug() << "RegisterHotKey не удался (код" << GetLastError() << "):" << hkStr;
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    qDebug() << "HotkeyManager запущен (" << hkStr << ")";
    return true;
}

// ─── Остановка ───────────────────────────────────────────────────────────────
// Снимает все регистрации и уничтожает скрытое окно.
void HotkeyManager::stop()
{
    removeLLHook();  // Снимаем LL-хук если был установлен
    if (m_hwnd) {
        UnregisterHotKey(m_hwnd, HOTKEY_ID);  // Снимаем главный хоткей
        for (int id : m_profileIds.keys())     // Снимаем все профильные хоткеи
            UnregisterHotKey(m_hwnd, id);
        m_profileIds.clear();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// ─── Смена главного хоткея ────────────────────────────────────────────────────
// Вызывается из SettingsDialog когда пользователь изменил комбинацию.
void HotkeyManager::setMainHotkey(const QString &hotkey)
{
    if (!m_hwnd) return;

    // Снимаем старый хоткей и хук (если был)
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
        // Win+key — RegisterHotKey не удался, переходим на LL-хук
        qDebug() << "RegisterHotKey не удался, используем LL hook для Win+key:" << hotkey;
        installLLHook(vk);
    } else {
        qDebug() << "RegisterHotKey не удался (код" << GetLastError() << "):" << hotkey;
    }
}

// ─── Обновление хоткеев профилей ─────────────────────────────────────────────
// Снимает все старые профильные хоткеи и регистрирует новые.
// hotkeys — словарь {profileId → строка хоткея}
void HotkeyManager::setProfileHotkeys(const QMap<int, QString> &hotkeys)
{
    if (!m_hwnd) return;

    // Снимаем все старые
    for (int id : m_profileIds.keys())
        UnregisterHotKey(m_hwnd, id);
    m_profileIds.clear();

    // Регистрируем новые. ID начинается с PROFILE_ID_BASE (100) и увеличивается.
    int winId = PROFILE_ID_BASE;
    for (auto it = hotkeys.constBegin(); it != hotkeys.constEnd(); ++it) {
        int profileId         = it.key();
        const QString &hkStr  = it.value();
        if (hkStr.trimmed().isEmpty()) continue;  // Пустой хоткей — пропускаем

        UINT mod = 0, vk = 0;
        if (!parseHotkey(hkStr, mod, vk)) {
            qDebug() << "Не распознан хоткей профиля" << profileId << ":" << hkStr;
            continue;
        }

        if (RegisterHotKey(m_hwnd, winId, mod, vk)) {
            // Запоминаем соответствие: winId → profileId
            m_profileIds[winId] = profileId;
            qDebug() << "Зарегистрирован хоткей профиля" << profileId << ":" << hkStr;
            ++winId;
        } else {
            qDebug() << "Ошибка регистрации хоткея профиля" << profileId
                     << ":" << hkStr << ", код:" << GetLastError();
        }
    }
}

// ─── Парсер строки хоткея ────────────────────────────────────────────────────
// Разбирает строку "Ctrl+Shift+V" в числа для RegisterHotKey.
// modifiers — битовая маска (MOD_CONTROL | MOD_SHIFT | ...)
// vk — виртуальный код клавиши ('V', 0x56)
// Возвращает false если строка не содержит основной клавиши.
bool HotkeyManager::parseHotkey(const QString &str, UINT &modifiers, UINT &vk)
{
    // MOD_NOREPEAT — не повторять хоткей если удерживать клавишу
    modifiers = MOD_NOREPEAT;
    vk = 0;

    // Разбиваем "Ctrl+Shift+V" → ["Ctrl", "Shift", "V"]
    const QStringList parts = str.toUpper().split('+', Qt::SkipEmptyParts);
    for (const QString &raw : parts) {
        QString p = raw.trimmed();
        // Модификаторы:
        if      (p == "CTRL")  modifiers |= MOD_CONTROL;
        else if (p == "SHIFT") modifiers |= MOD_SHIFT;
        else if (p == "ALT")   modifiers |= MOD_ALT;
        else if (p == "WIN")   modifiers |= MOD_WIN;
        // Основная клавиша:
        else if (p.length() == 1 && p[0].isLetter())
            vk = static_cast<UINT>(p[0].toLatin1());  // 'V' → 0x56 (ASCII = VK)
        else if (p.length() == 1 && p[0].isDigit())
            vk = static_cast<UINT>(p[0].toLatin1());  // '1' → 0x31
        else if (p.startsWith('F')) {
            // F1-F12: VK_F1=0x70, VK_F2=0x71, ...
            bool ok = false;
            int n = p.mid(1).toInt(&ok);
            if (ok && n >= 1 && n <= 12) vk = VK_F1 + n - 1;
        }
    }
    return vk != 0;  // Успех только если нашли основную клавишу
}

// ─── Установка Low-Level клавиатурного хука ───────────────────────────────────
// Используется для Win+key комбинаций которые RegisterHotKey не берёт.
// WH_KEYBOARD_LL — перехватывает нажатия НА СИСТЕМНОМ УРОВНЕ, до любого приложения.
void HotkeyManager::installLLHook(UINT vk)
{
    removeLLHook();  // Снимаем старый хук если был
    m_llVk     = vk;    // Запоминаем какую клавишу перехватываем (без Win)
    s_instance = this;  // Делаем себя доступным для статического колбека
    // SetWindowsHookEx(WH_KEYBOARD_LL, ..., nullptr, 0):
    // nullptr = не инжектируемый (только для нашего процесса по MSDN для LL-хуков)
    // 0 = глобальный (не привязан к конкретному потоку)
    m_llHook = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyboardProc, nullptr, 0);
    if (!m_llHook)
        qDebug() << "SetWindowsHookEx не удался, код:" << GetLastError();
}

// ─── Снятие Low-Level хука ────────────────────────────────────────────────────
void HotkeyManager::removeLLHook()
{
    if (m_llHook) {
        UnhookWindowsHookEx(m_llHook);  // Снимаем хук — он больше не будет вызываться
        m_llHook = nullptr;
        m_llVk   = 0;
        if (s_instance == this)
            s_instance = nullptr;
    }
}

// ─── Колбек Low-Level хука клавиатуры ────────────────────────────────────────
// Вызывается Windows при КАЖДОМ нажатии клавиши в системе (не только в SmartClip).
// Должна работать ОЧЕНЬ быстро — медленные хуки Windows принудительно отключает.
LRESULT CALLBACK HotkeyManager::LLKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION) {
        auto *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
        const UINT vk       = kb->vkCode;
        // LLKHF_INJECTED — клавиша «инжектирована» (послана через SendInput).
        // Мы сами посылаем INPUT для подавления Пуска — игнорируем такие события.
        const bool injected = (kb->flags & LLKHF_INJECTED) != 0;

        // Отслеживаем состояние Win-клавиши (Left Win или Right Win).
        // Инжектированные события игнорируем — они наши собственные.
        if (!injected && (vk == VK_LWIN || vk == VK_RWIN)) {
            s_winDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        }

        // Проверяем: нажата ли наша целевая клавиша вместе с Win?
        if (!injected && s_instance && s_instance->m_llVk != 0
            && vk == s_instance->m_llVk && s_winDown)
        {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {

                // ── Подавляем открытие меню Пуск ─────────────────────────────
                // Проблема: если Win нажата и отпущена без «обычных» клавиш —
                // Windows открывает Пуск. Мы «проглотили» целевую клавишу (Win+V),
                // поэтому с точки зрения системы Win была без пары → открылся бы Пуск.
                //
                // Решение: инжектируем событие нажатия «несуществующей» клавиши 0x88.
                // Диапазон 0x88-0x8F — «Unassigned» по MSDN: не назначены ни одному
                // реальному ключу, не маппируются OEM-драйверами.
                // Система видит «Win + 0x88» → это уже не «одиночный Win» → Пуск не откроется.
                INPUT cancel[2]      = {};
                cancel[0].type       = INPUT_KEYBOARD;
                cancel[0].ki.wVk     = 0x88;                   // DOWN «несуществующей» клавиши
                cancel[1].type       = INPUT_KEYBOARD;
                cancel[1].ki.wVk     = 0x88;
                cancel[1].ki.dwFlags = KEYEVENTF_KEYUP;         // UP «несуществующей» клавиши
                SendInput(2, cancel, sizeof(INPUT));

                // Посылаем нашему скрытому окну WM_HOTKEY — как будто RegisterHotKey сработал.
                // PostMessage — асинхронно (не блокирует хук).
                PostMessage(s_instance->m_hwnd, WM_HOTKEY, HOTKEY_ID, 0);
            }

            // Возвращаем 1 — «съедаем» событие.
            // Windows не получит Win+V и не откроет Clipboard History.
            // Проглатываем и keydown и keyup чтобы не оставалось «висящих» клавиш.
            return 1;
        }
    }
    // Для всех остальных клавиш — передаём следующему хуку в цепочке.
    return CallNextHookEx(s_instance ? s_instance->m_llHook : nullptr,
                          code, wParam, lParam);
}

// ─── Обработчик сообщений скрытого окна ─────────────────────────────────────
// Вызывается Windows когда в очереди окна появляется сообщение.
// Нас интересует только WM_HOTKEY — зарегистрированный хоткей нажат.
LRESULT CALLBACK HotkeyManager::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Восстанавливаем this из GWLP_USERDATA (сохранили в start())
    HotkeyManager *self = reinterpret_cast<HotkeyManager*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA)
    );

    if (msg == WM_HOTKEY && self) {
        int id = static_cast<int>(wParam);  // ID нажатого хоткея
        if (id == HOTKEY_ID) {
            // Главный хоткей — открыть/скрыть SmartClip
            emit self->hotkeyPressed();
        } else if (self->m_profileIds.contains(id)) {
            // Хоткей профиля — вставить шаблон
            emit self->profileHotkeyPressed(self->m_profileIds[id]);
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
