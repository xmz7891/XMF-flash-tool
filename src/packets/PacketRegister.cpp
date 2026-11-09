#include "PacketStore.h"

#include "../app/ToolModules.h"
#include "TaskManager.h"
#include "ToolRegistry.h"
#include "Util.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace gf {

namespace {

void scanPacketsTool(TaskContext& ctx, const QVariantMap& options)
{
    Q_UNUSED(options);
    ctx.info(QStringLiteral("正在扫描引导包…"));
    auto& store = packets::PacketStore::instance();
    store.refresh();
    const QVariantList packets = store.packets();
    if (packets.isEmpty()) {
        ctx.warn(QStringLiteral("没有找到引导包，请把 GeekFlashPacket 压缩包放入 %1").arg(store.packetDir()));
        return;
    }
    for (const QVariant& entry : packets) {
        const QVariantMap packet = entry.toMap();
        ctx.info(QStringLiteral("%1 [%2] 品牌 %3 / 系列 %4 / 机型 %5")
                     .arg(packet.value(QStringLiteral("name")).toString(),
                          packet.value(QStringLiteral("type")).toString())
                     .arg(packet.value(QStringLiteral("brandCount")).toInt())
                     .arg(packet.value(QStringLiteral("seriesCount")).toInt())
                     .arg(packet.value(QStringLiteral("modelCount")).toInt()));
    }
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("引导包扫描完成，共 %1 个机型").arg(store.modelCount()));
}

void exportPacketModelsTool(TaskContext& ctx, const QVariantMap& options)
{
    const QString outputDir = options.value(QStringLiteral("output")).toString();
    if (outputDir.isEmpty())
        throw std::runtime_error("请选择导出目录");
    util::ensureDir(outputDir);

    auto& store = packets::PacketStore::instance();
    if (store.packetIds().isEmpty())
        store.refresh();

    int total = 0;
    for (const QString& id : store.packetIds()) {
        const QVariantList models = store.models(id);
        if (models.isEmpty())
            continue;
        const QString path = outputDir + QLatin1Char('/') + id + QStringLiteral("_models.csv");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            throw std::runtime_error(QStringLiteral("无法写入 %1").arg(path).toStdString());
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << "品牌,系列,机型,说明,引导文件,加载地址\n";
        for (const QVariant& entry : models) {
            const QVariantMap model = entry.toMap();
            QString guide = model.value(QStringLiteral("DA1")).toString();
            if (guide.isEmpty())
                guide = model.value(QStringLiteral("FDL1")).toString();
            if (guide.isEmpty())
                guide = model.value(QStringLiteral("DA2")).toString();
            QString address = model.value(QStringLiteral("DA1Address")).toString();
            if (address.isEmpty())
                address = model.value(QStringLiteral("FDL1Addr")).toString();
            stream << model.value(QStringLiteral("brand")).toString() << ','
                   << model.value(QStringLiteral("series")).toString() << ','
                   << model.value(QStringLiteral("name")).toString() << ','
                   << model.value(QStringLiteral("description")).toString() << ','
                   << guide << ',' << address << '\n';
            ++total;
        }
        file.close();
        ctx.info(QStringLiteral("已导出 %1").arg(path));
    }
    if (total == 0)
        throw std::runtime_error("没有可导出的机型数据，请先导入引导包");
    ctx.ok(QStringLiteral("导出完成，共 %1 个机型").arg(total));
}

void listPacketModelsTool(TaskContext& ctx, const QVariantMap& options)
{
    auto& store = packets::PacketStore::instance();
    if (store.packetIds().isEmpty())
        store.refresh();
    const QString only = options.value(QStringLiteral("packet")).toString().trimmed();
    int shown = 0;
    for (const QString& id : store.packetIds()) {
        if (!only.isEmpty() && !id.contains(only, Qt::CaseInsensitive))
            continue;
        const QVariantList models = store.models(id);
        ctx.info(QStringLiteral("[%1] %2 个机型").arg(id).arg(models.size()));
        for (const QVariant& entry : models) {
            const QVariantMap model = entry.toMap();
            QString guide = model.value(QStringLiteral("DA1")).toString();
            for (const QString& key : {QStringLiteral("Loader"), QStringLiteral("FDL1"),
                                       QStringLiteral("DA2"), QStringLiteral("Preloader")}) {
                if (!guide.isEmpty())
                    break;
                guide = model.value(key).toString();
            }
            ctx.log(0, QStringLiteral("  %1 | %2 | %3 | 引导=%4")
                           .arg(model.value(QStringLiteral("brand")).toString(),
                                model.value(QStringLiteral("series")).toString(),
                                model.value(QStringLiteral("name")).toString(),
                                guide.isEmpty() ? QStringLiteral("无") : guide));
            ++shown;
            if (shown >= 60)
                break;
        }
        if (shown >= 60)
            break;
    }
    if (shown == 0)
        throw std::runtime_error("引导包中没有匹配的机型，请检查 packet 关键字");
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("共列出 %1 个机型").arg(shown));
}

