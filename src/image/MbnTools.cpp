#include "ToolRegistry.h"
#include "TaskManager.h"
#include "ElfFormat.h"
#include "OplusDigest.h"
#include "QcomMapping.h"
#include "AmlogicImage.h"
#include "core/Util.h"

#include <QFileInfo>
#include <QProcess>
#include <QtEndian>
#include <QCryptographicHash>
#include <QSslCertificate>
#include <QSsl>
#include <QVariantMap>
#include <stdexcept>

namespace gf {
namespace {


struct QcomHashInfo {
    bool valid = false;
    quint32 version = 0, imageId = 0, hashSize = 0, oemId = 0, socHw = 0;
    QByteArray rootHash;
    QList<QByteArray> certificates;
    QString error;
};

static quint32 le32(const QByteArray& d, int o) {
    if (o < 0 || o + 4 > d.size()) return 0;
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(d.constData() + o));
}

static QcomHashInfo readQcomHash(const QByteArray& data) {
    QcomHashInfo out;
    const int elfAt = data.indexOf(QByteArray("\x7f""ELF", 4));
    if (elfAt < 0) { out.error = "ELF magic not found"; return out; }
    const QByteArray e = data.mid(elfAt);
    if (e.size() < 64) { out.error = "ELF too small"; return out; }
    const bool is64 = quint8(e[4]) == 2;
    if ((quint8(e[5]) != 1) || (quint8(e[6]) != 1)) { out.error = "not little-endian ELF"; return out; }
    const quint64 phoff = is64 ? qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(e.constData()+32))
                               : qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(e.constData()+28));
    const quint16 phentsize = is64 ? qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(e.constData()+54))
                                   : qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(e.constData()+42));
    const quint16 phnum = is64 ? qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(e.constData()+56))
                               : qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(e.constData()+44));
    if (phnum == 0 || phnum > 100 || phentsize < (is64 ? 56 : 32)) { out.error = "ELF program header table invalid"; return out; }
    int hashOff = -1, hashLen = 0;
    for (int i = 0; i < phnum; ++i) {
        const int base = int(phoff) + i * int(phentsize);
        if (base + int(phentsize) > e.size()) break;
        const quint32 flags = le32(e, base + (is64 ? 4 : 24));
        if (((flags >> 24) & 0x7) != 2) continue;
        hashOff = int(is64 ? qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(e.constData()+base+8))
                           : qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(e.constData()+base+4)));
        hashLen = int(is64 ? qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(e.constData()+base+32))
                           : qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(e.constData()+base+16)));
        break;
    }
    if (hashOff < 0 || hashLen <= 0 || hashOff + hashLen > e.size()) { out.error = "Qualcomm hash segment not found"; return out; }
    const QByteArray h = e.mid(hashOff, hashLen);
    out.version = le32(h, 4);
    if (out.version != 3 && out.version != 5 && out.version != 6 && out.version != 7) { out.error = "unsupported MBN hash version"; return out; }
    out.imageId = le32(h, 0);
    auto addCert = [&](int off, quint32 len) {
        if (off > 0 && len > 0 && qint64(off) + qint64(len) <= h.size())
            out.certificates.append(h.mid(off, int(len)));
    };
    if (out.version == 7) {
        const quint32 common = le32(h, 8), qti = le32(h, 12), oem = le32(h, 16);
        out.hashSize = le32(h, 20);
        const int meta = int(common) + int(qti) + int(oem);
        const int hashTable = 40 + meta;
        const quint32 sigQti = le32(h, 24), certQti = le32(h, 28);
        const int qtiCert = hashTable + int(out.hashSize) + int(sigQti);
        addCert(qtiCert, certQti);
        const int oemCert = qtiCert + int(certQti) + int(le32(h, 32));
        addCert(oemCert, le32(h, 36));
        const int oemAt = 40 + int(common) + int(qti);
        if (oem >= 224 && oemAt + 220 <= h.size()) {
            out.oemId = le32(h, oemAt + 136);
            out.socHw = le32(h, oemAt + 16);
            out.rootHash = h.mid(oemAt + 156, 64);
        }
    } else if (out.version == 6) {
        const quint32 qti = le32(h, 40), oem = le32(h, 44);
        out.hashSize = le32(h, 20);
        const int hashTable = 48 + int(qti) + int(oem);
        const quint32 sigQti = le32(h, 8), certQti = le32(h, 12);
        const int qtiCert = hashTable + int(out.hashSize) + int(sigQti);
        addCert(qtiCert, certQti);
        const int oemCert = qtiCert + int(certQti) + int(le32(h, 28));
        addCert(oemCert, le32(h, 36));
        const int oemAt = 48 + int(qti);
        if (oem >= 120 && oemAt + 120 <= h.size()) {
            out.oemId = le32(h, oemAt + 16);
            out.socHw = le32(h, oemAt + 32);
        }
    } else {
        out.hashSize = le32(h, 20);
        const int hashTable = 40;
        if (out.version == 5) {
            const quint32 sigQti = le32(h, 8), certQti = le32(h, 12);
            const int qtiCert = hashTable + int(out.hashSize) + int(sigQti);
            addCert(qtiCert, certQti);
            const int oemCert = qtiCert + int(certQti) + int(le32(h, 28));
            addCert(oemCert, le32(h, 36));
        } else {
            addCert(40 + int(out.hashSize) + int(le32(h, 28)), le32(h, 36));
        }
    }
    out.valid = true;
    return out;
}
static QString verifyQcomHash(const QByteArray& data)
{
    const int elfAt = data.indexOf(QByteArray("\x7f""ELF", 4));
    if (elfAt < 0) return QStringLiteral("not ELF");
    const QByteArray e = data.mid(elfAt);
    if (e.size() < 64) return QStringLiteral("ELF too small");
    const bool is64 = quint8(e[4]) == 2;
    const quint64 phoff = is64 ? qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(e.constData()+32))
                               : qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(e.constData()+28));
    const quint16 phentsize = is64 ? qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(e.constData()+54))
                                   : qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(e.constData()+42));
    const quint16 phnum = is64 ? qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(e.constData()+56))
                               : qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(e.constData()+44));
    if (!phnum || phnum > 100 || phentsize < (is64 ? 56 : 32)) return QStringLiteral("invalid program header table");

    auto fileOffset = [&](int b) -> qint64 {
        return is64 ? qint64(qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(e.constData()+b+8)))
                    : qint64(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(e.constData()+b+4)));
    };
    auto fileSize = [&](int b) -> qint64 {
        return is64 ? qint64(qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(e.constData()+b+32)))
                    : qint64(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(e.constData()+b+16)));
    };
    auto flagsOf = [&](int b) -> quint32 { return le32(e, b + (is64 ? 4 : 24)); };
    auto typeOf = [&](int b) -> quint32 { return le32(e, b); };

    int hashOff = -1, hashLen = 0;
    int nonPaged = 0;
    for (int i = 0; i < phnum; ++i) {
        const int b = int(phoff) + i * int(phentsize);
        if (b + int(phentsize) > e.size()) return QStringLiteral("program header out of range");
        const quint32 flags = flagsOf(b);
        const quint32 type = typeOf(b);
        if (((flags >> 20) & 1) == 0) {
            if (type != 6) ++nonPaged;
        }
        if (((flags >> 24) & 7) == 2) {
            hashOff = int(fileOffset(b));
            hashLen = int(fileSize(b));
        }
    }
    if (hashOff < 0 || hashLen <= 0 || hashOff + hashLen > e.size()) return QStringLiteral("hash segment not found");
    const QByteArray h = e.mid(hashOff, hashLen);
    const quint32 version = le32(h, 4);
    const quint32 hashSize = le32(h, 20);
    if (version != 3 && version != 5 && version != 6 && version != 7) return QStringLiteral("unsupported hash version");
    int hashOffset = 40;
    if (version == 6) hashOffset = 48 + int(le32(h,40)) + int(le32(h,44));
    else if (version == 7) hashOffset = 40 + int(le32(h,8)) + int(le32(h,12)) + int(le32(h,16));
    if (hashOffset < 0 || hashOffset + int(hashSize) > h.size()) return QStringLiteral("hash table out of range");
    const int digest = version >= 6 ? 48 : (qint64(nonPaged) * 32 == hashSize ? 32 : 20);
    if (qint64(nonPaged) * digest != hashSize && !(version == 6 && qint64(nonPaged)*digest + 20 == hashSize))
        return QStringLiteral("hash size / segment count mismatch");
    const QByteArray table = h.mid(hashOffset, int(hashSize));
    const QCryptographicHash::Algorithm alg = digest == 48 ? QCryptographicHash::Sha384
        : digest == 32 ? QCryptographicHash::Sha256 : QCryptographicHash::Sha1;

    auto zeroOk = [&](const QByteArray& expected) {
        for (char c : expected) if (c != 0) return false;
        return true;
    };
    auto shouldHash = [&](quint32 flags) {
        if (((flags >> 24) & 7) == 2) return false;
        const quint32 access = (flags >> 21) & 7;
        return access != 3 && access != 4;
    };
    int index = 0;
    for (int i = 0; i < phnum; ++i) {
        const int b = int(phoff) + i * int(phentsize);
        const quint32 flags = flagsOf(b);
        const quint32 type = typeOf(b);
        if (type == 6) continue;
        const qint64 off = fileOffset(b);
        qint64 len = fileSize(b);
        if (off < 0 || len < 0 || off + len > e.size()) return QStringLiteral("segment out of range");
        const bool paged = ((flags >> 20) & 1) != 0;
        if (!paged) {
            if (index * digest + digest > table.size()) return QStringLiteral("hash table too small");
            bool ok = true;
            if (!shouldHash(flags)) ok = zeroOk(table.mid(index*digest, digest));
            else ok = QCryptographicHash::hash(e.mid(int(off), int(len)), alg) == table.mid(index*digest, digest);
            if (!ok) return QStringLiteral("hash mismatch at segment %1").arg(i);
            ++index;
            continue;
        }
        const quint64 vaddr = is64 ? qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(e.constData()+b+16))
                                   : qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(e.constData()+b+8));
        const qint64 misalign = qint64(vaddr & 4095);
        qint64 pageOff = off;
        if (misalign) { pageOff += (4096 - misalign); len -= (4096 - misalign); }
        if (len < 0) return QStringLiteral("paged segment too small");
        while (len > 0) {
            const int pageLen = int(qMin<qint64>(len, 4096));
            if (index * digest + digest > table.size()) return QStringLiteral("hash table too small");
            bool ok = true;
            if (!shouldHash(flags)) ok = zeroOk(table.mid(index*digest, digest));
            else ok = QCryptographicHash::hash(e.mid(int(pageOff), pageLen), alg) == table.mid(index*digest, digest);
            if (!ok) return QStringLiteral("hash mismatch at paged segment %1").arg(i);
            ++index; pageOff += pageLen; len -= pageLen;
        }
    }
    if (index != nonPaged) return QStringLiteral("hash count mismatch (%1/%2)").arg(index).arg(nonPaged);
    return QStringLiteral("valid");
}

