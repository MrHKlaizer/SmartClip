// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  NavTabs.h — Боковое меню разделов (окно «Настройки», SmartClip 2.0)        ║
// ║                                                                              ║
// ║  Вместо вкладок сверху (5-я не помещалась, появлялись стрелки «‹ ›»):       ║
// ║  • слева — список разделов с одноцветными значками;                         ║
// ║  • выбранный раздел отмечен «пилюлей», которая плавно ПЕРЕЕЗЖАЕТ к новому  ║
// ║    пункту с лёгкой пружинкой; под мышкой пункт мягко подсвечивается;        ║
// ║  • справа — заголовок и содержимое раздела; при переключении новый раздел  ║
// ║    выезжает снизу (если пункт ниже) или сверху (если выше) с проявлением.   ║
// ║                                                                              ║
// ║  Анимации управляются строкой «Вкладки» в окне «Оформление» (кнопка 🖌).    ║
// ║  API похож на QTabWidget: addTab(страница, название, значок).               ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

#include <QWidget>
#include <QLabel>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QPointer>
#include <functional>
#include <QWheelEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QApplication>
#include "Anim.h"
#include "SmartButton.h"   // SmartButton::iconFontFamily() — системный шрифт значков
#include "SmoothScroll.h"  // Плавная прокрутка разделов

// ─── Боковое меню ────────────────────────────────────────────────────────────
class SideNav : public QWidget
{
public:
    explicit SideNav(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setFixedWidth(196);
        m_pillAnim.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_pillAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_pillY = v.toReal();
            update();
        });
    }

    void addItem(const QString &text, QChar glyph) { m_items.append({text, glyph}); update(); }
    int  count() const { return m_items.size(); }
    int  current() const { return m_current; }

    std::function<void(int)> onSelect;  // Пользователь выбрал пункт

    // Переключить выделение (с переездом «пилюли», если анимации включены)
    void setCurrent(int i)
    {
        if (i < 0 || i >= m_items.size()) return;
        const qreal to = itemTop(i);
        if (m_current < 0 || !Anim::enabled(Anim::Kind::Tabs) || !isVisible()) {
            m_pillAnim.stop();
            m_pillY = to;
        } else {
            QEasingCurve spring(QEasingCurve::OutBack);
            spring.setOvershoot(1.25);
            m_pillAnim.stop();
            m_pillAnim.setEasingCurve(spring);
            m_pillAnim.setDuration(Anim::ms(Anim::Kind::Tabs, 320));
            m_pillAnim.setStartValue(m_pillY);
            m_pillAnim.setEndValue(to);
            m_pillAnim.start();
        }
        m_current = i;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor accent(160, 200, 255);

        // Подсветка пункта под мышкой (кроме выбранного)
        if (m_hover >= 0 && m_hover != m_current) {
            QPainterPath hp;
            hp.addRoundedRect(QRectF(PAD, itemTop(m_hover), width() - 2 * PAD, ITEM_H), 9, 9);
            p.fillPath(hp, QColor(255, 255, 255, 12));
        }

        // «Пилюля» выбранного пункта — едет к новому пункту
        if (m_current >= 0) {
            const QRectF pill(PAD, m_pillY, width() - 2 * PAD, ITEM_H);
            QPainterPath pp;
            pp.addRoundedRect(pill, 9, 9);
            p.fillPath(pp, QColor(accent.red(), accent.green(), accent.blue(), 30));
            p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 110), 1.0));
            p.drawPath(pp);
            // Яркая полоска слева — как в Windows 11
            QPainterPath bar;
            bar.addRoundedRect(QRectF(pill.left() + 5, pill.center().y() - 9, 3, 18), 1.5, 1.5);
            p.fillPath(bar, QColor(accent.red(), accent.green(), accent.blue(), 235));
        }

        // Пункты: значок + подпись
        QFont iconFont(SmartButton::iconFontFamily());
        iconFont.setPixelSize(15);
        QFont textFont = font();
        textFont.setPixelSize(13);
        for (int i = 0; i < m_items.size(); ++i) {
            const qreal top = itemTop(i);
            const bool sel = (i == m_current);
            const QColor col = sel ? QColor(255, 255, 255) : QColor(255, 255, 255, 165);
            p.setPen(col);
            p.setFont(iconFont);
            p.drawText(QRectF(PAD + 14, top, 22, ITEM_H), Qt::AlignVCenter | Qt::AlignLeft,
                       QString(m_items[i].glyph));
            p.setFont(textFont);
            p.drawText(QRectF(PAD + 44, top, width() - PAD - 50, ITEM_H),
                       Qt::AlignVCenter | Qt::AlignLeft, m_items[i].text);
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = indexAt(e->position().y());
        if (h != m_hover) { m_hover = h; update(); }
    }
    void leaveEvent(QEvent *) override { m_hover = -1; update(); }

    // Колёсико над меню: щелчок = соседний раздел (быстрые щелчки не
    // проскакивают через несколько разделов разом)
    void wheelEvent(QWheelEvent *e) override
    {
        e->accept();
        m_wheelAcc += e->angleDelta().y();
        if (qAbs(m_wheelAcc) < 120) return;
        const int dir = m_wheelAcc < 0 ? +1 : -1;
        m_wheelAcc = 0;
        if (m_wheelLock.isValid() && m_wheelLock.elapsed() < 160) return;
        const int i = m_current + dir;
        if (i >= 0 && i < m_items.size() && onSelect) {
            m_wheelLock.restart();
            onSelect(i);
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) return;
        const int i = indexAt(e->position().y());
        if (i >= 0 && i != m_current && onSelect) onSelect(i);
    }
    QSize sizeHint() const override { return QSize(196, int(itemTop(m_items.size())) + 8); }

