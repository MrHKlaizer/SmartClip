// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  ScrollTopButton.h — Круглая кнопка «наверх» в колонке (SmartClip 2.0)      ║
// ║                                                                              ║
// ║  Появляется вверху колонки (по центру карточек), когда пролистали вниз      ║
// ║  примерно 10 карточек. Клик — колонка плавно уезжает в самое начало.        ║
// ║  Появление/исчезновение — мягкое проявление с лёгким увеличением.           ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

#include <QAbstractButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QEvent>
#include <QCoreApplication>
#include <functional>
#include "Anim.h"

class ScrollTopButton : public QAbstractButton
{
public:
    // centerX — где центр карточек колонки (по горизонтали, в координатах колонки).
    // scrollToTop — как прокрутить в начало (плавно, через SmoothScroller).
    ScrollTopButton(QScrollArea *area, QLayout *layout, int centerX, int diameter,
                    std::function<void()> scrollToTop)
        : QAbstractButton(area->viewport()), m_area(area), m_layout(layout), m_centerX(centerX)
    {
        setFixedSize(diameter, diameter);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QCoreApplication::translate("MainWindow", "Наверх"));
        hide();
        connect(this, &QAbstractButton::clicked, this, [scrollToTop]() { scrollToTop(); });

        m_anim.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_shown = v.toReal();
            update();
        });
        connect(&m_anim, &QVariantAnimation::finished, this, [this]() {
            if (m_shown <= 0.0) hide();
        });

        area->widget()->installEventFilter(this);   // Прокрутка двигает содержимое
        area->viewport()->installEventFilter(this); // Размер колонки
        place();
    }

protected:
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (obj == m_area->widget() && (e->type() == QEvent::Move || e->type() == QEvent::Resize))
            setWanted(passedEnough());
        else if (obj == m_area->viewport() && e->type() == QEvent::Resize)
            place();
        return false;
    }

    void paintEvent(QPaintEvent *) override
    {
        if (m_shown <= 0.0) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(m_shown);
        // Лёгкое увеличение при появлении (от 70% до 100%)
        const qreal s = 0.7 + 0.3 * m_shown;
        const QPointF c(width() / 2.0, height() / 2.0);
        p.translate(c);
        p.scale(s, s);
        p.translate(-c);

        const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        const bool hot = underMouse();
        p.setPen(QPen(QColor(255, 255, 255, hot ? 110 : 60), 1.2));
        p.setBrush(QColor(hot ? 48 : 30, hot ? 48 : 30, hot ? 74 : 48, 238));
        p.drawEllipse(r);

        // Стрелка вверх
        const qreal a = width() * 0.2;
        QPainterPath arrow;
        arrow.moveTo(c.x() - a, c.y() + a * 0.35);
        arrow.lineTo(c.x(), c.y() - a * 0.65);
        arrow.lineTo(c.x() + a, c.y() + a * 0.35);
        p.setPen(QPen(QColor(235, 240, 255, hot ? 255 : 220), qMax(1.8, width() / 16.0),
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(arrow);
    }

    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    // Вверху колонки, по центру карточек
    void place()
    {
        move(m_centerX - width() / 2, 10);
        raise();
    }

    // Пролистали ли примерно 10 карточек
    bool passedEnough() const
    {
        const int scrolled = -m_area->widget()->y();
        int passed = 0;
        for (int i = 0; i < m_layout->count(); ++i) {
            QWidget *w = m_layout->itemAt(i)->widget();
            if (!w || w->isHidden()) continue;
            if (w->geometry().bottom() < scrolled) ++passed;
            else break;  // Карточки идут сверху вниз — дальше уже видимые
        }
        return passed >= 10;
    }

    // Показать/спрятать (плавно в новом стиле прокрутки)
    void setWanted(bool want)
    {
        if (want == m_want) return;
        m_want = want;
        raise();
        if (want) show();
        if (!Anim::enabled(Anim::Kind::Scroll)) {
            m_anim.stop();
            m_shown = want ? 1.0 : 0.0;
            if (!want) hide();
            update();
            return;
        }
        m_anim.stop();
        m_anim.setDuration(Anim::ms(Anim::Kind::Scroll, want ? 220 : 160));
        m_anim.setStartValue(m_shown);
        m_anim.setEndValue(want ? 1.0 : 0.0);
        m_anim.start();
    }

    QScrollArea      *m_area;
    QLayout          *m_layout;
    int               m_centerX;
    bool              m_want  = false;
    qreal             m_shown = 0.0;   // 0 — спрятана, 1 — видна целиком
    QVariantAnimation m_anim;
};
