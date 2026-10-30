#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  FolderShelf.h — полка с папками внизу окна                                  ║
// ║                                                                              ║
// ║  Под полоской свежих файлов — две полки (левая и правая половина середины). ║
// ║  На каждой — папки, которые добавил пользователь («+» → выбрать папку).     ║
// ║  ПКМ по папке — открыть в Проводнике, переименовать, перенести на другую    ║
// ║  полку, убрать. Клик — папка открывается прямо в SmartClip (FolderPanel.h). ║
// ║  Зажал (или потянул) — папка едет за мышью, соседи расступаются: так        ║
// ║  меняют порядок; отпустил над другой полкой — папка переезжает туда.        ║
// ║  Сама полка только рисует и сообщает о кликах — что делать,                 ║
// ║  решает MainWindow (onAdd / onOpen / onMenu).                                ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QPointer>
#include <QApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QEasingCurve>
#include <functional>
#include <cmath>
#include "Anim.h"
#include "SmartButton.h"   // SmartButton::iconFontFamily() — системный шрифт значков

class FolderShelf : public QWidget
{
public:
    struct Folder { QString path; QString name; };  // name пустое — имя папки

    FolderShelf(QWidget *parent, int scalePct) : QWidget(parent), m_scale(scalePct)
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);
        // Зажал папку и держишь — она «поднимается» (можно двигать)
        m_holdTimer.setSingleShot(true);
        m_holdTimer.setInterval(260);
        connect(&m_holdTimer, &QTimer::timeout, this, [this]() {
            if (m_press >= 0 && (QApplication::mouseButtons() & Qt::LeftButton)) startDrag(mapFromGlobal(QCursor::pos()));
        });
        // Соседи плавно разъезжаются, отпущенная папка плавно садится на место
        m_slide.setTimerType(Qt::PreciseTimer);
        connect(&m_slide, &QTimer::timeout, this, [this]() { slideStep(); });
        hide();
    }

    std::function<void()> onAdd;                                    // «+»
    std::function<void(int index, const QRect &tile)> onOpen;      // Клик по папке (tile — в координатах полки)
    std::function<void(int index, const QPoint &globalPos)> onMenu; // ПКМ по папке (-1 — по пустому месту)
    std::function<void()> onReorder;                                // Поменяли порядок (папки — уже в folders())
    // Отпустили перетаскиваемую папку не над этой полкой: true — забрали (другая полка)
    std::function<bool(int index, const QPoint &globalPos)> onDropOut;
    std::function<void(const QPoint &globalPos)> onDragMove;        // Тащим — где мышь (подсветить другую полку)

    // Высота полки (снаружи — чтобы поставить полоску файлов над ней)
    static int baseHeight() { return 66; }
    int shelfHeight() const { return s(baseHeight()); }

    void setFolders(const QList<Folder> &folders)
    {
        m_folders = folders;
        m_scroll = qBound(0, m_scroll, maxScroll());
        m_hover = -1;
        update();
    }
    const QList<Folder> &folders() const { return m_folders; }

    // Папка под точкой экрана (для «руки»: бросил файлы на папку — копия туда)
    QString folderAt(const QPoint &global) const
    {
        if (!isVisible()) return QString();
        const int i = hitAt(mapFromGlobal(global));
        return i >= 0 && i < m_folders.size() ? m_folders[i].path : QString();
    }
    // Несут файлы — подсветить папку, на которую их бросят
    void setDropHint(const QPoint &global)
    {
        int i = isVisible() ? hitAt(mapFromGlobal(global)) : None;
        if (i < 0) i = None;
        if (i != m_drop) { m_drop = i; update(); }
    }
    void clearDropHint() { if (m_drop != None) { m_drop = None; update(); } }

    // Сюда переложат папку с другой полки: куда встанет (номер места) или -1
    int slotAt(const QPoint &global) const
    {
        if (!isVisible()) return -1;
        const QPoint p = mapFromGlobal(global);
        if (!rect().contains(p)) return -1;
        const int x = p.x() + m_scroll - s(8);
        return qBound(0, int(std::floor((x + pitch() / 2.0) / pitch())), int(m_folders.size()));
    }
    // Подсветить, куда встанет папка с другой полки (-1 — убрать)
    void setInsertHint(int slot) { if (slot != m_insert) { m_insert = slot; update(); } }
    bool dragging() const { return m_drag >= 0; }
    // Esc — положить папку обратно, ничего не меняя
    bool cancelDrag()
    {
        if (m_drag < 0) return false;
        m_target = m_drag;
        finishDrag(false, QPoint());
        return true;
    }

    // Подпись папки: своё название или имя папки на диске
    static QString label(const Folder &f)
    {
        if (!f.name.isEmpty()) return f.name;
        const QString n = QDir(QDir::cleanPath(f.path)).dirName();
        return n.isEmpty() ? QDir::toNativeSeparators(f.path) : n;  // Корень диска — «C:\»
    }

