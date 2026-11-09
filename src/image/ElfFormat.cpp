#include "ElfFormat.h"

namespace gf::img::elf {

namespace {

struct Endian {
    bool big = false;

    quint16 u16(const QByteArray& d, qsizetype at) const
    {
        if (at < 0 || at + 2 > d.size())
            fail(QStringLiteral("ELF 数据被截断"));
        const quint8 a = quint8(d.at(at));
        const quint8 b = quint8(d.at(at + 1));
        return big ? quint16((a << 8) | b) : quint16(a | (b << 8));
    }
    quint32 u32(const QByteArray& d, qsizetype at) const
    {
        if (at < 0 || at + 4 > d.size())
            fail(QStringLiteral("ELF 数据被截断"));
        quint32 v = 0;
        for (int i = 0; i < 4; ++i) {
            const quint32 byte = quint32(quint8(d.at(at + (big ? (3 - i) : i))));
            v |= byte << (8 * i);
        }
        return v;
    }
    quint64 u64(const QByteArray& d, qsizetype at) const
    {
        if (at < 0 || at + 8 > d.size())
            fail(QStringLiteral("ELF 数据被截断"));
        quint64 v = 0;
        for (int i = 0; i < 8; ++i) {
            const quint64 byte = quint64(quint8(d.at(at + (big ? (7 - i) : i))));
            v |= byte << (8 * i);
        }
        return v;
    }
};

QString machineName(quint16 machine)
{
    switch (machine) {
    case 3:
        return QStringLiteral("x86 (i386)");
    case 8:
        return QStringLiteral("MIPS");
    case 20:
        return QStringLiteral("PowerPC");
    case 40:
        return QStringLiteral("ARM");
    case 62:
        return QStringLiteral("x86-64");
    case 183:
        return QStringLiteral("AArch64 (ARM64)");
    case 243:
        return QStringLiteral("RISC-V");
    default:
        break;
    }
    return QStringLiteral("未知 (0x%1)").arg(machine, 4, 16, QLatin1Char('0'));
}

QString fixedString(const QByteArray& data, qsizetype at)
{
    if (at < 0 || at >= data.size())
        return {};
    const qsizetype nul = data.indexOf('\0', at);
    const qsizetype end = nul < 0 ? data.size() : nul;
    return QString::fromUtf8(data.mid(at, end - at));
}

} // namespace

QString Segment::typeText() const
{
    switch (type) {
    case 0:
        return QStringLiteral("PT_NULL");
    case 1:
        return QStringLiteral("PT_LOAD");
    case 2:
        return QStringLiteral("PT_DYNAMIC");
    case 3:
        return QStringLiteral("PT_INTERP");
    case 4:
        return QStringLiteral("PT_NOTE");
    case 6:
        return QStringLiteral("PT_PHDR");
    case 7:
        return QStringLiteral("PT_TLS");
    case 0x6474E551:
        return QStringLiteral("PT_GNU_STACK");
    default:
        break;
    }
    return QStringLiteral("type=0x%1").arg(type, 8, 16, QLatin1Char('0'));
}

QString Section::typeText() const
{
    switch (type) {
    case 0:
        return QStringLiteral("NULL");
    case 1:
        return QStringLiteral("PROGBITS");
    case 2:
        return QStringLiteral("SYMTAB");
    case 3:
        return QStringLiteral("STRTAB");
    case 4:
        return QStringLiteral("RELA");
    case 5:
        return QStringLiteral("HASH");
    case 6:
        return QStringLiteral("DYNAMIC");
    case 7:
        return QStringLiteral("NOTE");
    case 8:
        return QStringLiteral("NOBITS");
    case 9:
        return QStringLiteral("REL");
    case 11:
        return QStringLiteral("DYNSYM");
    case 0x70000001:
        return QStringLiteral("ARM_EXIDX");
    default:
        break;
    }
    return QStringLiteral("type=0x%1").arg(type, 8, 16, QLatin1Char('0'));
}

QString Symbol::bindText() const
{
    switch (info >> 4) {
    case 0:
        return QStringLiteral("LOCAL");
    case 1:
        return QStringLiteral("GLOBAL");
    case 2:
        return QStringLiteral("WEAK");
    default:
        break;
    }
    return QStringLiteral("BIND%1").arg(info >> 4);
}

QString Symbol::typeText() const
{
    switch (info & 0xF) {
    case 0:
        return QStringLiteral("NOTYPE");
    case 1:
        return QStringLiteral("OBJECT");
    case 2:
        return QStringLiteral("FUNC");
    case 3:
        return QStringLiteral("SECTION");
    case 4:
        return QStringLiteral("FILE");
    case 5:
        return QStringLiteral("COMMON");
    case 6:
        return QStringLiteral("TLS");
    default:
        break;
    }
    return QStringLiteral("TYPE%1").arg(info & 0xF);
}

QString Info::classText() const
{
    return elfClass == 64 ? QStringLiteral("ELF64") : QStringLiteral("ELF32");
}

QString Info::endianText() const
{
    return bigEndian ? QStringLiteral("大端") : QStringLiteral("小端");
}

QString Info::typeText() const
{
    switch (type) {
    case 0:
        return QStringLiteral("NONE");
    case 1:
        return QStringLiteral("REL（可重定位）");
    case 2:
        return QStringLiteral("EXEC（可执行）");
    case 3:
        return QStringLiteral("DYN（共享/内核）");
    case 4:
        return QStringLiteral("CORE（核心转储）");
    default:
        break;
    }
    return QStringLiteral("type=%1").arg(type);
}

QString Info::machineText() const
{
    return machineName(machine);
}

bool isElf(const QByteArray& data)
{
    return data.size() >= 16 && quint8(data.at(0)) == 0x7F && data.at(1) == 'E' &&
           data.at(2) == 'L' && data.at(3) == 'F';
}

Info parse(const QByteArray& data)
{
    if (!isElf(data))
        fail(QStringLiteral("不是 ELF 文件（magic 不匹配）"));
    Info info;
    info.elfClass = quint8(data.at(4)) == 2 ? 64 : 32;
    if (quint8(data.at(4)) != 1 && quint8(data.at(4)) != 2)
        fail(QStringLiteral("ELF class 非法：%1").arg(quint8(data.at(4))));
    info.bigEndian = quint8(data.at(5)) == 2;
    Endian e;
    e.big = info.bigEndian;

    if (info.elfClass == 64) {
        info.type = e.u16(data, 16);
        info.machine = e.u16(data, 18);
        info.version = e.u32(data, 20);
        info.entry = e.u64(data, 24);
        info.programHeaderOffset = e.u64(data, 32);
        info.sectionHeaderOffset = e.u64(data, 40);
        info.programHeaderEntrySize = e.u16(data, 54);
        info.programHeaderCount = e.u16(data, 56);
        info.sectionHeaderEntrySize = e.u16(data, 58);
        info.sectionHeaderCount = e.u16(data, 60);
        info.sectionNameIndex = e.u16(data, 62);
    } else {
        info.type = e.u16(data, 16);
        info.machine = e.u16(data, 18);
        info.version = e.u32(data, 20);
        info.entry = e.u32(data, 24);
        info.programHeaderOffset = e.u32(data, 28);
        info.sectionHeaderOffset = e.u32(data, 32);
        info.programHeaderEntrySize = e.u16(data, 42);
        info.programHeaderCount = e.u16(data, 44);
        info.sectionHeaderEntrySize = e.u16(data, 46);
        info.sectionHeaderCount = e.u16(data, 48);
        info.sectionNameIndex = e.u16(data, 50);
    }

    if (info.programHeaderCount > 0 && info.programHeaderEntrySize >= 32) {
        for (quint32 i = 0; i < info.programHeaderCount; ++i) {
            const qint64 at = qint64(info.programHeaderOffset) +
                              qint64(i) * info.programHeaderEntrySize;
            if (at + info.programHeaderEntrySize > data.size())
                break;
            Segment segment;
            if (info.elfClass == 64) {
                segment.type = e.u32(data, at + 0);
                segment.flags = e.u32(data, at + 4);
                segment.offset = e.u64(data, at + 8);
                segment.vaddr = e.u64(data, at + 16);
                segment.paddr = e.u64(data, at + 24);
                segment.filesz = e.u64(data, at + 32);
                segment.memsz = e.u64(data, at + 40);
                segment.align = e.u64(data, at + 48);
            } else {
                segment.type = e.u32(data, at + 0);
                segment.offset = e.u32(data, at + 4);
                segment.vaddr = e.u32(data, at + 8);
                segment.paddr = e.u32(data, at + 12);
                segment.filesz = e.u32(data, at + 16);
                segment.memsz = e.u32(data, at + 20);
                segment.flags = e.u32(data, at + 24);
                segment.align = e.u32(data, at + 28);
            }
            info.segments.append(segment);
        }
    }

    if (info.sectionHeaderCount > 0 && info.sectionHeaderEntrySize >= 40) {
        for (quint32 i = 0; i < info.sectionHeaderCount; ++i) {
            const qint64 at = qint64(info.sectionHeaderOffset) +
                              qint64(i) * info.sectionHeaderEntrySize;
            if (at + info.sectionHeaderEntrySize > data.size())
                break;
            Section section;
            section.name = QStringLiteral("#%1").arg(i);
            if (info.elfClass == 64) {
                const quint32 nameOffset = e.u32(data, at + 0);
                section.type = e.u32(data, at + 4);
                section.flags = e.u64(data, at + 8);
                section.addr = e.u64(data, at + 16);
                section.offset = e.u64(data, at + 24);
                section.size = e.u64(data, at + 32);
                section.link = e.u32(data, at + 40);
                section.info = e.u32(data, at + 44);
                section.addralign = e.u64(data, at + 48);
                section.entsize = e.u64(data, at + 56);
                Q_UNUSED(nameOffset);
            } else {
                section.type = e.u32(data, at + 4);
                section.flags = e.u32(data, at + 8);
                section.addr = e.u32(data, at + 12);
                section.offset = e.u32(data, at + 16);
                section.size = e.u32(data, at + 20);
                section.link = e.u32(data, at + 24);
                section.info = e.u32(data, at + 28);
                section.addralign = e.u32(data, at + 32);
                section.entsize = e.u32(data, at + 36);
            }
            info.sections.append(section);
        }
        // 解析节名。
        if (int(info.sectionNameIndex) < info.sections.size()) {
            const Section& strtab = info.sections.at(int(info.sectionNameIndex));
            if (strtab.offset + strtab.size <= quint64(data.size())) {
                const QByteArray names = data.mid(qsizetype(strtab.offset), qsizetype(strtab.size));
                for (int i = 0; i < info.sections.size(); ++i) {
                    const qint64 at = qint64(info.sectionHeaderOffset) +
                                      qint64(i) * info.sectionHeaderEntrySize;
                    if (at + info.sectionHeaderEntrySize > data.size())
                        break;
                    const quint32 nameOffset = e.u32(data, at + 0);
                    if (nameOffset < quint32(names.size()))
                        info.sections[i].name = fixedString(names, nameOffset);
                }
            }
        }
    }

    // 符号表：取第一个 SYMTAB/DYNSYM。
    for (const auto& symtab : info.sections) {
        if (symtab.type != 2 && symtab.type != 11)
            continue;
        if (symtab.entsize == 0 || symtab.link >= quint32(info.sections.size()))
            continue;
        const Section& strtab = info.sections.at(int(symtab.link));
        if (strtab.offset + strtab.size > quint64(data.size()))
            continue;
        if (symtab.offset + symtab.size > quint64(data.size()))
            continue;
        const QByteArray names = data.mid(qsizetype(strtab.offset), qsizetype(strtab.size));
        const quint64 count = symtab.size / symtab.entsize;
        if (count > 2000000)
            fail(QStringLiteral("符号表过大，已拒绝解析"));
        for (quint64 i = 0; i < count; ++i) {
            const qint64 at = qint64(symtab.offset) + qint64(i) * qint64(symtab.entsize);
            Symbol symbol;
            if (info.elfClass == 64) {
                const quint32 nameOffset = e.u32(data, at + 0);
                symbol.info = quint8(data.at(at + 4));
                symbol.other = quint8(data.at(at + 5));
                symbol.shndx = e.u16(data, at + 6);
                symbol.value = e.u64(data, at + 8);
                symbol.size = e.u64(data, at + 16);
                if (nameOffset < quint32(names.size()))
                    symbol.name = fixedString(names, nameOffset);
            } else {
                const quint32 nameOffset = e.u32(data, at + 0);
                symbol.value = e.u32(data, at + 4);
                symbol.size = e.u32(data, at + 8);
                symbol.info = quint8(data.at(at + 12));
                symbol.other = quint8(data.at(at + 13));
                symbol.shndx = e.u16(data, at + 14);
                if (nameOffset < quint32(names.size()))
                    symbol.name = fixedString(names, nameOffset);
            }
            info.symbols.append(symbol);
        }
        break;
    }
    info.valid = true;
    return info;
}

QStringList describe(const Info& info)
{
    QStringList lines;
    lines << QStringLiteral("格式：%1 %2，%3")
                 .arg(info.classText(), info.typeText(), info.machineText());
    lines << QStringLiteral("字节序：%1，入口地址：0x%2")
                 .arg(info.endianText())
                 .arg(info.entry, 0, 16);
    lines << QStringLiteral("程序头：%1 个（偏移 %2），节头：%3 个（偏移 %4）")
                 .arg(info.programHeaderCount)
                 .arg(info.programHeaderOffset)
                 .arg(info.sectionHeaderCount)
                 .arg(info.sectionHeaderOffset);
    for (const auto& segment : info.segments) {
        lines << QStringLiteral("  [段] %1 文件偏移 0x%2 虚拟地址 0x%3 大小 %4 对齐 %5")
                     .arg(segment.typeText())
                     .arg(segment.offset, 0, 16)
                     .arg(segment.vaddr, 0, 16)
                     .arg(formatBytes(segment.filesz))
                     .arg(segment.align);
    }
    for (const auto& section : info.sections) {
        if (section.name.isEmpty())
            continue;
        lines << QStringLiteral("  [节] %1 %2 地址 0x%3 大小 %4")
                     .arg(section.name, section.typeText())
                     .arg(section.addr, 0, 16)
                     .arg(formatBytes(section.size));
    }
    if (!info.symbols.isEmpty()) {
        lines << QStringLiteral("符号表：%1 个符号").arg(info.symbols.size());
        for (const auto& symbol : info.symbols.mid(0, 200))
            lines << QStringLiteral("  0x%1 %2 %3 %4")
                         .arg(symbol.value, 8, 16, QLatin1Char('0'))
                         .arg(symbol.bindText(), symbol.typeText(), symbol.name);
        if (info.symbols.size() > 200)
            lines << QStringLiteral("  …（仅显示前 200 个）");
    }
    return lines;
}

QVector<Symbol> readSymbols(const QString& path, int maxSymbols)
{
    const QByteArray data = readFileOrThrow(path);
    Info info = parse(data);
    QVector<Symbol> symbols = info.symbols;
    if (maxSymbols > 0 && symbols.size() > maxSymbols)
        symbols = symbols.mid(0, maxSymbols);
    return symbols;
}

} // namespace gf::img::elf
