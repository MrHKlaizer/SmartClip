// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  Watchdog.cpp — Реализация «сторожевого пса»                               ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "Watchdog.h"
#include "ClipboardManager.h"
#include <QDebug>

Watchdog::Watchdog(ClipboardManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    m_timer.setInterval(10000);  // Раз в 10 секунд — почти бесплатно для процессора
    connect(&m_timer, &QTimer::timeout, this, &Watchdog::check);
}

void Watchdog::start()
{
    m_timer.start();
}

void Watchdog::stop()
{
    m_timer.stop();
}

void Watchdog::check()
{
    // isAlive() проверяет не только указатель, но и то, что Windows
    // ещё считает это окно существующим (IsWindow).
    if (!m_manager->isAlive()) {
        qDebug() << "Watchdog: окно буфера обмена пропало, перезапускаем...";
        m_manager->stop();
        m_manager->start();
    }
}
