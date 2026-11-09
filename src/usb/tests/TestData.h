#pragma once

// Helpers for tests that want to look at the real sample binaries under
// _research/. Tests run from different working directories, so the lookup walks
// up from the executable directory until it finds the file. A missing sample is
// reported with QSKIP instead of failing (the repository may be exported
// without _research).

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>

namespace xmf_test {

inline QString repoFile(const QString& relative)
{
    QString dir = QCoreApplication::applicationDirPath();
    for (int depth = 0; depth < 10; ++depth) {
        const QString candidate = QDir(dir).filePath(relative);
        if (QFile::exists(candidate))
            return candidate;
        QDir parent(dir);
        if (!parent.cdUp() || parent.absolutePath() == dir)
            break;
        dir = parent.absolutePath();
    }
    return {};
}

inline QByteArray repoBytes(const QString& relative)
{
    const QString path = repoFile(relative);
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace xmf_test
