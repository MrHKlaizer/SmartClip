#pragma once

#include <QPushButton>
#include <QPainter>
#include <QPainterPath>

// QPushButton с ручным рисованием фона и рамки через QPainter —
// так же как ClickableCard. QSS border-radius создаёт артефакты на прозрачном
// окне; здесь рисуем rounded-rect через QPainterPath с Antialiasing,
// что даёт чистую альфу на углах.
//
// Использование:
//   btn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,150), 8);
//   btn->setActiveBtnStyle(QColor(40,40,58,245), Qt::white);
//   btn->setActiveState(true);   // вместо setProperty + unpolish/polish
class SmartButton : public QPushButton
{
    Q_OBJECT

public:
    explicit SmartButton(const QString &text = {}, QWidget *parent = nullptr)
        : QPushButton(text, parent)
    {
        setAutoFillBackground(false);
        setCursor(Qt::PointingHandCursor);
    }

    // Нормальный стиль кнопки
    void setBtnStyle(const QColor &bg, const QColor &textColor, int radius = 8)
    {
        m_bg        = bg;
        m_textColor = textColor;
        m_radius    = radius;
        m_activeBg   = bg;
        m_activeText = textColor;
        update();
    }

    // Стиль в активном/выбранном состоянии
    void setActiveBtnStyle(const QColor &activeBg, const QColor &activeText)
    {
        m_activeBg   = activeBg;
        m_activeText = activeText;
        update();
    }

    // Переключить активное состояние
    void setActiveState(bool active)
    {
        if (m_active == active) return;
        m_active = active;
        update();
    }

    bool isActiveState() const { return m_active; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const bool hovered = underMouse();
        QColor bg      = m_active ? m_activeBg   : m_bg;
        QColor textCol = m_active ? m_activeText : m_textColor;
        QColor border  = (hovered || m_active) ? QColor(255, 255, 255, 90)
                                               : QColor(255, 255, 255, 55);

        QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        QPainterPath path;
        path.addRoundedRect(r, m_radius, m_radius);

        p.fillPath(path, bg);
        p.setPen(QPen(border, 2.0));
        p.drawPath(path);

        p.setPen(textCol);
        p.setFont(font());
        p.drawText(rect(), Qt::AlignCenter | Qt::TextShowMnemonic, text());
    }

    void enterEvent(QEnterEvent *e) override { update(); QPushButton::enterEvent(e); }
    void leaveEvent(QEvent *e)       override { update(); QPushButton::leaveEvent(e); }

private:
    QColor m_bg        { 20, 20, 32, 245 };
    QColor m_textColor { 255, 255, 255, 160 };
    QColor m_activeBg  { 40, 40, 58, 245 };
    QColor m_activeText{ 255, 255, 255, 255 };
    int    m_radius = 8;
    bool   m_active = false;
};
