#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

namespace gf::img::amlogic {
struct Item {
    quint32 id = 0;
    quint32 fileType = 0;
    quint64 currentOffset = 0;
    quint64 offset = 0;
    quint64 size = 0;
    QString mainType;
    QString subType;
    quint32 verify = 0;
};
struct Image {
    bool valid = false;
    QString error;
    quint32 version = 0;
    quint32 align = 0;
    quint64 imageSize = 0;
    QList<Item> items;
};
Image parse(const QByteArray& data);
QString describe(const Image& image);
bool packDirectory(const QString& dir, const QString& output, QString* error);
}
