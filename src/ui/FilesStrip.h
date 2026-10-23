#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  FilesStrip.h — полоска свежих файлов внизу по центру, над поиском           ║
// ║                                                                              ║
// ║  Свёрнута — тонкая плашка с маленькими значками. Навели мышь — плавно        ║
// ║  раскрывается вверх: плитки или список. Клик — вставить файл (как у видео),  ║
// ║  можно перетащить мышкой в Telegram/Discord, правый клик — меню.             ║
// ║  Папки, срок, количество, вид и типы файлов — Настройки → Файлы.             ║
// ║  В Закрепах здесь — закреплённые файлы текущей папки.                        ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QCursor>
#include <QElapsedTimer>
#include <QEasingCurve>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QEnterEvent>
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QLocale>
#include <algorithm>
#include <functional>
#include <windows.h>   // GetAsyncKeyState — зажата ли кнопка мыши на самом деле

#include "../core/AppSettings.h"
#include "../core/Thumbs.h"
#include "Anim.h"
#include "SmartButton.h"   // SmartButton::iconFontFamily() — системный шрифт значков
#include "DragGhost.h"     // «Живой» файл в руке

namespace Files {

// Тип файла по расширению — для фильтра «Какие файлы показывать»
inline QString category(const QString &path)
{
    static const QHash<QString, QString> map = []() {
        QHash<QString, QString> m;
        auto add = [&m](const char *cat, std::initializer_list<const char *> exts) {
            for (const char *e : exts) m.insert(QLatin1String(e), QLatin1String(cat));
        };
        add("image",    {"png", "jpg", "jpeg", "gif", "webp", "bmp", "tif", "tiff", "heic", "svg", "ico"});
        add("video",    {"mp4", "mkv", "mov", "avi", "webm", "wmv", "m4v", "flv"});
        add("audio",    {"mp3", "wav", "ogg", "flac", "m4a", "aac", "opus", "wma"});
        add("docs",     {"pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "rtf",
                         "odt", "ods", "odp", "csv", "md", "epub", "json", "xml"});
        add("archives", {"zip", "rar", "7z", "tar", "gz", "bz2", "xz", "iso"});
        add("programs", {"exe", "msi", "bat", "cmd", "apk", "appx", "msix", "msixbundle"});
        return m;
    }();
    return map.value(QFileInfo(path).suffix().toLower(), QStringLiteral("other"));
}

inline QStringList allCategories()
{
    return {"image", "video", "audio", "docs", "archives", "programs", "other"};
}

// Стандартные папки — «Загрузки»
inline QStringList defaultFolders()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return d.isEmpty() ? QStringList() : QStringList{d};
}

// Папки из настроек; ещё не выбирали — стандартные
inline QStringList configuredFolders()
{
    return AppSettings::get().hasFilesFolders() ? AppSettings::get().filesFolders() : defaultFolders();
}

// Недокачанные и служебные файлы не показываем
inline bool skipFile(const QFileInfo &fi)
{
    static const QStringList junk = {"crdownload", "part", "partial", "tmp", "download",
                                     "opdownload", "lnk", "url", "ini"};
    return fi.fileName().startsWith(QLatin1String("~$")) || junk.contains(fi.suffix().toLower());
}

struct Entry {
    QString   path;
    QString   name;
    qint64    size = 0;
    QDateTime mtime;
    QString   folder;          // Из какой папки (вкладки в Истории)
    QString   group;           // Вкладка закреплённого файла внутри папки закрепов
    int       pinId = 0;       // Закреплённый файл — номер закрепа
    QString   title;           // Название закрепа (если задано)
    bool      missing = false; // Закреплённого файла больше нет на диске
    QString display() const { return title.isEmpty() ? name : title; }
};

// Закреплённый файл -> запись полоски (размер и дата — с диска)
inline Entry fromPin(int pinId, const QString &title, const QString &path, const QString &group = QString())
{
    const QFileInfo fi(path);
    Entry e;
    e.path = path;
    e.name = fi.fileName();
    e.size = fi.exists() ? fi.size() : 0;
    e.mtime = fi.exists() ? fi.lastModified() : QDateTime();
    e.pinId = pinId;
    e.title = title;
    e.group = group;
    e.missing = !fi.exists();
    return e;
}

// Свежие файлы: за days дней, не больше max, новые первыми (в фоновом потоке)
inline QList<Entry> scan(const QStringList &folders, int days, int max, const QStringList &cats)
{
    QList<Entry> out;
    const QDateTime since = QDateTime::currentDateTime().addDays(-days);
    for (const QString &folder : folders) {
        QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            // Скопированный файл сохраняет старую дату изменения — берём и дату
            // появления в папке, что новее
            const QDateTime t = qMax(fi.lastModified(), fi.birthTime().isValid() ? fi.birthTime() : QDateTime());
            if (skipFile(fi) || t < since) continue;
            if (!cats.contains(category(fi.filePath()))) continue;
            Entry e;
            e.path = fi.filePath();
            e.name = fi.fileName();
            e.size = fi.size();
            e.mtime = t;
            e.folder = folder;
            out.append(e);
        }
    }
    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.mtime > b.mtime; });
    if (out.size() > max) out.resize(max);
    return out;
}

inline QString sizeText(qint64 b)
{
    return QLocale().formattedDataSize(b, 1, QLocale::DataSizeTraditionalFormat);
}

// «14:05» — сегодня, «20.10» — раньше
inline QString timeText(const QDateTime &t)
{
    return t.date() == QDate::currentDate() ? t.toString("HH:mm") : t.toString("dd.MM");
}

} // namespace Files

