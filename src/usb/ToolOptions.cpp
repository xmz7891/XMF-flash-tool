#include "ToolOptions.h"

#include <QStringList>

namespace gf::proto {

QString optionString(const QVariantMap& options, const QString& key, const QString& fallback)
{
    const QVariant value = options.value(key);
    if (!value.isValid())
        return fallback;
    const QString text = value.toString();
    return text.isEmpty() ? fallback : text;
}

quint64 optionUInt(const QVariantMap& options, const QString& key, quint64 fallback)
{
    const QVariant value = options.value(key);
    if (!value.isValid())
        return fallback;

    if (value.typeId() == QMetaType::ULongLong || value.typeId() == QMetaType::LongLong
        || value.typeId() == QMetaType::UInt || value.typeId() == QMetaType::Int) {
        return value.toULongLong();
    }

    QString text = value.toString().trimmed();
    text.remove(QLatin1Char('_'));
    text.remove(QLatin1Char(' '));
    if (text.isEmpty())
        return fallback;
    bool ok = false;
    const quint64 parsed = text.toULongLong(&ok, 0); // base 0 understands 0x/0b prefixes
    return ok ? parsed : fallback;
}

int optionInt(const QVariantMap& options, const QString& key, int fallback)
{
    const quint64 value = optionUInt(options, key, quint64(quint32(fallback)));
    return int(value);
}

bool optionBool(const QVariantMap& options, const QString& key, bool fallback)
{
    const QVariant value = options.value(key);
    if (!value.isValid())
        return fallback;
    if (value.typeId() == QMetaType::Bool)
        return value.toBool();
    const QString text = value.toString().trimmed().toLower();
    if (text.isEmpty())
        return fallback;
    return text == QLatin1String("true") || text == QLatin1String("1")
           || text == QLatin1String("yes") || text == QLatin1String("on")
           || text == QLatin1String("是");
}

} // namespace gf::proto
