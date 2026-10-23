// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  SmartButton.h — Кнопка с ручной отрисовкой (без артефактов QSS)           ║
// ║                                                                              ║
// ║  Обычная кнопка QPushButton + QSS border-radius даёт некрасивые артефакты  ║
// ║  на углах при прозрачном фоне окна (пиксели не обрезаются корректно).      ║
// ║                                                                              ║
// ║  SmartButton решает это: рисует фон и рамку вручную через QPainter         ║
// ║  с Antialiasing (сглаживание) и QPainterPath (чёткое обрезание).           ║
// ║  Результат — пиксельно-чистые скруглённые углы на любом фоне.              ║
// ║                                                                              ║
// ║  Использование:                                                              ║
// ║    auto *btn = new SmartButton("Нажми меня", parent);                       ║
// ║    btn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,150), 8);      ║
// ║    btn->setActiveBtnStyle(QColor(40,40,58,245), Qt::white);                 ║
// ║    btn->setActiveState(true);  // «нажато» / «выбрано»                      ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

// QPushButton — базовый класс кнопки Qt. Мы наследуем его и переопределяем paintEvent.
#include <QPushButton>
// QPainter — «кисть» для рисования: прямоугольники, текст, градиенты и т.д.
#include <QPainter>
// QPainterPath — путь для векторного рисования (скруглённые прямоугольники, кривые).
// Позволяет точно обрезать область отрисовки → чистые углы без артефактов.
#include <QPainterPath>
#include <QFontDatabase>
#include <QVariantAnimation>
#include <QEasingCurve>

// ─── Класс SmartButton ────────────────────────────────────────────────────────
// Полностью кастомная кнопка — Qt не рисует её стандартным способом.
// Всё рисование происходит в переопределённом paintEvent().
class SmartButton : public QPushButton
{
    Q_OBJECT

public:
    // Конструктор — принимает необязательный текст и родительский виджет.
    // «explicit» запрещает неявные преобразования: SmartButton b = "текст" — ошибка.
    explicit SmartButton(const QString &text = {}, QWidget *parent = nullptr)
        : QPushButton(text, parent)
    {
        setAutoFillBackground(false); // Отключаем автозаполнение фона Qt — рисуем сами
        setCursor(Qt::PointingHandCursor); // Курсор-указатель при наведении (как у ссылки)

        // Нажатие видно: кнопка чуть «вдавливается» (меньше и темнее), а
        // отпустили — мягко пружинит обратно, на миг чуть больше обычного

        connect(&m_pressAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_press = v.toReal();
            update();
        });
        connect(this, &QPushButton::pressed, this, [this]() { animatePress(1.0, 70, QEasingCurve::OutCubic); });
        connect(this, &QPushButton::released, this, [this]() {
            QEasingCurve spring(QEasingCurve::OutBack);
            spring.setOvershoot(3.0);
            animatePress(0.0, 260, spring);
        });
    }

    // Установить нормальный стиль кнопки (когда не выбрана/не активна).
    // bg        — цвет фона (RGBA: красный, зелёный, синий, прозрачность 0-255)
    // textColor — цвет текста
    // radius    — радиус скругления углов в пикселях (8 = слабое, 16 = сильное)
    void setBtnStyle(const QColor &bg, const QColor &textColor, int radius = 8)
    {
        m_bg        = bg;
        m_textColor = textColor;
        m_radius    = radius;
        // Если активный стиль не задан отдельно — используем тот же
        m_activeBg   = bg;
        m_activeText = textColor;
        update(); // Просим Qt перерисовать кнопку с новыми параметрами
    }

    // Установить стиль для активного/выбранного состояния.
    // Активное состояние — например, выбранный профиль или нажатая вкладка.
    void setActiveBtnStyle(const QColor &activeBg, const QColor &activeText)
    {
        m_activeBg   = activeBg;
        m_activeText = activeText;
        update();
    }

    // Переключить активное состояние кнопки программно.
    // active = true  → кнопка выглядит «нажатой» / «выбранной»
    // active = false → обычный вид
    void setActiveState(bool active)
    {
        if (m_active == active) return; // Если не изменилось — не перерисовываем
        m_active = active;
        update();
    }

    // Проверить текущее состояние кнопки (для логики в коде).
    bool isActiveState() const { return m_active; }

    // Показать вместо текста значок из системного шрифта значков Windows
    // (Segoe Fluent Icons в Win11, Segoe MDL2 Assets в Win10).
    // glyph — код значка, например QChar(0xE713) = шестерёнка.
    void setIconGlyph(QChar glyph, int pixelSize = 14)
    {
        m_glyph   = glyph;
        m_glyphPx = pixelSize;
        update();
    }

    static QString iconFontFamily()
    {
        static const QString family =
            QFontDatabase::families().contains(QStringLiteral("Segoe Fluent Icons"))
                ? QStringLiteral("Segoe Fluent Icons")
                : QStringLiteral("Segoe MDL2 Assets");
        return family;
    }

