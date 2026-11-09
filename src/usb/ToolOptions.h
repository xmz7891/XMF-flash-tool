#pragma once

// Small helpers for reading tool-form values (QVariantMap produced by the QML
// form generator). Shared by all platform tool registrations.

#include <QString>
#include <QVariantMap>

namespace gf::proto {

QString optionString(const QVariantMap& options, const QString& key, const QString& fallback = {});
quint64 optionUInt(const QVariantMap& options, const QString& key, quint64 fallback = 0);
int optionInt(const QVariantMap& options, const QString& key, int fallback = 0);
bool optionBool(const QVariantMap& options, const QString& key, bool fallback = false);

} // namespace gf::proto
