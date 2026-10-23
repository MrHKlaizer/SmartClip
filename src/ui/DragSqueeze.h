#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  DragSqueeze.h — SmartClip «прижимается» к краям экрана, пока несёшь файл     ║
// ║                                                                              ║
// ║  Понесли файл из полоски — колонки уезжают вбок, верхняя панель вверх,       ║
// ║  поиск с полоской вниз; почти за край, с лёгким пружинящим сжатием. Всё      ║
// ║  остаётся в пределах своего монитора. Отпустили файл — всё мягко             ║
// ║  отпрыгивает на место.                                                       ║
// ║                                                                              ║
// ║  Как и анимация открытия (Anim.h), играет снимок окна на отдельной «сцене»:  ║
// ║  настоящее окно в это время полностью прозрачное и не мешает бросить файл    ║
// ║  в программу под ним.                                                        ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QWidget>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QElapsedTimer>
#include <QEasingCurve>
#include <QPointer>
#include <functional>

#include "Anim.h"

class DragSqueeze : public QWidget
{
public:
    struct Piece {
        QRect   src;          // Что рисуем (координаты окна)
        QPointF offset;       // Куда уезжает при полном сжатии
        qreal   scaleX = 1.0; // Сжатие «пружины» по горизонтали…
        qreal   scaleY = 1.0; // …и по вертикали
        QPointF anchor;       // Вокруг какой точки сжимаем (обычно край экрана)
    };

    DragSqueeze(QWidget *target, const QList<Piece> &pieces)
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                           Qt::WindowStaysOnTopHint)
        , m_target(target), m_pieces(pieces)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlag(Qt::WindowTransparentForInput);  // Файл бросают сквозь сцену
        // Окно-снимок для проверок (не на экране) — и сцена не на экране
        if (target->testAttribute(Qt::WA_DontShowOnScreen)) setAttribute(Qt::WA_DontShowOnScreen);
        m_snapshot = target->grab();
        setGeometry(target->geometry());
        m_tick.setTimerType(Qt::PreciseTimer);
        m_tick.setInterval(6);
        QObject::connect(&m_tick, &QTimer::timeout, this, [this]() { step(); });
    }

    ~DragSqueeze() override
    {
        if (m_target) m_target->setWindowOpacity(1.0);  // Страховка: окно не должно остаться невидимым
    }

    // SmartClip прячется: сцену убирают, а окну видимость не возвращаем
    void detach() { m_target = nullptr; m_done = nullptr; }

    // Прижаться к краям (с лёгкой пружинкой)
    void squeeze()
    {
        show();
        if (m_target) m_target->setWindowOpacity(0.0);  // Настоящее окно — прозрачное
        run(1.0, Anim::ms(Anim::Kind::WindowClose, 380), QEasingCurve::OutBack, 1.4);
    }

    // Отпрыгнуть на место и вернуть настоящее окно
    void release(std::function<void()> done = {})
    {
        m_done = std::move(done);
        run(0.0, Anim::ms(Anim::Kind::WindowOpen, 460), QEasingCurve::OutBack, 1.15);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const qreal dpr = m_snapshot.devicePixelRatio();
        for (const Piece &pc : m_pieces) {
            const qreal sx = 1.0 + (pc.scaleX - 1.0) * m_q;
            const qreal sy = 1.0 + (pc.scaleY - 1.0) * m_q;
            const QRectF from(pc.src);
            // Масштаб вокруг якоря, затем сдвиг
            QRectF to(pc.anchor.x() + (from.left() - pc.anchor.x()) * sx,
                      pc.anchor.y() + (from.top() - pc.anchor.y()) * sy,
                      from.width() * sx, from.height() * sy);
            to.translate(pc.offset * m_q);
            const QRectF srcPx(from.left() * dpr, from.top() * dpr, from.width() * dpr, from.height() * dpr);
            p.drawPixmap(to, m_snapshot, srcPx);
        }
    }

private:
    void run(qreal target, int ms, QEasingCurve::Type type, qreal overshoot)
    {
        m_from = m_q;
        m_to = target;
        m_curve = QEasingCurve(type);
        m_curve.setOvershoot(overshoot);
        m_duration = qMax(1, ms);
        if (!Anim::enabled(Anim::Kind::WindowOpen)) { m_q = target; finishStep(); return; }
        m_clock.restart();
        m_tick.setInterval(Anim::frameMs(this));  // По частоте монитора
        m_tick.start();
    }

    void step()
    {
        const qreal k = qMin(1.0, m_clock.elapsed() / qreal(m_duration));
        m_q = m_from + (m_to - m_from) * m_curve.valueForProgress(k);
        update();
        if (k >= 1.0) { m_q = m_to; finishStep(); }
    }

    void finishStep()
    {
        m_tick.stop();
        update();
        if (m_to <= 0.0) {  // Вернулись на место — показываем настоящее окно
            if (m_target) m_target->setWindowOpacity(1.0);
            if (m_done) m_done();
            close();
        }
    }

    QPointer<QWidget> m_target;
    QList<Piece>      m_pieces;
    QPixmap           m_snapshot;
    QTimer            m_tick;
    QElapsedTimer     m_clock;
    QEasingCurve      m_curve;
    qreal             m_q = 0, m_from = 0, m_to = 0;  // 0 — на месте, 1 — прижато
    int               m_duration = 1;
    std::function<void()> m_done;
};
