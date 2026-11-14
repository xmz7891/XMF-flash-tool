#include "Pit.h"
#include <QtEndian>
namespace gf::samsung {
namespace {
constexpr quint32 kMagic=0x12349876u;
constexpr int kHeader=28, kEntry=132;
QString fixed(const QByteArray& d,int off,int len){ QByteArray b=d.mid(off,len); int n=b.indexOf('\0'); if(n>=0)b.truncate(n); return QString::fromUtf8(b).trimmed(); }
quint32 u32(const QByteArray& d,int o){ if(o+4>d.size())return 0; return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(d.constData()+o)); }
}
QString deviceTypeText(quint32 t){ switch(t){case 0:return "OneNAND";case 1:return "File/FAT";case 2:return "MMC";case 3:return "All";case 8:return "UFS";default:return QString("type %1").arg(t);} }
quint64 PitEntry::byteSize() const { quint64 bs=deviceType==2?512:(deviceType==8?4096:0); return bs*blockCount; }
PitInfo parsePit(const QByteArray& data){
    PitInfo info;
    if(data.size()<kHeader){info.error="PIT smaller than header";return info;}
    if(u32(data,0)!=kMagic){info.error="PIT magic mismatch (0x12349876 missing)";return info;}
    const quint32 count=u32(data,4);
    if(count>4096 || data.size()<kHeader+int(count)*kEntry){info.error="PIT entry table truncated / count invalid";return info;}
    info.project=fixed(data,8,8); info.cpuBl=fixed(data,16,8); info.luCount=qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()+24));
    for(quint32 i=0;i<count;i++){
        const int b=kHeader+int(i)*kEntry;
        PitEntry e;
        e.binaryType=u32(data,b); e.deviceType=u32(data,b+4); e.partitionId=u32(data,b+8);
        e.attributes=u32(data,b+12); e.updateAttributes=u32(data,b+16);
        e.startBlock=u32(data,b+20); e.blockCount=u32(data,b+24);
        e.fileOffset=u32(data,b+28); e.fileSize=u32(data,b+32);
        e.name=fixed(data,b+36,32); e.flashFile=fixed(data,b+68,32); e.fotaFile=fixed(data,b+100,32);
        if(!e.name.isEmpty()||e.partitionId!=0||e.blockCount!=0) info.entries.append(e);
    }
    info.valid=true; return info;
}
QString describePit(const PitInfo& info){
    if(!info.valid) return info.error;
    return QString("Samsung PIT · %1 entries · project %2 · CPU %3").arg(info.entries.size()).arg(info.project,info.cpuBl);
}
}