static void printQcomCertificates(TaskContext& ctx, const QcomHashInfo& h)
{
    for (int i = 0; i < h.certificates.size(); ++i) {
        const QByteArray& der = h.certificates.at(i);
        const auto certs = QSslCertificate::fromData(der, QSsl::Der);
        const QString sha = QString::fromLatin1(QCryptographicHash::hash(der, QCryptographicHash::Sha256).toHex());
        if (certs.isEmpty()) {
            ctx.info(QStringLiteral("certificate %1: unparsed DER (%2 bytes), sha256 %3")
                         .arg(i).arg(der.size()).arg(sha));
            continue;
        }
        const QSslCertificate& c = certs.first();
        ctx.info(QStringLiteral("certificate %1: subject=%2 | issuer=%3 | serial=%4 | sha256=%5")
                     .arg(i).arg(c.subjectDisplayName(), c.issuerDisplayName(), c.serialNumber(), sha));
    }
    bool nonZero = false;
    for (char b : h.rootHash) if (b != 0) { nonZero = true; break; }
    if (nonZero)
        ctx.info(QStringLiteral("metadata root CA hash: %1")
                     .arg(QString::fromLatin1(h.rootHash.toHex())));
}

static QString takeAsciiString(const QByteArray& data, const QByteArray& prefix)
{
    const int at = data.indexOf(prefix);
    if (at < 0) return {};
    QByteArray value = data.mid(at + prefix.size(), 256);
    const int nul = value.indexOf('\0');
    if (nul >= 0) value.truncate(nul);
    return QString::fromLatin1(value).trimmed();
}