private:
    static constexpr qreal PAD    = 6;    // Отступ пунктов от краёв меню
    static constexpr qreal ITEM_H = 40;   // Высота пункта
    static constexpr qreal GAP    = 4;    // Промежуток между пунктами
    static constexpr qreal TOP    = 4;    // Отступ сверху

    qreal itemTop(int i) const { return TOP + i * (ITEM_H + GAP); }
    int indexAt(qreal y) const
    {
        for (int i = 0; i < m_items.size(); ++i)
            if (y >= itemTop(i) && y < itemTop(i) + ITEM_H) return i;
        return -1;
    }

    struct Item { QString text; QChar glyph; };
    QList<Item>       m_items;
    int               m_wheelAcc = 0;
    QElapsedTimer     m_wheelLock;
    int               m_current = -1;
    int               m_hover   = -1;
    qreal             m_pillY   = 0;
    QVariantAnimation m_pillAnim;
};

// ─── Слой перехода между разделами ───────────────────────────────────────────
// Рисует снимок старого раздела (уходит и тает) и нового (выезжает и
// проявляется) поверх области содержимого, пока идёт анимация.
class PageTransition : public QWidget
{
public:
    PageTransition(QWidget *area, const QPixmap &from, const QPixmap &to, int dir, int durationMs,
                   QColor bg)
        : QWidget(area), m_from(from), m_to(to), m_dir(dir), m_bg(bg)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(area->rect());
        raise();
        show();
        m_anim.setDuration(durationMs);
        m_anim.setStartValue(0.0);
        m_anim.setEndValue(1.0);
        connect(&m_anim, &QVariantAnimation::valueChanged, this, [this]() { update(); });
        connect(&m_anim, &QVariantAnimation::finished, this, [this]() { deleteLater(); });
        m_anim.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const qreal t = m_anim.currentValue().toReal();
        const qreal e = QEasingCurve(QEasingCurve::OutCubic).valueForProgress(t);
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), m_bg);  // Закрываем настоящую страницу под слоем
        // Старый раздел: уходит в противоположную сторону и тает (быстрее)
        const qreal fo = qMax(0.0, 1.0 - e * 1.6);
        if (fo > 0) {
            p.setOpacity(fo);
            p.drawPixmap(QPointF(0, -m_dir * 18.0 * e), m_from);
        }
        // Новый раздел: выезжает со своей стороны и проявляется
        p.setOpacity(e);
        p.drawPixmap(QPointF(0, m_dir * 34.0 * (1.0 - e)), m_to);
    }

private:
    QPixmap           m_from, m_to;
    int               m_dir;
    QColor            m_bg;
    QVariantAnimation m_anim;
};

