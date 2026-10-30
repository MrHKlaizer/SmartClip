// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  smartclip_tests — все тесты SmartClip                                       ║
// ║                                                                              ║
// ║    smartclip_tests quick        — быстро, после правок (минута-две)         ║
// ║    smartclip_tests              — прогнать всё (каждый тест — отдельным     ║
// ║                                   процессом: упал/завис один — идут        ║
// ║                                   остальные); код выхода 1, если что-то   ║
// ║                                   не так                                     ║
// ║    smartclip_tests run <имя>    — один тест (подробный вывод)               ║
// ║    smartclip_tests monkey <зерно> [шагов] — «обезьяна» с этим зерном:       ║
// ║                                   повторить найденный ею баг               ║
// ║    smartclip_tests list         — какие тесты есть                          ║
// ║                                                                              ║
// ║  Виды тестов:                                                                ║
// ║    • проверки (checks.cpp) — сами решают, верно ли (CHECK);                 ║
// ║    • обезьяна (monkey.cpp) — сотни случайных действий подряд и после        ║
// ║      каждого — правила, которые должны выполняться всегда;                 ║
// ║    • сценарии (scenarios.cpp) — старые прогоны отдельных функций: здесь     ║
// ║      ловят падения и зависания.                                             ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#include <QApplication>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QStringList>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QRect>
#include <QFileInfo>
#include <cstdio>
#include <windows.h>
#include "Check.h"
#include "core/AppSettings.h"

int scenarioMain(int argc, char **argv);              // scenarios.cpp
int monkeyMain(int argc, char **argv, Check::Env &);  // monkey.cpp
int cardsMonkeyMain(int argc, char **argv, Check::Env &);  // cards_monkey.cpp

// Старые сценарии, которые не показывают окон на экране (anim, click и т.п. — показывают)
static const QStringList kScenarios = {
    "folders", "ghosts", "carddrag", "pinfiles", "pinmenu", "files", "tabs", "hold", "media", "askpin",
    "pintext", "pick", "carry", "icons", "ghoststrip", "filestab", "dropdata", "dragmid", "textarc",
    "pinview", "searchtop", "stripwide", "filetypes", "dlgsize", "shelves", "panel", "stage4", "parity",
    "ctrlview", "sorttree", "perfolder",
};
// Экраны, на которых тестируем: все подключённые (как стоят — 16:9, повёрнутый
// 9:16…) и ноутбучные 3:2 и 2:3. Окно SmartClip — во весь экран, так что
// хватает поставить окну размер экрана
struct Screen { QString title; QRect geo; };
static QList<Screen> testScreens(int &argc, char **argv)
{
    QGuiApplication app(argc, argv);  // Только чтобы узнать экраны
    QList<Screen> out;
    auto ratio = [](const QSize &s) {
        const double r = double(s.width()) / s.height();
        static const QList<QPair<double, QString>> known = {
            {16.0 / 9, "16:9"}, {9.0 / 16, "9:16"}, {16.0 / 10, "16:10"}, {10.0 / 16, "10:16"},
            {3.0 / 2, "3:2"}, {2.0 / 3, "2:3"}, {21.0 / 9, "21:9"}, {4.0 / 3, "4:3"}};
        for (const auto &k : known) if (qAbs(r - k.first) < 0.03) return k.second;
        return QString::number(r, 'f', 2);
    };
    // Первым — экран, где сейчас мышь (там откроется SmartClip)
    QList<QScreen *> screens = QGuiApplication::screens();
    if (QScreen *cur = QGuiApplication::screenAt(QCursor::pos())) { screens.removeAll(cur); screens.prepend(cur); }
    for (QScreen *sc : screens) {
        const QRect g = sc->geometry();
        out << Screen{QString("экран %1x%2 %3").arg(g.width()).arg(g.height()).arg(ratio(g.size())), g};
    }
    // Ноутбук — на месте главного экрана, в логических пикселях (как при 150%)
    const QPoint at = QGuiApplication::primaryScreen()->geometry().topLeft();
    out << Screen{"ноутбук 1504x1003 3:2", QRect(at, QSize(1504, 1003))};
    out << Screen{"ноутбук 1003x1504 2:3", QRect(at, QSize(1003, 1504))};
    return out;
}
static QString geoArg(const QRect &r) { return QString("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()); }

// Окружение для проверок и обезьяны: QApplication и настройки во временной папке
static int runInEnv(int argc, char **argv, const std::function<int(Check::Env &)> &body)
{
    setvbuf(stderr, nullptr, _IONBF, 0);
    QTemporaryDir tmp;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path() + "/ini");
    QApplication app(argc, argv);
    app.setOrganizationName("SmartClipTests");
    // Своё имя у каждого запуска — своя база (QStandardPaths) и в конце она
    // удаляется: раньше закрепы и история копились от прогона к прогону
    app.setApplicationName(QString("SmartClipTests_%1").arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    Check::keepOffScreen();
    // Папка «Загрузок» по умолчанию — пустая временная (не настоящая у пользователя)
    QDir().mkpath(tmp.path() + "/downloads");
    AppSettings::get().setFilesFolders({tmp.path() + "/downloads"});
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    Check::Env env;
    env.tmp = &tmp;
    const int rc = body(env);
    Check::removeTestData(data);
    std::fprintf(stderr, "RESULT checks passed=%d failed=%d\n", Check::passed(), Check::failures());
    return rc != 0 ? rc : (Check::failures() ? 1 : 0);
}

// ── Состояние прогона для монитора (smartclip_testmon.exe) ─────────────────
// status.json — рядом с exe: что идёт сейчас и результаты; last_results.json —
// чем закончился прошлый прогон (монитор показывает «прошлый раз»)
struct StatusFile {
    QString dir, mode;
    QDateTime started;
    QJsonObject prev;  // Прошлый прогон: название → {state, sec}
    QJsonArray jobs;
    QString current;
    QDateTime currentStarted;
    bool finished = false;
    void load()
    {
        QFile f(dir + "/last_results.json");
        if (f.open(QIODevice::ReadOnly)) prev = QJsonDocument::fromJson(f.readAll()).object();
    }
    void write() const
    {
        QJsonObject o;
        o["mode"] = mode;
        o["started"] = started.toString(Qt::ISODate);
        o["now"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        o["current"] = current;
        o["currentStarted"] = currentStarted.toString(Qt::ISODate);
        o["finished"] = finished;
        o["pid"] = qint64(GetCurrentProcessId());
        o["jobs"] = jobs;
        QSaveFile f(dir + "/status.json");  // Целиком или никак: монитор не прочтёт половину
        if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(o).toJson()); f.commit(); }
    }
    void saveResults() const
    {
        QJsonObject r = prev;  // Не прогнанные в этот раз (quick) — остаются от прошлых
        for (const QJsonValue &v : jobs) {
            const QJsonObject j = v.toObject();
            if (j["state"] == "ok" || j["state"] == "fail")
                r[j["title"].toString()] = QJsonObject{{"state", j["state"]}, {"sec", j["sec"]},
                                                       {"when", QDateTime::currentDateTime().toString(Qt::ISODate)}};
        }
        QSaveFile f(dir + "/last_results.json");
        if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(r).toJson()); f.commit(); }
    }
};