void resolvePacketResourceTool(TaskContext& ctx, const QVariantMap& options)
{
    auto& store = packets::PacketStore::instance();
    if (store.packetIds().isEmpty())
        store.refresh();
    const QString packetId = options.value(QStringLiteral("packet")).toString().trimmed();
    const QString model = options.value(QStringLiteral("model")).toString().trimmed();
    QString attribute = options.value(QStringLiteral("attribute")).toString().trimmed();
    if (packetId.isEmpty() || model.isEmpty())
        throw std::runtime_error("请填写引导包 id 与机型名称");

    QString resolvedId = packetId;
    if (!store.packetIds().contains(resolvedId)) {
        for (const QString& id : store.packetIds()) {
            if (id.contains(packetId, Qt::CaseInsensitive)) {
                resolvedId = id;
                break;
            }
        }
    }
    const QStringList candidates = attribute.isEmpty()
                                       ? QStringList{QStringLiteral("DA1"), QStringLiteral("Loader"),
                                                     QStringLiteral("FDL1"), QStringLiteral("FDL2"),
                                                     QStringLiteral("Preloader"), QStringLiteral("DA2")}
                                       : QStringList{attribute};
    QString path;
    for (const QString& candidate : candidates) {
        path = store.modelResource(resolvedId, model, candidate);
        if (!path.isEmpty()) {
            attribute = candidate;
            break;
        }
    }
    if (path.isEmpty())
        throw std::runtime_error(QStringLiteral("机型 %1 没有 %2 资源").arg(model, attribute).toStdString());
    const QFileInfo info(path);
    ctx.info(QStringLiteral("引导包: %1").arg(resolvedId));
    ctx.info(QStringLiteral("机型: %1").arg(model));
    ctx.info(QStringLiteral("属性: %1").arg(attribute));
    ctx.ok(QStringLiteral("已解析到 %1（%2）").arg(info.fileName(), util::formatBytes(info.size())));
    ctx.log(0, QStringLiteral("路径: %1").arg(path));
    if (!info.exists() || info.size() == 0)
        ctx.warn(QStringLiteral("解析出的文件不存在或为空，请检查引导包内容"));
    ctx.setProgress(100);
}

} // namespace

void registerPacketTools()
{
    {
        ToolSpec spec;
        spec.id = QStringLiteral("packets.scan");
        spec.category = QStringLiteral("firmware");
        spec.name = QStringLiteral("引导包扫描");
        spec.desc = QStringLiteral("扫描 data/packets 下的 GeekFlashPacket 引导包并统计机型");
        spec.icon = QStringLiteral("package");
        spec.keywords = {QStringLiteral("引导包"), QStringLiteral("扫描"), QStringLiteral("packet")};
        spec.fields = {};
        spec.order = 5;
        addTool(spec, scanPacketsTool);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("packets.export");
        spec.category = QStringLiteral("firmware");
        spec.name = QStringLiteral("导出机型清单");
        spec.desc = QStringLiteral("把引导包中的品牌/系列/机型与引导文件导出为 CSV");
        spec.icon = QStringLiteral("file-down");
        spec.fields = {
            fields::dir(QStringLiteral("output"), QStringLiteral("导出目录")),
        };
        spec.order = 6;
        addTool(spec, exportPacketModelsTool);
    }

    {
        ToolSpec spec;
        spec.id = QStringLiteral("packets.models");
        spec.category = QStringLiteral("firmware");
        spec.name = QStringLiteral("引导包机型列表");
        spec.desc = QStringLiteral("按引导包列出品牌/系列/机型以及对应的引导文件（DA/FDL）");
        spec.icon = QStringLiteral("list");
        spec.keywords = {QStringLiteral("引导包"), QStringLiteral("机型"), QStringLiteral("packet")};
        spec.fields = {
            fields::text(QStringLiteral("packet"), QStringLiteral("引导包关键字（可留空）"), {}, {}, {}, false),
        };
        spec.order = 7;
        addTool(spec, listPacketModelsTool);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("packets.resolve");
        spec.category = QStringLiteral("firmware");
        spec.name = QStringLiteral("解析引导包文件");
        spec.desc = QStringLiteral("解析某个机型在引导包中的 DA / FDL / Preloader 文件并验证可读性");
        spec.icon = QStringLiteral("file-search");
        spec.keywords = {QStringLiteral("引导包"), QStringLiteral("DA"), QStringLiteral("FDL")};
        spec.fields = {
            fields::text(QStringLiteral("packet"), QStringLiteral("引导包 id 或关键字")),
            fields::text(QStringLiteral("model"), QStringLiteral("机型名称")),
            fields::text(QStringLiteral("attribute"),
                         QStringLiteral("属性（留空自动选择 DA1/Loader/FDL1/FDL2/Preloader）"),
                         {}, {}, {}, false),
        };
        spec.order = 8;
        addTool(spec, resolvePacketResourceTool);
    }
}

} // namespace gf
