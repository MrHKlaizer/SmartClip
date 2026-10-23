#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  FolderSlide.h — смена папки закрепов с анимацией                             ║
// ║                                                                              ║
// ║  Было: карточки старой папки мгновенно сменялись новыми. Теперь старые       ║
// ║  плавно уезжают и тают, а новые въезжают с другой стороны: вглубь папки —     ║
// ║  справа налево, назад/наверх — слева направо.                                ║
// ║                                                                              ║
// ║  Играет снимок колонки поверх неё; настоящая колонка (уже с новой папкой)    ║
// ║  на это время скрыта и появляется, когда анимация доиграла.                  ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QWidget>
#include <QAbstractScrollArea>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QEasingCurve>

#include "Anim.h"

class FolderSlide : public QWidget
{
public:
    // area — колонка; before — её снимок до смены папки; dir = +1 вглубь, -1 назад
    FolderSlide(QAbstractScrollArea *area, const QPixmap &before, int dir)
        : QWidget(area->parentWidget()), m_area(area), m_before(before), m_dir(dir)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(area->geometry());
        m_tick.setTimerType(Qt::PreciseTimer);
        m_tick.setInterval(6);
        QObject::connect(&m_tick, &QTimer::timeout, this, [this]() { step(); });
    }

    // Новая папка уже загружена в колонку — снимаем её и играем переход
    void play()
    {
        if (!m_area) { deleteLater(); return; }
        m_after = m_area->grab();
        m_area->viewport()->hide();   // Настоящую колонку прячем до конца анимации
        show();
        raise();
        m_duration = qMax(1, Anim::ms(Anim::Kind::Tabs, 300));
        m_clock.start();
        m_tick.setInterval(Anim::frameMs(this));  // По частоте монитора
        m_tick.start();
        step();
    }

    ~FolderSlide() override
    {
        if (m_area) m_area->viewport()->show();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const qreal shift = width() * 0.18;
        // Старая папка уезжает и тает быстрее, новая въезжает следом
        const qreal out = QEasingCurve(QEasingCurve::InCubic).valueForProgress(qMin(1.0, m_k * 1.6));
        const qreal in  = QEasingCurve(QEasingCurve::OutCubic).valueForProgress(m_k);
        p.setOpacity(1.0 - out);
        p.drawPixmap(QPointF(-m_dir * shift * out, 0), m_before);
        p.setOpacity(in);
        p.drawPixmap(QPointF(m_dir * shift * (1.0 - in), 0), m_after);
    }

private:
    void step()
    {
        m_k = qMin(1.0, m_clock.elapsed() / qreal(m_duration));
        update();
        if (m_k >= 1.0) {
            m_tick.stop();
            if (m_area) m_area->viewport()->show();
            deleteLater();
        }
    }

    QPointer<QAbstractScrollArea> m_area;
    QPixmap       m_before, m_after;
    int           m_dir = 1;
    qreal         m_k = 0;
    int           m_duration = 1;
    QTimer        m_tick;
    QElapsedTimer m_clock;
};