static void printQcomMetadata(TaskContext& ctx, const QByteArray& data)
{
    const QString qc = takeAsciiString(data, QByteArrayLiteral("QC_IMAGE_VERSION_STRING="));
    const QString oem = takeAsciiString(data, QByteArrayLiteral("OEM_IMAGE_VERSION_STRING="));
    const QString variant = takeAsciiString(data, QByteArrayLiteral("IMAGE_VARIANT_STRING="));
    if (!qc.isEmpty()) ctx.info(QStringLiteral("QC image version: %1").arg(qc));
    if (!oem.isEmpty()) ctx.info(QStringLiteral("OEM image version: %1").arg(oem));
    if (!variant.isEmpty()) ctx.info(QStringLiteral("Image variant: %1").arg(variant));
    const int buildAt = data.indexOf(QByteArrayLiteral("Binary build date: "));
    if (buildAt >= 0) {
        QByteArray tail = data.mid(buildAt, 96);
        const int nul = tail.indexOf('\0');
        if (nul >= 0) tail.truncate(nul);
        ctx.info(QStringLiteral("Build: %1").arg(QString::fromLatin1(tail)));
    }
}

void mbnInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString path = options.value(QStringLiteral("input")).toString().trimmed();
    if (path.isEmpty())
        throw std::runtime_error("select an MBN / ELF file");
    const QByteArray data = util::readFile(path);
    if (data.isEmpty())
        throw std::runtime_error("cannot read file");
    const QByteArray elfMagic("\x7f""ELF", 4);
    const bool wrappedMbn = data.startsWith(QByteArray::fromHex("1A00000006000000"));
    const int elfAt = data.indexOf(elfMagic);
    if (wrappedMbn)
        ctx.info(QStringLiteral("MBN wrapper detected, ELF payload at 0x%1").arg(elfAt, 0, 16));
    if (elfAt < 0)
        throw std::runtime_error("not an ELF/MBN image (ELF magic missing)");
    const QByteArray elfData = data.mid(elfAt);
    const img::elf::Info info = img::elf::parse(elfData);
    if (!info.valid)
        throw std::runtime_error("ELF parse failed");
    printQcomMetadata(ctx, data);
    const QcomHashInfo hash = readQcomHash(data);
    if (hash.valid) {
        ctx.info(QStringLiteral("Qualcomm hash v%1 · image id 0x%2 · hash size %3")
                     .arg(hash.version).arg(hash.imageId, 0, 16).arg(hash.hashSize));
        if (hash.oemId || hash.socHw) {
            ctx.info(QStringLiteral("OEM id 0x%1 (%2) · SoC hw 0x%3 (%4)")
                         .arg(hash.oemId, 0, 16).arg(qcom::oemName(hash.oemId))
                         .arg(hash.socHw, 0, 16).arg(qcom::socHwName(hash.socHw)));
        }
    } else {
        ctx.info(QStringLiteral("hash segment: %1").arg(hash.error));
    }
    ctx.info(QStringLiteral("hash table verify: %1").arg(verifyQcomHash(data)));
    printQcomCertificates(ctx, hash);
    ctx.info(QStringLiteral("ELF %1 %2 / %3, entry 0x%4")
                 .arg(info.classText(), info.endianText(), info.machineText())
                 .arg(info.entry, 0, 16));
    ctx.info(QStringLiteral("program headers: %1, sections: %2")
                 .arg(info.programHeaderCount).arg(info.sectionHeaderCount));
    for (const auto& seg : info.segments) {
        ctx.info(QStringLiteral("segment %1: file 0x%2 size 0x%3 vaddr 0x%4 paddr 0x%5")
                     .arg(seg.typeText())
                     .arg(seg.offset, 0, 16).arg(seg.filesz, 0, 16)
                     .arg(seg.vaddr, 0, 16).arg(seg.paddr, 0, 16));
    }
    ctx.ok(QStringLiteral("MBN/ELF parsed: %1 segment(s)").arg(info.segments.size()));
}
} // namespace
void oplusDigestInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString path = options.value(QStringLiteral("input")).toString().trimmed();
    if (path.isEmpty()) throw std::runtime_error("select an Oplus digest MBN/ELF");
    const QByteArray data = util::readFile(path);
    if (data.isEmpty()) throw std::runtime_error("cannot read file");
    printQcomMetadata(ctx, data);
    const QcomHashInfo hash = readQcomHash(data);
    if (hash.valid) {
        ctx.info(QStringLiteral("Qualcomm hash v%1 · image id 0x%2 · hash size %3")
                     .arg(hash.version).arg(hash.imageId, 0, 16).arg(hash.hashSize));
        if (hash.oemId || hash.socHw) {
            ctx.info(QStringLiteral("OEM id 0x%1 (%2) · SoC hw 0x%3 (%4)")
                         .arg(hash.oemId, 0, 16).arg(qcom::oemName(hash.oemId))
                         .arg(hash.socHw, 0, 16).arg(qcom::socHwName(hash.socHw)));
        }
    } else {
        ctx.info(QStringLiteral("hash segment: %1").arg(hash.error));
    }
    printQcomCertificates(ctx, hash);
    const img::oplus::Digest d = img::oplus::parse(data);
    if (!d.valid) throw std::runtime_error(d.error.toStdString());
    ctx.info(QStringLiteral("Oplus digest v%1 · %2 partitions · entry 0x%3")
                 .arg(d.version).arg(d.partitionCount).arg(d.partitionLength,0,16));
    for (const auto& p : d.partitions) {
        ctx.info(QStringLiteral("%1 | %2 | sector %3 + %4 | R=%5 W=%6 | %7")
                     .arg(p.label, p.fileName).arg(p.startSector).arg(p.sectors)
                     .arg(p.allowRead).arg(p.allowWrite).arg(p.hashHex));
    }
    ctx.ok(QStringLiteral("Oplus digest parsed: %1 partition(s)").arg(d.partitions.size()));
}


void amlogicImageInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString path = options.value(QStringLiteral("input")).toString().trimmed();
    if (path.isEmpty()) throw std::runtime_error("select an Amlogic USB Burning image");
    const QByteArray data = util::readFile(path);
    if (data.isEmpty()) throw std::runtime_error("cannot read image");
    const img::amlogic::Image image = img::amlogic::parse(data);
    if (!image.valid) throw std::runtime_error(image.error.toStdString());
    ctx.info(img::amlogic::describe(image));
    for (const auto& item : image.items) {
        ctx.info(QStringLiteral("%1.%2  offset=0x%3  size=0x%4  verify=%5")
                     .arg(item.subType, item.mainType)
                     .arg(item.offset, 0, 16).arg(item.size, 0, 16).arg(item.verify));
    }
    ctx.ok(QStringLiteral("Amlogic burning image parsed: %1 item(s)").arg(image.items.size()));
}


void amlogicImageExtract(TaskContext& ctx, const QVariantMap& options)
{
    const QString path = options.value(QStringLiteral("input")).toString().trimmed();
    const QString output = options.value(QStringLiteral("output")).toString().trimmed();
    if (path.isEmpty() || output.isEmpty())
        throw std::runtime_error("select an Amlogic image and an output directory");
    const QByteArray data = util::readFile(path);
    if (data.isEmpty()) throw std::runtime_error("cannot read image");
    const img::amlogic::Image image = img::amlogic::parse(data);
    if (!image.valid) throw std::runtime_error(image.error.toStdString());
    if (!util::ensureDir(output))
        throw std::runtime_error("cannot create output directory");
    int written = 0;
    QStringList commands;
    for (const auto& item : image.items) {
        QString name = item.subType;
        if (!item.mainType.isEmpty()) name += QLatin1Char('.') + item.mainType;
        if (name.isEmpty()) name = QStringLiteral("item_%1.bin").arg(item.id);
        name.replace(QLatin1Char('/'), QLatin1Char('_'));
        name.replace(QLatin1Char('\\'), QLatin1Char('_'));
        const QString outPath = output + QLatin1Char('/') + name;
        if (!util::writeFile(outPath, data.mid(int(item.offset), int(item.size))))
            throw std::runtime_error(QStringLiteral("write failed: %1").arg(outPath).toStdString());
        ctx.info(QStringLiteral("%1 → %2（%3 字节）").arg(name, outPath).arg(item.size));
        commands.append(QStringLiteral("%1:%2:%3:%4")
                            .arg(item.mainType.isEmpty() ? QStringLiteral("UNKNOWN") : item.mainType,
                                 item.subType.isEmpty() ? QStringLiteral("UNKNOWN") : item.subType,
                                 item.fileType == 0xFE ? QStringLiteral("sparse") : QStringLiteral("normal"),
                                 name));
        ++written;
    }
    util::writeFile(output + QStringLiteral("/commands.txt"),
                    (commands.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    ctx.ok(QStringLiteral("Amlogic image extracted: %1 file(s)").arg(written));
}


void amlogicUpdate(TaskContext& ctx, const QVariantMap& options)
{
    const QString img = options.value(QStringLiteral("img")).toString().trimmed();
    if (img.isEmpty()) throw std::runtime_error("select an Amlogic USB Burning image");
    if (!QFileInfo::exists(img)) throw std::runtime_error("Amlogic image not found");
    const QString exe = util::dataDir() + QStringLiteral("/thirdparty/amlogic/update.exe");
    if (!QFileInfo::exists(exe)) throw std::runtime_error("Amlogic update.exe is not bundled");
    QStringList args;
    args << QStringLiteral("--img=") + img;
    const QString parts = options.value(QStringLiteral("parts")).toString().trimmed();
    args << QStringLiteral("--parts=") + (parts.isEmpty() ? QStringLiteral("all") : parts);
    const QString soc = options.value(QStringLiteral("soc")).toString().trimmed();
    if (!soc.isEmpty()) args << QStringLiteral("--soc=") + soc;
    if (options.value(QStringLiteral("wipe")).toBool()) args << QStringLiteral("--wipe");
    if (options.value(QStringLiteral("reset")).toBool()) args << QStringLiteral("--reset=y");
    ctx.info(QStringLiteral("调用 Amlogic update.exe：%1").arg(args.join(QLatin1Char(' '))));
    QProcess process;
    process.setWorkingDirectory(QFileInfo(exe).absolutePath());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(exe, args);
    if (!process.waitForStarted(5000))
        throw std::runtime_error("无法启动 Amlogic update.exe");
    while (!process.waitForFinished(500)) {
        ctx.checkCanceled();
        if (process.state() == QProcess::NotRunning) break;
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
        throw std::runtime_error("Amlogic update 超时");
    }
    const QString output = QString::fromLocal8Bit(process.readAll()).trimmed();
    if (!output.isEmpty()) {
        for (const QString& line : output.split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty()) ctx.info(line.trimmed());
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw std::runtime_error(QStringLiteral("Amlogic update 失败，退出码 %1").arg(process.exitCode()).toStdString());
    ctx.ok(QStringLiteral("Amlogic 烧写命令已完成"));
}


void amlogicImagePack(TaskContext& ctx, const QVariantMap& options)
{
    const QString dir = options.value(QStringLiteral("input")).toString().trimmed();
    const QString output = options.value(QStringLiteral("output")).toString().trimmed();
    if (dir.isEmpty() || output.isEmpty())
        throw std::runtime_error("select an extracted directory and an output .img");
    QString error;
    if (!img::amlogic::packDirectory(dir, output, &error))
        throw std::runtime_error(error.toStdString());
    const QByteArray data = util::readFile(output);
    const img::amlogic::Image image = img::amlogic::parse(data);
    if (!image.valid) throw std::runtime_error(image.error.toStdString());
    ctx.info(img::amlogic::describe(image));
    ctx.ok(QStringLiteral("Amlogic image packed: %1").arg(output));
}

void registerMbnTools()
{
    using namespace gf::fields;
    ToolSpec spec;
    spec.id = QStringLiteral("qcom.mbn.info");
    spec.category = QStringLiteral("qcom");
    spec.group = QStringLiteral("tool");
    spec.name = QStringLiteral("MBN / ELF info");
    spec.desc = QStringLiteral("Parse Qualcomm MBN / ELF headers, segments and addresses");
    spec.icon = QStringLiteral("file-search");
    spec.keywords = {QStringLiteral("mbn"), QStringLiteral("elf"), QStringLiteral("qualcomm")};
    spec.fields = {path(QStringLiteral("input"), QStringLiteral("MBN / ELF file"),
                        QStringLiteral("*.mbn *.elf *.bin *.*"))};
    spec.order = 90;
    addTool(spec, mbnInfo);
    ToolSpec oplus;
    oplus.id = QStringLiteral("qcom.oplus.digest");
    oplus.category = QStringLiteral("qcom");
    oplus.group = QStringLiteral("tool");
    oplus.name = QStringLiteral("Oplus Digest");
    oplus.desc = QStringLiteral("Parse OPPO / realme / OnePlus Oplus digest MBN/ELF partition records");
    oplus.icon = QStringLiteral("file-shield");
    oplus.keywords = {QStringLiteral("oplus"), QStringLiteral("oppo"), QStringLiteral("realme"),
                      QStringLiteral("digest"), QStringLiteral("mbn")};
    oplus.fields = {path(QStringLiteral("input"), QStringLiteral("Oplus digest MBN/ELF"),
                         QStringLiteral("*.mbn *.elf *.*"))};
    oplus.order = 95;
    addTool(oplus, oplusDigestInfo);

    ToolSpec aml;
    aml.id = QStringLiteral("amlogic.image.info");
    aml.category = QStringLiteral("firmware");
    aml.group = QStringLiteral("tool");
    aml.name = QStringLiteral("Amlogic burning image");
    aml.desc = QStringLiteral("Parse Amlogic USB Burning .img header and item table");
    aml.icon = QStringLiteral("file-archive");
    aml.keywords = {QStringLiteral("amlogic"), QStringLiteral("worldcup"), QStringLiteral("burning")};
    aml.fields = {path(QStringLiteral("input"), QStringLiteral("Amlogic .img"), QStringLiteral("*.img *.*"))};
    aml.order = 96;
    addTool(aml, amlogicImageInfo);

    ToolSpec amlExtract;
    amlExtract.id = QStringLiteral("amlogic.image.extract");
    amlExtract.category = QStringLiteral("firmware");
    amlExtract.group = QStringLiteral("tool");
    amlExtract.name = QStringLiteral("Amlogic image extract");
    amlExtract.desc = QStringLiteral("Extract partitions from an Amlogic USB Burning .img");
    amlExtract.icon = QStringLiteral("package-open");
    amlExtract.keywords = {QStringLiteral("amlogic"), QStringLiteral("img"), QStringLiteral("extract")};
    amlExtract.fields = {path(QStringLiteral("input"), QStringLiteral("Amlogic .img"), QStringLiteral("*.img")),
                         dir(QStringLiteral("output"), QStringLiteral("输出目录"))};
    amlExtract.order = 97;
    addTool(amlExtract, amlogicImageExtract);

    ToolSpec amlUpdate;
    amlUpdate.id = QStringLiteral("amlogic.update");
    amlUpdate.category = QStringLiteral("firmware");
    amlUpdate.group = QStringLiteral("tool");
    amlUpdate.name = QStringLiteral("Amlogic USB Burn");
    amlUpdate.desc = QStringLiteral("Call bundled Amlogic update.exe to burn an Amlogic USB Burning image");
    amlUpdate.icon = QStringLiteral("flame");
    amlUpdate.keywords = {QStringLiteral("amlogic"), QStringLiteral("worldcup"), QStringLiteral("burn")};
    amlUpdate.fields = {path(QStringLiteral("img"), QStringLiteral("Amlogic .img"), QStringLiteral("*.img")),
                        text(QStringLiteral("parts"), QStringLiteral("分区"), QStringLiteral("all"),
                             QStringLiteral("all / bootloader / boot / system ...")),
                        choice(QStringLiteral("soc"), QStringLiteral("SoC"),
                               {QStringLiteral("gxl"), QStringLiteral("axg"), QStringLiteral("txlx"),
                                QStringLiteral("g12a"), QStringLiteral("m8")}, 0),
                        boolean(QStringLiteral("wipe"), QStringLiteral("擦除"), false),
                        boolean(QStringLiteral("reset"), QStringLiteral("完成后重启"), false)};
    amlUpdate.order = 98;
    addTool(amlUpdate, amlogicUpdate);

    ToolSpec amlPack;
    amlPack.id = QStringLiteral("amlogic.image.pack");
    amlPack.category = QStringLiteral("firmware");
    amlPack.group = QStringLiteral("tool");
    amlPack.name = QStringLiteral("Amlogic image pack");
    amlPack.desc = QStringLiteral("Pack an extracted Amlogic image directory back into a USB Burning .img");
    amlPack.icon = QStringLiteral("package");
    amlPack.keywords = {QStringLiteral("amlogic"), QStringLiteral("pack"), QStringLiteral("img")};
    amlPack.fields = {dir(QStringLiteral("input"), QStringLiteral("解包目录")),
                      path(QStringLiteral("output"), QStringLiteral("输出 .img"), QStringLiteral("*.img"))};
    amlPack.order = 99;
    addTool(amlPack, amlogicImagePack);




}
} // namespace gf
