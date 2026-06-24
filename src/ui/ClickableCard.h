// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  ClickableCard.h — Карточка истории/закрепов с кликом и перетаскиванием    ║
// ║                                                                              ║
// ║  ClickableCard — это «умная» версия QLabel:                                 ║
// ║  • Рисует скруглённый фон вручную (как SmartButton — без QSS-артефактов)   ║
// ║  • Сигнал clicked() при клике                                               ║
// ║  • Hover-эффект (рамка светлее при наведении)                               ║
// ║                                                                              ║
// ║  Режим редактирования (setEditMode(true)):                                  ║
// ║  • Короткий клик → выбрать/снять выбор карточки (для групповых операций)   ║
// ║  • Долгое нажатие (250мс) + движение → перетаскивание (drag-to-reorder)    ║
// ║                                                                              ║
// ║  Состояния рамки (визуальная обратная связь):                               ║
// ║  • Обычная       — полупрозрачная белая (255, 255, 255, 55)                ║
// ║  • Hover         — ярче (255, 255, 255, 90)                                ║
// ║  • Выбрана       — золотая (255, 215, 70, 210) + жёлтый оверлей           ║
// ║  • Перетаскивается — голубая (120, 200, 255, 220)                          ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

// QLabel — виджет для отображения текста и изображений.
// ClickableCard наследует его чтобы внутри карточки можно было показывать текст/картинку.
#include <QLabel>
// События мыши, наведения, и рисования:
#include <QMouseEvent>
#include <QEnterEvent>   // Qt6: отдельное событие наведения мыши
#include <QPainter>
#include <QPainterPath>
// QTimer — таймер для определения «долгого нажатия» (250мс → режим перетаскивания)
#include <QTimer>

// ─── Класс ClickableCard ─────────────────────────────────────────────────────
class ClickableCard : public QLabel
{
    Q_OBJECT

public:
    // Конструктор — настраивает курсор и таймер долгого нажатия.
    explicit ClickableCard(QWidget *parent = nullptr) : QLabel(parent)
    {
        setCursor(Qt::PointingHandCursor);     // Курсор «указатель» как у ссылок
        setAutoFillBackground(false);           // Рисуем фон сами в paintEvent

        // Таймер срабатывает один раз через 250мс после нажатия кнопки мыши.
        // Если за это время мышь не отпустили — это начало перетаскивания.
        m_pressTimer.setSingleShot(true);   // Срабатывает один раз (не повторяется)
        m_pressTimer.setInterval(250);      // 250 миллисекунд = «долгое нажатие»
        connect(&m_pressTimer, &QTimer::timeout, this, [this]() {
            if (m_pressing && m_editMode) {
                m_dragReady = true;                      // Разрешаем начать тащить
                setCursor(Qt::SizeVerCursor);            // Курсор «двунаправленная стрелка»
            }
        });
    }

    // Установить цвет фона и радиус скругления карточки.
    void setCardStyle(const QColor &bg, int radius = 10)
    {
        m_bgColor = bg;
        m_radius  = radius;
        update();  // Перерисовать
    }

    // Включить/выключить режим редактирования.
    // В режиме редактирования: клик = выбор, долгий зажим = перетаскивание.
    void setEditMode(bool on)
    {
        m_editMode  = on;
        m_dragReady = false;
        m_dragging  = false;
        m_pressing  = false;
        if (!on) setCursor(Qt::PointingHandCursor);
        update();
    }

    // Установить/снять состояние «выбрано» (золотая рамка + оверлей).
    void setSelected(bool sel) { m_selected = sel; update(); }

    // Проверить выбрана ли карточка.
    bool isSelected() const { return m_selected; }

// ─── Сигналы ─────────────────────────────────────────────────────────────────
signals:
    // Карточка кликнута (короткое нажатие).
    void clicked();

    // Начало перетаскивания — пользователь зажал мышь > 250мс и начал двигать.
    // globalPos — позиция курсора в координатах экрана.
    void dragStarted(QPoint globalPos);

    // Курсор сдвинулся во время перетаскивания.
    void dragMoved(QPoint globalPos);

    // Перетаскивание завершено — пользователь отпустил кнопку мыши.
    void dragFinished();

// ─── Переопределения QWidget ─────────────────────────────────────────────────
protected:
    // Рисование карточки: фон, рамка, оверлей выделения, чекбокс.
    void paintEvent(QPaintEvent *event) override
    {
        {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, true);

            // Рисуем скруглённый прямоугольник (фон карточки).
            QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
            QPainterPath path;
            path.addRoundedRect(r, m_radius, m_radius);

            // Основной фон карточки
            p.fillPath(path, m_bgColor);

            // Жёлтый полупрозрачный оверлей если карточка выбрана в режиме редактирования
            if (m_editMode && m_selected)
                p.fillPath(path, QColor(255, 200, 50, 45));

            // Выбор цвета рамки в зависимости от состояния:
            QColor border;
            if (m_dragging)                        // Тянем — голубая рамка
                border = QColor(120, 200, 255, 220);
            else if (m_editMode && m_selected)     // Выбрана — золотая рамка
                border = QColor(255, 215, 70, 210);
            else if (m_hovered)                    // Наведение — яркая белая рамка
                border = QColor(255, 255, 255, 90);
            else                                   // Обычная — полупрозрачная белая
                border = QColor(255, 255, 255, 55);

            p.setPen(QPen(border, 2.0));
            p.drawPath(path);
        }