// ─── Переопределение методов QWidget ─────────────────────────────────────────
protected:
    // paintEvent — главный метод рисования. Qt вызывает его когда нужно перерисовать кнопку.
    // Переопределяем его полностью — стандартное рисование QPushButton нам не нужно.
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // Antialiasing = сглаживание: края скруглённых углов становятся плавными
        p.setRenderHint(QPainter::Antialiasing, true);

        // underMouse() = true если курсор сейчас над кнопкой (hover-эффект)
        const bool hovered = underMouse();

        // Выбираем цвета в зависимости от состояния (активна ли кнопка)
        QColor bg      = m_active ? m_activeBg   : m_bg;
        QColor textCol = m_active ? m_activeText : m_textColor;

        // Нажата: меньше (до -6%) и темнее; при отскоке m_press < 0 — чуть больше
        if (m_press != 0.0) {
            const qreal k = 1.0 - 0.06 * m_press;
            p.translate(width() / 2.0, height() / 2.0);
            p.scale(k, k);
            p.translate(-width() / 2.0, -height() / 2.0);
            if (m_press > 0) {
                const int a = bg.alpha();
                bg = bg.darker(100 + int(45 * m_press));
                bg.setAlpha(a);
                textCol.setAlphaF(textCol.alphaF() * (1.0 - 0.2 * m_press));
            }
        }

        // Рамка чуть ярче при наведении или в активном состоянии
        QColor border  = (hovered || m_active) ? QColor(255, 255, 255, 90)
                                               : QColor(255, 255, 255, 55);

        // rect() — прямоугольник размером с кнопку (0,0 → ширина,высота)
        // adjusted(+1,+1,-1,-1) — уменьшаем на 1px со всех сторон чтобы рамка не обрезалась
        QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);

        // QPainterPath — описываем форму кнопки как «путь» со скруглёнными углами
        QPainterPath path;
        path.addRoundedRect(r, m_radius, m_radius); // скруглённый прямоугольник

        p.fillPath(path, bg);           // Заливаем фон внутри пути
        p.setPen(QPen(border, 2.0));    // Ставим перо для рамки (толщина 2px)
        p.drawPath(path);               // Рисуем рамку по контуру пути

        // Рисуем текст по центру кнопки
        p.setPen(textCol);
        p.setFont(font());
        // Qt::TextShowMnemonic — поддержка «горячих букв» (например &О → подчёркнутая О)
        if (!m_glyph.isNull()) {
            // Значок из системного шрифта Windows: одинаковый размер и стиль у
            // всех квадратных кнопок, ровно по центру (в отличие от эмодзи)
            QFont f(iconFontFamily());
            f.setPixelSize(m_glyphPx);
            p.setFont(f);
            p.drawText(rect(), Qt::AlignCenter, QString(m_glyph));
        } else {
            p.drawText(rect(), Qt::AlignCenter | Qt::TextShowMnemonic, text());
        }

        // Точка в правом верхнем углу — «есть что посмотреть» (обновление)
        if (m_badge) {
            const qreal d = qMax(7.0, height() / 4.5);
            const QRectF dot(width() - d - 3, 3, d, d);
            p.setPen(QPen(QColor(20, 20, 32), 1.5));
            p.setBrush(QColor(120, 190, 255));
            p.drawEllipse(dot);
        }
    }

public:
    void setBadge(bool on) { if (m_badge != on) { m_badge = on; update(); } }

protected:

    // При наведении/уходе курсора — перерисовываем для hover-эффекта рамки
    void enterEvent(QEnterEvent *e) override { update(); QPushButton::enterEvent(e); }
    void leaveEvent(QEvent *e)       override { update(); QPushButton::leaveEvent(e); }

private:
    // ─── Стили (цвета и геометрия) ────────────────────────────────────────────
    // Значения по умолчанию — тёмно-синий полупрозрачный фон, белый текст.
    // QColor(R, G, B, A): A = прозрачность (0=невидимый, 255=непрозрачный)
    QColor m_bg        { 20, 20, 32, 245 };   // Фон нормального состояния
    QColor m_textColor { 255, 255, 255, 160 }; // Текст нормального состояния (слегка прозрачный)
    QColor m_activeBg  { 40, 40, 58, 245 };   // Фон активного состояния (чуть светлее)
    QColor m_activeText{ 255, 255, 255, 255 }; // Текст активного состояния (полностью белый)
    int    m_radius = 8;   // Радиус скругления углов в пикселях
    bool   m_active = false; // Текущее состояние: false = обычное, true = активное
    QChar  m_glyph;          // Значок вместо текста (пусто = обычный текст)
    int    m_glyphPx = 14;   // Размер значка в пикселях
    bool   m_badge = false;  // Точка-значок в углу (например, есть обновление)
    qreal  m_press = 0.0;    // Вдавленность: 0 — обычная, 1 — нажата (см. animatePress)
    QVariantAnimation m_pressAnim;

    void animatePress(qreal to, int ms, const QEasingCurve &curve)
    {
        m_pressAnim.stop();
        m_pressAnim.setStartValue(m_press);
        m_pressAnim.setEndValue(to);
        m_pressAnim.setDuration(ms);
        m_pressAnim.setEasingCurve(curve);
        m_pressAnim.start();
    }
};
