#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <windows.h>

class Database;

class ClipboardManager : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardManager(Database *db, QObject *parent = nullptr);
    ~ClipboardManager();

    // Запустить — создать скрытое окно и подписаться на буфер
    bool start();

    // Остановить — отписаться и уничтожить окно
    void stop();

    // Живой ли менеджер (для Watchdog)
    bool isRunning() const { return m_hwnd != nullptr; }

public slots:
    // Попросить пропустить следующее WM_CLIPBOARDUPDATE (мы сами положили данные)
    void suppressNext() { m_suppressNext = true; }

signals:
    // Эмитируем после каждой успешной записи в историю
    void historyChanged();

private:
    bool m_suppressNext = false; // флаг: проигнорировать следующее событие буфера
    Database      *m_db;
    HWND           m_hwnd;
    QElapsedTimer  m_lastTextTimer;  // дебаунс для текста  (100мс)
    QElapsedTimer  m_lastImageTimer; // дебаунс для картинок (1500мс) — Win+Shift+S стреляет дважды

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
