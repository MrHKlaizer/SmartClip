#pragma once

#include <QDialog>
#include <QPoint>

class Database;
class QWidget;
class QPaintEvent;
class QMouseEvent;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(Database *db, QWidget *parent = nullptr);

signals:
    void mainHotkeyChanged(const QString &hotkey);
    void settingsChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QWidget *m_titleBar = nullptr;
    QPoint   m_drag;
};
