#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  FolderPanel.h — папка с полки, открытая прямо в SmartClip                   ║
// ║                                                                              ║
// ║  Клик по папке на полке открывает её здесь. Размер — по тому, сколько       ║
// ║  в ней всего:                                                                ║
// ║    • мало  — файлы встают прямо в полку, на место соседних папок;           ║
// ║    • средне — вытягивается вверх над своей половиной;                       ║
// ║    • много — во всю середину, поверх второй полки и полоски файлов.         ║
// ║  Внутри — подпапки (заходить кликом), путь сверху, «назад», поиск (лупа)     ║
// ║  по этой папке и её подпапкам. Клик по файлу — вставить его.               ║
// ║  Список с настройкой «деревом» — подпапки раскрываются стрелкой на месте.   ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLineEdit>
#include <QTimer>
#include <QElapsedTimer>
#include <QEasingCurve>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QCollator>
#include <QCursor>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QApplication>
#include <QFileSystemWatcher>
#include <functional>
#include <algorithm>
#include "../core/Thumbs.h"
#include "Anim.h"
#include "SmartButton.h"   // SmartButton::iconFontFamily() — системный шрифт значков
#include "FilesStrip.h"    // Files::Entry — для меню файла
#include "Carry.h"         // Рука: файлы, которые несёшь

class FolderPanel : public QWidget
{
public:
    enum Mode { Inline, Half, Full };

    // thumbnailer — превью/значок как в Проводнике (вызывается в фоновом потоке)
    FolderPanel(QWidget *parent, int scalePct, std::function<QImage(const QString &, QSize)> thumbnailer)
        : QWidget(parent), m_scale(scalePct), m_thumbnailer(std::move(thumbnailer))
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);
        m_anim.setTimerType(Qt::PreciseTimer);
        connect(&m_anim, &QTimer::timeout, this, [this]() { animStep(); });
        // Долго грузится (сетевая папка, тысячи файлов) — показываем «Загрузка…»
        m_slowLoad.setSingleShot(true);
        m_slowLoad.setInterval(150);
        connect(&m_slowLoad, &QTimer::timeout, this, [this]() { if (m_loading && m_open) relayout(true); });
        m_searchDelay.setSingleShot(true);
        m_searchDelay.setInterval(200);
        connect(&m_searchDelay, &QTimer::timeout, this, [this]() { runSearch(); });
        // В папке что-то появилось/пропало (скачалось, удалили, скопировали
        // рукой) — перечитываем, без мигания и прокрутки наверх
        m_refresh.setSingleShot(true);
        m_refresh.setInterval(350);
        connect(&m_refresh, &QTimer::timeout, this, [this]() { refresh(); });
        connect(&m_watch, &QFileSystemWatcher::directoryChanged, this, [this]() { if (m_open) m_refresh.start(); });
        // Зажал файл и держишь — он «берётся в руку» (как в полоске файлов)
        m_holdTimer.setSingleShot(true);
        m_holdTimer.setInterval(220);
        connect(&m_holdTimer, &QTimer::timeout, this, [this]() {
            if (m_press >= 0 && (QApplication::mouseButtons() & Qt::LeftButton)) pickUp(m_press);
        });

        m_searchEdit = new QLineEdit(this);
        m_searchEdit->setFrame(false);
        m_searchEdit->setStyleSheet(QStringLiteral(
            "background: transparent; border: none; color: #f0f0f0; font-size: %1px; "
            "selection-background-color: rgba(100,150,255,150);").arg(s(11)));
        m_searchEdit->setPlaceholderText(QApplication::translate("FolderPanel", "Поиск в папке"));
        m_searchEdit->hide();
        connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &t) {
            m_query = t.trimmed();
            m_hover = -1;  // Список сменился — подсветка была на другой строке
            m_scroll = 0;  // …и прокрутка — от другого списка
            placeSearch();
            update();
            m_searchDelay.start();
        });
        hide();
    }

    std::function<void(const QString &path)> onPickFile;                    // Клик по файлу
    std::function<void(const QString &path)> onView;                        // Ctrl+клик по фото/видео — просмотр
    std::function<void(const Files::Entry &, const QPoint &)> onFileMenu;   // ПКМ по файлу
    std::function<void(const QString &path, const QPoint &)> onFolderMenu;  // ПКМ по подпапке
    std::function<void()> onSearchFocus;                                    // Окну нужен фокус клавиатуры
    // Кнопка сортировки: показать меню (текущий порядок), apply — выбрали новый
    std::function<void(const QPoint &globalPos, const QString &key, bool desc,
                       const std::function<void(const QString &, bool)> &apply)> onSortMenu;
    std::function<void()> onLayoutChanged;  // Открылась/закрылась/сменила размер (MainWindow прячет лишнее)
    Carry *carry = nullptr;                  // Рука (общая с полоской и фото, см. Carry.h)

    // Зайти в подпапку (пункт меню «Открыть здесь»)
    void enter(const QString &path) { if (m_open) navigate(path); }

    // Сменили сортировку (кнопка в шапке) — переставляем то, что уже загружено
    void resort()
    {
        sortItems(m_items);
        sortItems(m_results);
        for (auto it = m_children.begin(); it != m_children.end(); ++it) sortItems(it.value());
        rebuildFlat();
        if (m_open) { requestThumbs(); update(); }
    }
    // Сменили настройки (дерево вкл/выкл)
    void settingsChanged()
    {
        const bool tree = AppSettings::get().filesTree();
        if (tree == m_tree) return;
        m_tree = tree;
        if (!tree) collapseAll();
        if (m_open) relayout(true);
    }

    // Файл под курсором — для руки (правой — добавить к тем, что несёшь)
    bool carryItemAt(const QPoint &global, CarryItem &it, QRectF &from) const
    {
        if (!isVisible() || !m_open) return false;
        const int i = itemAt(mapFromGlobal(global));
        if (i < 0) return false;
        it = carryItem(i);
        from = thumbRectGlobal(i);
        return true;
    }
    // Куда бросить файлы из руки: подпапка под курсором или сама открытая папка
    QString dropTargetAt(const QPoint &global) const
    {
        if (!isVisible() || !m_open || m_anim.isActive()) return QString();
        const QPoint p = mapFromGlobal(global);
        if (!rect().contains(p)) return QString();
        const int i = itemAt(p);
        if (i >= 0 && shown()[i].dir) return shown()[i].path;
        return m_query.isEmpty() ? m_path : QString();  // Среди найденного — непонятно куда
    }
    void setDropHint(const QPoint &global)
    {
        int h = DropNone;
        if (isVisible() && m_open && rect().contains(mapFromGlobal(global))) {
            const int i = itemAt(mapFromGlobal(global));
            h = (i >= 0 && shown()[i].dir) ? i : (m_query.isEmpty() ? DropHere : DropNone);
        }
        if (h != m_drop) { m_drop = h; update(); }
    }
    void clearDropHint() { if (m_drop != DropNone) { m_drop = DropNone; update(); } }

    // Где можно раскрываться (координаты родителя): полка своей стороны
    // и вся середина над полками
    void setArea(const QRect &shelf, const QRect &full)
    {
        if (shelf != m_shelfRect || full != m_fullRect) m_holdH = false;  // Место другое — размер заново
        m_shelfRect = shelf;
        m_fullRect = full;
        if (m_open) relayout(false);
    }

    // Открыть папку root с полки side; from — где на экране стояла её плитка
    void open(int side, const QString &root, const QString &title, const QRect &from)
    {
        m_side = side;
        m_root = QDir::cleanPath(root);
        m_title = title;
        m_open = true;
        m_closing = false;
        m_openFrom = from;
        // Уже открыта (другая папка) — плавно переезжаем на новое место, а не
        // исчезаем рывком и растём заново из плитки
        m_first = !isVisible();
        m_glide = !m_first;  // Первая раскладка — плавно с прежнего места
        m_entered = false;
        m_tree = AppSettings::get().filesTree();
        closeSearch();
        navigate(m_root);
    }
    // Закрыть с анимацией (обратно в полку)
    void close()
    {
        if (!m_open) return;
        if (!m_watch.directories().isEmpty()) m_watch.removePaths(m_watch.directories());
        m_open = false;
        m_closing = true;
        closeSearch();
        animateTo(QRect(m_shelfRect.left(), m_shelfRect.top(), m_shelfRect.width(), m_shelfRect.height()));
        if (onLayoutChanged) onLayoutChanged();
    }
    // Закрыть сразу (окно спряталось, сменили раздел)
    void closeNow()
    {
        if (!m_watch.directories().isEmpty()) m_watch.removePaths(m_watch.directories());
        m_open = m_closing = false;
        m_anim.stop();
        closeSearch();
        hide();
        if (onLayoutChanged) onLayoutChanged();
    }
    // Esc: сначала поиск, потом сама папка. true — было что закрыть
    bool escape()
    {
        if (!m_open) return false;
        if (closeSearch()) return true;
        close();
        return true;
    }

    bool isOpen() const { return m_open; }
    int side() const { return m_side; }
    Mode mode() const { return m_mode; }
    QString root() const { return m_root; }
    QString path() const { return m_path; }