protected:
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPoint pos = e->position().toPoint();
        if (m_drag >= 0) { dragTo(pos); return; }
        // Потянул папку — поднимаем сразу, не дожидаясь «зажал и держишь»
        if ((e->buttons() & Qt::LeftButton) && m_press >= 0
            && (pos - m_pressPos).manhattanLength() > QApplication::startDragDistance()) {
            startDrag(pos);
            return;
        }
        const int h = hitAt(e->position().toPoint());
        if (h != m_hover) { m_hover = h; update(); }
        setCursor(h != None ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    void leaveEvent(QEvent *) override
    {
        if (m_hover != None) { m_hover = None; update(); }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (m_drag >= 0) return;
        m_press = hitAt(e->position().toPoint());
        m_pressPos = e->position().toPoint();
        if (e->button() == Qt::LeftButton && m_press >= 0) m_holdTimer.start();
        if (e->button() != Qt::RightButton || !onMenu) return;
        const int i = m_press >= 0 ? m_press : -1;
        m_press = None;
        QPointer<FolderShelf> self = this;
        onMenu(i, e->globalPosition().toPoint());
        if (self) { m_hover = None; update(); }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        m_holdTimer.stop();
        if (m_drag >= 0) {  // Отпустили перетаскиваемую — не открываем, а ставим на место
            if (e->button() == Qt::LeftButton) {
                m_press = None;
                finishDrag(true, e->globalPosition().toPoint());
            }
            return;
        }
        const int h = hitAt(e->position().toPoint());
        const int pressed = m_press;
        m_press = None;
        if (e->button() != Qt::LeftButton || h != pressed || h == None) return;
        if (h == Plus) { if (onAdd) onAdd(); }
        else if (h >= 0 && h < m_folders.size() && onOpen) onOpen(h, itemRect(h).translated(-m_scroll, 0));
    }

    void wheelEvent(QWheelEvent *e) override
    {
        const int d = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
        m_scroll = qBound(0, m_scroll - d / 2, maxScroll());
        m_hover = hitAt(e->position().toPoint());
        update();
        e->accept();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath bg;
        bg.addRoundedRect(r, s(12), s(12));
        p.fillPath(bg, QColor(18, 18, 30, 245));
        p.setPen(QPen(QColor(255, 255, 255, 28), 1));
        p.drawPath(bg);
        p.setClipPath(bg);

        QFont f = font();
        f.setPixelSize(s(10));
        const QFontMetrics fm(f);
        p.save();
        p.translate(-m_scroll, 0);
        for (int i = 0; i < m_folders.size(); ++i) {
            if (i == m_drag) continue;  // Её рисуем последней — поверх, под мышью
            const QRect ir = shownRect(i);
            if (m_drop == i) {  // Сюда бросят файлы из руки
                p.setPen(QPen(QColor(120, 170, 255, 200), 1.5));
                p.setBrush(QColor(80, 130, 255, 60));
                p.drawRoundedRect(QRectF(ir).adjusted(0.75, 0.75, -0.75, -0.75), s(8), s(8));
            } else if (m_hover == i) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 20));
                p.drawRoundedRect(QRectF(ir), s(8), s(8));
            }
            const int isz = s(30);
            p.drawPixmap(QRect(ir.center().x() - isz / 2, ir.top() + s(5), isz, isz), iconOf(m_folders[i].path));
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, m_hover == i ? 235 : 185));
            p.drawText(QRect(ir.left() + s(3), ir.top() + s(38), ir.width() - s(6), s(16)), Qt::AlignCenter,
                       fm.elidedText(label(m_folders[i]), Qt::ElideMiddle, ir.width() - s(6)));
        }
        // Сюда встанет папка с другой полки — вертикальная черта
        if (m_insert >= 0) {
            const int x = s(8) + m_insert * pitch() - s(2);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(120, 170, 255, 220));
            p.drawRoundedRect(QRectF(x - s(1), s(8), s(3), height() - s(16)), s(1), s(1));
        }
        // Поднятая папка — под мышью, чуть больше и с подложкой
        if (m_drag >= 0 && m_drag < m_folders.size()) {
            const QRectF ir = dragRect();
            p.setPen(QPen(QColor(255, 255, 255, 60), 1));
            p.setBrush(QColor(60, 64, 90, 235));
            p.drawRoundedRect(ir.adjusted(0.5, 0.5, -0.5, -0.5), s(9), s(9));
            const int isz = s(32);
            p.drawPixmap(QRect(int(ir.center().x()) - isz / 2, int(ir.top()) + s(4), isz, isz), iconOf(m_folders[m_drag].path));
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 240));
            p.drawText(QRectF(ir.left() + s(3), ir.top() + s(38), ir.width() - s(6), s(16)), Qt::AlignCenter,
                       fm.elidedText(label(m_folders[m_drag]), Qt::ElideMiddle, int(ir.width()) - s(6)));
        }
        // «+» — добавить папку
        const QRect pr = plusRect();
        p.setPen(QPen(QColor(255, 255, 255, m_hover == Plus ? 90 : 45), 1, Qt::DashLine));
        p.setBrush(QColor(255, 255, 255, m_hover == Plus ? 24 : 8));
        p.drawRoundedRect(QRectF(pr).adjusted(0.5, 0.5, -0.5, -0.5), s(8), s(8));
        QFont ic(SmartButton::iconFontFamily());
        ic.setPixelSize(s(12));
        p.setFont(ic);
        p.setPen(QColor(255, 255, 255, m_hover == Plus ? 230 : 170));
        p.drawText(pr, Qt::AlignCenter, QString(QChar(0xE710)));  // Плюс
        p.restore();

        // Пустая полка — подсказка рядом с «+»
        if (m_folders.isEmpty()) {
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 110));
            p.drawText(QRect(pr.right() + s(10), 0, width() - pr.right() - s(20), height()),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QApplication::translate("FolderShelf", "Добавь папку — она будет здесь под рукой"));
        }

        // Не всё влезло — мягкие края подсказывают, что полка листается
        if (maxScroll() > 0) {
            const QColor bgc(18, 18, 30, 245), clear(18, 18, 30, 0);
            const int fw = s(22);
            if (m_scroll > 0) {
                QLinearGradient g(0, 0, fw, 0);
                g.setColorAt(0, bgc); g.setColorAt(1, clear);
                p.fillRect(QRect(0, 0, fw, height()), g);
            }
            if (m_scroll < maxScroll()) {
                QLinearGradient g(width() - fw, 0, width(), 0);
                g.setColorAt(0, clear); g.setColorAt(1, bgc);
                p.fillRect(QRect(width() - fw, 0, fw, height()), g);
            }
        }
    }

