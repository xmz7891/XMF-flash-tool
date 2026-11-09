#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ImageCommon.h"

namespace gf::img::elf {

struct Segment {
    quint32 type = 0;
    quint32 flags = 0;
    quint64 offset = 0;
    quint64 vaddr = 0;
    quint64 paddr = 0;
    quint64 filesz = 0;
    quint64 memsz = 0;
    quint64 align = 0;
    QString typeText() const;
};

struct Section {
    QString name;
    quint32 type = 0;
    quint64 flags = 0;
    quint64 addr = 0;
    quint64 offset = 0;
    quint64 size = 0;
    quint32 link = 0;
    quint32 info = 0;
    quint64 addralign = 0;
    quint64 entsize = 0;
    QString typeText() const;
};

struct Symbol {
    QString name;
    quint64 value = 0;
    quint64 size = 0;
    quint8 info = 0;
    quint8 other = 0;
    quint16 shndx = 0;
    QString bindText() const;
    QString typeText() const;
};

struct Info {
    bool valid = false;
    int elfClass = 0;  // 32 / 64
    bool bigEndian = false;
    quint16 type = 0;
    quint16 machine = 0;
    quint32 version = 0;
    quint64 entry = 0;
    quint64 programHeaderOffset = 0;
    quint64 sectionHeaderOffset = 0;
    quint16 programHeaderEntrySize = 0;
    quint32 programHeaderCount = 0;
    quint16 sectionHeaderEntrySize = 0;
    quint32 sectionHeaderCount = 0;
    quint32 sectionNameIndex = 0;
    QVector<Segment> segments;
    QVector<Section> sections;
    QVector<Symbol> symbols;

    QString classText() const;
    QString endianText() const;
    QString typeText() const;
    QString machineText() const;
};

bool isElf(const QByteArray& data);
Info parse(const QByteArray& data);
QStringList describe(const Info& info);
/// 仅读取符号表（用于「内核符号表」工具），限制数量避免内存爆掉。
QVector<Symbol> readSymbols(const QString& path, int maxSymbols = 100000);

} // namespace gf::img::elf
