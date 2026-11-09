#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "ImageCommon.h"

namespace gf::img::cpio {

/// newc / crc cpio 归档中的一条记录。
struct Entry {
    QString name;
    quint32 ino = 0;
    quint32 mode = 0;
    quint32 uid = 0;
    quint32 gid = 0;
    quint32 nlink = 1;
    quint32 mtime = 0;
    quint32 devMajor = 0;
    quint32 devMinor = 0;
    quint32 rdevMajor = 0;
    quint32 rdevMinor = 0;
    QByteArray data;

    bool isDir() const { return (mode & 0170000u) == 0040000u; }
    bool isRegular() const { return (mode & 0170000u) == 0100000u; }
    bool isSymlink() const { return (mode & 0170000u) == 0120000u; }
    bool isCharDevice() const { return (mode & 0170000u) == 0020000u; }
    bool isBlockDevice() const { return (mode & 0170000u) == 0060000u; }
    bool isFifo() const { return (mode & 0170000u) == 0010000u; }
    QString typeText() const;
    /// 权限文本，如 "rwxr-xr-x"。
    QString permissionText() const;
    QString fullModeText() const { return QStringLiteral("0%1").arg(QString::number(mode & 0xFFFFu, 8)); }
};

struct Archive {
    QVector<Entry> entries;
    bool crcMode = false;
};

/// 解析 newc/crc 格式；数据损坏时抛 std::runtime_error。
Archive parse(const QByteArray& data);
/// 生成 newc 归档（含 TRAILER!!! 结束项）。保留 entry 的 ino/mode 等元数据。
QByteArray serialize(const Archive& archive);

/// 解析并清空 cpio：多个归档简单拼接时取第一段有效数据。
QByteArray firstArchiveRegion(const QByteArray& data);

/// 解包到目录；符号链接写成文本文件（Windows 无符号链接权限）。
void extractTo(const Archive& archive, const QString& dir, const ProgressFn& progress = {},
               const CancelFn& cancel = {});

/// 从目录递归生成归档；path 为相对归档内的路径前缀。
Archive fromDirectory(const QString& dir, const QString& rootNameInArchive = {});

/// 修改权限/属主（mode 为 0 时保持原值）。
void setPermissions(Archive& archive, const QString& name, quint32 mode, quint32 uid, quint32 gid,
                    bool recursive = false);
/// 添加/替换文件；name 为归档内路径。
void putFile(Archive& archive, const QString& name, const QByteArray& data, quint32 mode = 0100644u);
bool removeEntry(Archive& archive, const QString& name, bool recursive = false);

QString formatMode(quint32 mode);
quint32 parseModeText(const QString& text, bool* ok = nullptr);

} // namespace gf::img::cpio
