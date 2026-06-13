#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <windows.h>

class HotkeyManager : public QObject
{
    Q_OBJECT

public:
    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager();

    bool start();
    void stop();

    // Перерегистрировать основной хоткей (из настроек)
    void setMainHotkey(const QString &hotkey);

    // Перерегистрировать хоткеи профилей
    // key = profileId, value = строка "Ctrl+Shift+1"
    void setProfileHotkeys(const QMap<int, QString> &hotkeys);

    // Вспомогательный парсер строки хоткея → WinAPI modifiers + vk
    static bool parseHotkey(const QString &str, UINT &modifiers, UINT &vk);

signals:
    void hotkeyPressed();                     // основной хоткей — открыть окно
    void profileHotkeyPressed(int profileId); // хоткей профиля — вставить текст

private:
    HWND           m_hwnd     = nullptr;
    QMap<int, int> m_profileIds; // hotkeyWinId -> profileId

    // ── Low-level keyboard hook (для Win+key, которые RegisterHotKey не берёт) ──
    HHOOK m_llHook  = nullptr;  // хэндл хука
    UINT  m_llVk    = 0;        // vk-код перехватываемой клавиши (Win+<m_llVk>)

    void installLLHook(UINT vk);
    void removeLLHook();

    // Singleton-указатель и состояние Win-клавиши для статического колбека
    static HotkeyManager *s_instance;
    static bool            s_winDown;    // true пока LWin или RWin зажата
    static LRESULT CALLBACK LLKeyboardProc(int code, WPARAM wParam, LPARAM lParam);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static const int HOTKEY_ID       = 1;   // ID основного хоткея
    static const int PROFILE_ID_BASE = 100; // профили: 100, 101, 102...
};
