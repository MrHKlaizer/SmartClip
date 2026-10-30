// Тестовый запуск интерфейса SmartClip — без касания настоящих данных
// пользователя (настройки и БД — во временной папке).
//   uitest <name> [pins]   — снимок окна в PNG (без показа на экране)
//   uitest anim <closeMs>  — открыть окно на экране, закрыть через closeMs и
//                            вывести яркость участка первой карточки по времени
#include <QApplication>
#include "Check.h"
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QScreen>
#include <QPushButton>
#include <QDialog>
#include <QTabWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QImage>
#include <QWheelEvent>
#include <QLineEdit>
#include <QComboBox>
#include <QMenu>
#include <windows.h>
#include <shellapi.h>
#include <QStackedWidget>
#define private public
#include "ui/ImageViewer.h"
#include <QAbstractButton>
#include <algorithm>
#include <QEnterEvent>
#include "ui/ClickableCard.h"
#include <QSqlQuery>
#include "ui/MediaCards.h"
#include "ui/VideoViewer.h"
#include "ui/FilesStrip.h"
#include "ui/FolderShelf.h"
#include "ui/FolderPanel.h"
#include "ui/CardArc.h"
#include "ui/DragSqueeze.h"
#include "ui/SettingsDialog.h"
#include <cmath>
#include <cstdio>
#include "core/Database.h"
#include "core/AppSettings.h"
#include "ui/MainWindow.h"
#include "ui/NavTabs.h"
#include "ui/PhotoWindow.h"
#undef private

// Точные отметки времени: кадры сцены анимации и скрытие окна
struct Spy : QObject {
    QElapsedTimer *t; QWidget *win; int paints = 0;
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::Paint && dynamic_cast<Anim::SnapshotOverlay*>(o) && paints < 3)
            std::fprintf(stderr, "  [%lld] overlay paint #%d\n", (long long)t->elapsed(), ++paints);
        if (e->type() == QEvent::Hide && o == win)
            std::fprintf(stderr, "  [%lld] window HIDDEN\n", (long long)t->elapsed());
        return false;
    }
};

