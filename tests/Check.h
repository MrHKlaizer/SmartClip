#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  Check.h — проверки для тестов SmartClip                                     ║
// ║                                                                              ║
// ║  CHECK(условие, "что не так %d", ...) — не выполнилось: пишет FAIL с местом  ║
// ║  в коде и подробностями, тест в итоге считается упавшим (но идёт дальше —    ║
// ║  видно сразу все проблемы, а не только первую).                             ║
// ║  TEST(имя) { ... } — тест; запускается как smartclip_tests run имя.         ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QString>
#include <QMap>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <cstdio>
#include <functional>

namespace Check {

inline int &failures() { static int n = 0; return n; }
inline int &passed() { static int n = 0; return n; }

inline void fail(const char *file, int line, const char *expr, const QString &why)
{
    ++failures();
    const char *f = file;
    for (const char *p = file; *p; ++p) if (*p == '/' || *p == '\\') f = p + 1;
    std::fprintf(stderr, "FAIL %s:%d: %s%s%s\n", f, line, expr, why.isEmpty() ? "" : " — ", qUtf8Printable(why));
}

// Окружение теста: окно SmartClip во временной папке
struct Env {
    QTemporaryDir *tmp = nullptr;
    QString dir() const { return tmp->path(); }
    // Подождать, обрабатывая события (анимации, фоновые загрузки)
    static void wait(int ms)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    // Ждать, пока cond не станет true (не дольше ms). true — дождались
    static bool waitFor(const std::function<bool()> &cond, int ms = 3000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            if (cond()) return true;
            QApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        return cond();
    }
};

// Удалить базу и данные теста (сначала закрыть соединения — иначе файл занят)
inline void removeTestData(const QString &dir)
{
    if (!dir.contains("qttest")) return;  // Только тестовые папки
    for (const QString &n : QSqlDatabase::connectionNames()) {
        { QSqlDatabase db = QSqlDatabase::database(n, false); db.close(); }
        QSqlDatabase::removeDatabase(n);
    }
    QDir(dir).removeRecursively();
    QDir().rmdir(QFileInfo(dir).absolutePath());  // Папка «организации» — если пустая
}

using TestFn = std::function<void(Env &)>;
inline QMap<QString, TestFn> &registry() { static QMap<QString, TestFn> r; return r; }
struct Reg { Reg(const char *name, TestFn fn) { registry().insert(QString::fromLatin1(name), std::move(fn)); } };

} // namespace Check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(name) \
    static void CHECK_CAT(test_, name)(Check::Env &env); \
    static Check::Reg CHECK_CAT(reg_, name)(#name, CHECK_CAT(test_, name)); \
    static void CHECK_CAT(test_, name)([[maybe_unused]] Check::Env &env)

// CHECK(cond) или CHECK(cond, "подробности %1", ...) — подробности через QString::arg
#define CHECK(cond, ...) \
    do { if (cond) ++Check::passed(); \
         else Check::fail(__FILE__, __LINE__, #cond, Check::details(__VA_ARGS__)); } while (0)

namespace Check {
inline QString details() { return QString(); }
template <class... A> QString details(const char *fmt, A &&...a)
{
    QString s = QString::fromUtf8(fmt);
    ((s = s.arg(a)), ...);
    return s;
}
} // namespace Check