class FilesStrip : public QWidget
{
public:
    // thumbnailer — превью файла как в Проводнике (вызывается в фоновом потоке)
    FilesStrip(QWidget *parent, int scalePct, std::function<QImage(const QString &, QSize)> thumbnailer)
        : QWidget(parent), m_scale(scalePct), m_thumbnailer(std::move(thumbnailer))
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);
        // Свой точный таймер (~165 кадров/с), как у просмотра видео: обычные
        // анимации Qt в прозрачном окне SmartClip шли рывками
        m_tick.setTimerType(Qt::PreciseTimer);
        m_tick.setInterval(6);
        connect(&m_tick, &QTimer::timeout, this, [this]() { tick(); });
        m_growTick.setTimerType(Qt::PreciseTimer);
        connect(&m_growTick, &QTimer::timeout, this, [this]() { growStep(); });
        // Ушла ли мышь — проверяем курсор сами: над полоской окно прозрачное, и
        // Windows не всегда сообщает об уходе. Небольшой запас по краям и снизу
        // (до поиска): чуть промахнулся — полоска не закрывается.
        m_hoverPoll.setTimerType(Qt::PreciseTimer);
        m_hoverPoll.setInterval(30);
        connect(&m_hoverPoll, &QTimer::timeout, this, [this]() { checkHover(); });
        // Зажал файл и держишь — он «берётся в руку» (даже если мышь не двигалась)
        m_holdTimer.setSingleShot(true);
        m_holdTimer.setInterval(220);
        // Пока файлы в руке — сами следим за мышью: над прозрачной серединой окна
        // Windows может не присылать SmartClip движения и отпускание кнопки, и
        // файлы оставались «в руке» после отпускания
        m_carryPoll.setTimerType(Qt::PreciseTimer);
        m_carryPoll.setInterval(15);
        connect(&m_carryPoll, &QTimer::timeout, this, [this]() { checkCarry(); });
        connect(&m_holdTimer, &QTimer::timeout, this, [this]() {
            if (m_press >= 0 && m_press < m_items.size() && (QApplication::mouseButtons() & Qt::LeftButton))
                pickUp(m_items[m_press]);
        });
        hide();
    }

    std::function<void(const Files::Entry &)> onPick;                  // Клик — вставить файл
    std::function<void(const Files::Entry &, const QPoint &)> onMenu;  // Правый клик
    std::function<void()> onDragStarted;                               // Понесли файл мышкой
    std::function<void(bool dropped)> onDragFinished;                  // Отпустили (dropped — куда-то вставили)

    // Вкладки над файлами. В Истории — папки, из которых показывать файлы
    // («Все» и по одной на папку), в Закрепах — дополнительные «полки» внутри
    // одной папки закрепов, чтобы разложить файлы.
    struct Tab { QString key; QString label; };
    std::function<void()> onAddTab;                                         // «+»
    std::function<void(const QString &key, const QPoint &globalPos)> onTabMenu;  // Правый клик по вкладке
    std::function<void(const QList<Files::Entry> &, const QString &key)> onMoveToTab;  // Бросили файлы на вкладку

    // Где стоять: прямоугольник прозрачной середины окна (координаты родителя)
    void place(const QRect &center)
    {
        m_center = center;
        relayout();
    }

    // В Закрепах — только закреплённые файлы текущей папки (их даёт setPinned),
    // в Истории — свежие файлы из папок
    void setPinsMode(bool on)
    {
        if (m_pinsMode == on) return;
        m_pinsMode = on;
        m_all.clear();
        collapseNow();
        if (on) applyFilter();
        else reload();
    }
    bool pinsMode() const { return m_pinsMode; }

    void setPinned(const QList<Files::Entry> &pinned, const QStringList &groups = {})
    {
        if (!m_pinsMode) return;
        m_all = pinned;
        m_pinTabs = groups;
        if (!m_tabPins.isEmpty() && !groups.contains(m_tabPins)) m_tabPins.clear();
        applyFilter();
    }

    // Перечитать папки (при каждом открытии SmartClip и после настроек)
    void reload()
    {
        const AppSettings &s = AppSettings::get();
        m_enabled = s.filesStrip();
        m_view = s.filesView();
        if (m_pinsMode) { relayout(); return; }  // Закреплённые приходят из loadPins
        if (!m_enabled) { m_all.clear(); applyFilter(); return; }
        const QStringList folders = Files::configuredFolders();
        m_folders = folders;
        if (!m_tabHistory.isEmpty() && !folders.contains(m_tabHistory)) m_tabHistory.clear();
        const int days = s.filesDays(), max = s.filesMax();
        const QStringList cats = s.filesTypes();
        const int gen = ++m_gen;
        QPointer<FilesStrip> self = this;
        Thumbs::runAsync(this, [folders, days, max, cats]() {
            QVariantList l;
            for (const Files::Entry &e : Files::scan(folders, days, max, cats))
                l << QVariant(QVariantList{e.path, e.name, e.size, e.mtime, e.folder});
            return QVariant(l);
        }, [self, gen](const QVariant &v) {
            if (!self || gen != self->m_gen || self->m_pinsMode) return;
            self->m_all.clear();
            for (const QVariant &x : v.toList()) {
                const QVariantList r = x.toList();
                Files::Entry e;
                e.path = r[0].toString();
                e.name = r[1].toString();
                e.size = r[2].toLongLong();
                e.mtime = r[3].toDateTime();
                e.folder = r[4].toString();
                self->m_all.append(e);
            }
            self->applyFilter();
        });
    }

    // Поиск внизу фильтрует и файлы — по имени
    void setFilter(const QString &q)
    {
        m_query = q.trimmed();
        applyFilter();
    }

    // Свернуть сразу, без анимации (окно спряталось, понесли файл)
    void collapseNow()
    {
        m_tick.stop();
        m_hoverPoll.stop();
        m_t = m_target = 0;
        m_holdBody = false;
        m_growTick.stop();
        m_scroll = 0;
        m_hover = -1;
        m_outside = 0;
        relayout();
    }

    // Видимая часть (виджет размером с раскрытую полоску, над видимой частью —
    // прозрачно, клики там проходят насквозь, см. MainWindow::isInteractiveArea)
    bool hitVisible(const QPoint &p) const { return p.y() >= top(); }

    // Видимая сейчас часть — в координатах родителя (для анимации открытия окна)
    QRect visibleRect() const { return QRect(x(), y() + top(), width(), curH()); }

    // Свёрнутая плашка — в координатах родителя (для анимации при перетаскивании)
    QRect collapsedRect() const
    {
        return QRect(x(), y() + height() - s(HeadH), width(), s(HeadH));
    }