protected:
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPoint p = e->position().toPoint();
        // Потянул файл — он в руке (перетаскивание в другую программу)
        if ((e->buttons() & Qt::LeftButton) && m_press >= 0 && !carrying() && !m_pressView
            && (p - m_pressPos).manhattanLength() > QApplication::startDragDistance()) {
            pickUp(m_press);
            return;
        }
        if (carrying()) return;  // Подсветку куда бросить даёт рука (setDropHint)
        const int h = itemAt(p), b = buttonAt(p);
        if (h != m_hover || b != m_hoverBtn || treeOn()) { m_hover = h; m_hoverBtn = b; update(); }  // Дерево: подсветка стрелки
        setCursor(h >= 0 || b != BtnNone ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        m_hoverBtn = BtnNone;
        update();
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (!m_open || m_anim.isActive() || carrying()) return;  // Пока несём — кнопками распоряжается рука
        const QPoint p = e->position().toPoint();
        m_press = -1;
        const int b = buttonAt(p);
        if (b != BtnNone) {
            if (e->button() != Qt::LeftButton) return;
            if (b == BtnClose) close();
            else if (b == BtnBack) goUp();
            else if (b == BtnTiles || b == BtnList) setView(b == BtnTiles ? "tiles" : "list");
            else if (b == BtnSearch) openSearch();
            else if (b == BtnSort && onSortMenu) {
                QPointer<FolderPanel> self = this;
                onSortMenu(mapToGlobal(sortRect().bottomLeft() + QPoint(0, s(4))), m_sortKey, m_sortDesc,
                           [self](const QString &k, bool d) {
                    if (!self || !self->m_open) return;
                    AppSettings::get().setFolderSort(self->m_path, k, d);  // Только этой папке (и её подпапкам)
                    self->m_sortKey = k;
                    self->m_sortDesc = d;
                    self->resort();
                });
            }
            else if (b >= BtnCrumb) navigate(crumbPaths().value(b - BtnCrumb));
            return;
        }
        const int i = itemAt(p);
        if (i < 0) return;
        const Item it = shown()[i];
        // Дерево: стрелка у подпапки — раскрыть/свернуть на месте
        if (e->button() == Qt::LeftButton && it.dir && treeOn() && chevronRect(i).contains(p)) {
            toggle(i);
            return;
        }
        if (e->button() == Qt::LeftButton) {
            m_press = i;
            m_pressPos = p;
            m_pressView = (e->modifiers() & Qt::ControlModifier) && !it.dir && Files::viewable(it.path);
            if (!m_pressView) m_holdTimer.start();  // Ctrl+клик — просмотр, а не «в руку»
            return;
        }
        if (e->button() != Qt::RightButton) return;
        QPointer<FolderPanel> self = this;
        if (it.dir) {
            if (onFolderMenu) onFolderMenu(it.path, e->globalPosition().toPoint());
        } else if (onFileMenu) {
            Files::Entry en;
            en.path = it.path;
            en.name = it.name;
            en.size = it.size;
            en.mtime = it.mtime;
            onFileMenu(en, e->globalPosition().toPoint());
        }
        if (self) { m_hover = -1; update(); }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        m_holdTimer.stop();
        const int pressed = m_press;
        m_press = -1;
        if (carrying()) return;  // Отпускание решает рука
        if (e->button() != Qt::LeftButton || pressed < 0 || itemAt(e->position().toPoint()) != pressed) return;
        const Item it = shown()[pressed];
        if (it.dir) navigate(it.path);
        else if (m_pressView) { if (onView) onView(it.path); }
        else if (onPickFile) onPickFile(it.path);
    }

    void wheelEvent(QWheelEvent *e) override
    {
        const int d = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
        m_scroll = qBound(0, m_scroll - d / 2, maxScroll());
        m_hover = itemAt(e->position().toPoint());
        requestThumbs();
        update();
        e->accept();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        // Фон — по анимированному прямоугольнику (раскрывается из плитки папки)
        const QRectF r = QRectF(revealRect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath bg;
        bg.addRoundedRect(r, s(12), s(12));
        p.fillPath(bg, QColor(18, 18, 30));  // Непрозрачный: полки под ним не просвечивают
        p.setPen(QPen(QColor(255, 255, 255, 34), 1));
        p.drawPath(bg);
        p.setClipPath(bg);
        p.setOpacity(contentOpacity());

        QFont base = font();
        QFont icon(SmartButton::iconFontFamily());
        // Пока едет, виджет больше итогового места — рисуем содержимое сразу там,
        // где оно встанет (иначе шапка уезжает вверх, а снизу — пустота)
        p.save();
        if (m_anim.isActive() && !m_closing) p.translate(mapFromParent(m_layoutGeo.topLeft()));
        if (m_mode == Inline) paintInline(p, base, icon);
        else paintBig(p, base, icon);
        p.restore();
        // Несут файлы — сюда, в саму папку, их и положат
        if (m_drop == DropHere) {
            p.setOpacity(1.0);
            p.setPen(QPen(QColor(120, 170, 255, 210), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), s(12), s(12));
        }
    }

private:
    struct Item {
        QString path, name;
        bool dir = false;
        qint64 size = 0;
        QDateTime mtime;
        int depth = 0;  // Дерево: насколько глубоко (0 — прямо в открытой папке)
    };
    enum Btn { BtnNone = -1, BtnClose = -2, BtnBack = -3, BtnSearch = -4, BtnTiles = -5, BtnList = -6,
               BtnSort = -7, BtnCrumb = 0 };
    static constexpr int DropNone = -1, DropHere = -2;  // m_drop: никуда / в саму папку, иначе — номер подпапки

    int s(int v) const { return v * m_scale / 100; }

    // ── Загрузка ─────────────────────────────────────────────────────────────
    static QVariant packItems(const QList<Item> &items)
    {
        QVariantList l;
        for (const Item &it : items) l << QVariant(QVariantList{it.path, it.name, it.dir, it.size, it.mtime});
        return QVariant(l);
    }
    static QList<Item> unpackItems(const QVariant &v)
    {
        QList<Item> out;
        for (const QVariant &x : v.toList()) {
            const QVariantList r = x.toList();
            Item it;
            it.path = r[0].toString();
            it.name = r[1].toString();
            it.dir = r[2].toBool();
            it.size = r[3].toLongLong();
            it.mtime = r[4].toDateTime();
            out << it;
        }
        return out;
    }
    static bool skip(const QFileInfo &fi)
    {
        return fi.fileName().startsWith(QLatin1String("~$"));  // Временные файлы Office
    }

    // Сначала подпапки, потом файлы; внутри — как выбрано кнопкой сортировки
    static void sortItems(QList<Item> &l, const QString &key, bool desc)
    {
        QList<Item> dirs, files;
        for (const Item &it : l) (it.dir ? dirs : files) << it;
        Files::sortList(dirs, key, desc);
        Files::sortList(files, key, desc);
        l = dirs + files;
    }
    void sortItems(QList<Item> &l) const { sortItems(l, m_sortKey, m_sortDesc); }
    // Содержимое папки (в фоновом потоке — порядок передаём сюда заранее)
    static QList<Item> listFolder(const QString &path, const QString &key, bool desc)
    {
        QList<Item> out;
        const QFileInfoList list = QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::NoSort);
        for (const QFileInfo &fi : list) {
            if (skip(fi)) continue;
            out << Item{fi.absoluteFilePath(), fi.fileName(), fi.isDir(), fi.isDir() ? 0 : fi.size(), fi.lastModified()};
        }
        sortItems(out, key, desc);
        return out;
    }
    QString sortKey() const { return m_sortKey; }
    bool sortDesc() const { return m_sortDesc; }

    // ── Дерево ───────────────────────────────────────────────────────────────
    bool treeOn() const { return m_tree && listView() && m_query.isEmpty() && m_mode != Inline; }
    // Что видно в дереве: папка, а под раскрытыми подпапками — их содержимое со сдвигом
    void rebuildFlat()
    {
        m_flat.clear();
        m_hover = -1;  // Строки сдвинулись — подсветка была на другой
        std::function<void(const QList<Item> &, int)> add = [&](const QList<Item> &list, int depth) {
            for (Item it : list) {
                it.depth = depth;
                m_flat << it;
                if (it.dir && m_expanded.contains(it.path) && m_children.contains(it.path))
                    add(m_children.value(it.path), depth + 1);
            }
        };
        add(m_items, 0);
    }
    void collapseAll()
    {
        for (const QString &d : std::as_const(m_expanded)) m_watch.removePath(d);
        m_expanded.clear();
        m_children.clear();
        rebuildFlat();
    }
    void toggle(int i)
    {
        const Item it = shown()[i];
        m_hover = -1;
        m_entered = true;  // Раскрыли — в полку больше не сворачиваемся
        // Размер панели не трогаем: она прижата к полке снизу и выросла бы
        // вверх — строка, по которой кликнул, уехала бы из-под мыши
        m_holdH = true;
        if (m_expanded.remove(it.path)) {
            m_watch.removePath(it.path);
            rebuildFlat();
            relayout(true);
            return;
        }
        m_expanded.insert(it.path);
        m_watch.addPath(it.path);  // Внутри что-то поменялось — тоже перечитаем
        if (m_children.contains(it.path)) { rebuildFlat(); relayout(true); revealChildren(it.path); return; }
        const QString dir = it.path, key = sortKey();
        const bool desc = sortDesc();
        const int gen = m_gen;
        QPointer<FolderPanel> self = this;
        update();
        Thumbs::runAsync(this, [dir, key, desc]() { return packItems(listFolder(dir, key, desc)); },
                         [self, gen, dir](const QVariant &v) {
            if (!self || gen != self->m_gen || !self->m_open || !self->m_expanded.contains(dir)) return;
            self->m_children.insert(dir, unpackItems(v));
            self->rebuildFlat();
            self->relayout(true);
            self->revealChildren(dir);
        });
    }
    // Раскрыли подпапку у нижнего края — докручиваем, чтобы было видно, что
    // внутри (но сама подпапка остаётся на экране)
    void revealChildren(const QString &dir)
    {
        const QList<Item> &list = shown();
        int row = -1;
        for (int i = 0; i < list.size(); ++i) if (list[i].path == dir) { row = i; break; }
        if (row < 0) return;
        int last = row;
        while (last + 1 < list.size() && list[last + 1].depth > list[row].depth) ++last;
        const int viewH = bodyRect().height() - s(8);
        const int rowTop = row * tileH(), lastBottom = (last + 1) * tileH();
        if (lastBottom - m_scroll > viewH) m_scroll = qMin(rowTop, lastBottom - viewH);
        m_scroll = qBound(0, m_scroll, maxScroll());
        requestThumbs();
        update();
    }
    // Стрелка подпапки в дереве
    QRect chevronRect(int i) const
    {
        const QRect ir = itemRect(i);
        return QRect(ir.left() + s(2) + shown()[i].depth * s(16), ir.top(), s(18), ir.height());
    }
    // Насколько сдвинуты значок и имя строки (место под стрелку и глубину)
    int treeShift(int i) const { return treeOn() ? s(18) + shown()[i].depth * s(16) : 0; }

    void navigate(const QString &path)
    {
        if (path.isEmpty()) return;
        if (!m_watch.directories().isEmpty()) m_watch.removePaths(m_watch.directories());
        m_watch.addPath(path);
        // Зашли внутрь (подпапка, «назад», путь) — дальше не меньше двух рядов
        if (!m_first) m_entered = true;
        m_path = QDir::cleanPath(path);
        // Вид и порядок — свои у каждой папки (или как у родительской)
        m_view = AppSettings::get().folderView(m_path);
        AppSettings::get().folderSort(m_path, m_sortKey, m_sortDesc);
        m_scroll = 0;
        m_hover = -1;
        m_loading = true;
        m_items.clear();
        m_holdH = false;     // Новая папка — размер по ней
        m_expanded.clear();  // Дерево — заново, от новой папки
        m_children.clear();
        m_flat.clear();
        const int gen = ++m_gen;
        QPointer<FolderPanel> self = this;
        const QString dir = m_path, key = sortKey();
        const bool desc = sortDesc();
        m_slowLoad.start();
        Thumbs::runAsync(this, [dir, key, desc]() { return packItems(listFolder(dir, key, desc)); },
                         [self, gen](const QVariant &v) {
            if (!self || gen != self->m_gen || !self->m_open) return;
            self->m_loading = false;
            self->m_slowLoad.stop();
            self->m_items = unpackItems(v);
            self->rebuildFlat();
            self->relayout(true);
            if (!self->m_query.isEmpty()) self->runSearch();
        });
        if (isVisible()) update();
    }
    // Перечитать открытую папку на месте (что-то изменилось на диске) —
    // и раскрытые в дереве подпапки
    void refresh()
    {
        if (!m_open || m_loading) return;
        // Само на диске поменялось — размер панели не трогаем: шапка не уедет
        // из-под руки, пока ты что-то в ней делаешь
        if (m_mode != Inline) m_holdH = true;
        const int gen = ++m_gen;
        QPointer<FolderPanel> self = this;
        const QString dir = m_path, key = sortKey();
        const bool desc = sortDesc();
        const QStringList open = QStringList(m_expanded.begin(), m_expanded.end());
        Thumbs::runAsync(this, [dir, open, key, desc]() {
            QVariantMap m;
            m.insert(QString(), packItems(listFolder(dir, key, desc)));
            for (const QString &d : open)
                if (QFileInfo(d).isDir()) m.insert(d, packItems(listFolder(d, key, desc)));
            return QVariant(m);
        }, [self, gen](const QVariant &v) {
            if (!self || gen != self->m_gen || !self->m_open) return;
            const QVariantMap m = v.toMap();
            self->m_items = unpackItems(m.value(QString()));
            self->m_children.clear();
            for (auto it = m.begin(); it != m.end(); ++it)
                if (!it.key().isEmpty()) self->m_children.insert(it.key(), unpackItems(it.value()));
            for (const QString &d : QStringList(self->m_expanded.begin(), self->m_expanded.end()))
                if (!self->m_children.contains(d)) self->m_expanded.remove(d);  // Подпапку удалили
            self->rebuildFlat();
            self->m_hover = -1;
            self->relayout(true);
            if (!self->m_query.isEmpty()) self->runSearch();
        });
    }

    void goUp()
    {
        if (m_path.compare(m_root, Qt::CaseInsensitive) == 0) { close(); return; }
        navigate(QFileInfo(m_path).absolutePath());
    }

    // Поиск по папке и подпапкам — в фоне, не больше 400 совпадений и ~1.5 с
    void runSearch()
    {
        const int gen = ++m_searchGen;
        if (m_query.isEmpty()) { m_results.clear(); m_scroll = 0; requestThumbs(); update(); return; }
        const QString dir = m_path, q = m_query, key = sortKey();
        const bool desc = sortDesc();
        QPointer<FolderPanel> self = this;
        Thumbs::runAsync(this, [dir, q, key, desc]() {
            QList<Item> found;
            QElapsedTimer t;
            t.start();
            QDirIterator itr(dir, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (itr.hasNext() && found.size() < 400 && t.elapsed() < 1500) {
                itr.next();
                const QFileInfo fi = itr.fileInfo();
                if (skip(fi) || !fi.fileName().contains(q, Qt::CaseInsensitive)) continue;
                found << Item{fi.absoluteFilePath(), fi.fileName(), fi.isDir(), fi.isDir() ? 0 : fi.size(), fi.lastModified()};
            }
            sortItems(found, key, desc);
            return packItems(found);
        }, [self, gen](const QVariant &v) {
            if (!self || gen != self->m_searchGen) return;
            self->m_results = unpackItems(v);
            self->m_scroll = 0;
            self->m_hover = -1;
            self->requestThumbs();
            self->update();
        });
    }
    // Что показываем: найденное (идёт поиск) или содержимое папки
    const QList<Item> &shown() const { return !m_query.isEmpty() ? m_results : (treeOn() ? m_flat : m_items); }

    // ── Рука ─────────────────────────────────────────────────────────────────
    bool carrying() const { return carry && carry->active(); }
    CarryItem carryItem(int i) const
    {
        const Item &it = shown()[i];
        return CarryItem{it.path, m_thumbs.value(thumbKey(it)), it.name};
    }
    QRect thumbRect(int i) const
    {
        const QRect ir = itemRect(i);
        if (m_mode == Inline) return QRect(ir.center().x() - s(15), ir.top() + s(5), s(30), s(30));
        if (listView()) return QRect(ir.left() + s(6) + treeShift(i), ir.center().y() - s(12), s(24), s(24));
        return QRect(ir.center().x() - s(28), ir.top() + s(6), s(56), s(56));
    }
    QRectF thumbRectGlobal(int i) const
    {
        const QRect r = thumbRect(i);
        return CarryUi::fit(m_thumbs.value(thumbKey(shown()[i])), QRectF(r.translated(mapToGlobal(QPoint(0, 0)))));
    }
    void pickUp(int i)
    {
        m_holdTimer.stop();
        m_press = -1;
        if (!carry || i < 0 || i >= shown().size() || carrying()) return;
        carry->pickUp(carryItem(i), thumbRectGlobal(i));
        update();
    }

    // ── Превью (только видимым) ──────────────────────────────────────────────
    static QString thumbKey(const Item &it) { return it.path + '|' + QString::number(it.mtime.toMSecsSinceEpoch()); }
    void requestThumbs()
    {
        const QSize px = QSize(s(56), s(56)) * devicePixelRatioF();
        const QList<Item> &list = shown();
        const QRect body = bodyRect();
        for (int i = 0; i < list.size(); ++i) {
            const QRect ir = itemRect(i);
            if (!ir.intersects(body.adjusted(-s(200), -s(200), s(200), s(200)))) continue;
            const QString key = thumbKey(list[i]);
            if (m_thumbs.contains(key) || m_waiting.contains(key)) continue;
            m_waiting.insert(key);
            const QString path = list[i].path;
            auto thumb = m_thumbnailer;
            QPointer<FolderPanel> self = this;
            Thumbs::runAsync(this, [thumb, path, px]() { return QVariant(thumb ? thumb(path, px) : QImage()); },
                             [self, key](const QVariant &v) {
                if (!self) return;
                self->m_waiting.remove(key);
                QPixmap pm = QPixmap::fromImage(v.value<QImage>());
                pm.setDevicePixelRatio(self->devicePixelRatioF());
                self->m_thumbs.insert(key, pm);
                self->update();
            });
        }
    }
    void drawThumb(QPainter &p, const Item &it, const QRect &r) const
    {
        const QPixmap pm = m_thumbs.value(thumbKey(it));
        if (pm.isNull()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 18));
            p.drawRoundedRect(r, s(6), s(6));
            return;
        }
        const QSizeF sz = QSizeF(pm.size() / pm.devicePixelRatio()).scaled(QSizeF(r.size()), Qt::KeepAspectRatio);
        p.drawPixmap(QRectF(r.left() + (r.width() - sz.width()) / 2, r.top() + (r.height() - sz.height()) / 2,
                            sz.width(), sz.height()), pm, QRectF(pm.rect()));
    }

    // ── Размер и место ───────────────────────────────────────────────────────
    int headH() const { return s(40); }
    // Размер раскладки: итоговое место (во время анимации виджет больше)
    int lw() const { return m_anim.isActive() && !m_closing ? m_layoutGeo.width() : width(); }
    int lh() const { return m_anim.isActive() && !m_closing ? m_layoutGeo.height() : height(); }
    int inlineItemW() const { return s(76); }
    int tileW() const { return s(92); }
    bool listView() const { return m_view == QLatin1String("list"); }
    int tileH() const { return listView() ? s(34) : s(100); }
    int cols(int w) const { return listView() ? 1 : qMax(1, (w - s(16)) / tileW()); }
    // Плитки/список — общая настройка с полоской файлов
    void setView(const QString &v)
    {
        if (m_view == v) return;
        m_view = v;
        AppSettings::get().setFolderView(m_path, v);  // Только этой папке (и её подпапкам)
        m_scroll = 0;
        // Ширина та же — и высоту не трогаем: иначе шапка с этой кнопкой уедет
        // из-под мыши (меняется ширина — список не во всю ширину — тогда уж по новой)
        m_holdH = true;
        relayout(true);
    }
    // Раскрытая папка — не ниже двух рядов (иначе при переходах она то
    // съёживается, то вытягивается, и кнопки уезжают из-под мыши)
    // В дереве — повыше (почти половина): есть куда раскрывать подпапки, не меняя размер
    // Совсем меньше нельзя — две строки
    int min2Rows() const { return headH() + 2 * tileH() + s(14); }
    int minBigH() const
    {
        const int rows2 = min2Rows();
        if (!(m_tree && listView())) return rows2;
        return qMax(rows2, (m_shelfRect.bottom() - m_fullRect.top() + 1) * 45 / 100);
    }

    // Сколько плиток влезает в полку (там ещё «назад» слева и «✕» справа)
    int inlineCapacity() const
    {
        return qMax(0, (m_shelfRect.width() - s(8) - s(40) - s(40)) / (inlineItemW() + s(4)));
    }
    // Выбрать размер по количеству и поставить панель (animate — плавно)
    void relayout(bool animate)
    {
        if (!m_open) return;
        // Сколько строк: в дереве — вместе с раскрытыми подпапками
        const int n = m_loading ? 0 : int((m_tree && listView() && m_query.isEmpty() ? m_flat : m_items).size());
        Mode mode;
        QRect target;
        // Дерево — всегда списком (в полку, плитками в ряд, не встаём)
        if (!m_loading && m_items.size() <= inlineCapacity() && !m_searchOpen && !m_entered && !(m_tree && listView())) {
            mode = Inline;
            target = m_shelfRect;
        } else {
            // Половина полки узкая (повёрнутый экран) — на всю ширину: в узкой
            // и шапка не влезает
            const bool narrow = m_shelfRect.width() < s(420);
            const int w = narrow ? m_fullRect.width() : m_shelfRect.width();
            const int left = narrow ? m_fullRect.left() : m_shelfRect.left();
            const int rows = qMax(1, (qMax(n, 1) + cols(w) - 1) / cols(w));
            const int h = qMax(minBigH(), headH() + rows * tileH() + s(14));
            // Список во всю ширину не растягиваем (строки слишком длинные) —
            // он тянется вверх над своей половиной, до самого верха
            const int maxHalf = listView() ? m_shelfRect.bottom() - m_fullRect.top() + 1
                                           : (m_shelfRect.bottom() - m_fullRect.top()) * 62 / 100;
            if (m_loading || h <= maxHalf || listView() || (m_searchOpen && m_mode == Half)) {
                mode = Half;
                target = QRect(left, m_shelfRect.bottom() - qMin(h, maxHalf) + 1, w, qMin(h, maxHalf));
            } else {
                // Во всю ширину — высота по содержимому, но не выше середины окна
                mode = Full;
                const int fw = m_fullRect.width();
                const int frows = qMax(1, (n + cols(fw) - 1) / cols(fw));
                const int maxH = m_shelfRect.bottom() - m_fullRect.top() + 1;
                const int fh = qBound(qMin(minBigH(), maxH), headH() + frows * tileH() + s(14), maxH);
                target = QRect(m_fullRect.left(), m_shelfRect.bottom() - fh + 1, fw, fh);
            }
        }
        // Раскрыли/свернули подпапку в дереве — размер прежний (см. toggle)
        if (m_holdH && m_mode != Inline && mode != Inline && m_layoutGeo.isValid() && isVisible()) {
            // Режим — новый, место — прежнее (если в нём хватает места — не ниже минимума)
            if (target.width() == m_layoutGeo.width() && m_layoutGeo.height() >= min2Rows() - 1) target = m_layoutGeo;
            else m_holdH = false;
        }
        if (m_glide) { animate = true; m_glide = false; }
        const bool changed = mode != m_mode || !isVisible();
        m_mode = mode;
        m_layoutGeo = target;  // Раскладка — сразу по новому месту
        m_scroll = qBound(0, m_scroll, maxScroll());
        if (m_first) {  // Только открыли — раскрываемся из плитки папки
            m_first = false;
            m_revealFrom = m_openFrom;
            setGeometry(target);
            show();
            raise();
            startReveal(target);
        } else if (m_anim.isActive() && !m_closing) {
            // Ещё едет к прошлому месту (быстро зашли в папку) — едем к новому
            if (m_finalGeo != target) animateTo(target);
        } else if (animate && geometry() != target) {
            animateTo(target);
        } else if (!m_anim.isActive()) {
            setGeometry(target);
        }
        placeSearch();
        requestThumbs();
        update();
        if (changed && onLayoutChanged) onLayoutChanged();
    }

    // Анимация: виджет сразу ставится на итоговое место (раскладка внутри не
    // «плывёт»), а фон раскрывается из прежнего прямоугольника в новый
    void animateTo(const QRect &target)
    {
        const QRect cur = isVisible() ? QRect(mapToParent(revealRect().topLeft()), revealRect().size()) : m_shelfRect;
        m_revealFrom = cur;
        m_reveal = false;  // Меняем размер — содержимое не мигает
        if (m_closing) {
            // Закрываемся — виджет остаётся, где был, фон сворачивается в полку
            m_revealTo = target;
            m_finalGeo = geometry();
        } else {
            m_revealTo = target;
            m_finalGeo = target;
            setGeometry(target.united(cur));
        }
        startAnim();
    }
    void startReveal(const QRect &target)
    {
        m_reveal = true;  // Содержимое проявляется (только при открытии)
        m_revealTo = target;
        m_finalGeo = target;
        startAnim();
    }
    void startAnim()
    {
        m_k = 0;
        m_animMs = qMax(1, Anim::ms(Anim::Kind::Menu, m_closing ? 200 : 260));
        if (!Anim::enabled(Anim::Kind::Menu)) { finishAnim(); return; }
        m_clock.restart();
        m_anim.setInterval(Anim::frameMs(this));
        m_anim.start();
        animStep();
    }
    void animStep()
    {
        m_k = qMin(1.0, m_clock.elapsed() / qreal(m_animMs));
        placeSearch();
        update();
        if (m_k >= 1.0) finishAnim();
    }
    void finishAnim()
    {
        m_anim.stop();
        m_k = 1.0;
        if (m_closing) {
            m_closing = false;
            hide();
            return;
        }
        setGeometry(m_finalGeo);
        placeSearch();
        update();
    }
    qreal eased() const
    {
        return QEasingCurve(m_closing ? QEasingCurve::InOutSine : QEasingCurve::OutCubic).valueForProgress(m_k);
    }
    // Видимый фон — в координатах виджета
    QRect revealRect() const
    {
        if (!m_anim.isActive()) return rect();
        const qreal t = eased();
        auto lerp = [t](int a, int b) { return qRound(a + (b - a) * t); };
        const QRect r(QPoint(lerp(m_revealFrom.left(), m_revealTo.left()), lerp(m_revealFrom.top(), m_revealTo.top())),
                      QPoint(lerp(m_revealFrom.right(), m_revealTo.right()), lerp(m_revealFrom.bottom(), m_revealTo.bottom())));
        return QRect(mapFromParent(r.topLeft()), r.size());
    }
    // Открываемся из плитки — содержимое проявляется; закрываемся — тает.
    // Просто меняем размер (зашли в папку, раскрыли подпапку) — не мигаем
    qreal contentOpacity() const
    {
        if (!m_anim.isActive()) return 1.0;
        if (m_closing) return 1.0 - eased();
        return m_reveal ? qBound(0.0, (eased() - 0.35) / 0.65, 1.0) : 1.0;
    }

    // ── Раскладка содержимого ────────────────────────────────────────────────
    QRect bodyRect() const
    {
        if (m_mode == Inline) return QRect(s(44), 0, lw() - s(44) - s(40), lh());
        return QRect(0, headH(), lw(), lh() - headH());
    }
    QRect itemRect(int i) const
    {
        const QRect b = bodyRect();
        if (m_mode == Inline)
            return QRect(b.left() + i * (inlineItemW() + s(4)) - m_scroll, s(4), inlineItemW(), lh() - s(8));
        if (listView()) return QRect(s(8), b.top() + s(2) + i * tileH() - m_scroll, lw() - s(16), tileH() - s(2));
        const int c = cols(lw());
        const int cw = (lw() - s(16)) / c;
        return QRect(s(8) + (i % c) * cw, b.top() + s(2) + (i / c) * tileH() - m_scroll, cw - s(4), tileH() - s(4));
    }
    int maxScroll() const
    {
        const int n = int(shown().size());
        if (m_mode == Inline) return qMax(0, n * (inlineItemW() + s(4)) - bodyRect().width());
        const int rows = (n + cols(lw()) - 1) / cols(lw());
        return qMax(0, rows * tileH() + s(8) - bodyRect().height());
    }
    int itemAt(const QPoint &p) const
    {
        if (!m_open || m_anim.isActive() || !bodyRect().contains(p)) return -1;
        const QList<Item> &list = shown();
        for (int i = 0; i < list.size(); ++i)
            if (itemRect(i).contains(p)) return i;
        return -1;
    }

    // Кнопки шапки
    QRect closeRect() const
    {
        const int sz = s(26);
        if (m_mode == Inline) return QRect(lw() - s(8) - sz, (lh() - sz) / 2, sz, sz);
        return QRect(lw() - s(10) - sz, (headH() - sz) / 2, sz, sz);
    }
    QRect backRect() const
    {
        const int sz = s(26);
        if (m_mode == Inline) return QRect(s(10), (lh() - sz) / 2, sz, sz);
        return QRect(s(10), (headH() - sz) / 2, sz, sz);
    }
    bool deep() const { return m_path.compare(m_root, Qt::CaseInsensitive) != 0; }
    // Поиск — справа, перед «✕»; открыт — растёт влево, когда текст не влезает
    QRect viewRect(int b) const  // 0 — плитки, 1 — список
    {
        const int sz = s(26);
        return QRect(closeRect().left() - s(10) - (2 - b) * (sz + s(4)), (headH() - sz) / 2, sz, sz);
    }
    QRect sortRect() const
    {
        const int sz = s(26);
        return QRect(viewRect(0).left() - s(6) - sz, (headH() - sz) / 2, sz, sz);
    }
    QRect searchRect() const
    {
        const int h = s(26), top = (headH() - h) / 2, right = sortRect().left() - s(6);
        if (!m_searchOpen) return QRect(right - h, top, h, h);
        QFont f = font();
        f.setPixelSize(s(11));
        const int need = QFontMetrics(f).horizontalAdvance(m_searchEdit->text()) + s(24) + s(14);
        const int maxW = qMax(s(140), lw() / 2);
        const int w = qBound(s(140), need, maxW);
        return QRect(right - w, top, w, h);
    }
    // Путь: «Папка › под › под» — каждый кусок кликабельный
    QStringList crumbPaths() const
    {
        QStringList out{m_root};
        if (!deep()) return out;
        const QString rel = QDir(m_root).relativeFilePath(m_path);
        QString cur = m_root;
        for (const QString &part : rel.split('/', Qt::SkipEmptyParts)) {
            cur = QDir(cur).filePath(part);
            out << cur;
        }
        return out;
    }
    QStringList crumbLabels() const
    {
        QStringList out{m_title};
        const QStringList paths = crumbPaths();
        for (int i = 1; i < paths.size(); ++i) out << QFileInfo(paths[i]).fileName();
        return out;
    }
    QList<QRect> crumbRects() const
    {
        QList<QRect> out;
        if (m_mode == Inline) return out;
        QFont f = font();
        f.setPixelSize(s(12));
        const QFontMetrics fm(f);
        int x = backRect().right() + s(8);
        const int limit = searchRect().left() - s(8);
        const QStringList labels = crumbLabels();
        // Не влезает — первые куски прячем (видно, где ты сейчас)
        int first = 0;
        auto total = [&](int from) {
            int w = 0;
            for (int i = from; i < labels.size(); ++i) w += qMin(s(180), fm.horizontalAdvance(labels[i]) + s(12)) + s(14);
            return w;
        };
        while (first < labels.size() - 1 && x + total(first) > limit) ++first;
        for (int i = 0; i < labels.size(); ++i) {
            if (i < first) { out << QRect(); continue; }
            const int w = qMin(s(180), fm.horizontalAdvance(labels[i]) + s(12));
            out << QRect(x, (headH() - s(26)) / 2, qMin(w, qMax(0, limit - x)), s(26));
            x += w + s(14);
        }
        return out;
    }
    int buttonAt(const QPoint &p) const
    {
        if (!m_open || m_anim.isActive()) return BtnNone;
        if (closeRect().contains(p)) return BtnClose;
        if ((m_mode == Inline || deep()) && backRect().contains(p)) return BtnBack;
        if (m_mode == Inline) return BtnNone;
        if (viewRect(0).contains(p)) return BtnTiles;
        if (viewRect(1).contains(p)) return BtnList;
        if (sortRect().contains(p)) return BtnSort;
        if (!m_searchOpen && searchRect().contains(p)) return BtnSearch;
        const QList<QRect> cr = crumbRects();
        for (int i = 0; i < cr.size() - 1; ++i)  // Последний кусок — это где мы сейчас
            if (cr[i].contains(p)) return BtnCrumb + i;
        return BtnNone;
    }

    // ── Поиск ────────────────────────────────────────────────────────────────
    void openSearch()
    {
        m_searchOpen = true;
        placeSearch();
        update();
        QPointer<FolderPanel> self = this;
        if (onSearchFocus) onSearchFocus();
        if (self) m_searchEdit->setFocus(Qt::MouseFocusReason);
    }
    bool closeSearch()
    {
        if (!m_searchOpen) return false;
        m_searchOpen = false;
        m_searchEdit->blockSignals(true);
        m_searchEdit->clear();
        m_searchEdit->blockSignals(false);
        m_query.clear();
        m_results.clear();
        m_hover = -1;
        ++m_searchGen;
        m_scroll = 0;
        placeSearch();
        update();
        return true;
    }
    void placeSearch()
    {
        const bool show = m_searchOpen && m_open && m_mode != Inline && !m_anim.isActive();
        if (show) m_searchEdit->setGeometry(searchRect().adjusted(s(24), 0, -s(6), 0));
        if (show != m_searchEdit->isVisible()) m_searchEdit->setVisible(show);
    }

    // ── Рисование ────────────────────────────────────────────────────────────
    void drawButton(QPainter &p, const QRect &r, QChar glyph, bool hot, const QFont &icon, int px = 11)
    {
        p.setPen(QPen(QColor(255, 255, 255, hot ? 70 : 36), 1));
        p.setBrush(QColor(255, 255, 255, hot ? 26 : 10));
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), s(7), s(7));
        QFont ic = icon;
        ic.setPixelSize(s(px));
        p.setFont(ic);
        p.setPen(QColor(255, 255, 255, hot ? 235 : 175));
        p.drawText(r, Qt::AlignCenter, QString(glyph));
    }

    // Под плиткой: подсветка наведения или «сюда бросят файлы»
    void drawItemBack(QPainter &p, int i, const QRect &ir)
    {
        if (m_drop == i) {
            p.setPen(QPen(QColor(120, 170, 255, 200), 1.5));
            p.setBrush(QColor(80, 130, 255, 60));
            p.drawRoundedRect(QRectF(ir).adjusted(0.75, 0.75, -0.75, -0.75), s(8), s(8));
        } else if (m_hover == i && !carrying()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 20));
            p.drawRoundedRect(QRectF(ir), s(8), s(8));
        }
    }

    void paintInline(QPainter &p, const QFont &base, const QFont &icon)
    {
        // Слева «назад» (в корне — закрыть), справа «✕»
        drawButton(p, backRect(), QChar(0xE72B), m_hoverBtn == BtnBack, icon);
        drawButton(p, closeRect(), QChar(0xE711), m_hoverBtn == BtnClose, icon, 10);
        QFont f = base;
        f.setPixelSize(s(10));
        const QFontMetrics fm(f);
        const QRect body = bodyRect();
        p.save();
        p.setClipRect(body, Qt::IntersectClip);
        const QList<Item> &list = shown();
        if (list.isEmpty() && !m_loading) {
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 110));
            p.drawText(body.adjusted(s(8), 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                       QApplication::translate("FolderPanel", "Папка пустая"));
        }
        for (int i = 0; i < list.size(); ++i) {
            const QRect ir = itemRect(i);
            if (!ir.intersects(body)) continue;
            drawItemBack(p, i, ir);
            const qreal op = p.opacity();
            if (carry && carry->has(list[i].path)) p.setOpacity(op * 0.35);  // Уже в руке
            const int isz = s(30);
            drawThumb(p, list[i], QRect(ir.center().x() - isz / 2, ir.top() + s(5), isz, isz));
            p.setOpacity(op);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, m_hover == i ? 235 : 185));
            p.drawText(QRect(ir.left() + s(3), ir.top() + s(38), ir.width() - s(6), s(16)), Qt::AlignCenter,
                       fm.elidedText(list[i].name, Qt::ElideMiddle, ir.width() - s(6)));
        }
        p.restore();
    }

    void paintBig(QPainter &p, const QFont &base, const QFont &icon)
    {
        // Шапка: назад, путь, поиск, закрыть
        if (deep()) drawButton(p, backRect(), QChar(0xE72B), m_hoverBtn == BtnBack, icon);
        else {
            QFont ic = icon;
            ic.setPixelSize(s(15));
            p.setFont(ic);
            p.setPen(QColor(255, 210, 110));
            p.drawText(backRect(), Qt::AlignCenter, QString(QChar(0xE8B7)));  // Папка
        }
        QFont cf = base;
        cf.setPixelSize(s(12));
        const QFontMetrics cfm(cf);
        const QList<QRect> cr = crumbRects();
        const QStringList labels = crumbLabels();
        p.setFont(cf);
        for (int i = 0; i < cr.size(); ++i) {
            if (cr[i].isEmpty()) continue;
            const bool last = i == cr.size() - 1;
            const bool hot = m_hoverBtn == BtnCrumb + i;
            if (hot) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 22));
                p.drawRoundedRect(QRectF(cr[i]), s(6), s(6));
            }
            p.setPen(QColor(255, 255, 255, last ? 235 : (hot ? 220 : 140)));
            p.drawText(cr[i], Qt::AlignCenter, cfm.elidedText(labels[i], Qt::ElideMiddle, cr[i].width() - s(8)));
            if (!last) {
                QFont ic = icon;
                ic.setPixelSize(s(8));
                p.setFont(ic);
                p.setPen(QColor(255, 255, 255, 90));
                p.drawText(QRect(cr[i].right() + 1, cr[i].top(), s(14), cr[i].height()), Qt::AlignCenter,
                           QString(QChar(0xE76C)));  // ›
                p.setFont(cf);
            }
        }
        // Поиск
        const QRect sr = searchRect();
        if (m_searchOpen) {
            p.setPen(QPen(QColor(255, 255, 255, 80), 1));
            p.setBrush(QColor(255, 255, 255, 16));
            p.drawRoundedRect(QRectF(sr).adjusted(0.5, 0.5, -0.5, -0.5), s(7), s(7));
            QFont ic = icon;
            ic.setPixelSize(s(11));
            p.setFont(ic);
            p.setPen(QColor(255, 255, 255, 210));
            p.drawText(QRect(sr.left(), sr.top(), s(24), sr.height()), Qt::AlignCenter, QString(QChar(0xE721)));
        } else {
            drawButton(p, sr, QChar(0xE721), m_hoverBtn == BtnSearch, icon);
        }
        drawButton(p, sortRect(), QChar(0xE8CB), m_hoverBtn == BtnSort, icon);  // Сортировка
        // Плитки / список — выбранный подсвечен, как в полоске файлов
        for (int b = 0; b < 2; ++b) {
            const bool on = (b == 1) == listView();
            const QRect vr = viewRect(b);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, on ? 34 : (m_hoverBtn == (b ? BtnList : BtnTiles) ? 20 : 0)));
            p.drawRoundedRect(QRectF(vr), s(6), s(6));
            QFont ic = icon;
            ic.setPixelSize(s(12));
            p.setFont(ic);
            p.setPen(QColor(255, 255, 255, on ? 230 : 140));
            p.drawText(vr, Qt::AlignCenter, QString(QChar(b ? 0xE8FD : 0xE80A)));  // Список / плитки (как в полоске)
        }
        drawButton(p, closeRect(), QChar(0xE711), m_hoverBtn == BtnClose, icon, 10);
        // Линия под шапкой
        p.setPen(QPen(QColor(255, 255, 255, 18), 1));
        p.drawLine(QPointF(s(10), headH() - 0.5), QPointF(lw() - s(10), headH() - 0.5));

        // Плитки
        const QRect body = bodyRect();
        p.save();
        p.setClipRect(body, Qt::IntersectClip);
        QFont f = base;
        f.setPixelSize(s(11));
        const QFontMetrics fm(f);
        const QList<Item> &list = shown();
        if (list.isEmpty()) {
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 110));
            const QString msg = m_loading ? QApplication::translate("FolderPanel", "Загрузка…")
                              : !m_query.isEmpty() ? QApplication::translate("FolderPanel", "Ничего не найдено")
                                                   : QApplication::translate("FolderPanel", "Папка пустая");
            p.drawText(body, Qt::AlignCenter, msg);
        }
        for (int i = 0; i < list.size(); ++i) {
            const QRect ir = itemRect(i);
            if (ir.top() > body.bottom()) break;
            if (!ir.intersects(body)) continue;
            drawItemBack(p, i, ir);
            const qreal op = p.opacity();
            if (carry && carry->has(list[i].path)) p.setOpacity(op * 0.35);  // Уже в руке
            if (listView()) {
                // Строка: [стрелка дерева], значок, имя, справа — размер и дата
                const int shift = treeShift(i);
                if (treeOn() && list[i].dir) {
                    const bool open = m_expanded.contains(list[i].path);
                    const bool hotArrow = m_hover == i && chevronRect(i).contains(mapFromGlobal(QCursor::pos()));
                    QFont ic = icon;
                    ic.setPixelSize(s(9));
                    p.setFont(ic);
                    p.setPen(QColor(255, 255, 255, hotArrow ? 240 : 140));
                    p.drawText(chevronRect(i), Qt::AlignCenter, QString(QChar(open ? 0xE70D : 0xE76C)));  // ⌄ / ›
                }
                if (treeOn() && list[i].depth > 0) {  // Линии уровней — видно, что внутри чего
                    p.setPen(QPen(QColor(255, 255, 255, 22), 1));
                    for (int d = 0; d < list[i].depth; ++d) {
                        const qreal x = ir.left() + s(2) + d * s(16) + s(9) + 0.5;
                        p.drawLine(QPointF(x, ir.top() - s(1)), QPointF(x, ir.bottom() + s(2)));
                    }
                }
                drawThumb(p, list[i], thumbRect(i));
                p.setOpacity(op);
                const QString meta = list[i].dir ? Files::timeText(list[i].mtime)
                                                 : Files::sizeText(list[i].size) + QStringLiteral("  ·  ") + Files::timeText(list[i].mtime);
                const int metaW = fm.horizontalAdvance(meta);
                p.setFont(f);
                p.setPen(QColor(255, 255, 255, 110));
                p.drawText(QRect(ir.right() - s(10) - metaW, ir.top(), metaW, ir.height()), Qt::AlignVCenter, meta);
                p.setPen(QColor(255, 255, 255, m_hover == i ? 240 : 200));
                const QRect nr(ir.left() + s(38) + shift, ir.top(), ir.width() - s(38) - shift - metaW - s(24), ir.height());
                p.drawText(nr, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(list[i].name, Qt::ElideMiddle, nr.width()));
                continue;
            }
            const int tsz = s(56);
            drawThumb(p, list[i], QRect(ir.center().x() - tsz / 2, ir.top() + s(6), tsz, tsz));
            p.setOpacity(op);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, m_hover == i ? 240 : 190));
            const QRect nr(ir.left() + s(4), ir.top() + s(66), ir.width() - s(8), s(28));
            p.drawText(nr, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                       fm.elidedText(list[i].name, Qt::ElideMiddle, nr.width() * 2 - s(6)));
        }
        p.restore();
        // Не всё влезло — мягкий край снизу
        if (m_scroll < maxScroll()) {
            QLinearGradient g(0, lh() - s(24), 0, lh());
            g.setColorAt(0, QColor(18, 18, 30, 0));
            g.setColorAt(1, QColor(18, 18, 30));
            p.fillRect(QRect(0, lh() - s(24), lw(), s(24)), g);
        }
    }

    int m_scale = 100;
    std::function<QImage(const QString &, QSize)> m_thumbnailer;
    QRect m_shelfRect, m_fullRect;  // Где раскрываться (координаты родителя)
    int m_side = 0;
    QString m_root, m_title, m_path;
    QList<Item> m_items, m_results;
    QString m_sortKey = QStringLiteral("date");  // Порядок этой папки (см. AppSettings::folderSort)
    bool m_sortDesc = true;
    QRect m_layoutGeo;  // Итоговое место (координаты родителя)
    bool m_holdH = false;   // Дерево: раскрыли подпапку — размер не меняем
    bool m_glide = false;   // Открыли другую папку, когда эта была открыта — переезжаем плавно
    bool m_reveal = false;  // Идёт раскрытие из плитки (содержимое проявляется)
    // Дерево (настройка «Список деревом»): раскрытые подпапки и их содержимое
    bool m_tree = false;
    QSet<QString> m_expanded;
    QHash<QString, QList<Item>> m_children;
    QList<Item> m_flat;  // Что видно: папка с раскрытым внутри
    bool m_open = false, m_closing = false, m_loading = false, m_first = false;
    bool m_entered = false;  // Уже ходили по папкам — в полку больше не сворачиваемся
    Mode m_mode = Inline;
    int m_gen = 0, m_searchGen = 0;
    int m_scroll = 0;
    int m_hover = -1, m_hoverBtn = BtnNone, m_press = -1;
    QHash<QString, QPixmap> m_thumbs;
    QSet<QString> m_waiting;
    QString m_view = QStringLiteral("tiles");  // Плитки или список
    QTimer m_slowLoad, m_searchDelay, m_refresh, m_holdTimer;
    QFileSystemWatcher m_watch;    // Следим за открытой папкой
    QPoint m_pressPos;
    bool m_pressView = false;      // Нажали с Ctrl по фото/видео — отпустим, будет просмотр
    int m_drop = DropNone;         // Куда бросят файлы из руки (подсветка)
    // Поиск
    QLineEdit *m_searchEdit = nullptr;
    bool m_searchOpen = false;
    QString m_query;
    // Анимация
    QTimer m_anim;
    QElapsedTimer m_clock;
    qreal m_k = 1.0;
    int m_animMs = 1;
    QRect m_openFrom, m_revealFrom, m_revealTo, m_finalGeo;
};
