#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace gf::mtk {

struct LegacyPmtEntry {
    QString name;
    quint64 size = 0;
    quint64 offset = 0;
    quint64 maskFlags = 0;
};

struct LegacyPmtInfo {
    bool valid = false;
    bool mirror = false;
    QString error;
    QList<LegacyPmtEntry> entries;
};

/// Parse the legacy MediaTek PT/MPT block at the end of a USER image.
LegacyPmtInfo parseLegacyPmt(const QByteArray& userImage);
QString describeLegacyPmt(const LegacyPmtInfo& info);

} // namespace gf::mtk
