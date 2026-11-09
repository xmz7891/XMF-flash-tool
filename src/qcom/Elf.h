#pragma once

// Minimal ELF32/ELF64 reader for Qualcomm programmer images.
//
// Sahara programmers shipped as `.elf` / `.mbn` / `devprg` files are regular
// ELF images (the target's Sahara loader parses the ELF itself, so the host
// streams the whole file). This parser is only used for:
//   * validating an image before uploading it,
//   * logging load addresses / total size to the user,
//   * unit tests against the real devprg samples in _research.
//
// Only little-endian images are accepted; that covers every Qualcomm EDL
// programmer we have seen. PT_LOAD segments with a non-zero file size carry
// the bytes that end up in memory.

#include <QByteArray>
#include <QList>
#include <QString>

namespace gf::qcom {

struct ElfSegment {
    quint32 type = 0; // PT_*
    quint32 flags = 0;
    quint64 offset = 0;          // offset of the segment data inside the file
    quint64 fileSize = 0;        // bytes present in the file
    quint64 memorySize = 0;      // bytes occupied in memory (>= fileSize)
    quint64 virtualAddress = 0;
    quint64 physicalAddress = 0; // load address used by Sahara
};

struct ElfImage {
    enum class Class { None, Elf32, Elf64 };

    bool valid = false;
    Class klass = Class::None;
    bool littleEndian = true;
    quint16 machine = 0; // e_machine, e.g. 0x28 ARM, 0xB7 AArch64
    quint32 type = 0;    // e_type, 2 = ET_EXEC
    quint64 entry = 0;
    QList<ElfSegment> segments;
    QString error;

    /// Segments that actually carry bytes (PT_LOAD + fileSize > 0).
    QList<ElfSegment> loadableSegments() const;
    quint64 totalLoadSize() const;

    static bool looksLikeElf(const QByteArray& data);
    static ElfImage parse(const QByteArray& data);
};

/// "ELF64 AArch64, 4 段, 入口 0x…, 加载 813 KB" style one-liner.
QString describeElf(const ElfImage& image);

} // namespace gf::qcom