private:
    static constexpr int None = -1;
    static constexpr int Plus = -2;

    int s(int v) const { return v * m_scale / 100; }

    // Раскладка — без прокрутки (её добавляет paint/hitAt)
    int itemW() const { return s(76); }
    int pitch() const { return itemW() + s(4); }
    QRect itemRect(int i) const
    {
        return QRect(s(8) + i * (itemW() + s(4)), s(4), itemW(), height() - s(8));
    }
    QRect plusRect() const
    {
        const int sz = s(34);
        const int x = s(8) + int(m_folders.size()) * (itemW() + s(4)) + (m_folders.isEmpty() ? 0 : s(6));
        return QRect(x, (height() - sz) / 2, sz, sz);
    }
    int maxScroll() const { return qMax(0, plusRect().right() + s(10) - width()); }

    // ── Перетаскивание ───────────────────────────────────────────────────────
    // Где папка i стоит сейчас (пока тащим — соседи разъезжаются плавно)
    QRect shownRect(int i) const
    {
        const QRect r = itemRect(i);
        if (i < m_x.size() && (m_drag >= 0 || m_slide.isActive())) return QRect(qRound(m_x[i]), r.top(), r.width(), r.height());
        return r;
    }
    // Место папки i, если поднятую поставить на m_target
    int slotOf(int i) const
    {
        if (m_drag < 0 || i == m_drag) return i == m_drag ? m_target : i;
        int k = i;
        if (i > m_drag) --k;          // Поднятую вынули…
        if (k >= m_target) ++k;       // …и вставили на m_target
        return k;
    }
    QRectF dragRect() const
    {
        const qreal w = itemW() * 1.08, h = (height() - s(8)) * 1.04;
        const qreal cx = m_dragX - m_grab + itemW() / 2.0;
        return QRectF(cx - w / 2, (height() - h) / 2, w, h);
    }
    void startDrag(const QPoint &pos)
    {
        m_holdTimer.stop();
        if (m_press < 0 || m_press >= m_folders.size()) return;
        m_drag = m_target = m_press;
        const QRect r = itemRect(m_drag);
        m_grab = pos.x() + m_scroll - r.left();  // Где на папке схватили — так и держим
        m_dragX = pos.x() + m_scroll;
        m_x.clear();
        for (int i = 0; i < m_folders.size(); ++i) m_x << itemRect(i).left();
        m_hover = None;
        setCursor(Qt::ClosedHandCursor);
        update();
    }
    void dragTo(const QPoint &pos)
    {
        m_dragX = pos.x() + m_scroll;
        // У края полки — листаем (папок больше, чем влезает)
        const int edge = s(28);
        if (pos.x() < edge) m_scroll = qMax(0, m_scroll - s(8));
        else if (pos.x() > width() - edge) m_scroll = qMin(maxScroll(), m_scroll + s(8));
        m_dragX = pos.x() + m_scroll;
        const int left = int(std::round(m_dragX - m_grab));
        const int t = qBound(0, int(std::floor((left - s(8) + pitch() / 2.0) / pitch())), int(m_folders.size()) - 1);
        const bool inside = rect().adjusted(0, -s(30), 0, s(30)).contains(pos);
        if (t != m_target && inside) { m_target = t; startSlide(); }
        if (onDragMove) onDragMove(mapToGlobal(pos));
        update();
    }
    void finishDrag(bool drop, const QPoint &global)
    {
        const int from = m_drag;
        setCursor(Qt::ArrowCursor);
        // Отпустили не над полкой — может, над другой
        if (drop && !rect().adjusted(0, -s(30), 0, s(30)).contains(mapFromGlobal(global)) && onDropOut) {
            m_drag = None;
            m_slide.stop();
            m_x.clear();
            QPointer<FolderShelf> self = this;
            if (onDropOut(from, global)) { if (self) update(); return; }  // Забрала другая полка
            if (!self) return;
            m_target = from;  // Никуда — обратно на своё место
        }
        // Плавно садится на место: начинаем с того места, где была под мышью
        QList<qreal> x;
        for (int i = 0; i < m_folders.size(); ++i) x << (i == from ? dragRect().left() + (dragRect().width() - itemW()) / 2 : (i < m_x.size() ? m_x[i] : itemRect(i).left()));
        const int to = m_target;
        m_drag = None;
        if (to != from) {
            m_folders.move(from, to);
            x.move(from, to);
        }
        m_x = x;
        startSlide();
        update();
        if (to != from && onReorder) onReorder();
    }
    void startSlide()
    {
        m_slideFrom = m_x;
        m_slideClock.restart();
        m_slideMs = qMax(1, Anim::ms(Anim::Kind::Menu, 180));
        if (!Anim::enabled(Anim::Kind::Menu)) { m_slideMs = 1; }
        m_slide.setInterval(Anim::frameMs(this));
        m_slide.start();
        slideStep();
    }
    void slideStep()
    {
        const qreal k = qMin(1.0, m_slideClock.elapsed() / qreal(m_slideMs));
        const qreal e = QEasingCurve(QEasingCurve::OutCubic).valueForProgress(k);
        for (int i = 0; i < m_x.size() && i < m_slideFrom.size(); ++i) {
            const qreal to = s(8) + slotOf(i) * pitch();
            m_x[i] = m_slideFrom[i] + (to - m_slideFrom[i]) * e;
        }
        update();
        if (k >= 1.0) {
            m_slide.stop();
            if (m_drag < 0) m_x.clear();
        }
    }

    int hitAt(const QPoint &pos) const
    {
        const QPoint p = pos + QPoint(m_scroll, 0);
        if (plusRect().contains(p)) return Plus;
        for (int i = 0; i < m_folders.size(); ++i)
            if (itemRect(i).contains(p)) return i;
        return None;
    }

    // Значок как в Проводнике (у «Загрузок», «Рабочего стола» — свои)
    QPixmap iconOf(const QString &path)
    {
        auto it = m_icons.constFind(path);
        if (it != m_icons.constEnd()) return *it;
        static QFileIconProvider provider;
        const int px = qRound(s(30) * devicePixelRatioF());
        QPixmap pm = provider.icon(QFileInfo(path)).pixmap(QSize(px, px));
        pm.setDevicePixelRatio(devicePixelRatioF());
        m_icons.insert(path, pm);
        return pm;
    }

    int m_scale = 100;
    QList<Folder> m_folders;
    QHash<QString, QPixmap> m_icons;
    int m_scroll = 0;
    int m_hover = None;
    int m_press = None;
    int m_drop = None;  // Подсвечена для «руки»
    // Перетаскивание (меняем порядок)
    QTimer m_holdTimer, m_slide;
    QElapsedTimer m_slideClock;
    int m_slideMs = 1;
    QPoint m_pressPos;
    int m_drag = None;          // Какую папку тащим
    int m_target = None;        // Куда встанет
    qreal m_dragX = 0;          // Где мышь (по полке, с прокруткой)
    int m_grab = 0;             // Где на папке схватили
    QList<qreal> m_x, m_slideFrom;  // Где сейчас нарисованы папки (пока разъезжаются)
    int m_insert = -1;          // Сюда встанет папка с другой полки
};