int scenarioMain(int argc, char **argv)
{
    setvbuf(stderr, nullptr, _IONBF, 0);  // Вывод сразу, даже если тест зависнет
    QTemporaryDir tmp;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path() + "/ini");
    QApplication app(argc, argv);
    app.setOrganizationName("SmartClipUiTest");
    app.setApplicationName(QString("SmartClipUiTest_%1").arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    // Окна сценариев — тоже не на экран (кроме тех, что нарочно показывают: anim, click…)
    if (!QStringList({"anim", "target", "click", "noact", "idlecpu"}).contains(QString(argv[1] ? argv[1] : "")))
        Check::keepOffScreen();
    QDir().mkpath(tmp.path() + "/downloads");  // Не настоящие «Загрузки» пользователя
    AppSettings::get().setFilesFolders({tmp.path() + "/downloads"});
    // Своя база у каждого запуска — и удаляется в конце (раньше копилась)
    struct DataCleanup {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        ~DataCleanup() { Check::removeTestData(dir); }
    } dataCleanup;

    const QString out = argc > 1 ? argv[1] : "shot";
    const bool pins = argc > 2 && QString(argv[2]) == "pins";

    if (out == "target") {
        QWidget t; t.setWindowTitle("SC_FocusTarget"); t.resize(400, 200);
        auto *le = new QLineEdit(&t); le->setGeometry(20, 20, 300, 30);
        t.show();
        INPUT in[2] = {}; in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = 0x88; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        SetForegroundWindow(HWND(t.winId()));
        t.activateWindow(); le->setFocus();
        struct Log : QObject {
            bool eventFilter(QObject *o, QEvent *e) override {
                if (e->type() == QEvent::FocusOut || e->type() == QEvent::WindowDeactivate
                    || e->type() == QEvent::FocusIn || e->type() == QEvent::WindowActivate)
                    std::fprintf(stderr, "TARGET %s %d\n", o->metaObject()->className(), int(e->type()));
                return false;
            }
        } log;
        le->installEventFilter(&log); t.installEventFilter(&log);
        QTimer::singleShot(15000, &app, &QApplication::quit);
        return app.exec();
    }
    Database db;
    db.init();
    db.addHistory("text", "Ich bin gespannt, wie der Test ablaufen wird.");
    db.addHistory("text", "Darf ich etwas essen?");
    db.addFolder("Работа");
    db.addPin("", "Пример", "text", "Текст закрепа", "");

    MainWindow w(&db);
    if (out == "anim") {
        for (int i = 0; i < 8; ++i)
            db.addHistory("text", QString("Пример текста номер %1 для проверки анимации").arg(i));
        const int closeAt = argc > 2 ? QString(argv[2]).toInt() : 1000;
        QElapsedTimer T;
        QList<QPair<qint64, double>> series;
        QTimer sampler;
        sampler.setTimerType(Qt::PreciseTimer);
        QObject::connect(&sampler, &QTimer::timeout, [&]() {
            // Состояние: окно видно? его прозрачность? есть ли сцена анимации и куда играет?
            int ovIn = 0, ovOut = 0;
            for (QWidget *tw : QApplication::topLevelWidgets())
                if (auto *ov = dynamic_cast<Anim::SnapshotOverlay*>(tw))
                    if (ov->isVisible() && ov->isPlaying()) (ov->isReverse() ? ovOut : ovIn)++;
            std::fprintf(stderr, "%4lld win=%d op=%.2f in=%d out=%d\n", (long long)T.elapsed(),
                        w.isVisible(), w.windowOpacity(), ovIn, ovOut);
            if (T.elapsed() > closeAt + 450) app.quit();
        });
        Spy spy; spy.t = &T; spy.win = &w;
        app.installEventFilter(&spy);
        QTimer::singleShot(closeAt - 5, [&]() { spy.paints = 0; });
        w.showWindow();
        T.start();
        sampler.start(5);
        QTimer::singleShot(closeAt, [&]() {
            QElapsedTimer g; g.start(); QPixmap px = w.grab();
            std::fprintf(stderr, "--- grab of %dx%d took %lld ms\n", px.width(), px.height(), (long long)g.elapsed());
            std::fprintf(stderr, "--- close at %lld\n", (long long)T.elapsed());
            w.hideAnimated();
        });
        app.exec();
        for (auto &p : series) std::fprintf(stderr, "%4lld %6.1f\n", (long long)p.first, p.second);
        return 0;
    }

    if (out == "paging") {
        // Подгрузка порциями: 150 текстов и 50 картинок, листаем до конца колонки
        AppSettings::get().setMaxTexts(1000);
        AppSettings::get().setMaxImages(1000);
        for (int i = 0; i < 150; ++i)
            db.addHistory("text", QString("Текст %1").arg(i));
        QImage im(8, 8, QImage::Format_ARGB32); im.fill(Qt::blue);
        for (int i = 0; i < 50; ++i) {
            const QString f = db.imagesPath() + QString("/img_p%1.png").arg(i);
            im.save(f);
            db.addHistory("image", "", f, "app");
        }
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        for (int i = 0; i < 30; ++i) app.processEvents();
        auto *L = w.findChild<QScrollArea*>("leftScroll");
        auto *R = w.findChild<QScrollArea*>("rightScroll");
        auto count = [](QScrollArea *a) {
            int n = 0;
            for (QWidget *c : a->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                if (c->property("itemId").isValid()) ++n;
            return n;
        };
        auto firstLast = [](QScrollArea *a) {
            QStringList t;
            for (QWidget *c : a->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                if (c->property("itemId").isValid()) t << c->property("itemId").toString();
            return t.isEmpty() ? QString("-") : t.first() + ".." + t.last()
                   + (t.removeDuplicates() ? " DUPS!" : "");
        };
        std::fprintf(stderr, "start: texts=%d images=%d\n", count(L), count(R));
        for (int step = 1; step <= 5; ++step) {
            for (QScrollArea *a : {L, R}) a->verticalScrollBar()->setValue(a->verticalScrollBar()->maximum());
            for (int i = 0; i < 30; ++i) app.processEvents();
            std::fprintf(stderr, "scroll %d: texts=%d [%s] images=%d [%s]\n", step,
                         count(L), qPrintable(firstLast(L)), count(R), qPrintable(firstLast(R)));
        }
        return 0;
    }
    if (out == "click") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        HWND target = nullptr;
        for (int i = 0; i < 50 && !target; ++i) { wait(100); target = FindWindowW(nullptr, L"SC_FocusTarget"); }
        auto fgIsTarget = [&]() { return GetForegroundWindow() == target; };
        std::fprintf(stderr, "start fgIsTarget=%d\n", fgIsTarget());
        w.showWindow();
        wait(900);
        std::fprintf(stderr, "shown fgIsTarget=%d\n", fgIsTarget());
        // Реальный клик в пустое место левой колонки (не по карточке — без вставки)
        auto *area = w.findChild<QScrollArea*>("leftScroll");
        const QPoint g = area->viewport()->mapToGlobal(QPoint(area->viewport()->width() / 2,
                                                              area->viewport()->height() - 40));
        POINT old; GetCursorPos(&old);
        SetCursorPos(g.x() * area->devicePixelRatio(), g.y() * area->devicePixelRatio());
        INPUT in[2] = {};
        in[0].type = in[1].type = INPUT_MOUSE;
        in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        std::fprintf(stderr, "--- click\n");
        SendInput(2, in, sizeof(INPUT));
        wait(600);
        SetCursorPos(old.x, old.y);
        std::fprintf(stderr, "after click fgIsTarget=%d open=%d\n", fgIsTarget(), w.isOpen());
        w.hide();
        wait(300);
        std::fprintf(stderr, "after hide fgIsTarget=%d\n", fgIsTarget());
        PostMessageW(target, WM_CLOSE, 0, 0);
        return 0;
    }
    if (out == "noact") {
        auto fgName = []() {
            wchar_t buf[128] = {}; HWND f = GetForegroundWindow();
            GetWindowTextW(f, buf, 128);
            DWORD pid = 0; GetWindowThreadProcessId(f, &pid);
            return QString::fromWCharArray(buf) + (pid == GetCurrentProcessId() ? " [OURS]" : "");
        };
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        HWND target = nullptr;
        for (int i = 0; i < 50 && !target; ++i) { wait(100); target = FindWindowW(nullptr, L"SC_FocusTarget"); }
        std::fprintf(stderr, "target found=%d fg='%s'\n", target != nullptr, qPrintable(fgName()));
        w.showWindow();
        wait(900);
        std::fprintf(stderr, "after show: visible=%d fg='%s'\n", w.isVisible(), qPrintable(fgName()));
        // Клавиша, пока мы без фокуса → забираем фокус, буква уходит в поиск
        
        w.onHookKey('A', MapVirtualKeyW('A', MAPVK_VK_TO_VSC), false);
        wait(400);
        std::fprintf(stderr, "after key: fg='%s' search='%s' open=%d\n", qPrintable(fgName()),
                     qPrintable(qobject_cast<QLineEdit*>(QApplication::focusWidget()) ? qobject_cast<QLineEdit*>(QApplication::focusWidget())->text().toUtf8().toHex() : QString("no-focus-edit")), w.isOpen());
        // Меню поверх неактивного окна
        QMenu m(&w); m.addAction("x");
        m.popup(QPoint(200, 200));
        wait(400);
        std::fprintf(stderr, "menu visible=%d fg='%s'\n", m.isVisible(), qPrintable(fgName()));
        m.close(); wait(200);
        // Фокус вернулся в программу пользователя — окно остаётся
        SetForegroundWindow(target);
        wait(400);
        std::fprintf(stderr, "fg back to target: fg='%s' open=%d\n", qPrintable(fgName()), w.isOpen());
        // Переключились в другое окно (рабочий стол) — окно закрывается
        w.onForegroundChanged(reinterpret_cast<quintptr>(GetShellWindow()));
        wait(800);
        std::fprintf(stderr, "other fg: open=%d visible=%d\n", w.isOpen(), w.isVisible());
        PostMessageW(target, WM_CLOSE, 0, 0);
        return 0;
    }
    if (out == "viewperf") {
        // Частота кадров анимации просмотра на настоящем экране (большой скриншот 4K)
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        QImage im(3840, 2160, QImage::Format_RGB32); im.fill(QColor(70, 140, 220));
        { QPainter pp(&im); for (int i = 0; i < 40; ++i) pp.fillRect(i * 90, i * 50, 300, 200, QColor(240, 200 - i * 4, 60)); }
        const QString f = db.imagesPath() + "/img_perf.png";
        im.save(f);
        db.addHistory("image", "", f, "app");
        w.showWindow();
        wait(1500);
        QWidget *card = nullptr;
        for (QWidget *c : w.findChildren<QWidget*>())
            if (c->property("filepath").toString() == f) { card = c; break; }
        auto *viewer = w.findChild<ImageViewer*>();
        struct PaintSpy : QObject {
            QElapsedTimer t; QList<qint64> ts;
            bool eventFilter(QObject *, QEvent *e) override {
                if (e->type() == QEvent::Paint) ts << t.nsecsElapsed() / 1000;
                return false;
            }
        } spy;
        viewer->installEventFilter(&spy);
        auto report = [&](const char *what) {
            QList<qint64> d;
            for (int i = 1; i < spy.ts.size(); ++i) d << (spy.ts[i] - spy.ts[i - 1]);
            qint64 mx = 0, sum = 0; for (auto x : d) { mx = qMax(mx, x); sum += x; }
            std::fprintf(stderr, "%s: frames=%d avg=%.1fms max=%.1fms\n", what, int(spy.ts.size()),
                         d.isEmpty() ? 0.0 : sum / 1000.0 / d.size(), mx / 1000.0);
        };
        spy.t.start(); spy.ts.clear();
        viewer->showImage(f, card);
        wait(500);
        report("open");
        spy.t.restart(); spy.ts.clear();
        viewer->closeAnimated();
        wait(400);
        report("close");
        w.hide();
        return 0;
    }
    if (out == "viewer") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        QImage im(800, 500, QImage::Format_ARGB32); im.fill(QColor(70, 140, 220));
        { QPainter pp(&im); pp.fillRect(100, 100, 600, 300, QColor(240, 200, 60)); }
        const QString f = db.imagesPath() + "/img_view.png";
        im.save(f);
        db.addHistory("image", "", f, "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1200);
        QWidget *card = nullptr;
        for (QWidget *c : w.findChildren<QWidget*>())
            if (c->property("filepath").toString() == f) { card = c; break; }
        auto *viewer = w.findChild<ImageViewer*>();
        std::fprintf(stderr, "card=%d viewer=%d\n", card != nullptr, viewer != nullptr);
        QElapsedTimer T; T.start();
        viewer->showImage(f, card);
        QList<QPair<int, QString>> shots = {{60, "v_open1"}, {150, "v_open2"}, {450, "v_open3"}};
        for (auto &sh : shots) { while (T.elapsed() < sh.first) app.processEvents(); [&]{ QPixmap bg = w.grab(); QPainter pp(&bg); pp.drawPixmap(0, 0, viewer->grab()); pp.end(); return bg; }().scaled(800, 450).save(sh.second + ".png"); }
        T.restart();
        viewer->closeAnimated();
        while (T.elapsed() < 80) app.processEvents();
        [&]{ QPixmap bg = w.grab(); QPainter pp(&bg); pp.drawPixmap(0, 0, viewer->grab()); pp.end(); return bg; }().scaled(800, 450).save("v_close1.png");
        while (T.elapsed() < 400) app.processEvents();
        std::fprintf(stderr, "after close visible=%d\n", viewer->isVisible());
        auto shot = [&](const char *name) {
            QPixmap bg = w.grab(); QPainter pp(&bg); pp.drawPixmap(0, 0, viewer->grab()); pp.end();
            bg.scaled(800, 450).save(name);
        };
        // Зум колёсиком: 3 щелчка вверх в точке 30%/30% экрана
        viewer->showImage(f, card); wait(700);
        QPointF at(viewer->width() * 0.3, viewer->height() * 0.3);
        for (int i = 0; i < 3; ++i) {
            QWheelEvent we(at, viewer->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton,
                           Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(viewer, &we);
        }
        wait(60);  shot("v_zoom_mid.png");
        wait(600); shot("v_zoom_end.png");
        return 0;
    }
    if (out == "arcperf") {
        // Прокрутка на настоящем экране: интервал между кадрами (дуга / плавная)
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        for (int i = 0; i < 40; ++i)
            db.addHistory("text", QString("Карточка номер %1 — проверка дуги и скорости прокрутки").arg(i));
        if (argc > 2) AppSettings::get().setScrollStyle(argv[2]);
        w.showWindow();
        wait(1500);
        auto *area = w.findChild<QScrollArea*>("leftScroll");
        if (qEnvironmentVariableIsSet("NOSHADOW"))
            for (QWidget *c : area->widget()->findChildren<QWidget*>()) c->setGraphicsEffect(nullptr);
        struct PaintSpy : QObject {
            QElapsedTimer t; QList<qint64> ts;
            bool eventFilter(QObject *, QEvent *e) override {
                if (e->type() == QEvent::Paint) { qint64 n = t.nsecsElapsed() / 1000; if (ts.isEmpty() || n - ts.last() > 500) ts << n; }
                return false;
            }
        } spy;
        area->viewport()->installEventFilter(&spy);
        spy.t.start();
        QPointF pt(area->viewport()->width() / 2.0, area->viewport()->height() / 2.0);
        for (int k = 0; k < 6; ++k) {
            QWheelEvent we(pt, area->viewport()->mapToGlobal(pt), QPoint(), QPoint(0, -120), Qt::NoButton,
                           Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(area->viewport(), &we);
            wait(70);
        }
        wait(700);
        QList<qint64> d;
        for (int i = 1; i < spy.ts.size(); ++i) d << (spy.ts[i] - spy.ts[i - 1]);
        std::sort(d.begin(), d.end());
        qint64 sum = 0; for (auto x : d) sum += x;
        std::fprintf(stderr, "%s: frames=%d avg=%.1fms p95=%.1fms max=%.1fms\n", argc > 2 ? argv[2] : "?",
                     int(spy.ts.size()), d.isEmpty() ? 0.0 : sum / 1000.0 / d.size(),
                     d.isEmpty() ? 0.0 : d[int(d.size() * 0.95)] / 1000.0, d.isEmpty() ? 0.0 : d.last() / 1000.0);
        w.hide();
        return 0;
    }
    if (out == "pinslag") {
        // Долго в Закрепах, потом обратно в Историю / закрыть: где замирает окно
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        QImage img(1920, 1080, QImage::Format_RGB32);
        for (int i = 0; i < 150; ++i) {
            img.fill(QColor::fromHsv(i * 7 % 360, 150, 200));
            const QString f = db.imagesPath() + QString("/lag%1.png").arg(i);
            img.save(f);
            db.addHistory("image", "", f, "app");
        }
        for (int i = 0; i < 3000; ++i)
            db.addHistory("text", QString("Текст номер %1 — немного содержимого, чтобы карточка была как настоящая. ").arg(i).repeated(1 + i % 4), "", "app");
        for (int i = 0; i < 40; ++i) db.addPin("", QString("pin %1").arg(i), "text", QString("pinned text %1").arg(i), "");

        QElapsedTimer clock; clock.start();
        qint64 last = 0, worst = 0;
        QTimer probe; probe.setTimerType(Qt::PreciseTimer);
        QObject::connect(&probe, &QTimer::timeout, [&]() { const qint64 n = clock.elapsed(); worst = qMax(worst, n - last); last = n; });
        probe.start(5);
        auto measure = [&](const char *what, std::function<void()> f, int settle) {
            last = clock.elapsed(); worst = 0;
            QElapsedTimer t; t.start();
            f();
            const qint64 sync = t.elapsed();
            wait(settle);
            std::fprintf(stderr, "%-34s sync %5lld ms, worst freeze %5lld ms\n", what, (long long)sync, (long long)worst);
        };
        auto click = [&](const char *txt) {
            for (auto *b : w.findChildren<QPushButton*>())
                if (b->text().contains(QString::fromUtf8(txt))) { b->click(); return; }
        };
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        measure("open (history)", [&]() { w.showWindow(); }, 3000);
        // Прокрутили историю далеко вниз (подгрузилось много карточек)
        auto *L = w.findChild<QScrollArea*>("leftScroll");
        for (int k = 0; k < 40; ++k) { L->verticalScrollBar()->setValue(L->verticalScrollBar()->maximum()); wait(60); }
        std::fprintf(stderr, "history cards loaded: %d\n", int(L->widget()->findChildren<QLabel*>(Qt::FindDirectChildrenOnly).size()));
        measure("switch to pins", [&]() { click("Закрепы"); }, 1500);
        // Пока в Закрепах — копируют много нового
        for (int i = 0; i < 200; ++i) { db.addHistory("text", QString("new while in pins %1").arg(i), "", "app"); }
        w.markHistoryDirty();
        measure("hide in pins", [&]() { w.hide(); }, 800);
        measure("open again (pins)", [&]() { w.showWindow(); }, 2000);
        measure("switch back to history", [&]() { click("История"); }, 4000);
        measure("hide in history", [&]() { w.hide(); }, 800);
        return 0;
    }
    if (out == "pinfiles") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        const QString dir = tmp.path() + "/downloads";
        QDir().mkpath(dir);
        for (const char *n : {"report.pdf", "notes.txt", "archive.zip", "table.xlsx"}) {
            QFile f(dir + "/" + n); f.open(QIODevice::WriteOnly); f.write(QByteArray(1500, 'x'));
        }
        QImage img(320, 200, QImage::Format_RGB32); img.fill(QColor(40, 140, 90)); img.save(dir + "/photo.png");
        AppSettings::get().setFilesFolders({dir});
        db.addFolder("Работа");
        db.addPin("", "Отчёт", "file", "", dir + "/report.pdf");
        db.addPin("", "", "file", "", dir + "/archive.zip");
        db.addPin("Работа", "", "file", "", dir + "/table.xlsx");
        db.addPin("", "", "file", "", dir + "/gone.docx");  // Файла нет
        db.addPin("", "Текстовый закреп", "text", "hello", "");
        for (int i = 0; i < 12; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1500);
        FilesStrip *fs = nullptr;
        for (QObject *o : w.children()) if (auto *x = dynamic_cast<FilesStrip *>(o)) fs = x;
        if (!fs) return 1;
        std::fprintf(stderr, "history: pinsMode=%d visible=%d\n", fs->pinsMode(), fs->isVisible());
        for (auto *b : w.findChildren<QPushButton*>()) if (b->text().contains("Закрепы")) { b->click(); break; }
        wait(1200);
        std::fprintf(stderr, "pins: pinsMode=%d visible=%d\n", fs->pinsMode(), fs->isVisible());
        const QPointF ep(20, fs->height() - 10);
        QEnterEvent en(ep, ep, fs->mapToGlobal(ep));
        QApplication::sendEvent(fs, &en);
        wait(700);
        w.grab().save("pinfiles_0.png");
        // Понесли файл: сцена «прижались к краям»
        w.startFileDragSqueeze();
        wait(150);
        DragSqueeze *sq = nullptr;
        for (QWidget *t : QApplication::topLevelWidgets()) if (auto *x = dynamic_cast<DragSqueeze *>(t)) sq = x;
        std::fprintf(stderr, "squeeze=%d opacity=%.2f\n", sq != nullptr, w.windowOpacity());
        if (sq) sq->grab().save("pinfiles_1.png");
        wait(500);
        if (sq) sq->grab().save("pinfiles_2.png");
        w.endFileDragSqueeze();
        wait(150);
        if (sq) sq->grab().save("pinfiles_3.png");
        wait(700);
        std::fprintf(stderr, "after release: opacity=%.2f squeezeAlive=%d\n", w.windowOpacity(),
                     [&]() { for (QWidget *t : QApplication::topLevelWidgets()) if (dynamic_cast<DragSqueeze *>(t) && t->isVisible()) return 1; return 0; }());
        for (auto *b : w.findChildren<QPushButton*>()) if (b->text().contains("История")) { b->click(); break; }
        wait(1500);
        std::fprintf(stderr, "back to history: pinsMode=%d visible=%d\n", fs->pinsMode(), fs->isVisible());
        return 0;
    }
    if (out == "carddrag") {
        // Перетаскивание закрепов много раз подряд — не должно падать
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        for (int i = 0; i < 15; ++i) db.addPin("", QString("pin %1").arg(i), "text", QString("text %1").arg(i), "");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        for (auto *b : w.findChildren<QPushButton*>()) if (b->text().contains("Закрепы")) { b->click(); break; }
        wait(800);
        auto *L = w.findChild<QScrollArea*>("leftScroll");
        for (int round = 0; round < 25; ++round) {
            QList<ClickableCard*> cards;
            for (auto *c : L->widget()->findChildren<ClickableCard*>(Qt::FindDirectChildrenOnly)) cards << c;
            if (cards.size() < 3) { std::fprintf(stderr, "no cards\n"); return 1; }
            ClickableCard *c = cards[round % 3];
            const QPoint g = c->mapToGlobal(QPoint(40, 20));
            emit c->dragStarted(g);
            for (int k = 1; k <= 10; ++k) { emit c->dragMoved(g + QPoint(0, k * 18)); wait(16); }
            emit c->dragFinished();
            wait(round % 2 ? 120 : 30);  // Следующее перетаскивание — пока карточки ещё доезжают
        }
        wait(800);
        std::fprintf(stderr, "carddrag ok\n");
        return 0;
    }
    if (out == "searchtop") {
        // Поиск наверху: три равные части по ширине карточек, длинный текст — шире
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        for (int i = 0; i < 4; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1500);
        QWidget *sb = w.m_searchEdit->parentWidget();
        auto *L = w.findChild<QScrollArea*>("leftScroll");
        QWidget *card = nullptr;
        for (auto *c : L->widget()->findChildren<ClickableCard*>()) if (c->isVisible()) { card = c; break; }
        const QRect cr = card ? QRect(card->mapTo(&w, QPoint(0, 0)), card->size()) : QRect();
        auto geo = [&](QWidget *x) { return QRect(x->mapTo(&w, QPoint(0, 0)), x->size()); };
        std::fprintf(stderr, "card x=%d..%d\n", cr.left(), cr.right());
        std::fprintf(stderr, "history %d..%d  pins %d..%d  search %d..%d  h=%d/%d\n",
            geo(w.m_historyBtn).left(), geo(w.m_historyBtn).right(), geo(w.m_pinsBtn).left(), geo(w.m_pinsBtn).right(),
            geo(sb).left(), geo(sb).right(), w.m_historyBtn->height(), sb->height());
        std::fprintf(stderr, "panel bottom=%d window h=%d leftScroll bottom=%d\n",
            geo(w.m_topPanel).bottom(), w.height(), geo(L).bottom());
        w.grab().save("searchtop_0.png");
        w.m_searchEdit->setText("history");
        wait(400);
        std::fprintf(stderr, "short text width=%d\n", sb->width());
        w.m_searchEdit->setText("a very long search query that does not fit into the small field at all");
        wait(400);
        std::fprintf(stderr, "long text width=%d right=%d profiles x=%d\n", sb->width(), geo(sb).right(), geo(w.m_profilesBtn).left());
        w.grab().save("searchtop_1.png");
        w.m_searchEdit->setText(QString(400, 'x'));
        wait(300);
        std::fprintf(stderr, "huge text right=%d profiles x=%d\n", geo(sb).right(), geo(w.m_profilesBtn).left());
        w.m_searchEdit->clear();
        wait(300);
        std::fprintf(stderr, "cleared width=%d\n", sb->width());
        w.switchView(MainWindow::ViewMode::Pins);
        wait(1000);
        std::fprintf(stderr, "pins: search %d..%d\n", geo(sb).left(), geo(sb).right());
        w.grab().save("searchtop_2.png");
        return 0;
    }
    if (out == "stripwide") {
        // Полоска файлов во всю ширину середины + поиск в шапке
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        QDir().mkpath(tmp.path() + "/dl");
        for (int i = 0; i < 40; ++i) {
            QFile f(tmp.path() + QString("/dl/file_%1%2.txt").arg(i).arg(i % 3 == 0 ? "_report" : ""));
            f.open(QIODevice::WriteOnly); f.write("x");
        }
        AppSettings::get().setFilesFolders({tmp.path() + "/dl"});
        for (int i = 0; i < 4; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.showWindow();
        wait(1500);
        FilesStrip *fs = w.m_filesStrip;
        auto geo = [&](QWidget *x) { return QRect(x->mapTo(&w, QPoint(0, 0)), x->size()); };
        const QRect L = geo(w.m_leftScroll), R = geo(w.m_rightScroll), S = geo(fs);
        std::fprintf(stderr, "left col right=%d  strip %d..%d  right col left=%d  visible=%d items=%d\n",
                     L.right(), S.left(), S.right(), R.left(), fs->isVisible(), int(fs->m_items.size()));
        fs->expand(true); wait(800);
        std::fprintf(stderr, "expanded t=%.2f search btn=%d,%d %dx%d\n", fs->m_t, fs->searchRect().x(), fs->searchRect().y(),
                     fs->searchRect().width(), fs->searchRect().height());
        w.grab().save("strip_0.png");
        fs->openSearch(); wait(100);
        std::fprintf(stderr, "open: edit visible=%d focus=%d width=%d tabs left=%d\n", fs->m_searchEdit->isVisible(),
                     fs->m_searchEdit->hasFocus(), fs->searchRect().width(), fs->tabsRow().left());
        fs->m_searchEdit->setText("report"); wait(200);
        std::fprintf(stderr, "'report': items=%d width=%d\n", int(fs->m_items.size()), fs->searchRect().width());
        fs->m_searchEdit->setText("a much longer search text that will not fit in the small field"); wait(200);
        std::fprintf(stderr, "long: width=%d tabs left=%d items=%d\n", fs->searchRect().width(), fs->tabsRow().left(), int(fs->m_items.size()));
        fs->m_searchEdit->setText("report"); wait(100);
        w.grab().save("strip_1.png");
        // Мышь далеко, а в поиске текст и фокус — не сворачивается
        fs->checkHover(); fs->checkHover(); fs->checkHover(); wait(200);
        std::fprintf(stderr, "stays open while typing: target=%.0f\n", fs->m_target);
        std::fprintf(stderr, "esc closes=%d again=%d open=%d items=%d\n", fs->closeSearch(), fs->closeSearch(),
                     fs->m_searchOpen, int(fs->m_items.size()));
        fs->openSearch(); wait(50); fs->m_searchEdit->setText("re");
        fs->collapseNow(); wait(100);
        std::fprintf(stderr, "collapse clears: open=%d query='%s'\n", fs->m_searchOpen, qPrintable(fs->m_localQuery));
        w.switchView(MainWindow::ViewMode::Pins); wait(1000);
        const QRect P = geo(fs);
        std::fprintf(stderr, "pins: strip %d..%d  import %d..%d / %d..%d\n", P.left(), P.right(),
                     geo(w.m_importVideoBtn).left(), geo(w.m_importVideoBtn).right(),
                     geo(w.m_importAudioBtn).left(), geo(w.m_importAudioBtn).right());
        w.grab().save("strip_2.png");
        return 0;
    }
    if (out == "filetypes") {
        // Новые типы файлов и старые сохранённые настройки
        for (const char *f : {"a.psd", "b.xlsx", "c.pptx", "d.py", "e.json", "f.ttf", "g.torrent", "h.ts",
                              "i.avif", "j.m2ts", "k.midi", "l.xapk", "m.zst", "n.pka", "o.pdf"})
            std::fprintf(stderr, "%s=%s ", f, qPrintable(Files::category(f)));
        std::fprintf(stderr, "\n");
        QSettings ini(QSettings::IniFormat, QSettings::UserScope, "SmartClipUiTest", "SmartClipUiTest");
        Q_UNUSED(ini);
        AppSettings &st = AppSettings::get();
        st.m_s.remove("files/typesKnown");
        st.m_s.setValue("files/types", QStringList{"docs", "audio"});  // Как сохранила старая версия
        std::fprintf(stderr, "old saved -> %s\n", qPrintable(st.filesTypes().join(',')));
        st.setFilesTypes({"docs", "audio"});  // Сохранили в новой — новые типы выключены
        std::fprintf(stderr, "new saved -> %s\n", qPrintable(st.filesTypes().join(',')));
        st.m_s.remove("files/types"); st.m_s.remove("files/typesKnown");
        std::fprintf(stderr, "default -> %s\n", qPrintable(st.filesTypes().join(',')));
        return 0;
    }
    if (out == "shelves") {
        // Полки с папками под полоской файлов
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        QDir().mkpath(tmp.path() + "/dl");
        for (int i = 0; i < 12; ++i) { QFile f(tmp.path() + QString("/dl/f%1.txt").arg(i)); f.open(QIODevice::WriteOnly); f.write("x"); }
        AppSettings::get().setFilesFolders({tmp.path() + "/dl"});
        QStringList left, right;
        for (const char *n : {"Projects", "Work documents", "Photos 2026", "Very long folder name for the shelf"}) {
            QDir().mkpath(tmp.path() + "/" + n); left << tmp.path() + "/" + n;
        }
        for (int i = 0; i < 14; ++i) { QDir().mkpath(tmp.path() + QString("/R%1").arg(i)); right << tmp.path() + QString("/R%1").arg(i); }
        AppSettings::get().setShelf(0, left, {"", "Работа", "", ""});
        AppSettings::get().setShelf(1, right, {});
        for (int i = 0; i < 4; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.loadShelves();
        w.showWindow();
        wait(1500);
        auto geo = [&](QWidget *x) { return QRect(x->mapTo(&w, QPoint(0, 0)), x->size()); };
        const QRect C = w.m_centerSpacer->geometry(), S = geo(w.m_filesStrip), L = geo(w.m_shelves[0]), R = geo(w.m_shelves[1]);
        std::fprintf(stderr, "center %d..%d bottom=%d\n", C.left(), C.right(), C.bottom());
        std::fprintf(stderr, "strip %d..%d bottom=%d\n", S.left(), S.right(), S.bottom());
        std::fprintf(stderr, "left shelf %d..%d y=%d..%d visible=%d n=%d\n", L.left(), L.right(), L.top(), L.bottom(),
                     w.m_shelves[0]->isVisible(), int(w.m_shelves[0]->folders().size()));
        std::fprintf(stderr, "right shelf %d..%d y=%d..%d n=%d maxScroll=%d\n", R.left(), R.right(), R.top(), R.bottom(),
                     int(w.m_shelves[1]->folders().size()), w.m_shelves[1]->maxScroll());
        std::fprintf(stderr, "interactive on shelf=%d, between=%d\n", w.isInteractiveArea(L.center()),
                     w.isInteractiveArea(QPoint((L.right() + R.left()) / 2, L.center().y())));
        std::fprintf(stderr, "label renamed='%s' plain='%s'\n", qPrintable(FolderShelf::label(w.m_shelves[0]->folders()[1])),
                     qPrintable(FolderShelf::label(w.m_shelves[0]->folders()[0])));
        w.grab().save("shelves_0.png");
        // Перенести 1-ю папку с левой на правую и убрать последнюю с правой — как в меню
        {
            QList<FolderShelf::Folder> from = w.m_shelves[0]->folders(), to = w.m_shelves[1]->folders();
            to.append(from.takeAt(0)); w.m_shelves[0]->setFolders(from); w.m_shelves[1]->setFolders(to);
            w.saveShelf(0); w.saveShelf(1);
        }
        std::fprintf(stderr, "after move: left=%d right=%d saved left=%d right=%d names=%s\n",
                     int(w.m_shelves[0]->folders().size()), int(w.m_shelves[1]->folders().size()),
                     int(AppSettings::get().shelfPaths(0).size()), int(AppSettings::get().shelfPaths(1).size()),
                     qPrintable(AppSettings::get().shelfNames(0).join('|')));
        // Пустая полка
        AppSettings::get().setShelf(0, {}, {}); w.loadShelves(); wait(100);
        w.grab().save("shelves_1.png");
        // Открытие/закрытие с анимацией и сжатие при переносе — без падений
        w.hideAnimated(); wait(800); w.showWindow(); wait(1200);
        w.startFileDragSqueeze(); wait(400); w.endFileDragSqueeze(); wait(800);
        std::fprintf(stderr, "after anims: shelves visible=%d,%d\n", w.m_shelves[0]->isVisible(), w.m_shelves[1]->isVisible());
        w.switchView(MainWindow::ViewMode::Pins); wait(1000);
        std::fprintf(stderr, "pins: shelves visible=%d strip bottom=%d shelf top=%d\n", w.m_shelves[0]->isVisible(),
                     geo(w.m_filesStrip).bottom(), geo(w.m_shelves[0]).top());
        w.grab().save("shelves_2.png");
        return 0;
    }
    if (out == "panel") {
        // Папка с полки: мало / средне / много, подпапки, поиск, закрытие
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        auto mk = [&](const QString &dir, int n, const QString &pref) {
            QDir().mkpath(dir);
            for (int i = 0; i < n; ++i) { QFile f(dir + QString("/%1_%2.txt").arg(pref).arg(i)); f.open(QIODevice::WriteOnly); f.write("x"); }
        };
        const QString few = tmp.path() + "/Few", mid = tmp.path() + "/Mid", many = tmp.path() + "/Many";
        mk(few, 3, "doc");
        mk(mid, 18, "note");
        mk(many, 260, "file");
        mk(many + "/Sub A", 5, "inner");
        mk(many + "/Sub A/Deeper", 2, "deep_report");
        mk(many + "/Sub B", 40, "bbb");
        QDir().mkpath(tmp.path() + "/dl");
        AppSettings::get().setFilesFolders({tmp.path() + "/dl"});
        AppSettings::get().setShelf(0, {few, mid}, {"", ""});
        AppSettings::get().setShelf(1, {many, tmp.path() + "/Gone"}, {"Много", ""});
        for (int i = 0; i < 4; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.loadShelves();
        w.showWindow();
        wait(1500);
        FolderPanel *fp = w.m_folderPanel;
        auto geo = [&](QWidget *x) { return QRect(x->mapTo(&w, QPoint(0, 0)), x->size()); };
        const char *names[] = {"Inline", "Half", "Full"};
        auto report = [&](const char *what) {
            const QRect g = geo(fp), L = geo(w.m_shelves[0]), R = geo(w.m_shelves[1]);
            std::fprintf(stderr, "%-12s open=%d vis=%d mode=%s geo=%d,%d %dx%d items=%d shown=%d stripBlocked=%d | shelfL=%d..%d shelfR=%d..%d\n",
                what, fp->isOpen(), fp->isVisible(), names[fp->mode()], g.x(), g.y(), g.width(), g.height(),
                int(fp->m_items.size()), int(fp->shown().size()), w.m_filesStrip->m_blocked, L.left(), L.right(), R.left(), R.right());
        };
        auto tile = [&](int side, int i) { return w.m_shelves[side]->itemRect(i); };
        w.openShelfFolder(0, 0, tile(0, 0)); wait(600); report("few"); w.grab().save("panel_few.png");
        QDir().mkpath(few + "/Empty sub");
        w.openShelfFolder(0, 0, tile(0, 0)); wait(400); w.openShelfFolder(0, 0, tile(0, 0)); wait(600); report("few+sub");
        fp->navigate(few + "/Empty sub"); wait(600); report("empty sub");
        fp->goUp(); wait(600); report("few again");
        w.grab().save("panel_few2.png");
        fp->closeNow(); QDir(few + "/Empty sub").removeRecursively();
        w.openShelfFolder(0, 1, tile(0, 1)); wait(600); report("mid"); w.grab().save("panel_mid.png");
        w.openShelfFolder(1, 0, tile(1, 0)); wait(600); report("many"); w.grab().save("panel_many.png");
        std::fprintf(stderr, "crumbs: %s\n", qPrintable(fp->crumbLabels().join(" > ")));
        fp->navigate(many + "/Sub A"); wait(600); report("sub A");
        std::fprintf(stderr, "crumbs: %s first=%s\n", qPrintable(fp->crumbLabels().join(" > ")),
                     fp->m_items.isEmpty() ? "-" : qPrintable(fp->m_items[0].name));
        fp->goUp(); wait(600); report("up");
        fp->openSearch(); wait(50);
        fp->m_searchEdit->setText("deep_report"); wait(900);
        report("search");
        std::fprintf(stderr, "found: %s\n", fp->m_results.isEmpty() ? "-" : qPrintable(fp->m_results[0].path.mid(tmp.path().size())));
        w.grab().save("panel_search.png");
        std::fprintf(stderr, "esc1=%d search=%d\n", fp->escape(), fp->m_searchOpen);
        std::fprintf(stderr, "esc2=%d\n", fp->escape()); wait(500); report("closed");
        // Тот же клик ещё раз — закрыть
        w.openShelfFolder(0, 1, tile(0, 1)); wait(600); w.openShelfFolder(0, 1, tile(0, 1)); wait(500); report("toggle");
        // Окно спряталось — папка закрыта
        w.openShelfFolder(1, 0, tile(1, 0)); wait(600); w.hideAnimated(); wait(800); report("after hide");
        w.showWindow(); wait(1000);
        std::fprintf(stderr, "gone folder exists=%d\n", QFileInfo(tmp.path() + "/Gone").isDir());
        return 0;
    }
    if (out == "stage4") {
        // Рука из открытой папки, копия на папку полки / подпапку / саму папку, слежение
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        auto mk = [&](const QString &f) { QDir().mkpath(QFileInfo(f).absolutePath()); QFile x(f); x.open(QIODevice::WriteOnly); x.write("data"); };
        const QString src = tmp.path() + "/Src", dstA = tmp.path() + "/DstA", dstB = tmp.path() + "/DstB";
        for (int i = 0; i < 12; ++i) mk(src + QString("/s%1.txt").arg(i));
        mk(src + "/Inner/x.txt");
        mk(dstA + "/keep.txt");
        mk(dstB + "/keep.txt");
        QDir().mkpath(tmp.path() + "/dl");
        AppSettings::get().setFilesFolders({tmp.path() + "/dl"});
        AppSettings::get().setShelf(0, {src}, {});
        AppSettings::get().setShelf(1, {dstA, dstB}, {});
        for (int i = 0; i < 4; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.loadShelves();
        w.showWindow();
        wait(1500);
        FolderPanel *fp = w.m_folderPanel;
        w.openShelfFolder(0, 0, w.m_shelves[0]->itemRect(0)); wait(700);
        std::fprintf(stderr, "open: mode=%d items=%d\n", int(fp->mode()), int(fp->m_items.size()));
        // Файл из папки под курсором — для руки
        int fileIdx = -1, dirIdx = -1;
        for (int i = 0; i < fp->shown().size(); ++i) { if (fp->shown()[i].dir) { if (dirIdx < 0) dirIdx = i; } else if (fileIdx < 0) fileIdx = i; }
        const QPoint fileG = fp->mapToGlobal(fp->itemRect(fileIdx).center()), dirG = fp->mapToGlobal(fp->itemRect(dirIdx).center());
        CarryItem it; QRectF from;
        std::fprintf(stderr, "carryItemAt file=%d name=%s\n", fp->carryItemAt(fileG, it, from), qPrintable(it.name));
        std::fprintf(stderr, "carryZone over panel=%d over shelf=%d\n", w.carryZone(fileG),
                     w.carryZone(w.m_shelves[1]->mapToGlobal(w.m_shelves[1]->rect().center())));
        // Цели: подпапка, фон папки, папка на полке
        const QPoint shelfB = w.m_shelves[1]->mapToGlobal(w.m_shelves[1]->itemRect(1).center());
        std::fprintf(stderr, "target dir tile=%s\n", qPrintable(w.carryDropFolder(dirG).mid(tmp.path().size())));
        std::fprintf(stderr, "target shelf B=%s\n", qPrintable(w.carryDropFolder(shelfB).mid(tmp.path().size())));
        const QPoint bgG = fp->mapToGlobal(QPoint(fp->width() - 20, fp->height() - 12));
        std::fprintf(stderr, "target panel bg=%s\n", qPrintable(w.carryDropFolder(bgG).mid(tmp.path().size())));
        w.m_shelves[1]->setDropHint(shelfB); fp->setDropHint(dirG);
        std::fprintf(stderr, "hints: shelf=%d panel=%d\n", w.m_shelves[1]->m_drop, fp->m_drop);
        w.m_shelves[1]->clearDropHint(); fp->clearDropHint();
        // Копия на папку полки
        const QList<CarryItem> two{{src + "/s1.txt", QPixmap(), "s1.txt"}, {src + "/s2.txt", QPixmap(), "s2.txt"}};
        fp->closeNow(); wait(100);
        std::fprintf(stderr, "closed: target shelf B=%s\n", qPrintable(w.carryDropFolder(shelfB).mid(tmp.path().size())));
        std::fprintf(stderr, "drop on shelf handled=%d\n", w.dropToFolder(shelfB, two));
        wait(1500);
        std::fprintf(stderr, "DstB has s1=%d s2=%d, src still s1=%d\n", QFile::exists(dstB + "/s1.txt"),
                     QFile::exists(dstB + "/s2.txt"), QFile::exists(src + "/s1.txt"));
        // Копия той же ещё раз — «(2)», без вопросов
        w.dropToFolder(shelfB, two); wait(1500);
        std::fprintf(stderr, "DstB entries=%d\n", int(QDir(dstB).entryList(QDir::Files).size()));
        w.openShelfFolder(0, 0, w.m_shelves[0]->itemRect(0)); wait(700);
        // В подпапку внутри открытой папки
        w.dropToFolder(dirG, {{dstA + "/keep.txt", QPixmap(), "keep.txt"}}); wait(1500);
        std::fprintf(stderr, "Inner has keep=%d\n", QFile::exists(src + "/Inner/keep.txt"));
        // На фон открытой папки — файл появляется в ней сам (слежение)
        const int before = int(fp->m_items.size());
        w.dropToFolder(bgG, {{dstB + "/keep.txt", QPixmap(), "keep.txt"}}); wait(2000);
        std::fprintf(stderr, "panel refreshed: %d -> %d\n", before, int(fp->m_items.size()));
        // Бросили туда же, где лежит, — ничего не копируется
        w.dropToFolder(bgG, {{src + "/s3.txt", QPixmap(), "s3.txt"}}); wait(1200);
        std::fprintf(stderr, "same folder copies=%d\n", int(QDir(src).entryList({"s3*"}, QDir::Files).size()));
        // Внешняя запись в папку — тоже видна
        mk(src + "/external_new.txt"); wait(1500);
        bool seen = false; for (const auto &x : fp->m_items) if (x.name == "external_new.txt") seen = true;
        std::fprintf(stderr, "external file seen=%d\n", seen);
        return 0;
    }
    if (out == "parity") {
        // Полоска и открытая папка умеют одно и то же
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        auto mk = [&](const QString &f) { QDir().mkpath(QFileInfo(f).absolutePath()); QFile x(f); x.open(QIODevice::WriteOnly); x.write("data"); };
        const QString dl = tmp.path() + "/dl", docs = tmp.path() + "/docs", shelf = tmp.path() + "/Shelf";
        for (int i = 0; i < 5; ++i) mk(dl + QString("/d%1.txt").arg(i));
        for (int i = 0; i < 3; ++i) mk(docs + QString("/doc%1.txt").arg(i));
        for (int i = 0; i < 30; ++i) mk(shelf + QString("/f%1.txt").arg(i));
        AppSettings::get().setFilesFolders({dl, docs});
        AppSettings::get().setShelf(0, {shelf}, {});
        AppSettings::get().setShelf(1, {}, {});
        for (int i = 0; i < 4; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.loadShelves();
        w.showWindow();
        wait(1500);
        FilesStrip *fs = w.m_filesStrip;
        std::fprintf(stderr, "strip items=%d watching=%d\n", int(fs->m_all.size()), int(fs->m_watch.directories().size()));
        // Новый файл в «Загрузках» — полоска сама его видит
        mk(dl + "/new_download.txt"); wait(1500);
        bool seen = false; for (const auto &e : fs->m_all) if (e.name == "new_download.txt") seen = true;
        std::fprintf(stderr, "strip auto-refresh seen=%d items=%d\n", seen, int(fs->m_all.size()));
        // Рука над вкладкой-папкой полоски — копия в эту папку
        fs->expand(true); wait(50); fs->m_hoverPoll.stop(); wait(700);  // Мышь не над полоской — не даём свернуться
        int docsTab = -1; const auto tabs = fs->currentTabs();
        for (int i = 0; i < tabs.size(); ++i) if (QDir::cleanPath(tabs[i].key) == QDir::cleanPath(docs)) docsTab = i;
        QRect plus; const QList<QRect> tr = fs->tabRects(&plus);
        const QPoint tabG = fs->mapToGlobal(tr[docsTab].center() + QPoint(-fs->m_tabScroll, fs->top()));
        const QPoint allG = fs->mapToGlobal(tr[0].center() + QPoint(-fs->m_tabScroll, fs->top()));
        std::fprintf(stderr, "drop folder on docs tab=%s on 'all' tab='%s'\n",
                     qPrintable(w.carryDropFolder(tabG).mid(tmp.path().size())), qPrintable(w.carryDropFolder(allG)));
        fs->setDropHint(tabG);
        std::fprintf(stderr, "strip tab hint=%d (want %d)\n", fs->m_dropTab, docsTab);
        fs->clearDropHint();
        w.dropToFolder(tabG, {{shelf + "/f1.txt", QPixmap(), "f1.txt"}}); wait(1500);
        std::fprintf(stderr, "copied to docs=%d\n", QFile::exists(docs + "/f1.txt"));
        fs->collapseNow();
        // Открытая папка: список, как в полоске (общая настройка)
        w.openShelfFolder(0, 0, w.m_shelves[0]->itemRect(0)); wait(700);
        FolderPanel *fp = w.m_folderPanel;
        const int tilesH = fp->itemRect(0).height();
        fp->setView("list"); wait(500);
        std::fprintf(stderr, "panel list: row h=%d (tiles %d) cols=%d setting=%s mode=%d\n", fp->itemRect(0).height(), tilesH,
                     fp->cols(fp->width()), qPrintable(AppSettings::get().filesView()), int(fp->mode()));
        w.grab().save("parity_list.png");
        fp->setView("tiles"); wait(300);
        // Переименовали файл — закреп указывает на новое имя
        const int pinId = db.addPin("", "", "file", "", shelf + "/f2.txt");
        QFile::rename(shelf + "/f2.txt", shelf + "/f2_renamed.txt");
        db.renameFilePath(shelf + "/f2.txt", shelf + "/f2_renamed.txt");
        QString pinPath; for (const auto &pin : db.getAllPins()) if (pin.id == pinId) pinPath = pin.filepath;
        std::fprintf(stderr, "pin path after rename=%s\n", qPrintable(QFileInfo(pinPath).fileName()));
        return 0;
    }
    if (out == "ctrlview") {
        // Ctrl+клик по фото/видео в полоске и в открытой папке — просмотр
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        const QString dl = tmp.path() + "/dl", shelf = tmp.path() + "/Shelf";
        QDir().mkpath(dl); QDir().mkpath(shelf);
        QImage img(64, 48, QImage::Format_RGB32); img.fill(Qt::red);
        img.save(dl + "/photo.png"); img.save(shelf + "/pic.png");
        { QFile f(dl + "/doc.txt"); f.open(QIODevice::WriteOnly); f.write("x"); }
        { QFile f(shelf + "/clip.mp4"); f.open(QIODevice::WriteOnly); f.write("x"); }
        AppSettings::get().setFilesFolders({dl});
        AppSettings::get().setShelf(0, {shelf}, {});
        AppSettings::get().setShelf(1, {}, {});
        for (int i = 0; i < 4; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.loadShelves();
        w.showWindow();
        wait(1500);
        QStringList viewed, picked;
        FilesStrip *fs = w.m_filesStrip;
        fs->onView = [&](const QString &p) { viewed << QFileInfo(p).fileName(); };
        fs->onPick = [&](const Files::Entry &e) { picked << e.name; };
        fs->expand(true); wait(50); fs->m_hoverPoll.stop(); wait(700);
        auto click = [&](QWidget *wd, QPoint pos, Qt::KeyboardModifiers mods) {
            QMouseEvent pr(QEvent::MouseButtonPress, pos, wd->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton, mods);
            QApplication::sendEvent(wd, &pr);
            QMouseEvent rl(QEvent::MouseButtonRelease, pos, wd->mapToGlobal(pos), Qt::LeftButton, Qt::NoButton, mods);
            QApplication::sendEvent(wd, &rl);
            wait(50);
        };
        for (int i = 0; i < fs->m_items.size(); ++i) {
            const QPoint pos = fs->itemRect(i).center() + QPoint(0, fs->top());
            click(fs, pos, Qt::ControlModifier);
        }
        std::fprintf(stderr, "strip ctrl: viewed=[%s] picked=[%s]\n", qPrintable(viewed.join(',')), qPrintable(picked.join(',')));
        viewed.clear(); picked.clear();
        fs->collapseNow();
        w.openShelfFolder(0, 0, w.m_shelves[0]->itemRect(0)); wait(700);
        FolderPanel *fp = w.m_folderPanel;
        fp->onView = [&](const QString &p) { viewed << QFileInfo(p).fileName(); };
        fp->onPickFile = [&](const QString &p) { picked << QFileInfo(p).fileName(); };
        for (int i = 0; i < fp->shown().size(); ++i) click(fp, fp->itemRect(i).center(), Qt::ControlModifier);
        std::fprintf(stderr, "panel ctrl: viewed=[%s] picked=[%s] mode=%d\n", qPrintable(viewed.join(',')), qPrintable(picked.join(',')), int(fp->mode()));
        viewed.clear(); picked.clear();
        for (int i = 0; i < fp->shown().size(); ++i) click(fp, fp->itemRect(i).center(), Qt::NoModifier);
        std::fprintf(stderr, "panel plain: viewed=[%s] picked=[%s]\n", qPrintable(viewed.join(',')), qPrintable(picked.join(',')));
        // Взять файл из открытой папки в руку
        fp->pickUp(0);
        std::fprintf(stderr, "panel pickUp: carry active=%d has=%d\n", w.m_carry->active(), w.m_carry->has(fp->shown()[0].path));
        w.m_carry->cancel(); wait(300);
        return 0;
    }
    if (out == "sorttree") {
        // Все файлы (без лимита 7 дней / 30 штук), сортировка, дерево в папке с полки
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        const QString dl = tmp.path() + "/dl", shelf = tmp.path() + "/Shelf";
        QDir().mkpath(dl); QDir().mkpath(shelf + "/sub/deeper");
        const QDateTime old = QDateTime::currentDateTime().addDays(-200);
        auto mk = [&](const QString &path, int bytes, const QDateTime &t) {
            QFile f(path); f.open(QIODevice::ReadWrite); f.write(QByteArray(bytes, 'x'));
            f.setFileTime(t, QFileDevice::FileModificationTime);
            f.setFileTime(t, QFileDevice::FileBirthTime);
            f.close();
        };
        for (int i = 0; i < 45; ++i) mk(dl + QString("/old%1.txt").arg(i, 2, 10, QChar('0')), 10 + i, old.addSecs(i));
        mk(dl + "/b.txt", 500, QDateTime::currentDateTime().addSecs(-50));
        mk(dl + "/a.txt", 5, QDateTime::currentDateTime().addSecs(-10));
        mk(shelf + "/big.txt", 9000, QDateTime::currentDateTime().addSecs(-100));
        mk(shelf + "/small.txt", 3, QDateTime::currentDateTime().addSecs(-5));
        mk(shelf + "/sub/in1.txt", 1, QDateTime::currentDateTime());
        mk(shelf + "/sub/in2.txt", 2, QDateTime::currentDateTime());
        AppSettings::get().setFilesFolders({dl});
        AppSettings::get().setFilesSort("date", true);
        AppSettings::get().setFilesView("list");
        AppSettings::get().setFilesTree(true);
        AppSettings::get().setShelf(0, {shelf}, {});
        AppSettings::get().setShelf(1, {}, {});
        for (int i = 0; i < 4; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.loadShelves();
        w.showWindow();
        wait(1500);
        FilesStrip *fs = w.m_filesStrip;
        auto names = [&]() { QStringList l; for (int i = 0; i < qMin(4, int(fs->m_items.size())); ++i) l << fs->m_items[i].name; return l.join(','); };
        std::fprintf(stderr, "strip all: n=%d first=[%s]\n", int(fs->m_items.size()), qPrintable(names()));
        AppSettings::get().setFilesSort("name", false); fs->resort();
        std::fprintf(stderr, "strip name asc: [%s]\n", qPrintable(names()));
        AppSettings::get().setFilesSort("size", true); fs->resort();
        std::fprintf(stderr, "strip size desc: [%s]\n", qPrintable(names()));
        fs->expand(true); wait(50); fs->m_hoverPoll.stop(); wait(700);
        std::fprintf(stderr, "strip sort btn: rect=%d,%d tabsRight=%d viewLeft=%d\n", fs->sortBtnRect().left(), fs->sortBtnRect().width(),
                     fs->tabsRow().right(), fs->viewBtnRect(0).left());
        {
            QImage img(fs->size() * 1.5, QImage::Format_ARGB32_Premultiplied); img.setDevicePixelRatio(1.5); img.fill(Qt::black);
            fs->render(&img); img.save(QString(qgetenv("SHOT_DIR")) + "/sort_strip.png");
        }
        fs->collapseNow();
        AppSettings::get().setFilesSort("name", false);
        w.openShelfFolder(0, 0, w.m_shelves[0]->itemRect(0)); wait(800);
        FolderPanel *fp = w.m_folderPanel;
        auto pnames = [&]() { QStringList l; for (const auto &it : fp->shown()) l << QString("%1%2").arg(QString(it.depth * 2, '-'), it.name); return l.join(','); };
        std::fprintf(stderr, "panel: mode=%d tree=%d [%s]\n", int(fp->mode()), int(fp->treeOn()), qPrintable(pnames()));
        int sub = -1;
        for (int i = 0; i < fp->shown().size(); ++i) if (fp->shown()[i].name == "sub") sub = i;
        const QPoint cp = fp->chevronRect(sub).center();
        QMouseEvent pr(QEvent::MouseButtonPress, cp, fp->mapToGlobal(cp), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(fp, &pr);
        QMouseEvent rl(QEvent::MouseButtonRelease, cp, fp->mapToGlobal(cp), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(fp, &rl);
        wait(800);
        std::fprintf(stderr, "panel expanded: path=%s [%s]\n", qPrintable(QFileInfo(fp->path()).fileName()), qPrintable(pnames()));
        fp->m_sortKey = "size"; fp->m_sortDesc = true; fp->resort();
        std::fprintf(stderr, "panel size desc: [%s]\n", qPrintable(pnames()));
        mk(shelf + "/sub/in3.txt", 4, QDateTime::currentDateTime());
        wait(1500);
        std::fprintf(stderr, "panel watch sub: [%s]\n", qPrintable(pnames()));
        {
            QImage img(fp->size() * 1.5, QImage::Format_ARGB32_Premultiplied); img.setDevicePixelRatio(1.5); img.fill(Qt::black);
            fp->render(&img); img.save(QString(qgetenv("SHOT_DIR")) + "/sort_panel.png");
        }
        // Клик по самой строке подпапки — заходим внутрь, как раньше
        int sub2 = -1;
        for (int i = 0; i < fp->shown().size(); ++i) if (fp->shown()[i].name == "sub") sub2 = i;
        const QPoint rp = fp->itemRect(sub2).center();
        QMouseEvent pr2(QEvent::MouseButtonPress, rp, fp->mapToGlobal(rp), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(fp, &pr2);
        QMouseEvent rl2(QEvent::MouseButtonRelease, rp, fp->mapToGlobal(rp), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(fp, &rl2);
        wait(800);
        std::fprintf(stderr, "panel row click: path=%s [%s]\n", qPrintable(QFileInfo(fp->path()).fileName()), qPrintable(pnames()));
        AppSettings::get().setFilesTree(false); fp->settingsChanged();
        std::fprintf(stderr, "tree off: tree=%d\n", int(fp->treeOn()));
        return 0;
    }
    if (out == "perfolder") {
        // Вид и порядок у каждой папки свои; список не во всю ширину; быстрый заход в папку
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        const QString dl = tmp.path() + "/dl", shelf = tmp.path() + "/Shelf";
        QDir().mkpath(dl); QDir().mkpath(shelf + "/sub");
        auto mk = [&](const QString &path) { QFile f(path); f.open(QIODevice::WriteOnly); f.write("x"); };
        for (int i = 0; i < 5; ++i) mk(dl + QString("/d%1.txt").arg(i));
        for (int i = 0; i < 80; ++i) mk(shelf + QString("/f%1.txt").arg(i, 2, 10, QChar('0')));
        for (int i = 0; i < 80; ++i) mk(shelf + QString("/sub/s%1.txt").arg(i, 2, 10, QChar('0')));
        AppSettings::get().setFilesFolders({dl});
        AppSettings::get().setShelf(0, {shelf}, {});
        AppSettings::get().setShelf(1, {}, {});
        for (int i = 0; i < 4; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.loadShelves();
        w.showWindow();
        wait(1500);
        FolderPanel *fp = w.m_folderPanel;
        w.openShelfFolder(0, 0, w.m_shelves[0]->itemRect(0)); wait(800);
        std::fprintf(stderr, "tiles: mode=%d w=%d\n", int(fp->mode()), fp->width());
        fp->setView("list"); wait(600);
        std::fprintf(stderr, "list: mode=%d w=%d h=%d shelfW=%d\n", int(fp->mode()), fp->width(), fp->height(), w.m_shelves[0]->width());
        fp->enter(shelf + "/sub"); wait(600);
        std::fprintf(stderr, "sub inherits: view=%s\n", qPrintable(fp->m_view));
        fp->setView("tiles"); wait(600);
        fp->goUp(); wait(600);
        std::fprintf(stderr, "back to root: view=%s; sub stored=%s\n", qPrintable(fp->m_view),
                     qPrintable(AppSettings::get().folderView(shelf + "/sub")));
        // Быстро: в подпапку и сразу назад, пока едет анимация
        fp->enter(shelf + "/sub"); wait(30); fp->goUp(); wait(20); fp->enter(shelf + "/sub");
        bool mid = fp->m_anim.isActive();
        wait(900);
        std::fprintf(stderr, "fast nav: midAnim=%d geo==layout=%d anim=%d mode=%d\n", int(mid),
                     int(fp->geometry() == fp->m_layoutGeo), int(fp->m_anim.isActive()), int(fp->mode()));
        fp->close(); wait(500);
        // Полоска: у вкладки папки — свой вид
        FilesStrip *fs = w.m_filesStrip;
        fs->expand(true); wait(50); fs->m_hoverPoll.stop(); wait(700);
        fs->m_tabHistory = dl; fs->applyFilter();
        AppSettings::get().setFolderView(fs->viewKey(), "list"); fs->applyFilter();
        const QString dlView = fs->m_view;
        fs->m_tabHistory.clear(); fs->applyFilter();
        std::fprintf(stderr, "strip: dl=%s all=%s\n", qPrintable(dlView), qPrintable(fs->m_view));
        return 0;
    }
    if (out == "ghosts") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        for (int i = 0; i < 3; ++i) db.addPin("", QString("pin %1").arg(i), "text", QString("short text %1").arg(i), "");
        for (int i = 0; i < 4; ++i) db.addHistory("text", QString("history %1").arg(i), "", "app");
        QImage img(640, 360, QImage::Format_RGB32); img.fill(QColor(60, 90, 160));
        const QString f = db.imagesPath() + "/g.png"; img.save(f); db.addHistory("image", "", f, "app");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1500);
        w.grab().save("ghosts_0.png");
        for (auto *b : w.findChildren<QPushButton*>()) if (b->text().contains("Закрепы")) { b->click(); break; }
        wait(1200);
        w.grab().save("ghosts_1.png");
        FilesStrip *fs = nullptr;
        for (QObject *o : w.children()) if (auto *x = dynamic_cast<FilesStrip *>(o)) fs = x;
        std::fprintf(stderr, "pins strip visible=%d\n", fs && fs->isVisible());
        // Перетаскивание: снимок посередине — соседи должны быть в пути, а не на месте
        auto *L = w.findChild<QScrollArea*>("leftScroll");
        QList<ClickableCard*> cards;
        for (auto *c : L->widget()->findChildren<ClickableCard*>(Qt::FindDirectChildrenOnly)) if (c->isVisible()) cards << c;
        std::sort(cards.begin(), cards.end(), [](QWidget *a, QWidget *b) { return a->y() < b->y(); });
        if (false) for (auto *c : cards) std::fprintf(stderr, "card y=%d h=%d vis=%d\n", c->y(), c->height(), c->isVisible());
        if (cards.size() >= 3) {
            ClickableCard *c = cards[0];
            const int y1 = cards[1]->y();
            const QPoint g = c->mapToGlobal(QPoint(40, 20));
            emit c->dragStarted(g);
            for (int k = 1; k <= 12; ++k) { emit c->dragMoved(g + QPoint(0, k * 12)); wait(8); }
            wait(30);
            std::fprintf(stderr, "neighbour y: before=%d mid=%d\n", y1, cards[1]->y());
            wait(600);
            std::fprintf(stderr, "neighbour y after settle=%d\n", cards[1]->y());
            emit c->dragFinished();
            wait(800);
        }
        return 0;
    }
    if (out == "pinflow") {
        // Закрепить файл так же, как из меню (без диалогов: без названия и папки)
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        const QString dir = tmp.path() + "/dl";
        QDir().mkpath(dir);
        { QFile f(dir + "/doc.pdf"); f.open(QIODevice::WriteOnly); f.write(QByteArray(800, 'x')); }
        AppSettings::get().setFilesFolders({dir});
        AppSettings::get().setPinsNoName(true);
        AppSettings::get().setPinsNoFolder(true);
        const int before = db.getAllPins().size();
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1200);
        FilesStrip *fs = nullptr;
        for (QObject *o : w.children()) if (auto *x = dynamic_cast<FilesStrip *>(o)) fs = x;
        QMetaObject::invokeMethod(&w, [&]() { w.pinFile(dir + "/doc.pdf"); });
        wait(200);
        int files = 0;
        for (const PinItem &pin : db.getAllPins()) if (pin.type == "file") ++files;
        std::fprintf(stderr, "pins before=%d after=%d filePins=%d\n", before, int(db.getAllPins().size()), files);
        for (auto *b : w.findChildren<QPushButton*>()) if (b->text().contains("Закрепы")) { b->click(); break; }
        wait(800);
        std::fprintf(stderr, "strip pinsMode=%d visible=%d\n", fs->pinsMode(), fs->isVisible());
        fs->grab().save("pinflow.png");
        return 0;
    }
    if (out == "pinmenu") {
        // Как у пользователя: правый клик по файлу -> меню -> «Закрепить» -> диалог названия
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        // Все всплывающие окна теста — не на настоящем экране
        struct Hide : QObject { bool eventFilter(QObject *o, QEvent *e) override {
            if (e->type() == QEvent::Polish && o->isWidgetType()) {
                auto *wd = static_cast<QWidget *>(o);
                if (wd->isWindow()) wd->setAttribute(Qt::WA_DontShowOnScreen);
            }
            return false; } } hide;
        app.installEventFilter(&hide);
        const QString dir = tmp.path() + "/dl2";
        QDir().mkpath(dir);
        { QFile f(dir + "/menu.pdf"); f.open(QIODevice::WriteOnly); f.write(QByteArray(800, 'x')); }
        AppSettings::get().setFilesFolders({dir});
        AppSettings::get().setPinsNoName(false);
        AppSettings::get().setPinsNoFolder(false);
        const int before = db.getAllPins().size();
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1200);
        FilesStrip *fs = nullptr;
        for (QObject *o : w.children()) if (auto *x = dynamic_cast<FilesStrip *>(o)) fs = x;
        // Автопилот: в меню — пункт «Закрепить», в диалоге — «OK»
        QTimer pilot; pilot.setInterval(150);
        int step = 0;
        QObject::connect(&pilot, &QTimer::timeout, [&]() {
            QWidget *popup = QApplication::activePopupWidget();
            if (!popup) for (QWidget *t : QApplication::topLevelWidgets()) if (qobject_cast<QMenu *>(t) && t->isVisible()) popup = t;
            QWidget *modal = QApplication::activeModalWidget();
            std::fprintf(stderr, "pilot: popup=%s modal=%s\n", popup ? popup->metaObject()->className() : "-",
                         modal ? modal->metaObject()->className() : "-");
            if (popup) {
                for (QAction *a : popup->findChildren<QAction *>())
                    if (a->text().contains("Закрепить")) { std::fprintf(stderr, "pilot: trigger pin\n"); QTimer::singleShot(0, popup, [popup, a]() { popup->hide(); a->trigger(); }); return; }
                if (auto *m = qobject_cast<QMenu *>(popup)) {
                    for (QAction *a : m->actions())
                        if (a->text().contains("Закрепить")) { std::fprintf(stderr, "pilot: trigger pin (menu)\n"); a->trigger(); m->close(); return; }
                }
            }
            if (modal) {
                if (auto *d = qobject_cast<QDialog *>(modal)) { std::fprintf(stderr, "pilot: accept dialog\n"); QTimer::singleShot(0, d, [d]() { d->accept(); }); ++step; }
            }
        });
        pilot.start();
        Files::Entry e; e.path = dir + "/menu.pdf"; e.name = "menu.pdf";
        QTimer::singleShot(0, [&]() { fs->onMenu(e, QPoint(800, 800)); });
        wait(3000);
        pilot.stop();
        int files = 0;
        for (const PinItem &pin : db.getAllPins()) if (pin.type == "file" && pin.filepath.endsWith("menu.pdf")) ++files;
        std::fprintf(stderr, "pins before=%d after=%d menu.pdf pins=%d\n", before, int(db.getAllPins().size()), files);
        return 0;
    }
    if (out == "folders") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        db.addFolder("Альфа");
        db.addFolder("Бета", "Альфа");
        for (int i = 0; i < 4; ++i) db.addPin("Альфа", QString("альфа %1").arg(i), "text", "a", "");
        for (int i = 0; i < 3; ++i) db.addPin("Альфа/Бета", QString("бета %1").arg(i), "text", "b", "");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1000);
        auto click = [&](const QString &txt) {
            for (auto *b : w.findChildren<QPushButton*>())
                if (b->isVisible() && b->text().contains(txt)) { b->click(); return true; }
            return false;
        };
        click("Закрепы");
        wait(800);
        std::fprintf(stderr, "importBtns visible=%d,%d\n", w.m_importVideoBtn->isVisible(), w.m_importAudioBtn->isVisible());
        FilesStrip *fs = nullptr;
        for (QObject *o : w.children()) if (auto *x = dynamic_cast<FilesStrip *>(o)) fs = x;
        const QRect sr = fs->collapsedRect();
        std::fprintf(stderr, "strip=%d,%d %dx%d video=%d,%d audio=%d,%d\n", sr.x(), sr.y(), sr.width(), sr.height(),
                     w.m_importVideoBtn->x(), w.m_importVideoBtn->y(), w.m_importAudioBtn->x(), w.m_importAudioBtn->y());
        w.grab().save("folders_0.png");
        std::fprintf(stderr, "click Альфа=%d\n", click("Альфа"));
        wait(90);
        w.grab().save("folders_mid.png");
        wait(600);
        std::fprintf(stderr, "folder now=%s\n", qPrintable(w.m_currentFolder));
        click("Бета");
        wait(700);
        bool home = false;
        for (auto *b : w.m_foldersBar->findChildren<QPushButton*>()) if (b->toolTip().contains("Ко всем")) home = b->isVisible();
        std::fprintf(stderr, "folder now=%s home=%d\n", qPrintable(w.m_currentFolder), home);
        w.grab().save("folders_1.png");
        click("История");
        wait(600);
        click("Закрепы");
        wait(800);
        std::fprintf(stderr, "after switch folder=%s\n", qPrintable(w.m_currentFolder));
        return 0;
    }
    if (out == "carry") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        const QString dir = tmp.path() + "/carry";
        QDir().mkpath(dir);
        for (const char *n : {"a.pdf", "b.txt", "c.zip", "d.xlsx"}) { QFile f(dir + "/" + n); f.open(QIODevice::WriteOnly); f.write(QByteArray(900, 'x')); }
        AppSettings::get().setFilesFolders({dir});
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1200);
        FilesStrip *fs = w.m_filesStrip;
        fs->m_t = fs->m_target = 1.0;  // Раскрыта
        fs->update();
        wait(100);
        const int y = fs->top() + 38 + 40;
        auto send = [&](QEvent::Type t, QPoint pt, Qt::MouseButton b, Qt::MouseButtons bs) {
            QMouseEvent ev(t, QPointF(pt), fs->mapToGlobal(QPointF(pt)), b, bs, Qt::NoModifier);
            QApplication::sendEvent(fs, &ev);
        };
        send(QEvent::MouseButtonPress, QPoint(56, y), Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, QPoint(70, y + 8), Qt::NoButton, Qt::LeftButton);  // Потянул — взял
        Carry *c = w.m_carry;
        c->m_poll.stop();  // Кнопки мыши в проверке не зажаты — за ними не следим
        wait(600);
        std::fprintf(stderr, "carrying=%d carry=%d ghost=%d t=%.2f\n", c->active(), int(c->items().size()), !c->m_ghost.isNull(), fs->m_t);
        CarryItem it; QRectF from;
        for (int x : {156, 256})
            if (fs->carryItemAt(fs->mapToGlobal(QPoint(x, y)), it, from)) c->toggle(it, from);
        wait(600);
        std::fprintf(stderr, "after RMB: carry=%d t=%.2f\n", int(c->items().size()), fs->m_t);
        if (c->m_ghost) c->m_ghost->grab().save("carry_ghost.png");
        fs->grab().save("carry_strip.png");
        c->cancel();
        wait(300);
        std::fprintf(stderr, "after release: carrying=%d carry=%d\n", c->active(), int(c->items().size()));
        return 0;
    }
    if (out == "tabs") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        QStringList dirs;
        for (int d = 0; d < 7; ++d) {
            const QString dir = tmp.path() + QString("/folder_%1_long_name").arg(d);
            QDir().mkpath(dir);
            for (int i = 0; i <= d % 3; ++i) { QFile f(dir + QString("/f%1_%2.txt").arg(d).arg(i)); f.open(QIODevice::WriteOnly); f.write("x"); }
            dirs << dir;
        }
        AppSettings::get().setFilesFolders(dirs);
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1200);
        FilesStrip *fs = w.m_filesStrip;
        fs->m_t = fs->m_target = 1.0;
        fs->update();
        wait(100);
        std::fprintf(stderr, "history: tabs=%d items=%d maxTabScroll=%d\n", int(fs->currentTabs().size()), int(fs->m_items.size()), fs->maxTabScroll());
        fs->grab().save("tabs_0.png");
        QRect plus;
        const QList<QRect> rects = fs->tabRects(&plus);
        const QPoint at = rects[2].center() + QPoint(0, fs->top());
        QMouseEvent pr(QEvent::MouseButtonPress, QPointF(at), fs->mapToGlobal(QPointF(at)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(fs, &pr);
        QMouseEvent rl(QEvent::MouseButtonRelease, QPointF(at), fs->mapToGlobal(QPointF(at)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(fs, &rl);
        wait(100);
        std::fprintf(stderr, "after tab click: items=%d tab=%d\n", int(fs->m_items.size()), int(!fs->m_tabHistory.isEmpty()));
        // Колёсико над вкладками — листаются
        const QPointF wp(fs->tabsRow().center() + QPoint(0, fs->top()));
        QWheelEvent we(wp, fs->mapToGlobal(wp), QPoint(), QPoint(0, -240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(fs, &we);
        std::fprintf(stderr, "tabScroll=%d\n", fs->m_tabScroll);
        fs->grab().save("tabs_1.png");
        // Закрепы: вкладки-полки
        const int a = db.addPin("", "", "file", "Работа", dirs[0] + "/f0_0.txt");
        db.addPin("", "", "file", "", dirs[1] + "/f1_0.txt");
        db.addPin("", "", "file", "Работа", dirs[1] + "/f1_1.txt");
        Q_UNUSED(a);
        AppSettings::get().setPinFileTabs("", {"Учёба"});
        for (auto *b : w.findChildren<QPushButton*>()) if (b->text().contains("Закрепы")) { b->click(); break; }
        wait(800);
        fs->m_t = fs->m_target = 1.0;
        fs->update();
        wait(100);
        std::fprintf(stderr, "pins: tabs=%d items=%d\n", int(fs->currentTabs().size()), int(fs->m_items.size()));
        fs->m_tabPins = "Работа"; fs->applyFilter();
        std::fprintf(stderr, "pins tab Работа: items=%d\n", int(fs->m_items.size()));
        fs->grab().save("tabs_2.png");
        return 0;
    }
    if (out == "hold") {
        // Переключение вкладок: открытая полоска не съёживается
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        const QString big = tmp.path() + "/bigdir", small = tmp.path() + "/smalldir";
        QDir().mkpath(big); QDir().mkpath(small);
        for (int i = 0; i < 20; ++i) { QFile f(big + QString("/b%1.txt").arg(i)); f.open(QIODevice::WriteOnly); f.write("x"); }
        for (int i = 0; i < 2; ++i) { QFile f(small + QString("/s%1.txt").arg(i)); f.open(QIODevice::WriteOnly); f.write("x"); }
        AppSettings::get().setFilesFolders({big, small});
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1200);
        FilesStrip *fs = w.m_filesStrip;
        fs->m_tabHistory = big; fs->applyFilter();
        fs->expand(true);
        fs->m_hoverPoll.stop();   // Настоящая мышь не над полоской — не даём свернуться
        wait(700);
        const int topBig = fs->mapTo(&w, QPoint(0, fs->top())).y();
        std::fprintf(stderr, "big: items=%d visibleTop=%d body=%d\n", int(fs->m_items.size()), topBig, fs->bodyHeight());
        fs->m_tabHistory = small; fs->applyFilter();
        wait(400);
        const int topSmall = fs->mapTo(&w, QPoint(0, fs->top())).y();
        std::fprintf(stderr, "small: items=%d visibleTop=%d body=%d sameTop=%d\n", int(fs->m_items.size()), topSmall, fs->bodyHeight(), int(topSmall == topBig));
        fs->grab().save("hold_small.png");
        fs->expand(false);
        wait(600);
        fs->expand(true);
        fs->m_hoverPoll.stop();
        wait(700);
        std::fprintf(stderr, "reopen small: body=%d\n", fs->bodyHeight());
        fs->m_tabHistory = big; fs->applyFilter();
        wait(80);
        std::fprintf(stderr, "growing: body=%d\n", fs->bodyHeight());
        wait(500);
        std::fprintf(stderr, "grown: body=%d\n", fs->bodyHeight());
        return 0;
    }
    if (out == "ghoststrip") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        const QString few = tmp.path() + "/fewdir";
        QDir().mkpath(few);
        for (int i = 0; i < 3; ++i) { QFile f(few + QString("/doc%1.pdf").arg(i)); f.open(QIODevice::WriteOnly); f.write("x"); }
        AppSettings::get().setFilesFolders({few});
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1200);
        FilesStrip *fs = w.m_filesStrip;
        fs->grab().save("gs_collapsed.png");
        fs->m_t = fs->m_target = 1.0; fs->update(); wait(100);
        fs->grab().save("gs_few.png");
        fs->m_t = fs->m_target = 0.0;
        db.addFolder("ПустаяПапка");
        for (auto *b : w.findChildren<QPushButton*>()) if (b->text().contains("Закрепы")) { b->click(); break; }
        wait(600);
        w.goToFolder("ПустаяПапка");
        wait(800);
        fs->m_t = fs->m_target = 1.0; fs->update(); wait(100);
        fs->grab().save("gs_empty.png");
        return 0;
    }
    if (out == "icons") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        const QString dir = tmp.path() + "/icondir";
        QDir().mkpath(dir);
        for (const char *n : {"Report.docx", "Archive.zip", "Doc.pdf", "Table.xlsx", "Notes.txt", "Slides.pptx"}) {
            QFile f(dir + "/" + n); f.open(QIODevice::WriteOnly); f.write("x");
        }
        QImage img(400, 260, QImage::Format_RGB32); img.fill(QColor(70, 140, 220)); img.save(dir + "/Photo.png");
        QFile::copy("C:/Windows/notepad.exe", dir + "/Tool.exe");
        AppSettings::get().setFilesFolders({dir});
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(2500);
        FilesStrip *fs = w.m_filesStrip;
        fs->m_t = fs->m_target = 1.0; fs->update(); wait(200);
        fs->grab().save("icons.png");
        return 0;
    }
    if (out == "pintext") {
        // Закрепить текст и картинку так же, как из меню; появятся ли они во «Все»
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        AppSettings::get().setPinsNoName(true);
        AppSettings::get().setPinsNoFolder(true);
        QImage img(320, 200, QImage::Format_RGB32); img.fill(QColor(200, 60, 60));
        const QString f = db.imagesPath() + "/pinme.png"; img.save(f);
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(800);
        const int before = db.getAllPins().size();
        w.pinText("PIN_TEXT_MARKER");
        w.pinImage(f);
        int text = 0, image = 0;
        for (const PinItem &pin : db.getAllPins()) {
            if (pin.content == "PIN_TEXT_MARKER") ++text;
            if (pin.filepath == f) ++image;
        }
        std::fprintf(stderr, "db: before=%d after=%d text=%d image=%d\n", before, int(db.getAllPins().size()), text, image);
        int inAll = 0;
        for (const PinItem &pin : db.getPins("")) if (pin.content == "PIN_TEXT_MARKER" || pin.filepath == f) ++inAll;
        std::fprintf(stderr, "getPins(All) has them: %d\n", inAll);
        for (auto *b : w.findChildren<QPushButton*>()) if (b->text().contains("Закрепы")) { b->click(); break; }
        wait(1000);
        int shownText = 0, shownImg = 0;
        for (auto *c : w.findChild<QScrollArea*>("leftScroll")->widget()->findChildren<QLabel*>(Qt::FindDirectChildrenOnly))
            if (c->isVisible() && c->text().contains("PIN_TEXT_MARKER")) ++shownText;
        for (auto *c : w.findChild<QScrollArea*>("rightScroll")->widget()->findChildren<QLabel*>(Qt::FindDirectChildrenOnly))
            if (c->isVisible() && c->property("filepath").toString() == f) ++shownImg;
        std::fprintf(stderr, "shown in Pins: text=%d image=%d folder=[%s]\n", shownText, shownImg, qPrintable(w.m_currentFolder));
        return 0;
    }
    if (out == "askpin") {
        // Закрепить текст с вопросами (название + папка): одно окно, закреп сохраняется
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        struct Hide : QObject { bool eventFilter(QObject *o, QEvent *e) override {
            if (e->type() == QEvent::Polish && o->isWidgetType()) {
                auto *wd = static_cast<QWidget *>(o);
                if (wd->isWindow()) wd->setAttribute(Qt::WA_DontShowOnScreen);
            }
            return false; } } hide;
        app.installEventFilter(&hide);
        AppSettings::get().setPinsNoName(false);
        AppSettings::get().setPinsNoFolder(false);
        db.addFolder("Работа");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(800);
        const int before = db.getAllPins().size();
        int dialogs = 0, combos = 0, edits = 0;
        QTimer pilot; pilot.setInterval(120);
        QObject::connect(&pilot, &QTimer::timeout, [&]() {
            if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                if (d->property("seen").toBool()) return;
                d->setProperty("seen", true);
                ++dialogs;
                combos = int(d->findChildren<QComboBox *>().size());
                edits = int(d->findChildren<QLineEdit *>().size());
                if (auto *c = d->findChild<QComboBox *>()) c->setCurrentIndex(c->findText("Работа"));
                QTimer::singleShot(0, d, [d]() { d->accept(); });
            }
        });
        pilot.start();
        QTimer::singleShot(0, [&]() { w.pinText("ASKPIN_MARKER"); });
        wait(2000);
        pilot.stop();
        QString folder;
        for (const PinItem &pin : db.getAllPins()) if (pin.content == "ASKPIN_MARKER") folder = pin.folder;
        std::fprintf(stderr, "dialogs=%d (edit=%d combo=%d) pins %d->%d folder=%s\n", dialogs, edits, combos,
                     before, int(db.getAllPins().size()), folder == "Работа" ? "Работа" : qPrintable(folder));
        return 0;
    }
    if (out == "pick") {
        // Рука: взять фото, добавить ещё, окошко (убрать, порядок), обратно в руку, отмена
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        for (int i = 0; i < 5; ++i) {
            QImage img(800, 450, QImage::Format_RGB32); img.fill(QColor::fromHsv(i * 60, 150, 210));
            const QString f = db.imagesPath() + QString("/pick%1.png").arg(i); img.save(f);
            db.addHistory("image", "", f, "app");
        }
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(2000);
        QList<ClickableCard *> imgs;
        for (auto *c : w.findChild<QScrollArea*>("rightScroll")->widget()->findChildren<ClickableCard*>(Qt::FindDirectChildrenOnly))
            if (c->isVisible() && c->property("pickable").toBool()) imgs << c;
        std::sort(imgs.begin(), imgs.end(), [](QWidget *a, QWidget *b) { return a->y() < b->y(); });
        std::fprintf(stderr, "pickable image cards=%d\n", int(imgs.size()));
        Carry *c = w.m_carry;
        w.carryPhoto(imgs[0]);
        c->m_poll.stop();  // Кнопки мыши в проверке не зажаты — за ними не следим
        CarryItem it; QRectF from;
        w.photoCarryItem(imgs[2], it, from); c->toggle(it, from);
        w.photoCarryItem(imgs[3], it, from); c->toggle(it, from);
        wait(600);
        std::fprintf(stderr, "carry=%d items=%d picked0=%d picked1=%d ghost=%d inflight=%d\n", c->active(), int(c->items().size()),
                     imgs[0]->isPicked(), imgs[1]->isPicked(), !c->m_ghost.isNull(), int(c->m_inFlight.size()));
        // Зона: над правой колонкой — да, посреди экрана — нет
        const QPoint inR = w.m_rightScroll->mapToGlobal(w.m_rightScroll->rect().center());
        const QPoint mid = w.mapToGlobal(QPoint(w.width() / 2, w.height() / 3));
        std::fprintf(stderr, "zone right=%d middle=%d\n", w.carryZone(inR), w.carryZone(mid));
        CarryItem under; QRectF uf;
        std::fprintf(stderr, "photoAt card1=%d\n", w.photoAt(imgs[1]->mapToGlobal(imgs[1]->rect().center()), under, uf));
        w.grab().save("carry_marks.png");
        // Окошко
        c->openPanel();
        wait(500);
        CarryPanel *pn = c->m_panel;
        std::fprintf(stderr, "panel=%d state=%d tiles=%d\n", pn != nullptr, int(c->m_state), pn ? pn->m_board.count() : -1);
        pn->grab().save("carry_panel.png");
        pn->removeTile(1);           // Убрать второе
        wait(400);
        std::fprintf(stderr, "after remove: items=%d tiles=%d\n", int(c->items().size()), pn->m_board.count());
        pn->m_board.tiles.move(1, 0); // Поменять порядок
        if (pn->onChanged) pn->onChanged(pn->m_board.items());
        std::fprintf(stderr, "order first=%s\n", qPrintable(QFileInfo(c->items()[0].path).fileName()));
        pn->regrab();
        wait(700);
        std::fprintf(stderr, "after regrab: state=%d items=%d ghost=%d panel=%d\n", int(c->m_state), int(c->items().size()),
                     !c->m_ghost.isNull(), !c->m_panel.isNull());
        // Маленькая копия
        auto *peek = new CarryPeek(&w, 100, c->items());
        wait(250);
        peek->grab().save("carry_peek.png");
        peek->leave();
        wait(300);
        c->cancel();
        wait(300);
        std::fprintf(stderr, "after cancel: carry=%d picked0=%d\n", c->active(), imgs[0]->isPicked());
        // Клик без удержания — вставка по отпусканию, рука не берётся
        {   // Ctrl+клик — просмотр, без вставки и без руки
            int pasted = 0;
            auto conn = QObject::connect(imgs[1], &ClickableCard::clicked, [&]() { ++pasted; });
            QMouseEvent cp(QEvent::MouseButtonPress, QPointF(20, 20), imgs[1]->mapToGlobal(QPointF(20, 20)), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
            w.pickEvent(imgs[1], &cp);
            wait(400);
            QMouseEvent cr(QEvent::MouseButtonRelease, QPointF(20, 20), imgs[1]->mapToGlobal(QPointF(20, 20)), Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
            w.pickEvent(imgs[1], &cr);
            wait(500);
            std::fprintf(stderr, "ctrl+click: viewer=%d pasted=%d carry=%d\n", w.m_imageViewer->isVisible(), pasted, c->active());
            QObject::disconnect(conn);
            w.m_imageViewer->hide();
            wait(200);
        }
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(20, 20), imgs[1]->mapToGlobal(QPointF(20, 20)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        w.pickEvent(imgs[1], &press);
        std::fprintf(stderr, "press: holdTimer=%d carry=%d\n", w.m_pickHoldTimer->isActive(), c->active());
        return 0;
    }
    if (out == "idlecpu") {
        // Нагрузка открытого окна: процессор за 10 с простоя и цена пересчёта дуги
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        auto cpu = []() { FILETIME a, b, k, u; GetProcessTimes(GetCurrentProcess(), &a, &b, &k, &u);
            auto v = [](FILETIME f) { return (qint64(f.dwHighDateTime) << 32 | f.dwLowDateTime) / 10000; }; return v(k) + v(u); };
        for (int i = 0; i < 150; ++i) db.addHistory("text", QString("history text %1 lorem ipsum").arg(i), "", "app");
        for (int i = 0; i < 40; ++i) {
            QImage img(800, 450, QImage::Format_RGB32); img.fill(QColor::fromHsv(i * 9, 150, 210));
            const QString f = db.imagesPath() + QString("/cpu%1.png").arg(i); img.save(f);
            db.addHistory("image", "", f, "app");
        }
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(3000);
        const qint64 c0 = cpu();
        { QEventLoop loop; QTimer::singleShot(10000, &loop, &QEventLoop::quit); loop.exec(); }
        const qint64 c1 = cpu();
        std::fprintf(stderr, "idle 10s: cpu=%lld ms (%.2f%% of one core)\n", c1 - c0, (c1 - c0) / 100.0);
        QElapsedTimer t; t.start();
        for (int i = 0; i < 200; ++i) { w.m_rightArc->apply(); w.m_leftArc->apply(); }
        std::fprintf(stderr, "apply both columns: %.3f ms per call\n", t.nsecsElapsed() / 1e6 / 200);
        return 0;
    }
    if (out == "dragmid") {
        // Колёсико во время переноса Windows: отменить перенос и открыть окошко
        Carry c(&w, 100);
        c.m_state = Carry::Dragging;
        c.m_items.append(CarryItem{"C:/x.png", QPixmap(), "x.png"});
        // Кнопки мыши в тесте не зажаты — отпущенная ЛКМ должна бросать
        auto *src = new CarryUi::DropSource(&c.m_abort, [&c]() { c.dragTick(); });
        const HRESULT up = src->QueryContinueDrag(FALSE, MK_LBUTTON);
        std::fprintf(stderr, "lmb up (stale keys): drop=%d\n", up == DRAGDROP_S_DROP);
        // Колёсико: нажали — ещё не отмена, отпустили — отмена ради окошка
        ++Carry::s_downs; Carry::s_mid = true;
        c.dragTick();
        std::fprintf(stderr, "pressed: abort=%d\n", c.m_abort);
        Carry::s_mid = false;
        c.dragTick();
        const HRESULT hr = src->QueryContinueDrag(FALSE, MK_LBUTTON);
        std::fprintf(stderr, "released: cancel=%d wantPanel=%d\n", hr == DRAGDROP_S_CANCEL, c.m_wantPanel);
        src->Release();
        c.m_state = Carry::Idle;
        c.m_wantPanel = false;
        return hr == DRAGDROP_S_CANCEL && up == DRAGDROP_S_DROP ? 0 : 1;
    }
    if (out == "dropdata") {
        // Данные для переноса: файлы из разных папок, как из Проводника
        QStringList paths;
        QDir().mkpath(tmp.path() + "/a"); QDir().mkpath(tmp.path() + "/b");
        for (const QString &f : {tmp.path() + "/a/one.png", tmp.path() + "/b/two.txt", tmp.path() + "/b/three.pdf"}) {
            QFile x(f); x.open(QIODevice::WriteOnly); x.write("x"); paths << f;
        }
        IDataObject *obj = CarryUi::filesDataObject(paths);
        std::fprintf(stderr, "dataObject=%d\n", obj != nullptr);
        if (!obj) return 1;
        FORMATETC fe = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM sm = {};
        if (SUCCEEDED(obj->GetData(&fe, &sm))) {
            HDROP h = static_cast<HDROP>(GlobalLock(sm.hGlobal));
            const UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
            std::fprintf(stderr, "hdrop files=%u\n", n);
            for (UINT i = 0; i < n; ++i) { wchar_t b[MAX_PATH]; DragQueryFileW(h, i, b, MAX_PATH);
                std::fprintf(stderr, "  %s\n", qPrintable(QFileInfo(QString::fromWCharArray(b)).fileName())); }
            GlobalUnlock(sm.hGlobal);
            ReleaseStgMedium(&sm);
        } else std::fprintf(stderr, "no CF_HDROP\n");
        obj->Release();
        return 0;
    }
    if (out == "files") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        // Тестовая папка вместо «Загрузок» пользователя
        const QString dir = tmp.path() + "/downloads";
        QDir().mkpath(dir);
        QImage img(320, 200, QImage::Format_RGB32); img.fill(QColor(40, 140, 90));
        img.save(dir + "/photo_from_chat.png");
        QImage img2(200, 320, QImage::Format_RGB32); img2.fill(QColor(200, 90, 40));
        img2.save(dir + "/screenshot 2026-10-21.jpg");
        QFile::copy("C:/Windows/SystemApps/Microsoft.Windows.CloudExperienceHost_cw5n1h2txyewy/media/oobe-intro.mp4", dir + "/clip.mp4");
        QFile::copy("C:/Windows/notepad.exe", dir + "/SomeInstaller_v1.2.exe");
        for (const char *n : {"report.pdf", "notes.txt", "archive.zip", "table.xlsx", "song.mp3", "half.crdownload", "data.bin"}) {
            QFile f(dir + "/" + n); f.open(QIODevice::WriteOnly); f.write(QByteArray(1500, 'x'));
        }
        AppSettings::get().setFilesFolders({dir});
        AppSettings::get().setFilesView("tiles");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(2500);
        FilesStrip *fs = nullptr;
        for (QObject *o : w.children()) if (auto *x = dynamic_cast<FilesStrip *>(o)) fs = x;
        std::fprintf(stderr, "strip=%d visible=%d geo=%d,%d %dx%d\n", fs != nullptr, fs && fs->isVisible(),
                     fs ? fs->x() : 0, fs ? fs->y() : 0, fs ? fs->width() : 0, fs ? fs->height() : 0);
        if (!fs) return 1;
        w.grab().save("files_0.png");
        const QPointF ep(20, fs->height() - 10);
        QEnterEvent en(ep, ep, fs->mapToGlobal(ep));
        QApplication::sendEvent(fs, &en);
        // Кадры анимации: сколько раз перерисовалась полоска за раскрытие
        int paints = 0;
        struct PaintCount : QObject { int *n; bool eventFilter(QObject *, QEvent *e) override { if (e->type() == QEvent::Paint) ++*n; return false; } } pc;
        pc.n = &paints;
        fs->installEventFilter(&pc);
        wait(120);
        w.grab().save("files_mid.png");
        wait(680);
        fs->removeEventFilter(&pc);
        std::fprintf(stderr, "paints during expand=%d\n", paints);
        std::fprintf(stderr, "expanded geo=%d,%d %dx%d\n", fs->x(), fs->y(), fs->width(), fs->height());
        w.grab().save("files_1.png");
        AppSettings::get().setFilesView("list");
        fs->reload();
        wait(800);
        w.grab().save("files_2.png");
        // Поиск фильтрует файлы
        w.findChild<QLineEdit *>("searchEdit")->setText("pdf");
        wait(500);
        w.grab().save("files_3.png");
        AppSettings::get().setFilesStrip(false);
        fs->reload();
        wait(300);
        std::fprintf(stderr, "disabled visible=%d\n", fs->isVisible());
        return 0;
    }
    if (out == "media") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        // Тестовые файлы: тон 3 секунды (WAV) и копия видео Windows
        const QString dir = tmp.path() + "/media";
        QDir().mkpath(dir);
        const QString wav = dir + "/tone.wav", mp4 = dir + "/clip.mp4";
        {
            QFile f(wav); f.open(QIODevice::WriteOnly);
            const int rate = 22050, n = rate * 3;
            QByteArray pcm(n * 2, 0);
            for (int i = 0; i < n; ++i) { qint16 v = qint16(8000 * std::sin(i * 2 * M_PI * 440 / rate)); memcpy(pcm.data() + i * 2, &v, 2); }
            auto u32 = [](quint32 v) { return QByteArray(reinterpret_cast<const char *>(&v), 4); };
            auto u16 = [](quint16 v) { return QByteArray(reinterpret_cast<const char *>(&v), 2); };
            f.write("RIFF" + u32(36 + pcm.size()) + "WAVEfmt " + u32(16) + u16(1) + u16(1) + u32(rate) + u32(rate * 2) + u16(2) + u16(16) + "data" + u32(pcm.size()) + pcm);
        }
        QFile::copy("C:/Windows/SystemApps/Microsoft.Windows.CloudExperienceHost_cw5n1h2txyewy/media/oobe-intro.mp4", mp4);
        db.addHistory("audio", "tone.wav", wav, "test");
        db.addHistory("video", "clip.mp4", mp4, "test");
        auto &pb = Media::Playback::get();
        pb.output()->setVolume(0.0);  // Тест не должен звучать
        pb.onUnsupported = [](const QString &f) { std::fprintf(stderr, "UNSUPPORTED (no external player in test): %s\n", qPrintable(f)); };
        w.setAttribute(Qt::WA_DontShowOnScreen);
        VideoViewer *vv = nullptr;
        for (QObject *o : w.children()) if (auto *x = dynamic_cast<VideoViewer *>(o)) vv = x;
        if (vv) vv->setAttribute(Qt::WA_DontShowOnScreen);  // Не показывать на настоящем экране
        w.resize(1600, 900);
        w.showWindow();
        wait(2500);  // Превью видео — в фоне
        auto cardOf = [&](const char *area, const QString &fp) -> QWidget * {
            for (QWidget *c : w.findChild<QScrollArea*>(area)->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                if (c->property("filepath").toString() == fp) return c;
            return nullptr;
        };
        QWidget *ac = cardOf("leftScroll", wav), *vc = cardOf("rightScroll", mp4);
        std::fprintf(stderr, "audio card=%d video card=%d\n", ac != nullptr, vc != nullptr);
        w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("media_0.png");
        if (auto *cc = qobject_cast<ClickableCard *>(vc)) {  // Видео — в руку, как фото
            CarryItem it; QRectF from;
            const bool ok = w.photoCarryItem(cc, it, from);
            std::fprintf(stderr, "video carry: pickable=%d ok=%d thumb=%dx%d from=%.0fx%.0f\n", cc->property("pickable").toBool(), ok,
                         it.thumb.width(), it.thumb.height(), from.width(), from.height());
            w.carryPhoto(cc);
            w.m_carry->m_poll.stop();
            wait(600);
            std::fprintf(stderr, "video carried=%d picked=%d\n", w.m_carry->has(mp4), cc->isPicked());
            cc->grab().save("video_picked.png");
            w.m_carry->cancel();
            wait(400);
            std::fprintf(stderr, "after cancel picked=%d\n", cc->isPicked());
        }
        auto buttons = [](QWidget *c) { QList<QAbstractButton*> b; for (auto *x : c->findChildren<QAbstractButton*>(Qt::FindDirectChildrenOnly)) b << x; return b; };
        // Аудио: ▶
        buttons(ac).value(0)->click();
        wait(1500);
        std::fprintf(stderr, "audio: state=%d pos=%lld dur=%lld err=%d\n", int(pb.player()->playbackState()),
                     (long long)pb.player()->position(), (long long)pb.player()->duration(), int(pb.player()->error()));
        // Видео: ▶ (первая кнопка)
        buttons(vc).value(0)->click();
        wait(2000);
        std::fprintf(stderr, "video: state=%d pos=%lld dur=%lld muted=%d err=%d\n", int(pb.player()->playbackState()),
                     (long long)pb.player()->position(), (long long)pb.player()->duration(), pb.output()->isMuted(), int(pb.player()->error()));
        w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("media_1.png");
        // Большой просмотр: ⤢ (третья кнопка)
        buttons(vc).value(2)->click();
        wait(1200);
        QWidget *viewer = nullptr;
        for (QWidget *tl : QApplication::topLevelWidgets()) if (tl->isVisible() && tl != &w && tl->metaObject()->className() == QByteArray("QWidget") && tl->width() > 800) viewer = tl;
        std::fprintf(stderr, "viewer visible=%d pos=%lld\n", viewer != nullptr, (long long)pb.player()->position());
        if (vv && vv->isVisible()) {
            QPixmap bg = w.grab(); QPainter pp(&bg); pp.drawPixmap(0, 0, vv->grab()); pp.end();
            bg.scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("media_2.png");
        }
        // Закрыли SmartClip — всё остановилось
        w.hide();
        wait(300);
        std::fprintf(stderr, "after hide: state=%d viewer visible=%d\n", int(pb.player()->playbackState()), viewer && viewer->isVisible());
        return 0;
    }
    if (out == "refresh") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        AppSettings::get().setMaxTexts(1000);
        for (int i = 0; i < 300; ++i)
            db.addHistory("text", QString("Старая запись %1 ").arg(i) + QString("текст ").repeated(40));
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        auto *L = w.findChild<QScrollArea*>("leftScroll");
        auto texts = [&]() {
            QStringList t;
            for (int i = 0; i < 400; ++i) {
                QLayoutItem *it = L->widget()->layout()->itemAt(i);
                if (!it) break;
                if (QWidget *c = it->widget()) if (c->property("itemId").isValid() && !c->property("fullText").toString().isEmpty())
                    t << c->property("fullText").toString().section(' ', 0, 2);
            }
            return t;
        };
        QElapsedTimer T; T.start();
        w.showWindow(); wait(50);
        std::fprintf(stderr, "first open: %lld ms, cards=%d top=%s\n", (long long)T.elapsed(), int(texts().size()), qPrintable(texts().value(0)));
        w.hide(); wait(100);
        // Скопировали две новые записи и повторно — одну старую («Без повторов» поднимает её наверх)
        db.addHistory("text", "Новая запись A");
        db.addHistory("text", "Новая запись B");
        const QString again = QString("Старая запись 298 ") + QString("текст ").repeated(40);
        db.removeTextDuplicates(again);
        db.addHistory("text", again);
        w.markHistoryDirty();
        T.restart();
        w.showWindow(); wait(50);
        const QStringList t2 = texts();
        std::fprintf(stderr, "second open: %lld ms, cards=%d\n  top4: %s\n  dup of 298: %d\n", (long long)T.elapsed(), int(t2.size()),
                     qPrintable(t2.mid(0, 4).join(" | ")), int(t2.count("Старая запись 298")));
        // Поиск
        auto *search = w.findChild<QLineEdit*>("searchEdit");
        if (!search) for (QLineEdit *le : w.findChildren<QLineEdit*>()) { search = le; break; }
        T.restart();
        search->setText("запись 17");
        wait(400);
        std::fprintf(stderr, "search 'запись 17': %d cards (expect 11: 17,170..179) in %lld ms incl. 200ms debounce\n", int(texts().size()), (long long)T.elapsed());
        return 0;
    }
    if (out == "ruler") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        if (argc > 2) AppSettings::get().setDateRuler(argv[2]);
        for (int i = 0; i < 40; ++i)
            db.addHistory("text", QString("Запись номер %1").arg(i));
        QImage im(400, 240, QImage::Format_RGB32);
        for (int i = 0; i < 16; ++i) {
            im.fill(QColor::fromHsv(i * 22, 150, 210));
            const QString f = db.imagesPath() + QString("/img_r%1.png").arg(i);
            im.save(f);
            db.addHistory("image", "", f, "app");
        }
        // Даты: свежие — сегодня, дальше по 3 записи на день, самые старые — год назад
        QSqlQuery q;
        q.exec("UPDATE history SET created_at = datetime('now', '-' || (((SELECT MAX(id) FROM history) - id) / 3) || ' days')");
        q.exec("UPDATE history SET created_at = datetime(created_at, '-400 days') WHERE id < 8");
        for (const HistoryItem &h : db.getHistoryPage({"text"}, 2, 0, false))
            std::fprintf(stderr, "createdAt='%s'\n", qPrintable(h.createdAt));
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1200);
        for (const char *name : {"leftScroll", "rightScroll"}) {
            auto *a = w.findChild<QScrollArea*>(name);
            QPointF pt(a->viewport()->width() / 2.0, a->viewport()->height() / 2.0);
            for (int i = 0; i < 5; ++i) {
                QWheelEvent we(pt, a->viewport()->mapToGlobal(pt), QPoint(), QPoint(0, -120), Qt::NoButton,
                               Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(a->viewport(), &we);
            }
        }
        wait(350);
        for (const char *name : {"leftScroll", "rightScroll"}) {
            auto *a = w.findChild<QScrollArea*>(name);
            for (QWidget *c : a->viewport()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                std::fprintf(stderr, "%s child %s vis=%d geo=%d,%d %dx%d transp=%d\n", name, c->metaObject()->className(),
                    c->isVisible(), c->x(), c->y(), c->width(), c->height(), c->testAttribute(Qt::WA_TransparentForMouseEvents));
            int dated = 0;
            for (QWidget *c : a->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) if (c->property("cardDate").isValid()) ++dated;
            std::fprintf(stderr, "%s dated cards=%d scroll=%d\n", name, dated, a->verticalScrollBar()->value());
        }
        w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("ruler.png");
        wait(1600);
        w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("ruler_after.png");
        if (argc > 3 && QString(argv[3]) == "scrub") {
            auto *a = w.findChild<QScrollArea*>("leftScroll");
            QPointF pt(a->viewport()->width() / 2.0, a->viewport()->height() / 2.0);
            QWheelEvent we(pt, a->viewport()->mapToGlobal(pt), QPoint(), QPoint(0, -120), Qt::NoButton,
                           Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(a->viewport(), &we);   // Показать шкалу
            wait(300);
            QWidget *grip = nullptr;
            for (QWidget *c : a->viewport()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                if (c->cursor().shape() == Qt::SizeVerCursor) grip = c;
            QScrollBar *sb = a->verticalScrollBar();
            std::fprintf(stderr, "grip=%d visible=%d before scroll=%d max=%d\n", grip != nullptr, grip && grip->isVisible(), sb->value(), sb->maximum());
            auto mouse = [&](QEvent::Type t, qreal y, Qt::MouseButtons btns) {
                QMouseEvent me(t, QPointF(10, y), grip->mapToGlobal(QPointF(10, y)), Qt::LeftButton, btns, Qt::NoModifier);
                QApplication::sendEvent(grip, &me);
            };
            const int H = grip->height();
            mouse(QEvent::MouseButtonPress, H * 0.5, Qt::LeftButton);
            for (int k = 0; k <= 10; ++k) { mouse(QEvent::MouseMove, H * (0.5 + 0.045 * k), Qt::LeftButton); wait(20); }
            std::fprintf(stderr, "during scrub at 95%%: scroll=%d max=%d\n", sb->value(), sb->maximum());
            w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("scrub.png");
            mouse(QEvent::MouseMove, 14, Qt::LeftButton); wait(50);
            std::fprintf(stderr, "scrub to top: scroll=%d\n", sb->value());
            mouse(QEvent::MouseButtonRelease, 14, Qt::NoButton);
        }
        return 0;
    }
    if (out == "drag") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        for (int i = 0; i < 8; ++i)
            db.addPin(argc > 2 ? QString("") : QString("Работа"), QString("Закреп %1").arg(i), "text", QString("Текст закрепа номер %1").arg(i), "");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(800);
        for (auto *b : w.findChildren<QAbstractButton*>())
            if (b->text().contains(QString::fromUtf8("Закрепы"))) { b->click(); break; }
        wait(900);
        for (auto *b : w.findChildren<QAbstractButton*>())
            if (argc <= 2 && b->text() == QString::fromUtf8("Работа")) { b->click(); break; }
        wait(900);
        auto *L = w.findChild<QScrollArea*>("leftScroll");
        QList<ClickableCard*> cards;
        for (QWidget *c : L->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
            if (auto *cc = qobject_cast<ClickableCard*>(c)) if (cc->property("pinId").toInt() > 0 && cc->isVisible()) cards << cc;
        std::sort(cards.begin(), cards.end(), [](QWidget *a, QWidget *b) { return a->y() < b->y(); });
        auto order = [&]() { QStringList o; for (const PinItem &pi : db.getPins(argc > 2 ? QString("") : QString("Работа"))) o << pi.name.section(' ', 1); return o.join(','); };
        std::fprintf(stderr, "cards=%d order before: %s\n", int(cards.size()), qPrintable(order()));
        ClickableCard *c0 = cards.value(0);
        const QPoint start = c0->mapToGlobal(QPoint(c0->width() / 2, c0->height() / 2));
        const QPoint end = cards.value(4)->mapToGlobal(QPoint(cards[4]->width() / 2, cards[4]->height() / 2 + 6));
        emit c0->dragStarted(start);
        for (int k = 1; k <= 20; ++k) {
            emit c0->dragMoved(start + (end - start) * k / 20);
            wait(15);
            if (k == 12) w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("drag_mid.png");
        }
        wait(300);
        w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("drag_hold.png");
        emit c0->dragFinished();
        wait(800);
        w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("drag_done.png");
        std::fprintf(stderr, "order after:  %s\n", qPrintable(order()));
        return 0;
    }
    if (out == "upddlg") {
        const QString body = QString::fromUtf8(
            "<!-- lang: ru -->\n### Главное\n- **Дуга** — крайние карточки уходят по дуге\n"
            "- Превью картинок: окно открывается быстрее\n- Надёжная проверка обновлений на мобильном интернете\n\n"
            "### Исправления\n- Вставка в поле переименования в Проводнике\n\n"
            "<!-- lang: en -->\n### Highlights\n- **Arc** scrolling\n");
        const bool minor = argc > 2 && QString(argv[2]) == "minor";
        AppSettings::get().setLanguage("ru");  // Описание — на русском
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.onUpdateAvailable(minor ? "2.0.1" : "2.1", "https://example.invalid/x.exe", body, "");
        QTimer::singleShot(900, [&]() {
            if (QWidget *d = QApplication::activeModalWidget()) {
                d->grab().save(minor ? "upd_minor.png" : "upd_major.png");
                d->close();
            }
        });
        if (argc > 3 && QString(argv[3]) == "settings") {
            for (auto *b : w.findChildren<QAbstractButton*>())
                if (b->toolTip().startsWith("Настройки")) { b->click(); break; }
        } else {
            w.showPendingUpdate();
        }
        return 0;
    }
    if (out == "imgload") {
        // 20 скриншотов 4K: насколько замирает окно, пока грузятся карточки
        QImage big(3840, 2160, QImage::Format_RGB32);
        for (int i = 0; i < 20; ++i) {
            big.fill(QColor::fromHsv(i * 18, 150, 200));
            {   // Похоже на скриншот: много мелких элементов и текста
                QPainter pp(&big);
                quint32 seed = 1234 + i;
                auto rnd = [&seed](int m) { seed = seed * 1664525u + 1013904223u; return int((seed >> 8) % quint32(m)); };
                for (int k = 0; k < 3000; ++k)
                    pp.fillRect(rnd(3840), rnd(2160), 20 + rnd(200), 8 + rnd(60), QColor::fromRgb(rnd(256), rnd(256), rnd(256)));
                pp.setPen(Qt::black);
                for (int k = 0; k < 400; ++k)
                    pp.drawText(rnd(3700), rnd(2100), QString("Строка текста номер %1 на скриншоте").arg(k));
            }
            const QString f = db.imagesPath() + QString("/img_big%1.png").arg(i);
            big.save(f);
            db.addHistory("image", "", f, "app");
        }
        auto run = [&](const char *what) {
            QElapsedTimer t; t.start();
            qint64 last = 0, worst = 0;
            QTimer probe; probe.setTimerType(Qt::PreciseTimer);
            QObject::connect(&probe, &QTimer::timeout, [&]() { const qint64 n = t.elapsed(); worst = qMax(worst, n - last); last = n; });
            probe.start(5);
            w.setAttribute(Qt::WA_DontShowOnScreen);
            w.resize(1600, 900);
            w.showWindow();
            app.processEvents();
            last = t.elapsed(); worst = 0;   // Считаем только после открытия окна
            auto *R = w.findChild<QScrollArea*>("rightScroll");
            int done = 0;
            while (t.elapsed() < 8000) {
                app.processEvents();
                done = 0;
                for (QLabel *c : R->widget()->findChildren<QLabel*>(Qt::FindDirectChildrenOnly))
                    if (!c->pixmap().isNull()) ++done;
                if (done >= 20) break;
            }
            std::fprintf(stderr, "%s: all 20 loaded in %lld ms, worst UI freeze %lld ms (loaded=%d)\n",
                         what, (long long)t.elapsed(), (long long)worst, done);
            w.hide(); w.markHistoryDirty();
        };
        run("first open (thumbs created)");
        run("second open (thumbs from disk)");
        return 0;
    }
    if (out == "switch") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        AppSettings::get().setMaxTexts(1000);
        for (int i = 0; i < 120; ++i)
            db.addHistory("text", QString("История %1").arg(i));
        for (int i = 0; i < 25; ++i)
            db.addPin("", QString("Закреп %1").arg(i), "text", QString("Текст закрепа %1").arg(i), "");
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1200);
        auto *L = w.findChild<QScrollArea*>("leftScroll");
        auto btn = [&]() -> bool {
            for (QAbstractButton *b : L->viewport()->findChildren<QAbstractButton*>(Qt::FindDirectChildrenOnly))
                return b->isVisible();
            return false;
        };
        auto cards = [&]() { int n = 0; for (QWidget *c : L->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) if (c->property("itemId").isValid() || c->property("pinName").isValid() || c->property("fullText").isValid()) ++n; return n; };
        auto wheel = [&](int notches) {
            QPointF pt(L->viewport()->width() / 2.0, L->viewport()->height() / 2.0);
            for (int i = 0; i < notches; ++i) {
                QWheelEvent we(pt, L->viewport()->mapToGlobal(pt), QPoint(), QPoint(0, -120), Qt::NoButton,
                               Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(L->viewport(), &we);
                wait(40);
            }
        };
        auto clickTab = [&](const char *name) {
            for (auto *b : w.findChildren<QAbstractButton*>())
                if (b->text().contains(QString::fromUtf8(name))) { b->click(); break; }
            wait(600);
        };
        wheel(30); wait(1200);
        const int before = L->verticalScrollBar()->value();
        std::fprintf(stderr, "history: scroll=%d cards=%d\n", before, cards());
        clickTab("Закрепы");
        std::fprintf(stderr, "pins: scroll=%d cards=%d btn=%d\n", L->verticalScrollBar()->value(), cards(), btn());
        wheel(12); wait(1000);
        std::fprintf(stderr, "pins scrolled: scroll=%d btn=%d\n", L->verticalScrollBar()->value(), btn());
        clickTab("История");
        std::fprintf(stderr, "back to history: scroll=%d (was %d) cards=%d btn=%d\n",
                     L->verticalScrollBar()->value(), before, cards(), btn());
        return 0;
    }
    if (out == "totop") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        for (int i = 0; i < 40; ++i)
            db.addHistory("text", QString("Карточка номер %1").arg(i));
        QImage im(400, 240, QImage::Format_RGB32);
        for (int i = 0; i < 20; ++i) {
            im.fill(QColor::fromHsv(i * 18, 160, 220));
            const QString f = db.imagesPath() + QString("/img_top%1.png").arg(i);
            im.save(f);
            db.addHistory("image", "", f, "app");
        }
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1200);
        auto *L = w.findChild<QScrollArea*>("leftScroll");
        auto *R = w.findChild<QScrollArea*>("rightScroll");
        auto btn = [](QScrollArea *a) -> QAbstractButton * {
            for (QAbstractButton *b : a->viewport()->findChildren<QAbstractButton*>(Qt::FindDirectChildrenOnly)) return b;
            return nullptr;
        };
        auto state = [&](const char *what) {
            std::fprintf(stderr, "%s: L scroll=%d btn=%d | R scroll=%d btn=%d\n", what,
                         L->verticalScrollBar()->value(), btn(L) && btn(L)->isVisible(),
                         R->verticalScrollBar()->value(), btn(R) && btn(R)->isVisible());
        };
        state("start");
        auto wheel = [&](QScrollArea *a, int notches) {
            QPointF pt(a->viewport()->width() / 2.0, a->viewport()->height() / 2.0);
            for (int i = 0; i < notches; ++i) {
                QWheelEvent we(pt, a->viewport()->mapToGlobal(pt), QPoint(), QPoint(0, -120), Qt::NoButton,
                               Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(a->viewport(), &we);
            }
        };
        wheel(L, 2); wheel(R, 2); wait(800); state("2 notches");
        wheel(L, 8); wheel(R, 14); wait(1000); state("more");
        w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("top_btn.png");
        if (btn(L)) btn(L)->click();
        if (btn(R)) btn(R)->click();
        wait(1500); state("after click");
        return 0;
    }
    if (out == "textbot") {
        // Низ колонки текста (колонка до края экрана): как стоят карточки у нижнего края
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        for (int i = 0; i < 40; ++i)
            db.addHistory("text", QString("adb -s 58990 shell settings put secure %1 ").arg(i)
                + QString("long command line ").repeated(i % 5 == 0 ? 6 : (i % 3)));
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.showWindow();
        wait(1500);
        auto *area = w.findChild<QScrollArea*>("leftScroll");
        auto dump = [&](const char *what) {
            const int vh = area->viewport()->height();
            std::fprintf(stderr, "%s: win h=%d vh=%d area=%d..%d\n", what, w.height(), vh,
                         area->mapTo(&w, QPoint(0,0)).y(), area->mapTo(&w, QPoint(0,0)).y() + area->height());
            for (QWidget *c : area->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
                if (!c->property("itemId").isValid() || !c->isVisible()) continue;
                const int top = c->mapTo(area->viewport(), QPoint(0, 0)).y();
                if (top + c->height() < vh - 500 || top > vh + 50) continue;
                std::fprintf(stderr, "   top=%d h=%d x=%d base=%d\n", top, c->height(), c->x(), c->property("arcBase").toPoint().x());
            }
        };
        auto wheel = [&](int dy, int n) {
            QPointF pt(area->viewport()->width() / 2.0, area->viewport()->height() / 2.0);
            for (int i = 0; i < n; ++i) {
                QWheelEvent we(pt, area->viewport()->mapToGlobal(pt), QPoint(), QPoint(0, dy), Qt::NoButton,
                               Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(area->viewport(), &we);
                wait(30);
            }
        };
        auto midBent = [&](const char *what) {
            const int vh = area->viewport()->height();
            int n = 0;
            for (QWidget *c : area->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
                if (!c->property("itemId").isValid() || !c->isVisible()) continue;
                const int top = c->mapTo(area->viewport(), QPoint(0, 0)).y();
                if (top < 60 || top + c->height() > vh - 60) continue;
                if (c->x() != c->property("arcBase").toPoint().x()) {
                    if (n < 3) std::fprintf(stderr, "   %s: mid card top=%d x=%d\n", what, top, c->x());
                    ++n;
                }
            }
            std::fprintf(stderr, "%s: bent in middle=%d\n", what, n);
        };
        dump("start");
        wheel(-120, 6); wait(1200); dump("scrolled");
        auto *sc = w.m_leftScroller;
        for (int v : {300, 900, 1500, 2400, 1200, 600, 3000, 4000, 200}) { sc->jumpTo(v); app.processEvents(); midBent(qPrintable(QString("jump %1").arg(v))); }
        wait(600); midBent("settled");
        for (int i = 0; i < 40; ++i) { sc->jumpTo(100 + i * 70); wait(16); } midBent("drag sweep");
        w.grab().save("textbot.png"); wait(1500); w.grab().save("textbot2.png"); dump("after sweep");
        return 0;
    }
    if (out == "textarc") {
        // Колонка текста: карточки посреди колонки не должны стоять сдвинутыми вбок
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        for (int i = 0; i < 40; ++i)
            db.addHistory("text", QString("adb -s 58990 shell settings put secure %1 ").arg(i)
                + QString("длинная строка команды ").repeated(i % 5 == 0 ? 6 : (i % 3)));
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1500);
        auto *area = w.findChild<QScrollArea*>("leftScroll");
        auto check = [&](const char *what) {
            const int vh = area->viewport()->height();
            QStringList bad;
            QMap<int,int> xs;
            for (QWidget *c : area->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
                if (!c->property("itemId").isValid() || !c->isVisible()) continue;
                const int top = c->mapTo(area->viewport(), QPoint(0, 0)).y();
                xs[c->property("arcBase").toPoint().x()]++;
                if (top < 60 || top + c->height() > vh - 60) continue;  // У краёв дуга — можно
                if (c->x() != c->property("arcBase").toPoint().x())
                    bad << QString("id%1 x=%2 base=%3").arg(c->property("itemId").toInt()).arg(c->x()).arg(c->property("arcBase").toPoint().x());
            }
            QStringList bx; for (auto it = xs.cbegin(); it != xs.cend(); ++it) bx << QString("%1:%2").arg(it.key()).arg(it.value());
            std::fprintf(stderr, "%-14s bases[%s] shifted=%d %s\n", what, qPrintable(bx.join(' ')), int(bad.size()), qPrintable(bad.mid(0, 4).join("; ")));
        };
        auto wheel = [&](int dy, int n) {
            QPointF pt(area->viewport()->width() / 2.0, area->viewport()->height() / 2.0);
            for (int i = 0; i < n; ++i) {
                QWheelEvent we(pt, area->viewport()->mapToGlobal(pt), QPoint(), QPoint(0, dy), Qt::NoButton,
                               Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(area->viewport(), &we);
                wait(30);
            }
        };
        check("start");
        wheel(-120, 6); wait(900); check("scroll down");
        wheel(120, 3); wait(900); check("scroll up");
        wheel(-120, 1); wait(40); wheel(120, 1); wait(40); wheel(-120, 2); wait(900); check("jitter");
        w.startFileDragSqueeze(); wait(500); w.endFileDragSqueeze(); wait(900); check("squeeze");
        db.addHistory("text", "новый текст сверху"); wait(1500); check("new item");
        wheel(120, 20); wait(1200); check("to top");
        w.hide(); wait(300); w.showWindow(); wait(1200); check("reshow");
        wheel(-120, 8); wait(60); w.hide(); wait(300); w.showWindow(); wait(1200); check("hide midscroll");
        // Выезд под мышью выключен: сбитая карточка всё равно возвращается на место
        AppSettings::get().setCardHover(false);
        w.hide(); wait(300); w.showWindow(); wait(1200);
        for (QWidget *c : area->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
            if (!c->property("itemId").isValid() || !c->isVisible()) continue;
            const int top = c->mapTo(area->viewport(), QPoint(0, 0)).y();
            if (top > 200) { c->move(c->x() - 120, c->y()); break; }
        }
        check("knocked");
        wait(400); check("hover off");
        return 0;
    }
    if (out == "pinview") {
        // Закрепы: Ctrl+клик по фото — просмотр, по видео — большой плеер (без вставки)
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        struct Hide : QObject { bool eventFilter(QObject *o, QEvent *e) override {
            if (e->type() == QEvent::Polish && o->isWidgetType()) {
                auto *wd = static_cast<QWidget *>(o);
                if (wd->isWindow()) wd->setAttribute(Qt::WA_DontShowOnScreen);
            }
            return false; } } hide;
        app.installEventFilter(&hide);
        QImage img(640, 360, QImage::Format_RGB32); img.fill(QColor(60, 140, 220));
        const QString f = db.imagesPath() + "/pinview.png"; img.save(f);
        const QString mp4 = tmp.path() + "/pinview.mp4";
        QFile::copy("C:/Windows/SystemApps/Microsoft.Windows.CloudExperienceHost_cw5n1h2txyewy/media/oobe-intro.mp4", mp4);
        db.addPin("", "фото", "image", "", f);
        db.addPin("", "видео", "video", "", mp4);
        Media::Playback::get().output()->setVolume(0.0);
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(800);
        for (auto *b : w.findChildren<QPushButton*>()) if (b->text().contains("Закрепы")) { b->click(); break; }
        wait(1500);
        int pasted = 0;
        QObject::connect(&w, &MainWindow::pasteRequested, [&]() { ++pasted; });
        auto pinCard = [&](const char *name) -> ClickableCard * {
            for (ClickableCard *c : w.findChildren<ClickableCard*>())
                if (c->isVisible() && c->property("pinName").toString() == QString::fromUtf8(name)) return c;
            return nullptr;
        };
        auto ctrlClick = [&](ClickableCard *c) {
            QMouseEvent pr(QEvent::MouseButtonPress, QPointF(40, 60), c->mapToGlobal(QPointF(40, 60)), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
            QApplication::sendEvent(c, &pr);
            QMouseEvent rl(QEvent::MouseButtonRelease, QPointF(40, 60), c->mapToGlobal(QPointF(40, 60)), Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
            QApplication::sendEvent(c, &rl);
        };
        w.m_prevFocusHwnd = 0;  // Даже если сломается — вставлять некуда (окна пользователя не трогаем)
        ClickableCard *pc = pinCard("фото"), *vc = pinCard("видео");
        std::fprintf(stderr, "photo pin=%d video pin=%d\n", pc != nullptr, vc != nullptr);
        if (!pc || !vc) return 1;
        ctrlClick(pc); wait(500);
        const bool photoView = w.m_imageViewer->isVisible();
        w.m_imageViewer->hide(); wait(200);
        ctrlClick(vc); wait(800);
        const bool videoView = w.m_videoViewer->isVisible();
        std::fprintf(stderr, "ctrl+click: photo viewer=%d video viewer=%d pasted=%d visible=%d\n", photoView, videoView, pasted, w.isVisible());
        return photoView && videoView && pasted == 0 ? 0 : 1;
    }
    if (out == "arc") {
        auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
        for (int i = 0; i < 30; ++i)
            db.addHistory("text", QString("Карточка номер %1 — проверка дуги").arg(i)
                + (i % 3 == 0 ? QString(" длинный текст, который переносится на несколько строк").repeated(1 + i % 4) : QString()));
        QImage im(400, 240, QImage::Format_RGB32);
        for (int i = 0; i < 12; ++i) {
            im.fill(QColor::fromHsv(i * 30, 160, 220));
            const QString f = db.imagesPath() + QString("/img_arc%1.png").arg(i);
            im.save(f);
            db.addHistory("image", "", f, "app");
        }
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        wait(1500);
        w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("arc_0.png");
        auto *area = w.findChild<QScrollArea*>("leftScroll");
        auto *rarea = w.findChild<QScrollArea*>("rightScroll");
        for (QScrollArea *a : {area, rarea}) {
            QPointF pt(a->viewport()->width() / 2.0, a->viewport()->height() / 2.0);
            for (int i = 0; i < 3; ++i) {
                QWheelEvent we(pt, a->viewport()->mapToGlobal(pt), QPoint(), QPoint(0, -120), Qt::NoButton,
                               Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(a->viewport(), &we);
            }
        }
        wait(110);
        w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("arc_mid.png");
        wait(790);
        w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("arc_1.png");
        // Наведение: «мышь зашла» на 8-ю видимую карточку слева и 3-ю справа
        auto enterNth = [&](QScrollArea *a, int n) {
            int k = 0;
            for (QWidget *c : a->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
                if (!c->property("itemId").isValid() && !c->property("filepath").isValid()) continue;
                const QRect g(c->mapTo(a->viewport(), QPoint(0, 0)), c->size());
                if (!a->viewport()->rect().contains(g)) continue;
                if (k++ == n) {
                    QEnterEvent ee(QPointF(5, 5), QPointF(5, 5), c->mapToGlobal(QPointF(5, 5)));
                    QApplication::sendEvent(c, &ee);
                    return;
                }
            }
        };
        enterNth(area, 6); enterNth(rarea, 2);
        wait(500);
        w.grab().scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation).save("arc_2.png");
        // x первых видимых карточек слева
        QStringList xs;
        for (QWidget *c : area->widget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
            if (c->property("itemId").isValid() && c->isVisible()) xs << QString::number(c->x());
        std::fprintf(stderr, "left x: %s\n", qPrintable(xs.mid(0, 14).join(' ')));
        return 0;
    }
    if (out == "scroll") {
        // Плавная прокрутка: шлём «щелчки» колёсика и пишем позицию по времени
        for (int i = 0; i < 40; ++i)
            db.addHistory("text", QString("Строка истории номер %1 для проверки прокрутки").arg(i));
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1600, 900);
        w.showWindow();
        for (int i = 0; i < 30; ++i) app.processEvents();
        auto *area = w.findChild<QScrollArea*>("leftScroll");
        QScrollBar *sb = area->verticalScrollBar();
        std::fprintf(stderr, "range 0..%d\n", sb->maximum());
        QElapsedTimer T; T.start();
        auto wheel = [&](int notches) {
            for (int i = 0; i < qAbs(notches); ++i) {
                QWheelEvent we(QPointF(50, 50), area->viewport()->mapToGlobal(QPointF(50, 50)),
                               QPoint(), QPoint(0, notches > 0 ? -120 : 120),
                               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(area->viewport(), &we);
            }
        };
        QTimer sampler; sampler.setTimerType(Qt::PreciseTimer);
        QObject::connect(&sampler, &QTimer::timeout, [&]() {
            std::fprintf(stderr, "%4lld value=%4d contentY=%5d\n", (long long)T.elapsed(),
                         sb->value(), area->widget()->y());
        });
        sampler.start(30);
        QTimer::singleShot(0,    [&]() { std::fprintf(stderr, "--- 3 notches down\n"); wheel(3); });
        QTimer::singleShot(700,  [&]() { std::fprintf(stderr, "--- 8 notches up (past the top)\n"); wheel(-8); });
        QTimer::singleShot(1700, [&]() { app.quit(); });
        app.exec();
        return 0;
    }
    if (out == "dlgsize") {
        // Размер окна настроек в момент показа (когда снимается снимок) и позже
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.showWindow();
        struct ShowSpy : QObject {
            bool eventFilter(QObject *o, QEvent *e) override {
                if (e->type() == QEvent::Show)
                    if (auto *d = qobject_cast<QDialog*>(o))
                        std::fprintf(stderr, "showEvent size=%dx%d\n", d->width(), d->height());
                return false;
            }
        } spy;
        app.installEventFilter(&spy);
        for (int ms : {150, 220, 320}) {
            QTimer::singleShot(ms, [&, ms]() {
                QWidget *d = QApplication::activeModalWidget();
                for (QWidget *tw : QApplication::topLevelWidgets())
                    if (dynamic_cast<Anim::SnapshotOverlay*>(tw) && tw->isVisible())
                        std::fprintf(stderr, "%3d ms: overlay %dx%d at (%d,%d); dialog %dx%d at (%d,%d)\n", ms,
                                     tw->width(), tw->height(), tw->x(), tw->y(),
                                     d ? d->width() : 0, d ? d->height() : 0, d ? d->x() : 0, d ? d->y() : 0);
            });
        }
        for (int ms : {640, 700, 800, 1000}) {
            QTimer::singleShot(ms, [&, ms]() {
                QDialog *dd = nullptr;
                for (QWidget *tw : QApplication::topLevelWidgets())
                    if (auto *x = qobject_cast<QDialog*>(tw)) dd = x;
                for (QWidget *tw : QApplication::topLevelWidgets())
                    if (auto *ov = dynamic_cast<Anim::SnapshotOverlay*>(tw))
                        if (ov->isVisible() && ov->width() < 1400)
                            std::fprintf(stderr, "%4d ms after close: dialog visible=%d, overlay playing=%d reverse=%d idle=%d\n",
                                         ms, dd ? dd->isVisible() : -1, ov->isPlaying(), ov->isReverse(), ov->isIdle());
            });
        }
        QTimer::singleShot(600, [&]() {
            if (QWidget *d = QApplication::activeModalWidget()) {
                std::fprintf(stderr, "after 600ms size=%dx%d\n", d->width(), d->height());
                d->close();
            }
        });
        for (auto *b : w.findChildren<QPushButton*>())
            if (b->toolTip() == "Настройки") { b->click(); break; }
        QTimer::singleShot(600, &app, &QCoreApplication::quit); app.exec();
        return 0;
    }
    if (out == "menu") {
        // Меню «Профили»: есть ли сцена анимации и какая прозрачность у меню
        const bool onScreen = argc > 2 && QString(argv[2]) == "screen";
        QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);  // Как в настоящем SmartClip
        QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);
        if (!onScreen) w.setAttribute(Qt::WA_DontShowOnScreen);
        w.showWindow();
        int frame = 0;
        QTimer grabber; grabber.setTimerType(Qt::PreciseTimer);
        QObject::connect(&grabber, &QTimer::timeout, [&]() {
            if (!onScreen) return;
            QWidget *m = QApplication::activePopupWidget();
            if (!m || frame > 20) return;
            QRect r = QRect(m->pos(), m->size()).adjusted(-20, -20, 20, 20);
            QScreen *s = QGuiApplication::screenAt(r.center());
            if (!s) return;
            const QRect sg = s->geometry();
            s->grabWindow(0, r.x() - sg.x(), r.y() - sg.y(), r.width(), r.height())
                .save(QString("frames/m_%1.png").arg(frame++, 2, 10, QChar('0')));
        });
        grabber.start(15);
        QElapsedTimer T; T.start();
        QTimer sampler;
        QObject::connect(&sampler, &QTimer::timeout, [&]() {
            QWidget *m = QApplication::activePopupWidget();
            int ov = 0;
            for (QWidget *tw : QApplication::topLevelWidgets())
                if (dynamic_cast<Anim::SnapshotOverlay*>(tw) && tw->isVisible() && tw->width() < 1000) ov++;
            std::fprintf(stderr, "%4lld popup=%s opacity=%.2f size=%dx%d overlays=%d\n", (long long)T.elapsed(),
                         m ? m->metaObject()->className() : "-", m ? m->windowOpacity() : -1.0,
                         m ? m->width() : 0, m ? m->height() : 0, ov);
            if (T.elapsed() > 900) { if (m) m->close(); }
        });
        sampler.start(40);
        QTimer::singleShot(1300, &app, &QCoreApplication::quit);
        QTimer::singleShot(100, [&]() {
            for (auto *b : w.findChildren<QPushButton*>())
                if (b->text().contains("Профили")) { b->click(); break; }
        });
        app.exec();
        return 0;
    }
    if (out == "wheelnav") {
        // Листание разделов настроек колёсиком
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.showWindow();
        QTimer::singleShot(700, [&]() {
            QWidget *d = QApplication::activeModalWidget();
            NavTabs *tabs = nullptr;
            for (QWidget *c : d->findChildren<QWidget*>())
                if (auto *t = dynamic_cast<NavTabs*>(c)) { tabs = t; break; }
            SideNav *nav = nullptr;
            for (QWidget *c : d->findChildren<QWidget*>())
                if (auto *n = dynamic_cast<SideNav*>(c)) { nav = n; break; }
            auto wheelAt = [&](QWidget *target, int notches) {
                QPointF p(target->width() / 2.0, target->height() / 2.0);
                QWheelEvent we(p, target->mapToGlobal(p), QPoint(), QPoint(0, notches * -120),
                               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(target, &we);
            };
            auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) app.processEvents(); };
            auto histBar = [&]() -> QString {
                auto *a = qobject_cast<QScrollArea*>(tabs->findChild<QStackedWidget*>()->currentWidget());
                return a ? QString("%1/%2").arg(a->verticalScrollBar()->value()).arg(a->verticalScrollBar()->maximum()) : "-";
            };
            std::fprintf(stderr, "start: section %d\n", tabs->currentIndex());
            wheelAt(nav, 1); wait(250);
            std::fprintf(stderr, "wheel over menu down: section %d\n", tabs->currentIndex());
            QWidget *page = qobject_cast<QScrollArea*>(tabs->findChild<QStackedWidget*>()->currentWidget())->viewport();
            for (int i = 0; i < 12; ++i) { wheelAt(page, 1); wait(60); }
            std::fprintf(stderr, "12 notches over current content: section %d, scroll %s\n", tabs->currentIndex(), qPrintable(histBar()));
            wait(450); wheelAt(page, 1); wait(300);
            std::fprintf(stderr, "pause at bottom + 1 notch: section %d\n", tabs->currentIndex());
            wheelAt(nav, -1); wait(250);
            std::fprintf(stderr, "wheel over menu up: section %d, scroll %s\n", tabs->currentIndex(), qPrintable(histBar()));
            d->close();
        });
        for (auto *b : w.findChildren<QPushButton*>())
            if (b->toolTip() == "Настройки") { b->click(); break; }
        return 0;
    }
    if (out == "filestab") {
        // Настройки без показа на экране: снимок вкладки «Файлы»
        SettingsDialog d(&db, nullptr);
        d.setAttribute(Qt::WA_DontShowOnScreen);
        d.show();
        NavTabs *tabs = nullptr;
        for (QWidget *c : d.findChildren<QWidget*>())
            if (auto *t = dynamic_cast<NavTabs*>(c)) { tabs = t; break; }
        for (int i = 0; tabs && i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            QElapsedTimer wt; wt.start(); while (wt.elapsed() < 450) app.processEvents();
            d.grab().save(QString("filestab_%1.png").arg(i));
        }
        std::fprintf(stderr, "tabs=%d\n", tabs ? tabs->count() : -1);
        return 0;
    }
    if (out == "settings") {
        // Окно «Настройки»: снимок каждой вкладки
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.showWindow();
        QTimer::singleShot(900, [&]() {
            if (QWidget *d = QApplication::activeModalWidget()) {
                NavTabs *tabs = nullptr;
                for (QWidget *c : d->findChildren<QWidget*>())
                    if (auto *t = dynamic_cast<NavTabs*>(c)) { tabs = t; break; }
                for (int i = 0; tabs && i < tabs->count(); ++i) {
                    tabs->setCurrentIndex(i);
                    QElapsedTimer wt; wt.start(); while (wt.elapsed() < 450) app.processEvents();
                    for (int k = 0; k < 5; ++k) app.processEvents();
                    d->grab().save(QString("settings_%1.png").arg(i));
                }
                d->close();
            }
        });
        for (auto *b : w.findChildren<QPushButton*>())
            if (b->toolTip() == "Настройки") { b->click(); break; }
        return 0;
    }
    if (out == "appearance") {
        // Открываем «Оформление» кнопкой 🖌 и снимаем диалог
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.showWindow();
        QTimer::singleShot(900, [&]() {
            if (QWidget *d = QApplication::activeModalWidget()) {
                d->grab().save("appearance.png");
                d->close();
            }
        });
        for (auto *b : w.findChildren<QAbstractButton*>())
            if (b->toolTip().contains("Оформление")) { b->click(); break; }
        return 0;
    }
    w.setAttribute(Qt::WA_DontShowOnScreen);
    w.resize(1600, 900);
    w.showWindow();
    if (pins) {
        for (auto *b : w.findChildren<QPushButton*>())
            if (b->text().contains("Закрепы")) { b->click(); break; }
    }
    w.resize(1600, 900);
    for (int i = 0; i < 20; ++i) app.processEvents();

    QImage img(w.size(), QImage::Format_ARGB32);
    img.fill(QColor(60, 60, 70));
    QPainter p(&img);
    w.render(&p, QPoint(), QRegion(), QWidget::DrawChildren);
    p.end();
    img.copy(QRect(0, 0, w.width(), 110)).save(out + "_top.png");
    img.save(out + "_full.png");
    return 0;
}
