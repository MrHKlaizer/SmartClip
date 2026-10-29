#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  TestUtil.h — общее для проверок и обезьяны                                  ║
// ║                                                                              ║
// ║  App — окно SmartClip во временной папке (не на экране); файлы, клики и    ║
// ║  правила, которые у папки с полки должны выполняться всегда.               ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMouseEvent>
#include <QDateTime>
#include <QLineEdit>
#define private public    // Тестам можно смотреть внутрь
#define protected public
#include "ui/ClickableCard.h"
#include "ui/FilesStrip.h"
#include "ui/FolderShelf.h"
#include "ui/FolderPanel.h"
#include "core/Database.h"
#include "core/AppSettings.h"
#include "ui/MainWindow.h"
#undef protected
#undef private
#include "Check.h"

namespace TestUtil {

// Файл нужного размера и даты
inline void makeFile(const QString &path, int bytes, const QDateTime &time = QDateTime())
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QByteArray(bytes, 'x'));
    if (time.isValid()) {
        f.setFileTime(time, QFileDevice::FileModificationTime);
        f.setFileTime(time, QFileDevice::FileBirthTime);
    }
}

inline void click(QWidget *w, const QPoint &pos, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent pr(QEvent::MouseButtonPress, pos, w->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton, mods);
    QApplication::sendEvent(w, &pr);
    QMouseEvent rl(QEvent::MouseButtonRelease, pos, w->mapToGlobal(pos), Qt::LeftButton, Qt::NoButton, mods);
    QApplication::sendEvent(w, &rl);
    QApplication::processEvents();
}

inline int indexOf(FolderPanel *fp, const QString &name)
{
    const auto &l = fp->shown();
    for (int i = 0; i < l.size(); ++i) if (l[i].name == name) return i;
    return -1;
}

template <class T>
bool isSorted(const QList<T> &l, const QString &key, bool desc)
{
    QList<T> c = l;
    Files::sortList(c, key, desc);
    for (int i = 0; i < l.size(); ++i) if (c[i].path != l[i].path) return false;
    return true;
}

