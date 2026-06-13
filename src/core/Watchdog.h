#pragma once

#include <QThread>

class ClipboardManager;

class Watchdog : public QThread
{
    Q_OBJECT

public:
    explicit Watchdog(ClipboardManager *manager, QObject *parent = nullptr);

    void stop();

protected:
    // Код который выполняется в отдельном потоке
    void run() override;

private:
    ClipboardManager *m_manager;
    bool              m_running;
};