// ─── Боковое меню + разделы ──────────────────────────────────────────────────
class NavTabs : public QWidget
{
public:
    // bg — цвет фона окна под разделами (нужен слою перехода)
    explicit NavTabs(QWidget *parent = nullptr, QColor bg = QColor(18, 18, 30))
        : QWidget(parent), m_bg(bg)
    {
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(14);

        m_nav = new SideNav(this);
        auto *navCol = new QVBoxLayout();
        navCol->setContentsMargins(0, 0, 0, 0);
        navCol->addWidget(m_nav);
        navCol->addStretch();
        row->addLayout(navCol);

        // Тонкая вертикальная линия-разделитель
        auto *sep = new QWidget(this);
        sep->setFixedWidth(1);
        sep->setStyleSheet("background: rgba(255,255,255,22);");
        row->addWidget(sep);

        m_content = new QWidget(this);
        auto *col = new QVBoxLayout(m_content);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(10);
        m_title = new QLabel(m_content);
        m_title->setStyleSheet("color:#ffffff;font-size:20px;font-weight:600;background:transparent;");
        m_stack = new QStackedWidget(m_content);
        col->addWidget(m_title);
        col->addWidget(m_stack, 1);
        row->addWidget(m_content, 1);

        m_nav->onSelect = [this](int i) { setCurrentIndex(i); };
    }

    void addTab(QWidget *page, const QString &label, QChar glyph)
    {
        // Каждый раздел прокручивается сам. Раньше прокручивалась только
        // «История», а длинные разделы растягивали окно за край экрана.
        auto *area = qobject_cast<QScrollArea *>(page);
        if (!area) {
            area = new QScrollArea();
            area->setWidgetResizable(true);
            area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            area->setFrameShape(QFrame::NoFrame);
            area->setWidget(page);
        }
        new SmoothScroller(area);  // Плавная прокрутка, как у колонок SmartClip
        // Колёсико над полями значений (см. eventFilter)
        for (QWidget *child : area->findChildren<QWidget*>())
            child->installEventFilter(this);
        m_stack->addWidget(area);
        m_nav->addItem(label, glyph);
        m_labels << label;
        if (m_stack->count() == 1) setCurrentIndex(0);
    }

    int count() const { return m_stack->count(); }
    int currentIndex() const { return m_stack->currentIndex(); }

    void setCurrentIndex(int i)
    {
        if (i < 0 || i >= m_stack->count()) return;
        const int prev = m_stack->currentIndex();
        const bool animate = prev >= 0 && prev != i && isVisible() &&
                             Anim::enabled(Anim::Kind::Tabs);
        QPixmap from;
        if (animate) from = m_content->grab();

        m_stack->setCurrentIndex(i);
        m_title->setText(m_labels.value(i));
        m_nav->setCurrent(i);

        if (animate) {
            // Новый раздел раскладываем сразу, чтобы снять его снимок
            if (m_content->layout()) m_content->layout()->activate();
            QPixmap to = m_content->grab();
            if (m_transition) m_transition->deleteLater();
            m_transition = new PageTransition(m_content, from, to, i > prev ? +1 : -1,
                                              Anim::ms(Anim::Kind::Tabs, 280), m_bg);
        }
    }

protected:
    // Колёсико над содержимым раздела прокручивает ТОЛЬКО этот раздел
    // (переключать разделы колёсиком — над меню слева, см. SideNav).
    // Списки и числовые поля меняют значение колёсиком, только если по ним
    // кликнули — иначе при прокрутке случайно менялись настройки.
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (e->type() != QEvent::Wheel) return false;
        auto *w = qobject_cast<QWidget *>(obj);
        const bool valueWidget = qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w);
        if (!valueWidget || w->hasFocus()) return false;

        // Над полем значения — прокручиваем раздел вместо смены значения
        auto *we = static_cast<QWheelEvent *>(e);
        if (auto *area = qobject_cast<QScrollArea *>(m_stack->currentWidget())) {
            QWheelEvent copy(area->viewport()->mapFromGlobal(we->globalPosition()),
                             we->globalPosition(), we->pixelDelta(), we->angleDelta(),
                             we->buttons(), we->modifiers(), we->phase(), we->inverted());
            QApplication::sendEvent(area->viewport(), &copy);
        }
        return true;
    }

private:
    SideNav        *m_nav     = nullptr;
    QWidget        *m_content = nullptr;
    QLabel         *m_title   = nullptr;
    QStackedWidget *m_stack   = nullptr;
    QStringList     m_labels;
    QColor          m_bg;
    QPointer<PageTransition> m_transition;
};
