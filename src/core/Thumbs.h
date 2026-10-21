// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  Thumbs.h — Уменьшенные превью картинок (SmartClip 2.0)                     ║
// ║                                                                              ║
// ║  Раньше каждая карточка фото декодировала ОРИГИНАЛ (скриншот 4K — десятки   ║
// ║  МБ и ~50–100 мс) прямо в основном потоке — окно подтормаживало, пока       ║
// ║  грузились картинки.                                                         ║
// ║                                                                              ║
// ║  Теперь:                                                                     ║
// ║  • превью нужного размера хранится на диске (images/thumbs) — со второго    ║
// ║    раза читается маленький файл, а не оригинал;                             ║
// ║  • декодирование идёт в фоновом потоке, интерфейс его не ждёт;              ║
// ║  • превью — под масштаб экрана (на 150% — в 1.5 раза больше пикселей),     ║
// ║    поэтому картинки в карточках чёткие;                                     ║
// ║  • удаляется картинка — удаляются и её превью (Database::removeFileIfUnused).║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

#include <QString>
#include <QSize>
#include <QImage>
#include <QImageReader>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QThreadPool>
#include <QPointer>
#include <QCoreApplication>
#include <functional>

namespace Thumbs {

// Папка превью (задаёт Database::init — внутри папки картинок)
inline QString &rootRef() { static QString r; return r; }
inline void setRoot(const QString &dir) { rootRef() = dir; }

// Имя превью — по пути к оригиналу (слеши и регистр не важны)
inline QString keyFor(const QString &path)
{
    const QString norm = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()).toLower();
    return QString::fromLatin1(QCryptographicHash::hash(norm.toUtf8(), QCryptographicHash::Md5).toHex());
}

inline QString thumbPath(const QString &path, QSize px)
{
    return rootRef() + "/" + keyFor(path) + QString("_%1x%2.png").arg(px.width()).arg(px.height());
}

// Удалить все превью этой картинки (любых размеров)
inline void removeFor(const QString &path)
{
    if (rootRef().isEmpty()) return;
    QDir dir(rootRef());
    for (const QString &f : dir.entryList({keyFor(path) + "_*.png"}, QDir::Files))
        dir.remove(f);
}

// Превью размером не больше px (в пикселях экрана). Готовое берём с диска,
// если оно не старше оригинала; иначе делаем из оригинала и сохраняем.
// Можно вызывать из фонового потока.
inline QImage load(const QString &path, QSize px)
{
    if (path.isEmpty() || px.isEmpty()) return {};
    const QFileInfo src(path);
    if (!src.exists()) return {};

    const QString cached = rootRef().isEmpty() ? QString() : thumbPath(path, px);
    if (!cached.isEmpty()) {
        const QFileInfo ci(cached);
        if (ci.exists() && ci.lastModified() >= src.lastModified()) {
            QImage img(cached);
            if (!img.isNull()) return img;
        }
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);  // Фото с телефона — с учётом поворота
    const QSize full = reader.size();
    const bool big = full.isValid() && (full.width() > px.width() || full.height() > px.height());
    if (big) reader.setScaledSize(full.scaled(px, Qt::KeepAspectRatio));
    QImage img = reader.read();
    if (img.isNull()) return {};
    if (img.width() > px.width() || img.height() > px.height())
        img = img.scaled(px, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Маленькие картинки и так быстрые — превью храним только для больших
    if (big && !cached.isEmpty()) {
        QDir().mkpath(rootRef());
        const QString tmp = cached + ".tmp";
        if (img.save(tmp, "PNG")) {
            QFile::remove(cached);
            QFile::rename(tmp, cached);
        } else {
            QFile::remove(tmp);
        }
    }
    return img;
}

// Загрузить превью в фоне; done вызывается в основном потоке, только если
// ctx (карточка) ещё жив. Одновременно — не больше двух картинок.
inline void loadAsync(const QString &path, QSize px, QObject *ctx,
                      std::function<void(const QImage &)> done)
{
    static QThreadPool *pool = []() {
        auto *p = new QThreadPool(qApp);
        p->setMaxThreadCount(2);
        return p;
    }();
    QPointer<QObject> guard(ctx);
    pool->start([path, px, guard, done]() {
        const QImage img = load(path, px);
        QMetaObject::invokeMethod(qApp, [guard, done, img]() {
            if (guard) done(img);
        }, Qt::QueuedConnection);
    });
}

} // namespace Thumbs