// Окно SmartClip (не на экране) со своей базой
struct App {
    Check::Env &env;
    Database db;
    MainWindow *w = nullptr;
    explicit App(Check::Env &e) : env(e)
    {
        db.init();
        for (int i = 0; i < 4; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
    }
    ~App() { delete w; }
    void start()
    {
        w = new MainWindow(&db);
        w->setAttribute(Qt::WA_DontShowOnScreen);
        // Наружу — ничего: клик по файлу не вставляет его в чужие программы,
        // просмотр и меню не открываются (тест, которому надо, ставит своё)
        w->m_filesStrip->onPick = [](const Files::Entry &) {};
        w->m_filesStrip->onView = [](const QString &) {};
        w->m_filesStrip->onMenu = [](const Files::Entry &, const QPoint &) {};
        w->m_folderPanel->onPickFile = [](const QString &) {};
        w->m_folderPanel->onView = [](const QString &) {};
        w->m_folderPanel->onFileMenu = [](const Files::Entry &, const QPoint &) {};
        w->m_folderPanel->onFolderMenu = [](const QString &, const QPoint &) {};
        w->loadShelves();
        w->showWindow();
        // Экран, на котором тестируем (ставит smartclip_tests: каждый твой
        // экран — 16:9, повёрнутый 9:16 — и ноутбучные 3:2, 2:3)
        const QStringList g = qEnvironmentVariable("SMARTCLIP_TEST_SCREEN").split(',');
        if (g.size() == 4) w->setGeometry(g[0].toInt(), g[1].toInt(), g[2].toInt(), g[3].toInt());
        Check::Env::wait(1200);
    }
    bool busy() const
    {
        FolderPanel *fp = w->m_folderPanel;
        FilesStrip *fs = w->m_filesStrip;
        return fp->m_anim.isActive() || (fp->m_open && fp->m_loading) || fs->m_tick.isActive() || fs->m_growTick.isActive();
    }
    // Дождаться, пока всё доедет и догрузится
    void settle(int ms = 3000)
    {
        Check::Env::waitFor([this]() { return !busy(); }, ms);
        Check::Env::wait(30);
    }
    void openShelf(int side, int index = 0)
    {
        w->openShelfFolder(side, index, w->m_shelves[side]->itemRect(index));
        settle();
    }
};

inline QString rectText(const QRect &r) { return QString("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()); }

// Каждый кадр анимации: содержимое рисуется на итоговом месте (оно внутри
// виджета) и не мигает (кроме раскрытия из плитки и закрытия)
inline bool panelFrameOk(FolderPanel *fp, QString *why)
{
    if (!fp->m_open || !fp->m_anim.isActive() || fp->m_closing) return true;
    if (!fp->geometry().contains(fp->m_layoutGeo)) {
        *why = "итоговое место " + rectText(fp->m_layoutGeo) + " вне виджета " + rectText(fp->geometry());
        return false;
    }
    if (!fp->m_reveal && fp->contentOpacity() < 0.99) {
        *why = QString("содержимое мигает при смене размера (%1)").arg(fp->contentOpacity());
        return false;
    }
    return true;
}

// Доехала и стоит: всё на своих местах
inline bool panelOk(FolderPanel *fp, QString *why)
{
    if (!fp->m_open) {
        if (fp->isVisible() && !fp->m_anim.isActive()) { *why = "закрыта, но видна"; return false; }
        return true;
    }
    if (fp->m_anim.isActive()) return panelFrameOk(fp, why);
    QWidget *par = fp->parentWidget();
    const QRect g = fp->geometry();
    if (!fp->isVisible()) { *why = "открыта, но не видна"; return false; }
    if (g != fp->m_layoutGeo) { *why = "стоит не там: " + rectText(g) + " вместо " + rectText(fp->m_layoutGeo); return false; }
    if (par && !par->rect().contains(g)) { *why = "вылезла за окно: " + rectText(g); return false; }
    if (g.top() < fp->m_fullRect.top() - 1 || g.bottom() > fp->m_shelfRect.bottom() + 1) {
        *why = "вылезла за середину: " + rectText(g) + " (середина " + rectText(fp->m_fullRect) + ")";
        return false;
    }
    if (fp->m_scroll < 0 || fp->m_scroll > fp->maxScroll()) {
        *why = QString("прокрутка %1 вне 0..%2").arg(fp->m_scroll).arg(fp->maxScroll());
        return false;
    }
    if (fp->listView() && fp->mode() == FolderPanel::Full) { *why = "список растянут во всю ширину"; return false; }
    if (fp->mode() != FolderPanel::Inline && g.width() < fp->s(420) && g.width() < fp->m_fullRect.width()) {
        *why = QString("слишком узкая: %1 (шапка не влезет)").arg(g.width());
        return false;
    }
    if (fp->m_hover >= int(fp->shown().size())) { *why = "подсветка за концом списка"; return false; }
    if (fp->contentOpacity() < 0.999) { *why = "стоит полупрозрачной"; return false; }
    const bool wantEdit = fp->m_searchOpen && fp->mode() != FolderPanel::Inline;
    if (fp->m_searchEdit->isVisible() != wantEdit) { *why = "поле поиска видно не тогда"; return false; }
    if (fp->mode() != FolderPanel::Inline && g.height() < qMin(fp->min2Rows(), fp->m_shelfRect.bottom() - fp->m_fullRect.top() + 1) - 1) {
        *why = QString("ниже двух строк: %1").arg(g.height());
        return false;
    }
    // Правило «размер по делу»: не выше, чем нужно содержимому (кроме
    // «держим размер» в дереве и пока грузится)
    if (fp->mode() != FolderPanel::Inline && !fp->m_holdH && !fp->m_loading) {
        // Пока ищешь, панель не сжимается под найденное (не дёргается на каждой букве)
        const int n = int((fp->m_query.isEmpty() ? fp->shown() : fp->m_items).size());
        const int c = fp->cols(g.width());
        const int need = qMax(fp->minBigH(), fp->headH() + ((qMax(n, 1) + c - 1) / c) * fp->tileH() + fp->s(14));
        if (g.height() > need + 2) {
            *why = QString("выше, чем нужно: %1 при нужных %2 (%3 строк)").arg(g.height()).arg(need).arg(n);
            return false;
        }
    }
    // Правило «кнопки на месте»: шапка внутри панели, кнопки не налезают друг на друга
    if (fp->mode() != FolderPanel::Inline) {
        QList<QPair<QString, QRect>> btns = {{"закрыть", fp->closeRect()}, {"плитки", fp->viewRect(0)},
                                             {"список", fp->viewRect(1)}, {"сортировка", fp->sortRect()},
                                             {"поиск", fp->searchRect()}, {"назад", fp->backRect()}};
        const QList<QRect> crumbs = fp->crumbRects();
        for (int i = 0; i < crumbs.size(); ++i)
            if (!crumbs[i].isEmpty() && crumbs[i].width() > 0) btns << qMakePair(QString("путь %1").arg(i), crumbs[i]);
        for (int i = 0; i < btns.size(); ++i) {
            if (!fp->rect().contains(btns[i].second)) { *why = "кнопка «" + btns[i].first + "» за краем панели"; return false; }
            for (int j = i + 1; j < btns.size(); ++j)
                if (btns[i].second.intersects(btns[j].second)) {
                    *why = "кнопки «" + btns[i].first + "» и «" + btns[j].first + "» налезают (" + rectText(btns[i].second)
                         + " и " + rectText(btns[j].second) + ", ширина панели " + QString::number(g.width()) + ")";
                    return false;
                }
        }
    }
    return true;
}

// ── Правила «неудобно» во времени (обезьяна смотрит каждые несколько мс) ────
// • без рывков: панель/полоска меняет размер и место только плавно — стоит
//   на месте, а в следующий миг уже в другом месте без анимации — рывок;
//   анимация начинается оттуда, где панель была;
// • без дёрганий: за одно действие размер меняется в одну сторону — вырос,
//   сжался и снова вырос — это «не успевает перестроиться».
struct UxTracker {
    QRect lastPanel, lastStrip;
    bool lastPanelAnim = false, lastStripAnim = false, havePanel = false, haveStrip = false;
    QList<int> heights;  // Куда панель собиралась встать за это действие

    static QRect panelVisible(FolderPanel *fp)
    {
        if (!fp->isVisible() || !fp->m_open) return QRect();
        const QRect r = fp->revealRect();
        return QRect(fp->mapToParent(r.topLeft()), r.size());
    }
    static int diff(const QRect &a, const QRect &b)
    {
        return qMax(qMax(qAbs(a.left() - b.left()), qAbs(a.right() - b.right())),
                    qMax(qAbs(a.top() - b.top()), qAbs(a.bottom() - b.bottom())));
    }
    void beginAction(FolderPanel *fp)
    {
        heights.clear();
        if (fp->m_open && fp->isVisible()) heights << fp->m_layoutGeo.height();
    }
    bool sample(FolderPanel *fp, FilesStrip *fs, QString *why)
    {
        // Панель
        const QRect v = panelVisible(fp);
        const bool anim = fp->m_anim.isActive();
        if (havePanel && v.isValid() && lastPanel.isValid() && !fp->m_reveal) {
            const int d = diff(v, lastPanel);
            if (!anim && !lastPanelAnim && d > 2) {
                *why = "рывок панели без анимации: " + rectText(lastPanel) + " → " + rectText(v);
                return false;
            }
            // Анимация стартовала с того места, где панель стояла (первый кадр
            // уже может быть чуть дальше — в начале она быстрая)
            if (anim && !lastPanelAnim && !fp->m_closing && diff(fp->m_revealFrom, lastPanel) > 2) {
                *why = "анимация началась не оттуда, где панель была: " + rectText(lastPanel) + " → старт "
                     + rectText(fp->m_revealFrom);
                return false;
            }
        }
        lastPanel = v;
        lastPanelAnim = anim;
        havePanel = true;
        if (fp->m_open && !fp->m_closing && (heights.isEmpty() || heights.last() != fp->m_layoutGeo.height()))
            heights << fp->m_layoutGeo.height();
        // Полоска
        const QRect sv = fs->isVisible() ? fs->visibleRect() : QRect();
        const bool sanim = fs->m_tick.isActive() || fs->m_growTick.isActive();
        if (haveStrip && sv.isValid() && lastStrip.isValid() && !sanim && !lastStripAnim && diff(sv, lastStrip) > 2) {
            *why = "рывок полоски без анимации: " + rectText(lastStrip) + " → " + rectText(sv);
            return false;
        }
        lastStrip = sv;
        lastStripAnim = sanim;
        haveStrip = true;
        return true;
    }
    // Конец действия: размер менялся в одну сторону?
    bool endAction(QString *why) const
    {
        int dir = 0, turns = 0;
        for (int i = 1; i < heights.size(); ++i) {
            const int d = heights[i] > heights[i - 1] ? 1 : -1;
            if (dir && d != dir) ++turns;
            dir = d;
        }
        if (turns > 0) {
            QStringList h;
            for (int x : heights) h << QString::number(x);
            *why = "панель дёргается за одно действие (высота " + h.join(" → ") + ")";
            return false;
        }
        return true;
    }
};

// Полоска: прокрутка в пределах, порядок как выбран у вкладки
inline bool stripOk(FilesStrip *fs, QString *why)
{
    if (fs->m_scroll < 0 || fs->m_scroll > qMax(0, fs->maxScroll())) {
        *why = QString("прокрутка полоски %1 вне 0..%2").arg(fs->m_scroll).arg(fs->maxScroll());
        return false;
    }
    if (!fs->m_pinsMode) {
        QString key;
        bool desc = true;
        AppSettings::get().folderSort(fs->viewKey(), key, desc);
        if (!isSorted(fs->m_items, key, desc)) { *why = "полоска не в том порядке (" + key + ")"; return false; }
    }
    if (fs->m_hover >= int(fs->m_items.size())) { *why = "подсветка полоски за концом"; return false; }
    // Кнопки шапки не налезают и не уходят за край
    if (fs->m_t >= 0.999) {
        QList<QPair<QString, QRect>> btns = {{"плитки", fs->viewBtnRect(0)}, {"список", fs->viewBtnRect(1)},
                                             {"поиск", fs->searchRect()}, {"вкладки", fs->tabsRow()}};
        if (!fs->m_pinsMode) btns << qMakePair(QString("сортировка"), fs->sortBtnRect());
        for (int i = 0; i < btns.size(); ++i) {
            if (!QRect(0, 0, fs->width(), fs->s(fs->HeadH)).contains(btns[i].second)) {
                *why = "кнопка полоски «" + btns[i].first + "» за краем"; return false;
            }
            for (int j = i + 1; j < btns.size(); ++j)
                if (btns[i].second.intersects(btns[j].second)) {
                    *why = "в полоске налезают «" + btns[i].first + "» и «" + btns[j].first + "»"; return false;
                }
        }
    }
    return true;
}

} // namespace TestUtil
