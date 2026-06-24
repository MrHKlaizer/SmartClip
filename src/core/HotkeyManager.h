// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  HotkeyManager.h — Менеджер глобальных горячих клавиш                     ║
// ║                                                                              ║
// ║  «Глобальные» хоткеи — это клавиши которые работают даже когда SmartClip  ║
// ║  свёрнут или не в фокусе. Нажал Ctrl+Shift+V в любой программе —           ║
// ║  SmartClip открылся.                                                        ║
// ║                                                                              ║
// ║  Два механизма перехвата клавиш:                                            ║
// ║  1. RegisterHotKey (WinAPI) — стандартный способ для большинства комбо     ║
// ║     (Ctrl+Alt+X, Ctrl+Shift+V и т.д.). Ограничение: не работает с Win-клавишей. ║
// ║  2. Low-level keyboard hook (SetWindowsHookEx) — для комбинаций с Win:     ║
// ║     Win+C, Win+X и т.п. Перехватывает ВСЕ нажатия на системном уровне.    ║
// ║                                                                              ║
// ║  Хоткеи:                                                                    ║
// ║  • Основной (HOTKEY_ID=1)     — открыть/скрыть SmartClip                  ║
// ║  • Профильные (100, 101, ...) — вставить текстовый профиль                 ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

// QObject — базовый класс Qt для сигналов/слотов
#include <QObject>
// QMap — словарь (ключ → значение), аналог dict в Python. Здесь: WinId → profileId.
#include <QMap>
// QString — строка Qt
#include <QString>
// windows.h — Windows API. Нужны типы: HWND, UINT, WPARAM, LPARAM, LRESULT, HHOOK.
#include <windows.h>

// ─── Класс HotkeyManager ─────────────────────────────────────────────────────
// Управляет регистрацией и обработкой глобальных горячих клавиш через Windows API.
class HotkeyManager : public QObject
{
    Q_OBJECT

public:
    // Конструктор — инициализирует все поля нулями/nullptr.
    explicit HotkeyManager(QObject *parent = nullptr);

    // Деструктор — снимает все зарегистрированные хоткеи и хуки.
    ~HotkeyManager();

    // Создать скрытое сообщение-окно и зарегистрировать основной хоткей из настроек.
    // Возвращает false если RegisterHotKey не удался (занят другой программой).
    bool start();

    // Снять все регистрации хоткеев, удалить скрытое окно.
    void stop();

    // Перерегистрировать основной хоткей с новым значением.
    // Вызывается из SettingsDialog когда пользователь меняет комбинацию.
    // hotkey — строка формата "Ctrl+Shift+V" или "Win+C"
    void setMainHotkey(const QString &hotkey);

    // Обновить все хоткеи профилей.
    // key = profileId, value = строка "Ctrl+Shift+1"
    // Старые регистрации снимаются, новые добавляются.
    void setProfileHotkeys(const QMap<int, QString> &hotkeys);

    // Разобрать строку хоткея в числа для Windows RegisterHotKey().
    // "Ctrl+Shift+V" → modifiers = MOD_CONTROL|MOD_SHIFT, vk = 'V' (0x56)
    // Возвращает false если строка некорректна.
    static bool parseHotkey(const QString &str, UINT &modifiers, UINT &vk);

// ─── Сигналы ─────────────────────────────────────────────────────────────────
signals:
    // Основной хоткей нажат — открыть/скрыть главное окно SmartClip.
    void hotkeyPressed();

    // Хоткей конкретного профиля нажат — вставить шаблон с profileId.
    void profileHotkeyPressed(int profileId);

private:
    // ─── Данные ───────────────────────────────────────────────────────────────
    HWND m_hwnd = nullptr;     // Скрытое Win32-окно для приёма WM_HOTKEY сообщений.
                                // RegisterHotKey требует HWND чтобы знать кому посылать WM_HOTKEY.

    QMap<int, int> m_profileIds; // Словарь: hotkeyWinId → profileId.
                                  // Нужен чтобы по WM_HOTKEY сообщению понять какой профиль активировать.

    // ─── Low-level keyboard hook ───────────────────────────────────────────────
    // Используется для перехвата комбинаций с Win-клавишей (Win+C, Win+X и т.д.),
    // которые RegisterHotKey перехватить не умеет.

    HHOOK m_llHook = nullptr; // Дескриптор хука (nullptr если хук не установлен).
                               // HHOOK = Handle to Hook (ID хука в системе).
    UINT  m_llVk   = 0;       // Код клавиши которую перехватываем совместно с Win.
                               // Например 0x43 = 'C' для Win+C.

    // Установить низкоуровневый хук клавиатуры для виртуального кода vk.
    void installLLHook(UINT vk);

    // Снять низкоуровневый хук клавиатуры.
    void removeLLHook();

    // ─── Singleton для статического колбека ───────────────────────────────────
    // Windows вызывает статические функции — они не имеют доступа к this.
    // s_instance хранит указатель на текущий объект HotkeyManager,
    // чтобы статический колбек мог обратиться к нему.
    static HotkeyManager *s_instance;
    static bool           s_winDown; // true пока зажата Win-клавиша (LWin или RWin)

    // Колбек низкоуровневого хука — вызывается Windows при каждом нажатии клавиши.
    // Должен быть static — Windows не знает о C++-объектах.
    static LRESULT CALLBACK LLKeyboardProc(int code, WPARAM wParam, LPARAM lParam);

    // Обработчик сообщений скрытого окна.
    // Здесь ловим WM_HOTKEY и испускаем hotkeyPressed() / profileHotkeyPressed().
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // ─── Идентификаторы хоткеев ───────────────────────────────────────────────
    // Windows различает зарегистрированные хоткеи по целочисленным ID.
    static const int HOTKEY_ID       = 1;   // ID основного хоткея (открыть SmartClip)
    static const int PROFILE_ID_BASE = 100; // Базовый ID профильных хоткеев.
                                             // Профиль 0 → ID 100, профиль 1 → ID 101, и т.д.
};
