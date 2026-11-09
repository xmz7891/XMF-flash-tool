#pragma once

// Builders shared by the flashing-pipeline unit tests. All data is synthetic so
// the tests run without any hardware or firmware samples.

#include <QByteArray>
#include <QList>
#include <QString>

#include "usb/ProtocolSupport.h"
#include "sprd/Pac.h"

namespace flow_test {

struct GptEntrySpec {
    QString name;
    quint64 firstLba = 0;
    quint64 lastLba = 0;
};

inline void putU16(QByteArray& data, int offset, quint16 value)
{
    const QByteArray bytes = gf::proto::u16le(value);
    for (int i = 0; i < 2; ++i)
        data[offset + i] = bytes[i];
}

inline void putU32(QByteArray& data, int offset, quint32 value)
{
    const QByteArray bytes = gf::proto::u32le(value);
    for (int i = 0; i < 4; ++i)
        data[offset + i] = bytes[i];
}

inline void putU64(QByteArray& data, int offset, quint64 value)
{
    const QByteArray bytes = gf::proto::u64le(value);
    for (int i = 0; i < 8; ++i)
        data[offset + i] = bytes[i];
}

/// Builds an MBR+GPT prefix large enough for `parseGptPrefix`.
inline QByteArray makeGptPrefix(quint32 sectorSize, const QList<GptEntrySpec>& entries,
                                int totalSize = 64 * 1024)
{
    QByteArray data(totalSize, '\0');
    // Protective MBR signature.
    data[510] = char(0x55);
    data[511] = char(0xAA);

    const int headerOffset = int(sectorSize);
    const char* signature = "EFI PART";
    for (int i = 0; i < 8; ++i)
        data[headerOffset + i] = signature[i];
    putU32(data, headerOffset + 8, 0x00010000);          // revision
    putU32(data, headerOffset + 12, 92);                 // header size
    putU64(data, headerOffset + 24, 1);                  // my LBA
    putU64(data, headerOffset + 32, 63);                 // alternate LBA
    putU64(data, headerOffset + 40, 34);                 // first usable
    putU64(data, headerOffset + 48, 62);                 // last usable
    putU64(data, headerOffset + 72, 2);                  // entries LBA
    putU32(data, headerOffset + 80, 128);                // entry count
    putU32(data, headerOffset + 84, 128);                // entry size

    const int entriesOffset = int(2 * sectorSize);
    for (int index = 0; index < entries.size(); ++index) {
        const int base = entriesOffset + index * 128;
        for (int i = 0; i < 16; ++i)
            data[base + i] = char(0x11 + index); // non-zero type GUID
        putU64(data, base + 32, entries[index].firstLba);
        putU64(data, base + 40, entries[index].lastLba);
        const QString name = entries[index].name;
        for (int i = 0; i < name.size() && i < 36; ++i) {
            data[base + 56 + i * 2] = char(name.at(i).unicode() & 0xFF);
            data[base + 57 + i * 2] = char((name.at(i).unicode() >> 8) & 0xFF);
        }
    }
    return data;
}

inline QByteArray makeBootTableGpt(quint32 sectorSize, int totalSize = 64 * 1024,
                                   quint64 bootFirstLba = 8, quint64 bootSectors = 8)
{
    QList<GptEntrySpec> entries;
    entries.append({QStringLiteral("boot"), bootFirstLba, bootFirstLba + bootSectors - 1});
    entries.append({QStringLiteral("misc"), bootFirstLba + bootSectors,
                    bootFirstLba + bootSectors + 1});
    return makeGptPrefix(sectorSize, entries, totalSize);
}

/// Minimal valid ELF64 (AArch64, one PT_LOAD segment) used as a programmer.
inline QByteArray makeMinimalElf64()
{
    QByteArray data(0x90, '\0');
    // e_ident
    data[0] = char(0x7F);
    data[1] = 'E';
    data[2] = 'L';
    data[3] = 'F';
    data[4] = 2; // ELF64
    data[5] = 1; // little endian
    data[6] = 1; // version
    putU16(data, 0x10, 2); // ET_EXEC
    putU16(data, 0x12, 0xB7); // AArch64
    putU32(data, 0x14, 1);
    putU64(data, 0x18, 0x400000); // entry
    putU64(data, 0x20, 0x40);     // program header offset
    putU16(data, 0x34, 0x40);     // ehsize
    putU16(data, 0x36, 56);       // phentsize
    putU16(data, 0x38, 1);        // phnum

    const int ph = 0x40;
    putU32(data, ph + 0, 1);      // PT_LOAD
    putU32(data, ph + 4, 5);      // RX
    putU64(data, ph + 8, 0x78);   // offset
    putU64(data, ph + 16, 0x400000);
    putU64(data, ph + 24, 0x400000);
    putU64(data, ph + 32, 16);    // filesz
    putU64(data, ph + 40, 16);    // memsz
    for (int i = 0; i < 16; ++i)
        data[0x78 + i] = char(i + 1);
    return data;
}

/// Scatter file with `partitions` entries; the first is preloader-like.
inline QString makeScatterText()
{
    return QStringLiteral(
        "- general: MTK_PLATFORM_CFG\n"
        "  info:\n"
        "    - config_version: V1.1.2\n"
        "      platform: MT6768\n"
        "      project: k62v1_64_bsp\n"
        "      storage: EMMC\n"
        "      boot_channel: MSDC_0\n"
        "      block_size: 0x20000\n"
        "- partition_index: SYS0\n"
        "  partition_name: preloader\n"
        "  file_name: preloader.bin\n"
        "  is_download: true\n"
        "  type: SV5_BL_BIN\n"
        "  linear_start_addr: 0x0\n"
        "  physical_start_addr: 0x0\n"
        "  partition_size: 0x40000\n"
        "  region: EMMC_BOOT_1\n"
        "  storage: HW_STORAGE_EMMC\n"
        "  boundary_check: true\n"
        "  is_reserved: false\n"
        "  operation_type: BOOTLOADERS\n"
        "- partition_index: SYS1\n"
        "  partition_name: boot\n"
        "  file_name: boot.img\n"
        "  is_download: true\n"
        "  type: NORMAL_ROM\n"
        "  linear_start_addr: 0x40000\n"
        "  physical_start_addr: 0x40000\n"
        "  partition_size: 0x200000\n"
        "  region: EMMC_USER\n"
        "  storage: HW_STORAGE_EMMC\n"
        "  boundary_check: true\n"
        "  is_reserved: false\n"
        "  operation_type: UPDATE\n"
        "- partition_index: SYS2\n"
        "  partition_name: userdata\n"
        "  file_name: NONE\n"
        "  is_download: false\n"
        "  type: EXT4_IMG\n"
        "  linear_start_addr: 0x240000\n"
        "  physical_start_addr: 0x240000\n"
        "  partition_size: 0x100000\n"
        "  region: EMMC_USER\n"
        "  storage: HW_STORAGE_EMMC\n"
        "  boundary_check: true\n"
        "  is_reserved: false\n"
        "  operation_type: FORMAT\n");
}

inline QString makeParameterText()
{
    return QStringLiteral(
        "CMDLINE: console=ttyFIQ0 androidboot.baseband=N/A mtdparts=rk29xxnand:"
        "0x00002000@0x00004000(uboot),0x00002000@0x00006000(trust),"
        "0x00010000@0x00008000(boot),0x00020000@0x00018000(system)\n");
}

inline QString makePartitionMapText()
{
    return QStringLiteral(
        "# name,offset,size\n"
        "boot,0x100000,0x1000000\n"
        "system,0x1100000,0x4000000\n"
        "vendor,0x5100000,0x8000000\n");
}

/// Builds a PAC container with the given images (name, payload).
struct PacImageSpec {
    QString name;
    quint32 type = 0;
    QByteArray data;
};

inline QByteArray makePac(const QList<PacImageSpec>& images)
{
    const int headerSize = 0x100;
    const int tableOffset = 0x20;
    int dataOffset = headerSize;
    QByteArray pac(headerSize, '\0');
    pac[0] = 'P';
    pac[1] = 'A';
    pac[2] = 'C';
    pac[3] = '\0';
    putU32(pac, 0x04, 1);
    putU32(pac, 0x08, quint32(headerSize));
    putU32(pac, 0x0C, quint32(images.size()));
    putU32(pac, 0x10, quint32(tableOffset));

    for (int index = 0; index < images.size(); ++index) {
        const int base = tableOffset + index * int(gf::sprd::kPacImageEntrySize);
        putU32(pac, base + 0x00, images[index].type);
        putU32(pac, base + 0x04, quint32(dataOffset));
        putU32(pac, base + 0x08, quint32(images[index].data.size()));
        const QByteArray name = images[index].name.toUtf8();
        for (int i = 0; i < name.size() && i < 32; ++i)
            pac[base + 0x10 + i] = name[i];
        dataOffset += images[index].data.size();
    }
    putU32(pac, 0x14, quint32(dataOffset));
    for (const auto& image : images)
        pac.append(image.data);
    return pac;
}

} // namespace flow_test
