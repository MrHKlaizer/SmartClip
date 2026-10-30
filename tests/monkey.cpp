// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  monkey.cpp — «обезьяна»: сотни случайных действий подряд                    ║
// ║                                                                              ║
// ║  Открывает папки с полок, заходит, выходит, раскрывает дерево, меняет вид   ║
// ║  и порядок, крутит, ищет, закрывает, создаёт и удаляет файлы — в случайном  ║
// ║  порядке и со случайными паузами (часто — посреди анимации). После каждого  ║
// ║  шага проверяет правила из TestUtil.h (panelOk, panelFrameOk, stripOk).      ║
// ║                                                                              ║
// ║  Нашла баг — пишет зерно, номер шага и последние действия. Повторить ровно  ║
// ║  так же: smartclip_tests monkey <зерно> [шагов].                             ║
// ║  Ничего наружу не делает: вставка, просмотр, меню и «рука» отключены.       ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#include <QRandomGenerator>
#include <QWheelEvent>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <cstdio>
#include "Check.h"
#include "TestUtil.h"

using namespace TestUtil;

int monkeyMain(int argc, char **argv, Check::Env &env)
{
    const quint32 seed = argc > 2 ? QString::fromLocal8Bit(argv[2]).toUInt() : 1;
    const int steps = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 300;
    QRandomGenerator rnd(seed);

    // ── Папки: дерево с вложенными, пустыми, кириллицей, много и мало файлов
    const QString root = env.dir() + "/Проект", other = env.dir() + "/Other", dl = env.dir() + "/dl";
    const QStringList dirs = {"", "/src", "/src/ui", "/src/core", "/Пусто", "/assets", "/assets/icons", "/Release",
                              "/очень длинное имя папки которое не влезает в строку"};
    for (const QString &d : dirs) QDir().mkpath(root + d);
    for (int i = 0; i < 25; ++i) makeFile(root + QString("/file%1.txt").arg(i), rnd.bounded(1, 5000));
    for (int i = 0; i < 70; ++i) makeFile(root + QString("/src/ui/w%1.h").arg(i), rnd.bounded(1, 5000));
    for (int i = 0; i < 6; ++i) makeFile(root + QString("/src/core/c%1.cpp").arg(i), rnd.bounded(1, 5000));
    for (int i = 0; i < 4; ++i) makeFile(root + QString("/assets/icons/i%1.png").arg(i), 10);
    for (int i = 0; i < 3; ++i) makeFile(root + QString("/Release/v%1.exe").arg(i), 10);
    QDir().mkpath(other);
    for (int i = 0; i < 3; ++i) makeFile(other + QString("/o%1.pdf").arg(i), 10);  // Мало — встаёт в полку
    QDir().mkpath(dl);
    for (int i = 0; i < 80; ++i) makeFile(dl + QString("/d%1 файл.zip").arg(i), rnd.bounded(1, 9000));

    AppSettings::get().setFilesFolders({dl});
    AppSettings::get().setShelf(0, {root, root + "/src", root + "/assets", root + "/Release"}, {});
    AppSettings::get().setShelf(1, {other, root + "/Пусто"}, {});
    AppSettings::get().setFilesTree(rnd.bounded(2) == 1);

    App a(env);
    a.start();
    FolderPanel *fp = a.w->m_folderPanel;
    FilesStrip *fs = a.w->m_filesStrip;
    // Наружу — ничего: ни вставки в другие программы, ни окон, ни меню, ни руки
    int picks = 0;
    fp->onPickFile = [&](const QString &) { ++picks; };
    fp->onView = [&](const QString &) { ++picks; };
    fp->onFileMenu = [](const Files::Entry &, const QPoint &) {};
    fp->onFolderMenu = [](const QString &, const QPoint &) {};
    fp->onSearchFocus = []() {};
    fp->carry = nullptr;
    fs->onPick = [&](const Files::Entry &) { ++picks; };
    fs->onView = [&](const QString &) { ++picks; };
    fs->onMenu = [](const Files::Entry &, const QPoint &) {};
    fs->onTabMenu = [](const QString &, const QPoint &) {};
    fs->onAddTab = []() {};
    fs->onSearchFocus = []() {};
    fs->carry = nullptr;
    const QStringList keys = {"date", "name", "size", "type"};
    // Меню сортировки — сразу «выбираем» случайный пункт
    auto sortPick = [&](const QPoint &, const QString &, bool, const std::function<void(const QString &, bool)> &apply) {
        apply(keys[rnd.bounded(int(keys.size()))], rnd.bounded(2) == 1);
    };
    fp->onSortMenu = sortPick;
    fs->onSortMenu = sortPick;

    QStringList log;
    // Правило: ни одна папка с полок не пропала и не задвоилась
    auto shelfSet = [&]() {
        QStringList l;
        for (FolderShelf *sh : a.w->m_shelves) for (const auto &f : sh->folders()) l << f.path;
        l.sort();
        return l;
    };
    const QStringList shelvesAtStart = shelfSet();
    UxTracker ux;
    // Правило «под мышью ничего не уезжает»: что проверить после этого шага
    QString underPath;          // Раскрыли/свернули эту подпапку…
    int underY = 0, underScroll = 0;
    QPoint underBtn;            // …или нажали эту кнопку (точка на экране)
    QString underBtnName;
    QRect underGeo;
    int underMode = 0;
    int underW = 0;             // Ширина панели до клика: список не во всю ширину — тогда кнопке можно уехать
    int fsScroll = -1;          // Правило «ничего не меняется само»: прокрутка до изменения на диске
    QString fsPath;
    auto randomRow = [&](bool dirOnly) {
        QList<int> rows;
        const auto &l = fp->shown();
        for (int i = 0; i < l.size(); ++i) {
            const QRect r = fp->itemRect(i);
            if (r.bottom() < fp->bodyRect().top() || r.top() > fp->bodyRect().bottom()) continue;
            if (!dirOnly || l[i].dir) rows << i;
        }
        return rows.isEmpty() ? -1 : rows[rnd.bounded(int(rows.size()))];
    };
    auto wheel = [&](QWidget *wd, const QPoint &pos, int delta) {
        QWheelEvent we(QPointF(pos), QPointF(wd->mapToGlobal(pos)), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(wd, &we);
    };

    // Ждать, не переставая смотреть на экран (иначе анимация пройдёт «за кадром»
    // и в следующий раз покажется рывком)
    QString waitWhy;
    bool waitOk = true;
    auto sampledWait = [&](int ms) {
        QElapsedTimer wt;
        wt.start();
        while (wt.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 5);
            if (waitOk && !ux.sample(fp, fs, &waitWhy)) waitOk = false;
        }
    };
    for (int step = 0; step < steps; ++step) {
        const int act = rnd.bounded(100);
        QString what;
        ux.beginAction(fp);
        underPath.clear();
        underBtnName.clear();
        fsScroll = -1;
        if (act < 12) {  // Открыть папку с полки
            const int side = rnd.bounded(2);
            what = QString("открыть полку %1").arg(side);
            a.w->openShelfFolder(side, 0, a.w->m_shelves[side]->itemRect(0));
        } else if (act < 30 && fp->isOpen()) {  // Клик по строке/плитке (папка — зайти, файл — «вставить»)
            const int i = randomRow(rnd.bounded(3) != 0);
            if (i >= 0) {
                what = "клик по «" + fp->shown()[i].name + "»";
                click(fp, fp->itemRect(i).center());
            }
        } else if (act < 42 && fp->isOpen() && fp->treeOn()) {  // Стрелка дерева
            const int i = randomRow(true);
            if (i >= 0) {
                what = "стрелка у «" + fp->shown()[i].name + "»";
                underPath = fp->m_anim.isActive() ? QString() : fp->shown()[i].path;  // Пока едет, клики не принимаются
                underY = fp->mapToGlobal(fp->itemRect(i).topLeft()).y();
                underScroll = fp->m_scroll;
                underGeo = fp->geometry();
                underMode = int(fp->mode());
                click(fp, fp->chevronRect(i).center());
            }
        } else if (act < 48 && fp->isOpen()) {
            what = "назад";
            click(fp, fp->backRect().center());
        } else if (act < 54 && fp->isOpen()) {
            const int b = rnd.bounded(2);
            what = b ? "вид: список" : "вид: плитки";
            underBtn = fp->mapToGlobal(fp->viewRect(b).center());
            underBtnName = b ? "список" : "плитки";
            if (fp->m_anim.isActive()) underBtnName.clear();  // Пока едет, клики не принимаются — не считаем
            underW = fp->m_layoutGeo.width();
            underGeo = fp->geometry();
            underMode = int(fp->mode());
            click(fp, fp->viewRect(b).center());
        } else if (act < 58 && fp->isOpen()) {
            what = "сортировка папки";
            underBtn = fp->mapToGlobal(fp->sortRect().center());
            underBtnName = "сортировка";
            if (fp->m_anim.isActive()) underBtnName.clear();  // Пока едет, клики не принимаются — не считаем
            underW = fp->m_layoutGeo.width();
            underGeo = fp->geometry();
            underMode = int(fp->mode());
            click(fp, fp->sortRect().center());
        } else if (act < 66 && fp->isOpen()) {
            const int d = (rnd.bounded(2) ? 1 : -1) * rnd.bounded(60, 900);
            what = QString("прокрутка %1").arg(d);
            wheel(fp, fp->rect().center(), d);
        } else if (act < 70 && fp->isOpen()) {
            const QStringList qs = {"w1", "file", "png", "zzz-нет-такого", "c"};
            const QString q = qs[rnd.bounded(int(qs.size()))];
            what = "поиск «" + q + "»";
            click(fp, fp->searchRect().center());
            fp->m_searchEdit->setText(q);
        } else if (act < 76) {
            what = "Esc";
            if (!fs->closeSearch()) fp->escape();
        } else if (act < 79 && fp->isOpen()) {
            what = "закрыть";
            click(fp, fp->closeRect().center());
        } else if (act < 85) {  // Полоска: раскрыть, вкладка, вид, порядок, прокрутка
            const int k = rnd.bounded(5);
            fs->expand(true);
            if (k == 0) { what = "полоска: вкладка"; fs->m_tabHistory = rnd.bounded(2) ? dl : QString(); fs->applyFilter(); }
            else if (k == 1) { what = "полоска: вид"; sampledWait(450); click(fs, fs->viewBtnRect(rnd.bounded(2)).center() + QPoint(0, fs->top())); }
            else if (k == 2) { what = "полоска: сортировка"; sampledWait(450); click(fs, fs->sortBtnRect().center() + QPoint(0, fs->top())); }
            else if (k == 3) { what = "полоска: прокрутка"; wheel(fs, QPoint(fs->width() / 2, fs->height() - 20), -rnd.bounded(100, 900)); }
            else { what = "полоска: свернуть"; fs->expand(false); }
        } else if (act < 89) {  // На диске что-то поменялось
            const QString d = fp->isOpen() ? fp->path() : root;
            if (fp->isOpen() && !fp->m_anim.isActive() && !fp->m_loading) { fsScroll = fp->m_scroll; fsPath = fp->path(); }
            if (rnd.bounded(2)) {
                const QString f = d + QString("/new%1.txt").arg(step);
                what = "создать " + QFileInfo(f).fileName();
                makeFile(f, rnd.bounded(1, 100));
            } else {
                const QStringList files = QDir(d).entryList(QDir::Files);
                if (!files.isEmpty()) {
                    const QString f = files[rnd.bounded(int(files.size()))];
                    what = "удалить " + f;
                    QFile::remove(d + "/" + f);
                }
            }
        } else if (act < 94) {  // Перетащить папку на полке (на своей или на другую, иногда — Esc)
            FolderShelf *sh = a.w->m_shelves[rnd.bounded(2)];
            if (!sh->folders().isEmpty()) {
                const int i = rnd.bounded(int(sh->folders().size()));
                FolderShelf *dst = a.w->m_shelves[rnd.bounded(2)];
                const QPoint start = sh->itemRect(i).center() - QPoint(sh->m_scroll, 0);
                const QPoint endG = dst->mapToGlobal(QPoint(rnd.bounded(qMax(1, dst->width())), dst->height() / 2));
                const QPoint end = sh->mapFromGlobal(endG);
                what = QString("тащить папку %1 → %2").arg(i).arg(dst == sh ? "своя полка" : "другая полка");
                auto send = [&](QEvent::Type t, const QPoint &pos, Qt::MouseButtons b) {
                    QMouseEvent e(t, pos, sh->mapToGlobal(pos), Qt::LeftButton, b, Qt::NoModifier);
                    QApplication::sendEvent(sh, &e);
                };
                send(QEvent::MouseButtonPress, start, Qt::LeftButton);
                const int n = rnd.bounded(2, 12);
                for (int k = 1; k <= n; ++k) { send(QEvent::MouseMove, start + (end - start) * k / n, Qt::LeftButton); sampledWait(rnd.bounded(0, 15)); }
                if (rnd.bounded(6) == 0) { what += " и Esc"; sh->cancelDrag(); }
                send(QEvent::MouseButtonRelease, end, Qt::NoButton);
            }
        } else if (act < 96) {
            const bool on = !AppSettings::get().filesTree();
            what = on ? "дерево: вкл" : "дерево: выкл";
            AppSettings::get().setFilesTree(on);
            fp->settingsChanged();
        } else {  // Мышь просто водит
            const QPoint pos(rnd.bounded(qMax(1, fp->width())), rnd.bounded(qMax(1, fp->height())));
            what = "мышь";
            QMouseEvent mv(QEvent::MouseMove, pos, fp->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(fp, &mv);
        }
        if (what.isEmpty()) continue;
        log << QString("%1: %2").arg(step).arg(what);

        // Пауза — чаще короткая: посреди анимации и загрузки и ловятся баги
        static const int pauses[] = {0, 5, 15, 40, 90, 200, 450};
        const int pause = pauses[rnd.bounded(7)];
        QString why;
        bool ok = waitOk;
        if (!waitOk) why = waitWhy;
        QElapsedTimer t;
        t.start();
        // Проверки «под мышью» и «само не меняется» — когда всё встанет
        const bool needSettle = !underPath.isEmpty() || !underBtnName.isEmpty() || fsScroll >= 0;
        do {
            QApplication::processEvents(QEventLoop::AllEvents, 5);
            if (!panelFrameOk(fp, &why) || !ux.sample(fp, fs, &why)) { ok = false; break; }
        } while (t.elapsed() < pause || (needSettle && a.busy() && t.elapsed() < 3000));
        if (ok && needSettle) {
            Check::Env::wait(400);  // Фоновая загрузка подпапки / перечитывание после изменения на диске
            while (a.busy() && t.elapsed() < 4000) {
                QApplication::processEvents(QEventLoop::AllEvents, 5);
                if (!ux.sample(fp, fs, &why)) { ok = false; break; }
            }
        }
        if (ok && !underPath.isEmpty() && fp->isOpen()) {
            int i = -1;
            for (int k = 0; k < fp->shown().size(); ++k) if (fp->shown()[k].path == underPath) { i = k; break; }
            if (i >= 0 && fp->m_scroll == underScroll && fp->mapToGlobal(fp->itemRect(i).topLeft()).y() != underY) {
                why = QString("строка «%1» уехала из-под мыши: %2 → %3 (панель %4 режим %5 → %6 режим %7, держим=%8)")
                          .arg(QFileInfo(underPath).fileName()).arg(underY).arg(fp->mapToGlobal(fp->itemRect(i).topLeft()).y())
                          .arg(rectText(underGeo)).arg(underMode).arg(rectText(fp->geometry())).arg(int(fp->mode()))
                          .arg(int(fp->m_holdH));
                ok = false;
            }
        }
        if (ok && !underBtnName.isEmpty() && fp->isOpen() && fp->mode() != FolderPanel::Inline
            && fp->m_layoutGeo.width() == underW
            && underGeo.height() >= fp->min2Rows() - 1) {  // Было ниже минимума нового вида — расти вверх приходится
            const QRect now = underBtnName == "сортировка" ? fp->sortRect() : fp->viewRect(underBtnName == "список" ? 1 : 0);
            if (!now.contains(fp->mapFromGlobal(underBtn))) {
                why = QString("кнопка «%1» уехала из-под мыши (панель %2 режим %3 → %4 режим %5, держим=%6)")
                          .arg(underBtnName, rectText(underGeo)).arg(underMode).arg(rectText(fp->geometry()))
                          .arg(int(fp->mode())).arg(int(fp->m_holdH));
                ok = false;
            }
        }
        if (ok && fsScroll >= 0 && fp->isOpen() && fp->path() == fsPath && fp->m_scroll != fsScroll
            && fp->m_scroll != fp->maxScroll()) {
            why = QString("прокрутка сама сменилась после изменения на диске: %1 → %2").arg(fsScroll).arg(fp->m_scroll);
            ok = false;
        }
        if (ok && !ux.endAction(&why)) ok = false;
        // Ни одного окна теста на экране пользователя
        QString shown;
        if (ok && Check::visibleOwnWindows(&shown)) { why = "окно теста видно на экране: " + shown; ok = false; }
        if (ok && shelfSet() != shelvesAtStart) { why = "папки на полках пропали или задвоились: " + shelfSet().join(", "); ok = false; }
        if (ok) for (FolderShelf *sh : a.w->m_shelves)
            if (sh->dragging()) { why = "папка на полке осталась «поднятой» после отпускания"; ok = false; }
        // Каждые 8 шагов — дождаться, пока всё встанет, и проверить всё
        if (ok && (step % 8 == 7 || step == steps - 1)) {
            QElapsedTimer st;
            st.start();
            while (a.busy() && st.elapsed() < 3000) {
                QApplication::processEvents(QEventLoop::AllEvents, 5);
                if (!ux.sample(fp, fs, &why)) { ok = false; break; }
            }
            Check::Env::wait(30);
            if (!panelOk(fp, &why) || !stripOk(fs, &why)) ok = false;
        } else if (ok && !a.busy()) {
            if (!panelOk(fp, &why) || !stripOk(fs, &why)) ok = false;
        }
        if (!ok) {
            std::fprintf(stderr, "MONKEY seed=%u шаг=%d: %s\n", seed, step, qUtf8Printable(why));
            std::fprintf(stderr, "MONKEY повторить: smartclip_tests monkey %u %d\n", seed, step + 1);
            for (int i = qMax(0, int(log.size()) - 12); i < log.size(); ++i)
                std::fprintf(stderr, "    %s\n", qUtf8Printable(log[i]));
            CHECK(false, "обезьяна, зерно %1, шаг %2: %3", seed, step, why);
            return 1;
        }
    }
    std::fprintf(stderr, "monkey seed=%u: %d шагов, кликов по файлам %d — всё в порядке\n", seed, steps, picks);
    CHECK(true);
    return 0;
}