        // Рисуем содержимое QLabel (текст или изображение) поверх нашего фона
        QLabel::paintEvent(event);

        // ── Чекбокс в правом верхнем углу (только в режиме редактирования) ────
        if (m_editMode) {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            // Кружок 16x16 в правом верхнем углу с отступами
            QRectF cr(width() - 23, 5, 16, 16);

            if (m_selected) {
                // Выбрана: заполненный золотой кружок с галочкой
                p.setBrush(QColor(255, 200, 50, 225));
                p.setPen(QPen(QColor(255, 220, 100, 240), 1.5));
                p.drawEllipse(cr);
                // Рисуем галочку ✓ как ломаную линию через QPainterPath
                p.setPen(QPen(QColor(20, 10, 0, 230), 2,
                              Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                QPainterPath check;
                check.moveTo(cr.x() + 3.5, cr.center().y() + 0.5);
                check.lineTo(cr.center().x() - 0.5, cr.bottom() - 3.5);
                check.lineTo(cr.right() - 2.5, cr.top() + 3.5);
                p.drawPath(check);
            } else {
                // Не выбрана: пустой полупрозрачный кружок
                p.setBrush(QColor(255, 255, 255, 25));
                p.setPen(QPen(QColor(255, 255, 255, 100), 1.5));
                p.drawEllipse(cr);
            }
        }
    }

    // Нажатие кнопки мыши
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (!m_editMode) {
                // Обычный режим: просто emit clicked()
                emit clicked();
            } else {
                // Режим редактирования: запускаем таймер долгого нажатия
                m_pressing  = true;
                m_dragReady = false;
                m_dragging  = false;
                m_pressPos  = event->pos();  // Запоминаем точку нажатия
                m_pressTimer.start();        // Запускаем 250мс таймер
            }
        }
        QLabel::mousePressEvent(event);
    }

    // Движение мыши (только если кнопка зажата)
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_pressing && m_editMode) {
            // Начать перетаскивание если: таймер сработал И мышь сдвинулась > 6px
            // manhattanLength() — приближение расстояния (|dx| + |dy|), быстрее чем sqrt
            if (!m_dragging && m_dragReady &&
                (event->pos() - m_pressPos).manhattanLength() > 6)
            {
                m_dragging = true;
                update();
                emit dragStarted(mapToGlobal(event->pos()));  // Координаты на экране
            }
            if (m_dragging)
                emit dragMoved(mapToGlobal(event->pos()));
        }
        QLabel::mouseMoveEvent(event);
    }

    // Отпускание кнопки мыши
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_editMode) {
            m_pressTimer.stop();  // Отменяем таймер если ещё не сработал
            if (m_dragging) {
                // Завершаем перетаскивание
                m_dragging  = false;
                m_dragReady = false;
                m_pressing  = false;
                setCursor(Qt::PointingHandCursor);
                update();
                emit dragFinished();
            } else if (m_pressing) {
                // Короткое нажатие (таймер не сработал) → переключить выбор
                m_pressing  = false;
                m_dragReady = false;
                emit clicked();
            }
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }

    // Наведение мыши — включаем hover-эффект
    void enterEvent(QEnterEvent *event) override
    {
        m_hovered = true;
        update();
        QLabel::enterEvent(event);
    }

    // Уход мыши — выключаем hover-эффект
    void leaveEvent(QEvent *event) override
    {
        m_hovered = false;
        update();
        QLabel::leaveEvent(event);
    }

private:
    // ─── Стили ────────────────────────────────────────────────────────────────
    QColor m_bgColor { 20, 20, 32, 245 };  // Тёмно-синий полупрозрачный фон по умолчанию
    int    m_radius  = 10;                  // Радиус скругления углов
    bool   m_hovered = false;               // Курсор над карточкой?

    // ─── Состояния режима редактирования ─────────────────────────────────────
    bool   m_editMode = false;   // Включён режим мультивыбора
    bool   m_selected = false;   // Карточка выбрана (галочка/оверлей)

    // ─── Состояния drag-to-reorder ────────────────────────────────────────────
    bool   m_pressing  = false;  // Кнопка зажата (ожидаем таймер или движение)
    bool   m_dragReady = false;  // Таймер 250мс сработал — можно начинать тащить
    bool   m_dragging  = false;  // Прямо сейчас тащим карточку
    QPoint m_pressPos;           // Точка нажатия (для проверки manhattanLength)
    QTimer m_pressTimer;         // Таймер 250мс для «долгого нажатия»
};