protected:
    void enterEvent(QEnterEvent *e) override
    {
        if (!hitVisible(e->position().toPoint())) return;
        expand(true);
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
        // Сворачивание решает checkHover — с запасом по краям
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_carrying) {
            // Файл в руке: пока над полоской — можно добавлять ещё (правый клик);
            // вынес за полоску — начинается настоящее перетаскивание
            const QPoint p = e->position().toPoint();
            const QRect zone = QRect(0, top(), width(), curH()).adjusted(-s(6), -s(6), s(6), s(6));
            if (!zone.contains(p)) { startCarryDrag(); return; }
            const int h = itemAt(p);
            if (h != m_hover) { m_hover = h; update(); }
            // Зажал правую и ведёшь — собираешь (или убираешь) файлы подряд
            if (m_sweeping && (e->buttons() & Qt::RightButton) && h >= 0 && !m_items[h].missing) {
                const bool carried = isCarried(m_items[h]);
                if (m_sweepAdd != carried) toggleCarry(m_items[h]);
            }
            return;
        }
        if (!hitVisible(e->position().toPoint())) {  // Над полоской — прозрачно
            if (m_hover != -1) { m_hover = -1; update(); }
            return;
        }
        if (m_target < 1) expand(true);
        const int h = itemAt(e->position().toPoint());
        if (h != m_hover) { m_hover = h; update(); }
        const int ht = tabAt(e->position().toPoint());
        if (ht != m_hoverTab) { m_hoverTab = ht; update(); }
        setCursor(h >= 0 || ht != -1 || viewBtnAt(e->position().toPoint()) >= 0 ? Qt::PointingHandCursor
                                                                                 : Qt::ArrowCursor);
        // Тянем файл мышкой — перетаскивание в другую программу
        if ((e->buttons() & Qt::LeftButton) && m_press >= 0 && m_press < m_items.size()
            && (e->position().toPoint() - m_pressPos).manhattanLength() > QApplication::startDragDistance())
            pickUp(m_items[m_press]);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (m_carrying) {
            // Файл в руке, правый клик по другому файлу — добавить его в стопку
            // (ещё раз — убрать), чтобы потом закинуть всё разом
            if (e->button() == Qt::RightButton) {
                const int i = itemAt(e->position().toPoint());
                if (i >= 0 && !m_items[i].missing) {
                    toggleCarry(m_items[i]);
                    // Дальше, пока правая зажата, — так же со всеми, над чем проведёшь
                    m_sweeping = true;
                    m_sweepAdd = isCarried(m_items[i]);
                }
            }
            return;
        }
        m_press = -1;
        if (m_t < 0.9) return;  // Ещё раскрывается
        const QPoint p = e->position().toPoint();
        if (!hitVisible(p)) return;
        const int tab = tabAt(p);
        if (tab == TabPlus) {
            if (e->button() == Qt::LeftButton && onAddTab) {
                m_menuOpen = true;
                QPointer<FilesStrip> self = this;
                onAddTab();
                if (!self) return;
                m_menuOpen = false;
                m_outside = 0;
            }
            return;
        }
        if (tab >= 0) {
            const QList<Tab> tabs = currentTabs();
            if (e->button() == Qt::LeftButton) {
                currentTabRef() = tabs[tab].key;
                m_scroll = 0;
                applyFilter();
            } else if (e->button() == Qt::RightButton && !tabs[tab].key.isEmpty() && onTabMenu) {
                m_menuOpen = true;
                QPointer<FilesStrip> self = this;
                onTabMenu(tabs[tab].key, e->globalPosition().toPoint());
                if (!self) return;
                m_menuOpen = false;
                m_outside = 0;
            }
            return;
        }
        const int btn = viewBtnAt(p);
        if (btn >= 0 && e->button() == Qt::LeftButton) {
            m_view = btn == 0 ? QStringLiteral("tiles") : QStringLiteral("list");
            AppSettings::get().setFilesView(m_view);
            m_scroll = 0;
            relayout();
            return;
        }
        const int i = itemAt(p);
        if (i < 0) return;
        if (e->button() == Qt::LeftButton) { m_press = i; m_pressPos = p; m_holdTimer.start(); }
        else if (e->button() == Qt::RightButton && onMenu) {
            // Пока открыто меню — полоска не сворачивается
            m_menuOpen = true;
            const Files::Entry entry = m_items[i];
            QPointer<FilesStrip> self = this;
            onMenu(entry, e->globalPosition().toPoint());
            if (!self) return;
            m_menuOpen = false;
            m_outside = 0;
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        m_holdTimer.stop();
        if (m_carrying) {
            if (e->button() == Qt::RightButton) { m_sweeping = false; return; }
            if (e->button() != Qt::LeftButton) return;
            // Отпустил на вкладке в Закрепах — файлы перекладываются в неё
            const int tab = tabAt(e->position().toPoint());
            if (m_pinsMode && tab >= 0 && onMoveToTab) {
                const QList<Files::Entry> moved = m_carry;
                const QString key = currentTabs()[tab].key;
                dropCarry(true);
                onMoveToTab(moved, key);
                return;
            }
            // Иначе передумал: файлы «кладутся» обратно
            dropCarry(false);
            return;
        }
        if (e->button() == Qt::LeftButton && m_press >= 0 && m_press < m_items.size()
            && itemAt(e->position().toPoint()) == m_press) {
            const Files::Entry entry = m_items[m_press];
            m_press = -1;
            if (onPick && !entry.missing) onPick(entry);
            return;
        }
        m_press = -1;
    }

    void wheelEvent(QWheelEvent *e) override
    {
        if (tabsRow().contains(local(e->position().toPoint())) && m_t > 0.5) {
            const int d = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
            m_tabScroll = qBound(0, m_tabScroll - d / 2, maxTabScroll());
            update();
            e->accept();
            return;
        }
        m_scroll = qBound(0, m_scroll - e->angleDelta().y() / 2, maxScroll());
        m_hover = itemAt(e->position().toPoint());
        update();
        e->accept();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.translate(0, top());  // Дальше — координаты видимой части
        const QRectF r = QRectF(0, 0, width(), curH()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath bg;
        bg.addRoundedRect(r, s(12), s(12));
        p.fillPath(bg, QColor(18, 18, 30, 245));
        p.setPen(QPen(QColor(255, 255, 255, 28), 1));
        p.drawPath(bg);
        p.setClipPath(bg);

        // Заголовок: значок, название, сколько; справа — плитки/список
        QFont icon(SmartButton::iconFontFamily());
        icon.setPixelSize(s(14));
        QFont f = font();
        f.setPixelSize(s(12));
        const QRect head(s(12), 0, width() - s(24), s(HeadH));
        p.setFont(icon);
        p.setPen(m_pinsMode ? QColor(255, 210, 90) : QColor(160, 200, 255));
        p.drawText(head, Qt::AlignLeft | Qt::AlignVCenter, QString(QChar(m_pinsMode ? 0xE718 : 0xE8B7)));
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, 190));
        p.setOpacity(qBound(0.0, 1.0 - m_t / 0.45, 1.0));  // Раскрылась — вместо названия вкладки
        p.drawText(head.adjusted(s(24), 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   (m_pinsMode ? QApplication::translate("FilesStrip", "Закреплённые файлы · %1")
                               : QApplication::translate("FilesStrip", "Свежие файлы · %1")).arg(m_items.size()));
        p.setOpacity(1.0);

        if (m_t < 0.4) {
            // Свёрнута — маленькие значки в ряд после заголовка, слева направо,
            // как читаешь: самый свежий — первый (тают при раскрытии)
            const int sz = s(22), gap = s(5);
            const int maxX = width() - s(12);
            int x = s(190);
            p.setOpacity(1.0 - m_t / 0.4);
            for (int i = 0; i < m_items.size() && x + sz <= maxX; ++i, x += sz + gap)
                drawThumb(p, m_items[i], QRect(x, (s(HeadH) - sz) / 2, sz, sz), s(4));
            p.setPen(QPen(QColor(255, 255, 255, 22), 1));
            p.setBrush(QColor(255, 255, 255, 8));
            for (; x + sz <= maxX; x += sz + gap)  // Пустые места — бледные заглушки
                p.drawRoundedRect(QRectF(x + 0.5, (s(HeadH) - sz) / 2 + 0.5, sz - 1, sz - 1), s(4), s(4));
            p.setOpacity(1.0);
            if (m_t <= 0.0) return;
        }

        // Содержимое проявляется вместе с ростом и чуть «въезжает» снизу
        const qreal fade = qBound(0.0, (m_t - 0.2) / 0.8, 1.0);
        p.setOpacity(fade);
        p.translate(0, (1.0 - fade) * s(12));
        p.setFont(icon);
        for (int b = 0; b < 2; ++b) {
            const QRect br = viewBtnRect(b);
            const bool on = (b == 0) == (m_view != QLatin1String("list"));
            if (on) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 30));
                p.drawRoundedRect(br, s(5), s(5));
            }
            p.setPen(QColor(255, 255, 255, on ? 230 : 120));
            p.drawText(br, Qt::AlignCenter, QString(QChar(b == 0 ? 0xE80A : 0xE8FD)));  // Плитки / список
        }
        drawTabs(p, f, icon);

        const QRect body = bodyRect();
        p.setClipRect(body, Qt::IntersectClip);
        if (m_items.isEmpty()) {
            drawGhostItems(p, body);  // Пустая вкладка — тоже не пустота, а пустые плитки
            p.setFont(f);
            const QString msg = (m_pinsMode && m_all.isEmpty())
                ? QApplication::translate("FilesStrip", "Пока нет закреплённых файлов.\n"
                                                        "Закрепить: История → правый клик по файлу внизу → «Закрепить».")
                : QApplication::translate("FilesStrip", "Ничего не найдено");
            // Надпись — на тёмной подложке, чтобы читалась поверх заглушек
            const QRect tr = QFontMetrics(f).boundingRect(body.adjusted(s(24), 0, -s(24), 0),
                                                          Qt::AlignCenter | Qt::TextWordWrap, msg);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(18, 18, 30, 230));
            p.drawRoundedRect(QRectF(tr.adjusted(-s(14), -s(8), s(14), s(8))), s(10), s(10));
            p.setPen(QColor(255, 255, 255, 130));
            p.drawText(body.adjusted(s(24), 0, -s(24), 0), Qt::AlignCenter | Qt::TextWordWrap, msg);
            return;
        }
        drawGhostItems(p, body);
        for (int i = 0; i < m_items.size(); ++i) {
            const QRect ir = itemRect(i);
            if (!ir.intersects(body)) continue;
            if (i == m_hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 22));
                p.drawRoundedRect(ir, s(8), s(8));
            }
            const Files::Entry &e = m_items[i];
            // Файла уже нет — бледный; взят «в руку» — тоже (его место пустеет)
            const qreal itemOpacity = (e.missing || isCarried(e)) ? fade * 0.35 : fade;
            p.setOpacity(itemOpacity);
            if (m_view == QLatin1String("list")) {
                const int isz = s(24);
                drawThumb(p, e, QRect(ir.left() + s(6), ir.center().y() - isz / 2 + 1, isz, isz), s(4));
                p.setFont(f);
                const QString meta = e.missing ? QApplication::translate("FilesStrip", "файл не найден")
                                               : Files::sizeText(e.size) + "   " + Files::timeText(e.mtime);
                const int metaW = QFontMetrics(f).horizontalAdvance(meta) + s(10);
                p.setPen(QColor(255, 255, 255, 110));
                p.drawText(ir.adjusted(0, 0, -s(8), 0), Qt::AlignRight | Qt::AlignVCenter, meta);
                p.setPen(QColor(255, 255, 255, 220));
                const QRect nr = ir.adjusted(s(38), 0, -metaW - s(8), 0);
                p.drawText(nr, Qt::AlignLeft | Qt::AlignVCenter,
                           QFontMetrics(f).elidedText(e.display(), Qt::ElideMiddle, nr.width()));
            } else {
                const int tsz = s(56);
                drawThumb(p, e, QRect(ir.center().x() - tsz / 2, ir.top() + s(6), tsz, tsz), s(6));
                QFont small = f;
                small.setPixelSize(s(10));
                p.setFont(small);
                p.setPen(QColor(255, 255, 255, 210));
                const QRect nr(ir.left() + s(4), ir.top() + s(66), ir.width() - s(8), s(28));
                p.drawText(nr, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWrapAnywhere,
                           QFontMetrics(small).elidedText(e.display(), Qt::ElideMiddle, nr.width() * 2 - s(6)));
            }
            p.setOpacity(fade);
        }
    }

