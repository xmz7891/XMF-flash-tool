#include "ToolRegistry.h"
#include "TaskManager.h"
#include "ElfFormat.h"
#include "OplusDigest.h"
#include "core/Util.h"

#include <QFileInfo>
#include <QVariantMap>
#include <stdexcept>

namespace gf {
namespace {
void mbnInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString path = options.value(QStringLiteral("input")).toString().trimmed();
    if (path.isEmpty())
        throw std::runtime_error("select an MBN / ELF file");
    const QByteArray data = util::readFile(path);
    if (data.isEmpty())
        throw std::runtime_error("cannot read file");
    if (!img::elf::isElf(data))
        throw std::runtime_error("not an ELF/MBN image (ELF magic missing)");
    const img::elf::Info info = img::elf::parse(data);
    if (!info.valid)
        throw std::runtime_error("ELF parse failed");
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
}
} // namespace gf
