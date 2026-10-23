// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  SmoothScroll.h — Плавная прокрутка колонок (SmartClip 2.0, этап 3)         ║
// ║                                                                              ║
// ║  Обычная прокрутка Qt прыгает рывками: щелчок колёсика = сразу +3 строки.  ║
// ║  Здесь колёсико двигает только ЦЕЛЬ, а сама позиция плавно её догоняет     ║
// ║  (экспоненциальное замедление, ~60 кадров/с). Быстрые щелчки подряд         ║
// ║  складываются в одну длинную плавную прокрутку.                             ║
// ║                                                                              ║
// ║  Мягкий край: у верха и низа список можно чуть «перетянуть» — он            ║
// ║  сопротивляется (как резинка) и пружинкой возвращается обратно.             ║
// ║  Перетяжку показываем, сдвигая содержимое колонки (полоса прокрутки сама   ║
// ║  за границы выйти не может).                                                 ║
// ║                                                                              ║
// ║  Анимации выключены в Windows — обычная прокрутка Qt, класс не вмешивается. ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

#include <QObject>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QtMath>
#include "Anim.h"

class SmoothScroller : public QObject
{
public:
    explicit SmoothScroller(QScrollArea *area)
        : QObject(area)
        , m_area(area)
    {
        area->viewport()->installEventFilter(this);
        m_timer.setTimerType(Qt::PreciseTimer);
        m_timer.setInterval(8);  // При старте — по частоте монитора (Anim::frameMs)
        connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
    }

    // Текущая скорость прокрутки (px/с) — пригодится эффектам карточек (этап 4)
    qreal velocity() const { return m_velocity; }

    // Сразу встать на позицию, прервав плавную прокрутку (переключение
    // История/Закрепы — недоигранная прокрутка увела бы список обратно)
    void jumpTo(int value)
    {
        m_timer.stop();
        m_velocity = 0;
        QScrollBar *sb = m_area->verticalScrollBar();
        sb->setValue(value);
        m_pos = m_target = sb->value();
        if (QWidget *content = m_area->widget())
            content->move(content->x(), -sb->value());
    }

    // Плавно доехать до позиции (кнопка «наверх»). Без анимаций — сразу.
    void scrollTo(qreal target)
    {
        QScrollBar *sb = m_area->verticalScrollBar();
        if (!Anim::enabled(Anim::Kind::Scroll)) { sb->setValue(qRound(target)); return; }
        if (!m_timer.isActive()) {
            m_pos = sb->value();
            m_clock.start();
            m_lastTick = 0;
        }
        m_target = qBound<qreal>(sb->minimum(), target, sb->maximum());
        m_lastWheel.start();
        m_timer.setInterval(Anim::frameMs(m_area));
        m_timer.start();
    }

protected:
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (e->type() != QEvent::Wheel || obj != m_area->viewport())
            return false;
        if (!Anim::enabled(Anim::Kind::Scroll))
            return false;  // Анимации выключены в Windows — обычная прокрутка Qt

        auto *we = static_cast<QWheelEvent *>(e);
        QScrollBar *sb = m_area->verticalScrollBar();

        // Начинаем с нуля — синхронизируемся с текущей позицией (её могли
        // поменять снаружи: перезагрузка списка, поиск)
        if (!m_timer.isActive()) {
            m_pos = m_target = sb->value();
            m_clock.start();
            m_lastTick = 0;
        }

        // Тачпад (pixelDelta) — уже плавный, 1:1. Колёсико (angleDelta):
        // один щелчок = 120 единиц = ~110 px. Мелкие значения (точные тачпады
        // Windows присылают angleDelta кусочками) — пропорционально.
        qreal delta;
        if (!we->pixelDelta().isNull())
            delta = -we->pixelDelta().y();
        else
            delta = -we->angleDelta().y() * 110.0 / 120.0;

        // За границей список «тянется» тяжело — как резинка
        const qreal lo = sb->minimum(), hi = sb->maximum();
        const bool outside = (m_target < lo && delta < 0) || (m_target > hi && delta > 0);
        m_target += outside ? delta * 0.3 : delta;
        m_target = qBound(lo - MAX_PULL, m_target, hi + MAX_PULL);

        m_lastWheel.start();
        if (!m_timer.isActive()) { m_timer.setInterval(Anim::frameMs(m_area)); m_timer.start(); }
        e->accept();
        return true;  // Сами двигаем — обычную прокрутку Qt не запускаем
    }

private:
    void tick()
    {
        QScrollBar *sb = m_area->verticalScrollBar();
        const qreal lo = sb->minimum(), hi = sb->maximum();

        // Пружинка у края: цель за границей возвращаем на край, когда колёсико
        // отпустили И список уже реально заехал за край и прошёл половину
        // перетяжки. Если возвращать раньше (как только отпустили колёсико),
        // список просто останавливается у края и пружинки не видно.
        if (m_target < lo || m_target > hi) {
            const qreal edge = (m_target < lo) ? lo : hi;
            const bool idle = m_lastWheel.isValid() && m_lastWheel.elapsed() > 90;
            const bool past = (m_target < lo) ? (m_pos < lo) : (m_pos > hi);
            const bool halfway = qAbs(m_pos - edge) >= 0.5 * qAbs(m_target - edge);
            if (idle && past && halfway)
                m_target = edge;
        }

        // Сколько прошло времени с прошлого кадра (кадры могут опаздывать)
        const qint64 now = m_clock.elapsed();
        const qreal dt = qBound<qreal>(0.001, (now - m_lastTick) / 1000.0, 0.05);
        m_lastTick = now;

        // Экспоненциальное догоняние цели. tau — «время реакции»: меньше =
        // резвее. Регулируется ползунком скорости прокрутки (Настройки → Оформление).
        const qreal tau = Anim::ms(Anim::Kind::Scroll, 110) / 1000.0;
        const qreal k = 1.0 - qExp(-dt / tau);
        const qreal prev = m_pos;
        m_pos += (m_target - m_pos) * k;
        m_velocity = (m_pos - prev) / dt;

        // Внутри границ — обычная позиция полосы. Снаружи — полоса на краю,
        // а содержимое сдвигаем на величину перетяжки (визуально ~40%)
        const qreal inside = qBound(lo, m_pos, hi);
        sb->setValue(qRound(inside));
        const int over = qRound((inside - m_pos) * 0.5);
        if (QWidget *content = m_area->widget()) {
            const int wantY = -sb->value() + over;
            if (content->y() != wantY) content->move(content->x(), wantY);
        }

        // Доехали и стоим внутри — останавливаемся
        if (qAbs(m_target - m_pos) < 0.5 && m_target >= lo && m_target <= hi) {
            m_pos = m_target;
            sb->setValue(qRound(m_pos));
            if (QWidget *content = m_area->widget())
                content->move(content->x(), -sb->value());
            m_velocity = 0;
            m_timer.stop();
        }
    }

    static constexpr qreal MAX_PULL = 150;  // Насколько можно «перетянуть» за край (до сопротивления)

    QPointer<QScrollArea> m_area;
    QTimer        m_timer;
    QElapsedTimer m_clock;
    QElapsedTimer m_lastWheel;
    qint64        m_lastTick = 0;
    qreal         m_pos = 0;       // Текущая позиция (может быть чуть за краем)
    qreal         m_target = 0;    // Куда едем
    qreal         m_velocity = 0;  // px/с
};
