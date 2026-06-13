#include "Watchdog.h"
#include "ClipboardManager.h"
#include <QDebug>

Watchdog::Watchdog(ClipboardManager *manager, QObject *parent)
    : QThread(parent)
    , m_manager(manager)
    , m_running(false)
{
}

void Watchdog::stop()
{
    m_running = false;
    wait(); // ждём пока поток завершится
}

void Watchdog::run()
{
    m_running = true;
    qDebug() << "Watchdog запущен";

    while (m_running) {
        // Спим 5 секунд между проверками
        QThread::sleep(5);

        if (!m_running) break;

        // Проверяем жив ли ClipboardManager
        if (!m_manager->isRunning()) {
            qDebug() << "Watchdog: ClipboardManager не отвечает, перезапускаем...";
            m_manager->start();
        }
    }

    qDebug() << "Watchdog остановлен";
}