// Прогнать всё (quick — быстро: проверки и одна короткая обезьяна, без
// сценариев; полный прогон — перед выпуском). Каждый тест — своим процессом
//   quick — проверки на экране, где мышь, и короткая обезьяна на самом
//           непохожем (повёрнутом);
//   полный — проверки и обезьяна на каждом экране, сценарии — один раз
static int runAll(const QString &self, bool quick, const QList<Screen> &screens)
{
    struct Job { QString title; QStringList args; int timeoutMs; QString screen; };
    QList<Job> jobs;
    const QStringList checks = Check::registry().keys();
    if (quick) {
        const Screen &main = screens.first();
        Screen other = screens.last();
        for (const Screen &sc : screens)
            if ((sc.geo.width() < sc.geo.height()) != (main.geo.width() < main.geo.height())) { other = sc; break; }
        for (const QString &name : checks) jobs << Job{"check  " + name, {"run", name}, 120000, geoArg(main.geo)};
        jobs << Job{"monkey 1 · 120 шагов · " + other.title, {"monkey", "1", "120"}, 120000, geoArg(other.geo)};
        jobs << Job{"cards 1 · 60 шагов · " + main.title, {"cards", "1", "60"}, 120000, geoArg(main.geo)};
        std::printf("Проверки — на: %s\n", qUtf8Printable(main.title));
    } else {
        int seed = 1;
        for (const Screen &sc : screens) {
            for (const QString &name : checks)
                jobs << Job{"check  " + name + " · " + sc.title, {"run", name}, 120000, geoArg(sc.geo)};
            const QString sd = QString::number(seed++);
            jobs << Job{"monkey " + sd + " · " + sc.title, {"monkey", sd}, 240000, geoArg(sc.geo)};
            jobs << Job{"cards " + sd + " · " + sc.title, {"cards", sd}, 240000, geoArg(sc.geo)};
        }
        for (const QString &name : kScenarios) jobs << Job{"scen   " + name, {"run", name}, 120000, QString()};
    }
    std::printf("Экраны: ");
    for (const Screen &sc : screens) std::printf("%s;  ", qUtf8Printable(sc.title));
    std::printf("\n\n");

    StatusFile st;
    st.dir = QFileInfo(self).absolutePath();
    st.mode = quick ? "быстрый" : "полный";
    st.started = QDateTime::currentDateTime();
    st.load();
    for (const Job &j : jobs) {
        const QJsonObject pv = st.prev.value(j.title).toObject();
        st.jobs.append(QJsonObject{{"title", j.title}, {"state", "wait"}, {"sec", 0},
                                   {"prevState", pv.value("state")}, {"prevSec", pv.value("sec")}, {"details", ""}});
    }
    st.write();

    QTemporaryDir shots;  // Сценарии сохраняют сюда снимки — удаляются вместе с папкой
    int bad = 0;
    int idx = -1;
    QStringList failed;
    QElapsedTimer total;
    total.start();
    for (const Job &j : jobs) {
        ++idx;
        {
            QJsonObject jo = st.jobs[idx].toObject();
            jo["state"] = "run";
            st.jobs[idx] = jo;
            st.current = j.title;
            st.currentStarted = QDateTime::currentDateTime();
            st.write();
        }
        QProcess p;
        QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
        pe.insert("SHOT_DIR", shots.path());
        if (!j.screen.isEmpty()) pe.insert("SMARTCLIP_TEST_SCREEN", j.screen);
        p.setProcessEnvironment(pe);
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.setWorkingDirectory(shots.path());  // Старые сценарии кладут снимки в текущую папку — пусть во временную
        QElapsedTimer t;
        t.start();
        p.start(self, j.args);
        const bool finished = p.waitForFinished(j.timeoutMs);
        if (!finished) { p.kill(); p.waitForFinished(2000); }
        const QString out = QString::fromUtf8(p.readAll());
        const bool crashed = finished && p.exitStatus() != QProcess::NormalExit;
        const bool ok = finished && !crashed && p.exitCode() == 0;
        std::printf("%s  %-58s %5.1f s%s\n", ok ? "  ok " : " FAIL", qUtf8Printable(j.title), t.elapsed() / 1000.0,
                    !finished ? "  (завис)" : crashed ? "  (упал)" : "");
        QStringList details;  // Что не так — для монитора
        if (!ok) {
            ++bad;
            failed << j.title;
            // Что именно не так — строки FAIL и хвост вывода
            const QStringList lines = out.split('\n');
            int shown = 0;
            for (const QString &l : lines)
                if (l.startsWith("FAIL") || l.startsWith("MONKEY") || l.startsWith("    ")) {
                    if (!l.startsWith("    ")) std::printf("        %s\n", qUtf8Printable(l.trimmed()));
                    details << l.trimmed();
                    ++shown;
                }
            if (!shown)
                for (int i = qMax(0, int(lines.size()) - 8); i < lines.size(); ++i)
                    if (!lines[i].trimmed().isEmpty()) {
                        std::printf("        | %s\n", qUtf8Printable(lines[i].trimmed()));
                        details << lines[i].trimmed();
                    }
            if (!finished) details.prepend("Завис: не закончился за " + QString::number(j.timeoutMs / 1000) + " с");
            else if (crashed) details.prepend("Упал (программа теста аварийно завершилась)");
        }
        {
            QJsonObject jo = st.jobs[idx].toObject();
            jo["state"] = ok ? "ok" : "fail";
            jo["sec"] = qRound(t.elapsed() / 100.0) / 10.0;
            jo["details"] = details.join("\n");
            st.jobs[idx] = jo;
            st.current.clear();
            st.write();
        }
        std::fflush(stdout);
    }
    std::printf("\n%s: %d из %d, %.0f с\n", bad ? "ЕСТЬ ОШИБКИ" : "ВСЁ ХОРОШО", int(jobs.size()) - bad, int(jobs.size()),
                total.elapsed() / 1000.0);
    if (bad) std::printf("Упали: %s\n", qUtf8Printable(failed.join(", ")));
    st.finished = true;
    st.write();
    st.saveResults();
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    SetConsoleOutputCP(CP_UTF8);  // Русский текст в консоли — не «????»
    const QString cmd = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (cmd == "list") {
        for (const QString &n : Check::registry().keys()) std::printf("check  %s\n", qUtf8Printable(n));
        std::printf("monkey <зерно> [шагов]\n");
        for (const QString &n : kScenarios) std::printf("scen   %s\n", qUtf8Printable(n));
        return 0;
    }
    if (cmd == "run" && argc > 2) {
        const QString name = QString::fromLocal8Bit(argv[2]);
        if (Check::registry().contains(name))
            return runInEnv(argc, argv, [&](Check::Env &env) { Check::registry().value(name)(env); return 0; });
        return scenarioMain(argc - 1, argv + 1);  // Старый сценарий: argv[1] — его имя
    }
    if (cmd == "monkey")
        return runInEnv(argc, argv, [&](Check::Env &env) { return monkeyMain(argc, argv, env); });
    if (cmd == "cards")
        return runInEnv(argc, argv, [&](Check::Env &env) { return cardsMonkeyMain(argc, argv, env); });
    if (!cmd.isEmpty() && cmd != "quick") {
        std::fprintf(stderr, "Не знаю команду «%s». Есть: (пусто) — всё, quick — быстро, run <имя>, monkey <зерно>, list\n", argv[1]);
        return 2;
    }
    // Для запуска себя же нужен путь к exe — QApplication для этого не нужен
    QString self = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[0]));
    if (!QFileInfo::exists(self)) self += ".exe";
    const QList<Screen> screens = testScreens(argc, argv);
    return runAll(QFileInfo(self).absoluteFilePath(), cmd == "quick", screens);
}
