#pragma once
// ─── Plugins — установленные плагины ─────────────────────────────────────────
// Панель «Плагины» посередине сверху показывает их значки в ряд: клик — открыть
// плагин (своим окном, с анимацией, как остальные окна SmartClip). Пока ни одного
// плагина нет (все — «Скоро» в Настройки → Плагины): в панели только «+».

#include <QList>
#include <QString>
#include <functional>

namespace Plugins {

struct Installed {
    QString name;                 // Название (подсказка у значка)
    QString icon;                 // Значок — эмодзи
    std::function<void()> open;   // Открыть плагин
};

// Установленные плагины — в том порядке, в каком стоят на панели.
// Плагин при запуске SmartClip добавляет себя сюда (тесты — свои для проверки).
inline QList<Installed> &registry() { static QList<Installed> list; return list; }
inline QList<Installed> installed() { return registry(); }

}  // namespace Plugins
