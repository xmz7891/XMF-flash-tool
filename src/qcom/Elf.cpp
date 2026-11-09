#include "Elf.h"

#include "usb/ProtocolSupport.h"

namespace gf::qcom {

namespace {

constexpr quint32 kProgramHeaderLoad = 1; // PT_LOAD
constexpr quint16 kMachineArm = 0x28;
constexpr quint16 kMachineAArch64 = 0xB7;

QString machineName(quint16 machine)
{
    switch (machine) {
    case kMachineArm:
        return QStringLiteral("ARM");
    case kMachineAArch64:
        return QStringLiteral("AArch64");
    case 0x3E:
        return QStringLiteral("x86-64");
    default:
        return QStringLiteral("machine=0x%1").arg(machine, 0, 16);
    }
}

} // namespace

bool ElfImage::looksLikeElf(const QByteArray& data)
{
    return data.size() >= 4 && quint8(data[0]) == 0x7F && data[1] == 'E' && data[2] == 'L'
           && data[3] == 'F';
}

QList<ElfSegment> ElfImage::loadableSegments() const
{
    QList<ElfSegment> out;
    for (const auto& segment : segments) {
        if (segment.type == kProgramHeaderLoad && segment.fileSize > 0)
            out.append(segment);
    }
    return out;
}

quint64 ElfImage::totalLoadSize() const
{
    quint64 total = 0;
    for (const auto& segment : loadableSegments())
        total += segment.fileSize;
    return total;
}

ElfImage ElfImage::parse(const QByteArray& data)
{
    ElfImage image;
    if (!looksLikeElf(data)) {
        image.error = QStringLiteral("不是 ELF 文件（缺少 7F 45 4C 46 魔数）");
        return image;
    }
    if (data.size() < 0x34) {
        image.error = QStringLiteral("ELF 文件头不完整");
        return image;
    }

    const quint8 elfClass = quint8(data[4]); // 1 = ELF32, 2 = ELF64
    const quint8 elfData = quint8(data[5]);  // 1 = little endian
    image.littleEndian = (elfData == 1);
    if (!image.littleEndian) {
        image.error = QStringLiteral("只支持小端 ELF（EI_DATA=%1）").arg(elfData);
        return image;
    }
    if (elfClass != 1 && elfClass != 2) {
        image.error = QStringLiteral("未知 ELF 类型 EI_CLASS=%1").arg(elfClass);
        return image;
    }
    image.klass = elfClass == 1 ? Class::Elf32 : Class::Elf64;

    bool ok = false;
    image.type = proto::readU16le(data, 0x10, &ok);
    if (!ok) {
        image.error = QStringLiteral("ELF 类型字段越界");
        return image;
    }
    image.machine = proto::readU16le(data, 0x12);

    quint64 programHeaderOffset = 0;
    quint16 programHeaderEntrySize = 0;
    quint16 programHeaderCount = 0;
    if (image.klass == Class::Elf32) {
        if (data.size() < 0x34) {
            image.error = QStringLiteral("ELF32 文件头不完整");
            return image;
        }
        image.entry = proto::readU32le(data, 0x18);
        programHeaderOffset = proto::readU32le(data, 0x1C);
        programHeaderEntrySize = proto::readU16le(data, 0x2A);
        programHeaderCount = proto::readU16le(data, 0x2C);
    } else {
        if (data.size() < 0x40) {
            image.error = QStringLiteral("ELF64 文件头不完整");
            return image;
        }
        image.entry = proto::readU64le(data, 0x18);
        programHeaderOffset = proto::readU64le(data, 0x20);
        programHeaderEntrySize = proto::readU16le(data, 0x36);
        programHeaderCount = proto::readU16le(data, 0x38);
    }

    const quint16 minimumEntrySize = image.klass == Class::Elf32 ? 32 : 56;
    if (programHeaderEntrySize < minimumEntrySize) {
        image.error = QStringLiteral("程序头大小异常（%1 < %2）")
                          .arg(programHeaderEntrySize)
                          .arg(minimumEntrySize);
        return image;
    }
    if (programHeaderOffset == 0 || programHeaderCount == 0) {
        image.error = QStringLiteral("ELF 没有程序头，无法确定加载段");
        return image;
    }
    if (programHeaderOffset > quint64(data.size())
        || programHeaderOffset + quint64(programHeaderEntrySize) * programHeaderCount
               > quint64(data.size())) {
        image.error = QStringLiteral("程序头超出文件范围");
        return image;
    }

    for (quint16 index = 0; index < programHeaderCount; ++index) {
        const qsizetype base = qsizetype(programHeaderOffset)
                               + qsizetype(index) * qsizetype(programHeaderEntrySize);
        ElfSegment segment;
        if (image.klass == Class::Elf32) {
            segment.type = proto::readU32le(data, base + 0);
            segment.offset = proto::readU32le(data, base + 4);
            segment.virtualAddress = proto::readU32le(data, base + 8);
            segment.physicalAddress = proto::readU32le(data, base + 12);
            segment.fileSize = proto::readU32le(data, base + 16);
            segment.memorySize = proto::readU32le(data, base + 20);
            segment.flags = proto::readU32le(data, base + 24);
        } else {
            segment.type = proto::readU32le(data, base + 0);
            segment.flags = proto::readU32le(data, base + 4);
            segment.offset = proto::readU64le(data, base + 8);
            segment.virtualAddress = proto::readU64le(data, base + 16);
            segment.physicalAddress = proto::readU64le(data, base + 24);
            segment.fileSize = proto::readU64le(data, base + 32);
            segment.memorySize = proto::readU64le(data, base + 40);
        }
        if (segment.offset > quint64(data.size())
            || segment.fileSize > quint64(data.size()) - segment.offset) {
            image.error = QStringLiteral("第 %1 个程序头的数据段超出文件范围").arg(index);
            return image;
        }
        image.segments.append(segment);
    }

    const auto loadable = image.loadableSegments();
    if (loadable.isEmpty()) {
        image.error = QStringLiteral("ELF 中没有可加载（PT_LOAD）的段");
        return image;
    }
    // The first loadable segment is what the Sahara loader ultimately jumps to
    // for programmers that do not publish an explicit entry point.
    if (image.entry == 0)
        image.entry = loadable.first().physicalAddress;

    image.valid = true;
    return image;
}

QString describeElf(const ElfImage& image)
{
    if (!image.valid)
        return QStringLiteral("ELF 无效：%1").arg(image.error);
    return QStringLiteral("ELF%1 %2, %3 段（可加载 %4 段）, 入口 0x%5, 加载大小 %6 字节")
        .arg(image.klass == ElfImage::Class::Elf64 ? QStringLiteral("64") : QStringLiteral("32"))
        .arg(machineName(image.machine))
        .arg(image.segments.size())
        .arg(image.loadableSegments().size())
        .arg(image.entry, 0, 16)
        .arg(image.totalLoadSize());
}

} // namespace gf::qcom