private:
    static constexpr int HeadH = 38;    // Высота свёрнутой плашки и заголовка
    static constexpr int MaxBody = 460; // Высота раскрытой части (не выше 60% середины окна)

    int s(int v) const { return v * m_scale / 100; }

    void expand(bool on)
    {
        if (m_items.isEmpty() && on && m_query.isEmpty() && !m_pinsMode) return;
        const qreal target = on ? 1.0 : 0.0;
        if (target == m_target && (m_tick.isActive() || m_t == target)) return;
        m_target = target;
        m_from = m_t;
        // Раскрытие чуть дольше и мягче, сворачивание — не медленнее, но без рывка
        m_duration = qMax(1, int(Anim::ms(Anim::Kind::Menu, on ? 400 : 260) * qAbs(target - m_t)));
        m_outside = 0;
        if (on) {
            m_hoverPoll.start();
            if (!m_holdBody) {  // Открываемся — эту высоту и держим, пока открыты
                m_holdBody = true;
                m_shownBody = m_bodyTarget = naturalBody();
            }
        } else m_hover = -1;
        if (!Anim::enabled(Anim::Kind::Menu)) { m_t = target; finish(); return; }
        m_clock.restart();
        m_tick.setInterval(Anim::frameMs(this));  // По частоте монитора
        m_tick.start();
    }

    void tick()
    {
        const qreal k = qMin(1.0, m_clock.elapsed() / qreal(m_duration));
        // Раскрытие — плавный старт и долгое мягкое торможение;
        // сворачивание — плавно разгоняется и плавно садится на место
        const QEasingCurve curve(m_target > m_from ? QEasingCurve::OutCubic : QEasingCurve::InOutSine);
        m_t = m_from + (m_target - m_from) * curve.valueForProgress(k);
        update();
        if (k >= 1.0) { m_t = m_target; finish(); }
    }

    void finish()
    {
        m_tick.stop();
        if (m_t <= 0.0) {
            m_scroll = 0;
            releaseBody();  // Свернулась — дальше снова по размеру вкладки
        }
        update();
    }

    // Подрасти до target (плавно)
    void growBody(int target)
    {
        m_bodyTarget = target;
        m_growFrom = m_shownBody;
        m_growClock.restart();
        m_growDuration = qMax(1, Anim::ms(Anim::Kind::Menu, 220));
        if (!Anim::enabled(Anim::Kind::Menu)) { m_shownBody = target; update(); return; }
        m_growTick.setInterval(Anim::frameMs(this));
        m_growTick.start();
    }

    void growStep()
    {
        const qreal k = qMin(1.0, m_growClock.elapsed() / qreal(m_growDuration));
        m_shownBody = m_growFrom + (m_bodyTarget - m_growFrom) * QEasingCurve(QEasingCurve::OutCubic).valueForProgress(k);
        update();
        if (k >= 1.0) m_growTick.stop();
    }

    void releaseBody()
    {
        if (!m_holdBody) return;
        m_holdBody = false;
        m_growTick.stop();
        relayout();
    }

    // Лишнее место (вкладка, где файлов меньше) — бледные пустые плитки,
    // как фантомные карточки в колонках, а не просто пустота
    void drawGhostItems(QPainter &p, const QRect &body)
    {
        p.save();
        const qreal base = p.opacity();
        for (int i = int(m_items.size()); i < int(m_items.size()) + 200; ++i) {
            const QRect ir = itemRect(i);
            if (ir.top() >= body.bottom()) break;
            if (!body.intersects(ir)) continue;
            p.setOpacity(base * 0.9);
            p.setPen(QPen(QColor(255, 255, 255, 26), 1));
            p.setBrush(QColor(255, 255, 255, 9));
            if (m_view == QLatin1String("list")) {
                p.drawRoundedRect(QRectF(ir).adjusted(0.5, 0.5, -0.5, -0.5), s(8), s(8));
            } else {
                const int tsz = s(56);
                p.drawRoundedRect(QRectF(ir.center().x() - tsz / 2, ir.top() + s(6), tsz, tsz), s(8), s(8));
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 16));
                p.drawRoundedRect(QRectF(ir.center().x() - s(28), ir.top() + s(70), s(56), s(6)), s(3), s(3));
            }
        }
        p.restore();
    }

    // ── Вкладки ──────────────────────────────────────────────────────────────
    static constexpr int TabPlus = -2;  // tabAt(): попали в «+»

    QList<Tab> currentTabs() const
    {
        QList<Tab> tabs{{QString(), QApplication::translate("FilesStrip", "Все")}};
        if (m_pinsMode) {
            for (const QString &g : m_pinTabs) tabs.append({g, g});
        } else {
            const QString dl = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            const QString desk = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
            for (const QString &f : m_folders) {
                const QString c = QDir::cleanPath(f);
                QString label = QDir(c).dirName();
                if (c.compare(QDir::cleanPath(dl), Qt::CaseInsensitive) == 0)
                    label = QApplication::translate("FilesStrip", "Загрузки");
                else if (c.compare(QDir::cleanPath(desk), Qt::CaseInsensitive) == 0)
                    label = QApplication::translate("FilesStrip", "Рабочий стол");
                tabs.append({f, label});
            }
        }
        return tabs;
    }
    QString &currentTabRef() { return m_pinsMode ? m_tabPins : m_tabHistory; }
    QString currentTab() const { return m_pinsMode ? m_tabPins : m_tabHistory; }

    // Где вкладки: от значка до кнопок вида (координаты видимой части)
    QRect tabsRow() const
    {
        const int left = s(34), right = viewBtnRect(0).left() - s(8);
        return QRect(left, (s(HeadH) - s(24)) / 2, qMax(0, right - left), s(24));
    }

    // Прямоугольники вкладок (без прокрутки) и «+» в конце
    QList<QRect> tabRects(QRect *plus = nullptr) const
    {
        QFont f = font();
        f.setPixelSize(s(11));
        const QFontMetrics fm(f);
        const QRect row = tabsRow();
        QList<QRect> out;
        int x = row.left();
        for (const Tab &t : currentTabs()) {
            const int w = qMin(s(160), fm.horizontalAdvance(t.label) + s(20));
            out.append(QRect(x, row.top(), w, row.height()));
            x += w + s(5);
        }
        if (plus) *plus = QRect(x, row.top(), row.height(), row.height());
        return out;
    }
    int maxTabScroll() const
    {
        QRect plus;
        tabRects(&plus);
        return qMax(0, plus.right() + s(4) - tabsRow().right());
    }

    int tabAt(const QPoint &widgetPos) const
    {
        if (m_t < 0.9) return -1;
        const QPoint p = local(widgetPos) + QPoint(m_tabScroll, 0);
        if (!tabsRow().contains(local(widgetPos))) return -1;
        QRect plus;
        const QList<QRect> rects = tabRects(&plus);
        for (int i = 0; i < rects.size(); ++i) if (rects[i].contains(p)) return i;
        if (plus.contains(p)) return TabPlus;
        return -1;
    }

    void drawTabs(QPainter &p, const QFont &base, const QFont &icon)
    {
        QFont f = base;
        f.setPixelSize(s(11));
        const QRect row = tabsRow();
        QRect plus;
        const QList<QRect> rects = tabRects(&plus);
        const QList<Tab> tabs = currentTabs();
        const QString cur = currentTab();
        p.save();
        p.setClipRect(row.adjusted(0, -2, 0, 2), Qt::IntersectClip);
        p.translate(-m_tabScroll, 0);
        for (int i = 0; i < rects.size(); ++i) {
            const bool on = tabs[i].key == cur;
            const bool hot = m_hoverTab == i;
            p.setPen(QPen(QColor(255, 255, 255, on ? 90 : 40), 1));
            p.setBrush(on ? (m_pinsMode ? QColor(80, 60, 14, 235) : QColor(36, 60, 104, 235))
                          : QColor(255, 255, 255, hot ? 26 : 12));
            p.drawRoundedRect(QRectF(rects[i]).adjusted(0.5, 0.5, -0.5, -0.5), s(7), s(7));
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, on ? 240 : 170));
            p.drawText(rects[i].adjusted(s(8), 0, -s(8), 0), Qt::AlignCenter,
                       QFontMetrics(f).elidedText(tabs[i].label, Qt::ElideMiddle, rects[i].width() - s(16)));
        }
        p.setPen(QPen(QColor(255, 255, 255, 45), 1));
        p.setBrush(QColor(255, 255, 255, m_hoverTab == TabPlus ? 26 : 10));
        p.drawRoundedRect(QRectF(plus).adjusted(0.5, 0.5, -0.5, -0.5), s(7), s(7));
        QFont ic = icon;
        ic.setPixelSize(s(10));
        p.setFont(ic);
        p.setPen(QColor(255, 255, 255, 180));
        p.drawText(plus, Qt::AlignCenter, QString(QChar(0xE710)));  // Плюс
        p.restore();
        // Не всё влезло — мягкие края подсказывают, что вкладки листаются
        if (maxTabScroll() > 0) {
            const QColor bg(18, 18, 30, 245);
            QLinearGradient gl(row.left(), 0, row.left() + s(18), 0);
            gl.setColorAt(0, bg); gl.setColorAt(1, QColor(18, 18, 30, 0));
            if (m_tabScroll > 0) p.fillRect(QRect(row.left(), row.top(), s(18), row.height()), gl);
            QLinearGradient gr(row.right() - s(18), 0, row.right(), 0);
            gr.setColorAt(0, QColor(18, 18, 30, 0)); gr.setColorAt(1, bg);
            if (m_tabScroll < maxTabScroll()) p.fillRect(QRect(row.right() - s(18), row.top(), s(18), row.height()), gr);
        }
    }

    // Мышь ушла дальше запаса (60 мс подряд) — сворачиваемся
    void checkHover()
    {
        if (m_target <= 0) { m_hoverPoll.stop(); return; }
        if (m_dragging || m_carrying || m_menuOpen || QApplication::activePopupWidget()) { m_outside = 0; return; }
        const QRect visible(mapToGlobal(QPoint(0, top())), QSize(width(), curH()));
        // Запас: по бокам немного, снизу — до поля поиска и чуть в него
        const QRect zone = visible.adjusted(-s(14), -s(10), s(14), s(28));
        if (zone.contains(QCursor::pos())) { m_outside = 0; return; }
        m_outside += m_hoverPoll.interval();
        if (m_outside >= 60) expand(false);
    }

    void applyFilter()
    {
        m_items.clear();
        const QString tab = currentTab();
        for (const Files::Entry &e : m_all) {
            if (!tab.isEmpty()) {  // Открыта вкладка — только её файлы
                if (m_pinsMode ? e.group != tab
                               : QDir::cleanPath(e.folder).compare(QDir::cleanPath(tab), Qt::CaseInsensitive) != 0)
                    continue;
            }
            if (m_query.isEmpty() || e.name.contains(m_query, Qt::CaseInsensitive)
                || e.title.contains(m_query, Qt::CaseInsensitive)) m_items.append(e);
        }
        m_tabScroll = qBound(0, m_tabScroll, maxTabScroll());
        m_scroll = qBound(0, m_scroll, maxScroll());
        m_hover = -1;
        requestThumbs();
        relayout();
    }

    static QString thumbKey(const Files::Entry &e)
    {
        return e.path + '|' + QString::number(e.mtime.toMSecsSinceEpoch());
    }

    // Превью — по одному разу на файл (ключ — путь и время изменения)
    void requestThumbs()
    {
        const QSize px = QSize(s(56), s(56)) * devicePixelRatioF();
        for (const Files::Entry &e : m_items) {
            if (e.missing) continue;
            const QString key = thumbKey(e);
            if (m_thumbs.contains(key) || m_waiting.contains(key)) continue;
            m_waiting.insert(key);
            const QString path = e.path;
            auto thumb = m_thumbnailer;
            QPointer<FilesStrip> self = this;
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

    void drawThumb(QPainter &p, const Files::Entry &e, const QRect &r, int radius) const
    {
        const QPixmap pm = m_thumbs.value(thumbKey(e));
        if (pm.isNull()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 18));
            p.drawRoundedRect(r, radius, radius);
            return;
        }
        const QSizeF sz = QSizeF(pm.size() / pm.devicePixelRatio()).scaled(QSizeF(r.size()), Qt::KeepAspectRatio);
        const QRectF tr(r.left() + (r.width() - sz.width()) / 2, r.top() + (r.height() - sz.height()) / 2,
                        sz.width(), sz.height());
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(tr, radius, radius);
        p.setClipPath(clip, Qt::IntersectClip);
        p.drawPixmap(tr, pm, QRectF(pm.rect()));
        p.restore();
    }

    // ── Раскладка ────────────────────────────────────────────────────────────
    int fullBodyHeight() const
    {
        if (m_items.isEmpty()) return m_view == QLatin1String("list") ? s(80) : s(108);  // Ровно ряд заглушек
        if (m_view == QLatin1String("list")) return m_items.size() * s(34) + s(8);
        const int cols = qMax(1, (width() - s(16)) / s(92));
        return int((m_items.size() + cols - 1) / cols) * s(100) + s(8);
    }
    int maxBody() const { return qMax(s(120), qMin(s(MaxBody), m_center.height() * 6 / 10)); }
    // Сколько места нужно файлам открытой вкладки
    int naturalBody() const { return qMin(fullBodyHeight(), maxBody()); }
    // Пока полоска открыта, её высота не уменьшается: переключился на вкладку,
    // где файлов меньше, — полоска не съёживается и вкладки не уезжают из-под
    // мыши (раньше она от этого сворачивалась). Больше файлов — плавно
    // подрастает. Свернулась — в следующий раз откроется по размеру вкладки.
    int bodyHeight() const { return m_holdBody ? qRound(m_shownBody) : naturalBody(); }
    int geometryBody() const { return m_holdBody ? qMax(m_bodyTarget, naturalBody()) : naturalBody(); }
    int maxScroll() const { return qMax(0, fullBodyHeight() - bodyHeight()); }
    // Видимая высота сейчас и где начинается видимая часть
    int curH() const { return s(HeadH) + qRound(bodyHeight() * m_t); }
    int top() const { return height() - curH(); }
    // Дальше всё — в координатах видимой части (её верх = 0)
    QRect bodyRect() const { return QRect(0, s(HeadH), width(), bodyHeight()); }
    QPoint local(const QPoint &p) const { return p - QPoint(0, top()); }

    QRect itemRect(int i) const
    {
        const QRect b = bodyRect();
        if (m_view == QLatin1String("list"))
            return QRect(s(8), b.top() + s(2) + i * s(34) - m_scroll, width() - s(16), s(32));
        const int cols = qMax(1, (width() - s(16)) / s(92));
        const int cw = (width() - s(16)) / cols;
        return QRect(s(8) + (i % cols) * cw, b.top() + s(2) + (i / cols) * s(100) - m_scroll, cw - s(4), s(96));
    }

    int itemAt(const QPoint &widgetPos) const
    {
        const QPoint p = local(widgetPos);
        if (m_t < 0.9 || !bodyRect().contains(p)) return -1;
        for (int i = 0; i < m_items.size(); ++i)
            if (itemRect(i).contains(p)) return i;
        return -1;
    }

    QRect viewBtnRect(int b) const
    {
        const int sz = s(26);
        return QRect(width() - s(10) - (2 - b) * (sz + s(4)), (s(HeadH) - sz) / 2, sz, sz);
    }
    int viewBtnAt(const QPoint &widgetPos) const
    {
        const QPoint p = local(widgetPos);
        if (m_t < 0.9) return -1;
        for (int b = 0; b < 2; ++b) if (viewBtnRect(b).contains(p)) return b;
        return -1;
    }

    void relayout()
    {
        // В Закрепах полоска есть всегда (даже пустая — с подсказкой),
        // в Истории — если включена и есть свежие файлы
        const bool show = (m_pinsMode || (m_enabled && !m_all.isEmpty())) && m_center.width() > s(260);
        if (!show) { hide(); return; }
        const int w = qMin(m_center.width() - s(40), s(720));
        const int h = s(HeadH) + geometryBody();  // Размер не меняется во время анимации
        if (m_holdBody && naturalBody() > m_bodyTarget) growBody(naturalBody());
        setGeometry(m_center.center().x() - w / 2, m_center.bottom() - s(6) - h, w, h);
        if (!isVisible()) { QWidget::show(); raise(); }
        update();
    }

    // ── Файлы «в руке» ──────────────────────────────────────────────────────
    // Зажал файл (или потянул) — он берётся в руку: висит под курсором, а
    // полоска остаётся открытой. Правым кликом по другим файлам их можно
    // добавить в стопку. Вынес за полоску — SmartClip прижимается к краям, и
    // стопку можно бросить в любую программу. Отпустил над полоской — отмена.
    bool isCarried(const Files::Entry &e) const
    {
        for (const Files::Entry &c : m_carry) if (c.path == e.path) return true;
        return false;
    }

    void pickUp(const Files::Entry &entry)
    {
        m_holdTimer.stop();
        m_press = -1;
        if (entry.missing || m_carrying) return;
        m_carrying = true;
        m_carry = {entry};
        m_outside = 0;
        if (!m_ghost) m_ghost = new DragGhost(this, m_scale);
        updateGhost();
        m_ghost->start();
        setCursor(Qt::ClosedHandCursor);
        m_carryPoll.start();
        update();
    }

    // Левая кнопка мыши зажата прямо сейчас (с учётом «левши»: кнопки поменяны)
    static bool leftHeld()
    {
        const int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    void checkCarry()
    {
        if (!m_carrying) { m_carryPoll.stop(); return; }
        if (m_dragging) return;  // Идёт перетаскивание Windows — она сама следит за кнопкой
        // Отпустил кнопку — файлы просто пропадают из руки
        if (!leftHeld()) { dropCarry(false); return; }
        // Вынес за полоску — начинаем перетаскивание, даже если движения не пришли
        const QPoint p = mapFromGlobal(QCursor::pos());
        const QRect zone = QRect(0, top(), width(), curH()).adjusted(-s(6), -s(6), s(6), s(6));
        if (!zone.contains(p)) startCarryDrag();
    }

    void toggleCarry(const Files::Entry &e)
    {
        for (int i = 0; i < m_carry.size(); ++i)
            if (m_carry[i].path == e.path) {
                if (i > 0) m_carry.removeAt(i);  // Первый (взятый) не убираем
                updateGhost();
                update();
                return;
            }
        m_carry.append(e);
        updateGhost();
        update();
    }

    void updateGhost()
    {
        if (!m_ghost) return;
        QList<DragGhost::Item> items;
        for (const Files::Entry &e : m_carry) items.append({m_thumbs.value(thumbKey(e)), e.display()});
        m_ghost->setItems(items);
    }

    // Положить обратно / бросили: убрать «руку»
    void dropCarry(bool dropped)
    {
        m_carryPoll.stop();
        if (m_ghost) { m_ghost->finish(dropped); m_ghost = nullptr; }
        m_carrying = false;
        m_carry.clear();
        m_outside = 0;
        setCursor(Qt::ArrowCursor);
        update();
    }

    // Вынес за полоску — настоящее перетаскивание всей стопки в другую программу
    void startCarryDrag()
    {
        auto *mime = new QMimeData;
        QList<QUrl> urls;
        for (const Files::Entry &e : m_carry) urls << QUrl::fromLocalFile(e.path);
        mime->setUrls(urls);
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        // Картинку под курсором рисует «живая» карточка (DragGhost) — у Windows своей нет
        QPixmap none(1, 1);
        none.fill(Qt::transparent);
        drag->setPixmap(none);
        m_dragging = true;
        QPointer<FilesStrip> self = this;
        if (onDragStarted) onDragStarted();   // SmartClip прижимается к краям экрана
        const Qt::DropAction act = drag->exec(Qt::CopyAction, Qt::CopyAction);
        if (!self) return;
        m_dragging = false;
        // Бросили туда, где файлы не принимают, или отменили — рука всё равно пустеет
        const bool dropped = act != Qt::IgnoreAction;
        dropCarry(dropped);
        if (onDragFinished) onDragFinished(dropped);  // …и отпрыгивает на место
    }

    int m_scale = 100;
    std::function<QImage(const QString &, QSize)> m_thumbnailer;
    QList<Files::Entry> m_all;     // Всё найденное / закреплённое
    QList<Files::Entry> m_items;   // После поиска
    QHash<QString, QPixmap> m_thumbs;
    QSet<QString> m_waiting;
    QString m_view = QStringLiteral("tiles");
    QString m_query;
    QRect m_center;
    bool m_enabled = true;
    bool m_pinsMode = false;
    bool m_dragging = false;
    bool m_menuOpen = false;
    bool m_carrying = false;       // Файлы «в руке» (см. pickUp)
    bool m_sweeping = false;       // Правая зажата — собираем файлы подряд
    bool m_sweepAdd = true;        // ...добавляем (или убираем)
    QStringList m_folders;         // Папки Истории (вкладки)
    QStringList m_pinTabs;         // Вкладки текущей папки закрепов
    QString m_tabHistory, m_tabPins;  // Открытая вкладка
    int m_tabScroll = 0;
    int m_hoverTab = -1;
    bool m_holdBody = false;       // Открыта — высоту держим (см. bodyHeight)
    qreal m_shownBody = 0, m_growFrom = 0;
    int m_bodyTarget = 0, m_growDuration = 1;
    QElapsedTimer m_growClock;
    QTimer m_growTick;
    QList<Files::Entry> m_carry;   // Что в руке: первый — взятый, дальше — добавленные
    QPointer<DragGhost> m_ghost;
    QTimer m_holdTimer;
    QTimer m_carryPoll;            // Следит за кнопкой мыши, пока файлы в руке
    int m_gen = 0;
    int m_scroll = 0;
    int m_hover = -1;
    int m_press = -1;
    int m_outside = 0;             // Сколько мс мышь за пределами полоски
    QPoint m_pressPos;
    qreal m_t = 0;                 // 0 — свёрнута, 1 — раскрыта
    qreal m_from = 0, m_target = 0;
    int m_duration = 1;
    QElapsedTimer m_clock;
    QTimer m_tick;
    QTimer m_hoverPoll;
};
