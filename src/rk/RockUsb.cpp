#include "RockUsb.h"

#include "usb/UsbIo.h"

#include <QString>
#include <stdexcept>

namespace gf::rk {

namespace {

QByteArray padCommandBlock(const QByteArray& cb)
{
    QByteArray out = cb.left(16);
    if (out.size() < 16)
        out.append(QByteArray(16 - out.size(), '\0'));
    return out;
}

QString hex8(quint8 value)
{
    return QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0'));
}

} // namespace

QByteArray Cbw::encode() const
{
    QByteArray out;
    out.reserve(kCbwSize);
    out += proto::u32le(signature);
    out += proto::u32le(tag);
    out += proto::u32le(transferLength);
    out.append(char(flags));
    out.append(char(lun));
    out.append(char(cbLength));
    out.append(padCommandBlock(cb));
    return out;
}

Cbw Cbw::decode(const QByteArray& raw)
{
    Cbw cbw;
    if (raw.size() < kCbwSize)
        return cbw;
    cbw.signature = proto::readU32le(raw, 0);
    cbw.tag = proto::readU32le(raw, 4);
    cbw.transferLength = proto::readU32le(raw, 8);
    cbw.flags = quint8(raw[12]);
    cbw.lun = quint8(raw[13]);
    cbw.cbLength = quint8(raw[14]);
    cbw.cb = raw.mid(15, 16);
    return cbw;
}

Csw Csw::decode(const QByteArray& raw)
{
    Csw csw;
    if (raw.size() < kCswSize) {
        csw.error = QStringLiteral("RockUSB CSW 长度不足（%1）").arg(raw.size());
        return csw;
    }
    csw.signature = proto::readU32le(raw, 0);
    csw.tag = proto::readU32le(raw, 4);
    csw.residue = proto::readU32le(raw, 8);
    csw.status = quint8(raw[12]);
    if (csw.signature != kCswSignature) {
        csw.error = QStringLiteral("RockUSB CSW 签名错误：0x%1")
                        .arg(csw.signature, 8, 16, QLatin1Char('0'));
        return csw;
    }
    csw.valid = true;
    return csw;
}

Cbw makeCbw(quint8 opcode, quint32 transferLength, bool directionIn,
            const QByteArray& parameters, quint32 tag)
{
    Cbw cbw;
    cbw.tag = tag;
    cbw.transferLength = transferLength;
    cbw.flags = directionIn ? kDirectionIn : kDirectionOut;
    cbw.lun = 0;
    QByteArray cb(16, '\0');
    cb[0] = char(opcode);
    const QByteArray params = parameters.left(15);
    for (qsizetype i = 0; i < params.size(); ++i)
        cb[1 + i] = params[i];
    cbw.cb = cb;
    cbw.cbLength = quint8(qMin<qsizetype>(16, 1 + params.size()));
    return cbw;
}

namespace {

QByteArray sectorParameters(quint32 startSector, quint16 count)
{
    // cb[1] = 0, cb[2..5] = start sector (LE), cb[6..7] = count (LE).
    QByteArray params(7, '\0');
    const QByteArray sector = proto::u32le(startSector);
    for (int i = 0; i < 4; ++i)
        params[1 + i] = sector[i];
    const QByteArray countBytes = proto::u16le(count);
    params[5] = countBytes[0];
    params[6] = countBytes[1];
    return params;
}

} // namespace

Cbw makeReadSector(quint32 startSector, quint32 count, int sectorSize)
{
    QByteArray params = sectorParameters(startSector, quint16(qMin<quint32>(count, 0xFFFF)));
    return makeCbw(kCmdReadSector, count * quint32(sectorSize), true, params);
}

Cbw makeWriteSector(quint32 startSector, quint32 count, int sectorSize)
{
    QByteArray params = sectorParameters(startSector, quint16(qMin<quint32>(count, 0xFFFF)));
    return makeCbw(kCmdWriteSector, count * quint32(sectorSize), false, params);
}

Cbw makeEraseSector(quint32 startSector, quint32 count)
{
    QByteArray params = sectorParameters(startSector, quint16(qMin<quint32>(count, 0xFFFF)));
    return makeCbw(kCmdEraseSector, 0, false, params);
}

Cbw makeDeviceInfo(quint32 transferLength)
{
    return makeCbw(kCmdDeviceInfo, transferLength, true);
}

Client::Client(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_transport(transport), m_reporter(std::move(reporter))
{
}

Csw Client::transfer(const Cbw& cbw, const QByteArray& outData, QByteArray* inData,
                     int timeoutMs)
{
    m_reporter.checkCanceled();
    Cbw command = cbw;
    command.tag = m_nextTag++;

    const bool directionIn = (command.flags & kDirectionIn) != 0;
    if (!directionIn && command.transferLength != quint32(outData.size())) {
        command.transferLength = quint32(outData.size());
    }

    usb::writeAll(m_transport, command.encode(), timeoutMs, m_reporter);
    if (!outData.isEmpty())
        usb::writeAll(m_transport, outData, timeoutMs, m_reporter);

    QByteArray data;
    if (directionIn && command.transferLength > 0)
        data = usb::readExact(m_transport, command.transferLength, timeoutMs, m_reporter);

    const Csw csw = Csw::decode(usb::readExact(m_transport, kCswSize, timeoutMs, m_reporter));
    if (!csw.valid)
        throw std::runtime_error(QStringLiteral("RockUSB 状态异常：%1").arg(csw.error).toStdString());
    if (csw.tag != command.tag) {
        throw std::runtime_error(
            QStringLiteral("RockUSB CSW 标签不匹配（期望 %1，收到 %2）")
                .arg(command.tag)
                .arg(csw.tag)
                .toStdString());
    }
    if (!csw.ok()) {
        throw std::runtime_error(
            QStringLiteral("RockUSB 命令 %1 失败：status=%2，residue=%3")
                .arg(hex8(quint8(command.cb.at(0))))
                .arg(csw.status)
                .arg(csw.residue)
                .toStdString());
    }
    if (inData)
        *inData = data;
    return csw;
}

DeviceInfo Client::deviceInfo(int timeoutMs)
{
    m_reporter.status(QStringLiteral("读取 RockUSB 设备信息…"));
    QByteArray raw;
    transfer(makeDeviceInfo(128), {}, &raw, timeoutMs);
    DeviceInfo info;
    info.raw = raw;
    info.version = proto::readU32le(raw, 0);
    // Most loaders return a printable chip string after the version word.
    const QByteArray chipBytes = raw.mid(4, 8);
    QString chip;
    for (const char byte : chipBytes) {
        const quint8 value = quint8(byte);
        if (value == 0)
            break;
        if (value < 0x20 || value > 0x7E) {
            chip.clear();
            break;
        }
        chip.append(QLatin1Char(char(value)));
    }
    info.chip = chip.trimmed();
    return info;
}

QByteArray Client::readCapability(int timeoutMs)
{
    QByteArray raw;
    transfer(makeCbw(kCmdReadCapability, 8, true), {}, &raw, timeoutMs);
    return raw;
}

quint32 Client::readFlashId(int timeoutMs)
{
    QByteArray raw;
    transfer(makeCbw(kCmdReadFlashId, 4, true), {}, &raw, timeoutMs);
    return proto::readU32le(raw, 0);
}

QByteArray Client::readChipInfo(int timeoutMs)
{
    QByteArray raw;
    transfer(makeCbw(kCmdReadChipInfo, 64, true), {}, &raw, timeoutMs);
    return raw;
}

QByteArray Client::readSectors(quint32 startSector, quint32 count, int sectorSize,
                               int timeoutMs)
{
    if (count == 0)
        throw std::runtime_error("RockUSB: 读取扇区数不能为 0");
    m_reporter.status(QStringLiteral("RockUSB 读取 %1 扇区（起始 %2）").arg(count).arg(startSector));
    QByteArray data;
    transfer(makeReadSector(startSector, count, sectorSize), {}, &data, timeoutMs);
    return data.left(int(count * quint32(sectorSize)));
}

void Client::writeSectors(quint32 startSector, const QByteArray& data, int sectorSize,
                          int timeoutMs)
{
    if (data.isEmpty())
        throw std::runtime_error("RockUSB: 写入数据为空");
    if (sectorSize <= 0 || data.size() % sectorSize != 0) {
        throw std::runtime_error(
            QStringLiteral("RockUSB: 数据大小 %1 不是扇区大小 %2 的整数倍")
                .arg(data.size())
                .arg(sectorSize)
                .toStdString());
    }
    const quint32 count = quint32(data.size() / sectorSize);
    m_reporter.status(QStringLiteral("RockUSB 写入 %1 扇区（起始 %2）").arg(count).arg(startSector));
    transfer(makeWriteSector(startSector, count, sectorSize), data, nullptr, timeoutMs);
}

void Client::eraseSectors(quint32 startSector, quint32 count, int sectorSize, int timeoutMs)
{
    Q_UNUSED(sectorSize);
    if (count == 0)
        throw std::runtime_error("RockUSB: 擦除扇区数不能为 0");
    m_reporter.status(QStringLiteral("RockUSB 擦除 %1 扇区（起始 %2）").arg(count).arg(startSector));
    transfer(makeEraseSector(startSector, count), {}, nullptr, timeoutMs);
}

QByteArray Client::readLba(quint64 lba, quint32 count, int timeoutMs)
{
    if (count == 0)
        throw std::runtime_error("RockUSB: 读取 LBA 数不能为 0");
    // LBA commands use a 64-bit logical block address: cb[2..9] = LBA,
    // cb[10..11] = count (little endian). TODO: validate on hardware.
    QByteArray params(11, '\0');
    const QByteArray lbaBytes = proto::u64le(lba);
    for (int i = 0; i < 8; ++i)
        params[1 + i] = lbaBytes[i];
    const QByteArray countBytes = proto::u16le(quint16(qMin<quint32>(count, 0xFFFF)));
    params[9] = countBytes[0];
    params[10] = countBytes[1];
    QByteArray data;
    transfer(makeCbw(kCmdReadLba, count * kSectorSize, true, params), {}, &data, timeoutMs);
    return data.left(int(count * kSectorSize));
}

void Client::writeLba(quint64 lba, const QByteArray& data, int timeoutMs)
{
    if (data.isEmpty() || data.size() % int(kSectorSize) != 0)
        throw std::runtime_error("RockUSB: LBA 写入数据必须是 512 字节的整数倍");
    const quint32 count = quint32(data.size() / int(kSectorSize));
    QByteArray params(11, '\0');
    const QByteArray lbaBytes = proto::u64le(lba);
    for (int i = 0; i < 8; ++i)
        params[1 + i] = lbaBytes[i];
    const QByteArray countBytes = proto::u16le(quint16(qMin<quint32>(count, 0xFFFF)));
    params[9] = countBytes[0];
    params[10] = countBytes[1];
    transfer(makeCbw(kCmdWriteLba, quint32(data.size()), false, params), data, nullptr, timeoutMs);
}

void Client::reset(quint8 flag, int timeoutMs)
{
    m_reporter.status(QStringLiteral("RockUSB 重启标志：%1").arg(flag));
    QByteArray params(4, '\0');
    params[3] = char(flag); // cb[4]
    transfer(makeCbw(kCmdSetResetFlag, 0, false, params), {}, nullptr, timeoutMs);
}

QString deviceModeName(quint16 pid)
{
    // Rockchip uses VID 0x2207 in MaskROM and Loader modes; the PID encodes the
    // chip family and changes between loader revisions, so only a generic mode
    // name is reported here (see docs/PROTOCOLS.md TODO).
    return QStringLiteral("Rockchip MaskROM/Loader (PID 0x%1)")
        .arg(pid, 4, 16, QLatin1Char('0'));
}

} // namespace gf::rk
