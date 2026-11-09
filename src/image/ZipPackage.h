#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ImageCommon.h"

namespace gf::img::zip {

struct Entry {
    QString name;
    quint16 flags = 0;
    quint16 method = 0;
    quint16 modTime = 0;
    quint16 modDate = 0;
    quint32 crc = 0;
    quint64 compressedSize = 0;
    quint64 uncompressedSize = 0;
    quint64 localHeaderOffset = 0;

    bool isDirectory() const { return name.endsWith(QLatin1Char('/')); }
    bool isImage() const;
    QString methodText() const;
    QString sizeText() const;
};

struct Archive {
    QString path;
    qint64 fileSize = 0;
    QVector<Entry> entries;
    QString comment;
    QByteArray buffer; // parse() 从内存解析时保留
    bool hasEndRecord = false;
};

struct UpdaterInfo {
    QStringList packageExtractFile;  // package_extract_file 的文件
    QStringList packageExtractDir;
    QStringList uiPrint;
    QStringList mountPoints;
    QStringList formatTargets;
    QStringList setPerm;
    QStringList asserts;
    QStringList rawLines;
};

bool isZip(const QByteArray& data);
/// 从文件解析（只读 EOCD 与中央目录，适合大卡刷包）。
Archive open(const QString& path);
/// 从内存解析。
Archive parse(const QByteArray& data);
/// 读取某个条目的原始数据（自动解压并校验 CRC32）。
QByteArray readEntry(const Archive& archive, const Entry& entry);
/// 读取指定名字的条目；不存在返回空 QByteArray。
QByteArray readEntryByName(const Archive& archive, const QString& name);
/// 逐个校验 CRC32，返回问题列表（空 = 全部通过）。
QStringList verify(const Archive& archive, const CancelFn& cancel = {});
/// 解析 updater-script。
UpdaterInfo parseUpdaterScript(const QByteArray& text);
QStringList updaterSummary(const UpdaterInfo& info);
/// 返回条目中的镜像文件名。
QStringList imageNames(const Archive& archive);

} // namespace gf::img::zip
