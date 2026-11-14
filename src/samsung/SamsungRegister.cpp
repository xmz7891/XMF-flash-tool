#include "../app/ToolModules.h"
#include "ToolRegistry.h"
#include "TaskManager.h"
#include "Pit.h"
#include "core/Util.h"
#include <QFileInfo>
#include <QVariantMap>
#include <stdexcept>
namespace gf {
namespace {
void pitInfo(TaskContext& ctx,const QVariantMap& options){
    const QString path=options.value("file").toString().trimmed();
    if(path.isEmpty()) throw std::runtime_error("select a PIT file");
    const QByteArray data=util::readFile(path);
    if(data.isEmpty()) throw std::runtime_error("cannot read PIT");
    const samsung::PitInfo info=samsung::parsePit(data);
    if(!info.valid) throw std::runtime_error(info.error.toStdString());
    ctx.info(samsung::describePit(info));
    for(const auto& e: info.entries){
        ctx.info(QString("%1 | id %2 | %3 | start %4 | blocks %5 | bytes %6 | %7")
            .arg(e.name).arg(e.partitionId).arg(samsung::deviceTypeText(e.deviceType))
            .arg(e.startBlock).arg(e.blockCount).arg(e.byteSize()).arg(e.flashFile));
    }
    ctx.ok(QString("PIT parsed: %1 partition(s)").arg(info.entries.size()));
}
}
void registerSamsungTools(){
    using namespace gf::fields;
    ToolSpec spec;
    spec.id="samsung.pit.info";
    spec.category="samsung";
    spec.group="tool";
    spec.name="Samsung PIT";
    spec.desc="Parse Samsung PIT partition table (AP/CP, MMC/UFS, block ranges)";
    spec.icon="table-2";
    spec.keywords={"samsung","odin","pit","heimdall"};
    spec.fields={path("file","PIT file","*.pit *.*")};
    spec.order=10;
    addTool(spec,pitInfo);
}
}
