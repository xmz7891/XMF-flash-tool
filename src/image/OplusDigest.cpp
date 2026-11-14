#include "OplusDigest.h"
#include <QtEndian>
namespace gf::img::oplus {
namespace {
constexpr quint32 kDigestMagic=0x7CEF0312u;
quint32 u32(const QByteArray& d,int o){ if(o+4>d.size())return 0; return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(d.constData()+o)); }
quint64 u64(const QByteArray& d,int o){ if(o+8>d.size())return 0; return qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(d.constData()+o)); }
QString fixed(const QByteArray& d,int o,int n){ QByteArray b=d.mid(o,n); int z=b.indexOf('\0'); if(z>=0)b.truncate(z); return QString::fromLatin1(b).trimmed(); }
Digest parseBuf(const QByteArray& b){
    Digest d;
    if(b.size()<32){d.error="digest header too small";return d;}
    if(u32(b,0)!=kDigestMagic){d.error="Oplus digest magic 0x7CEF0312 missing";return d;}
    d.version=u32(b,4); d.headerSize=u32(b,8); d.partitionCount=u32(b,12);
    const quint32 total=u32(b,16); d.partitionLength=u32(b,20);
    if(d.headerSize+d.partitionCount*d.partitionLength!=quint64(b.size()) || d.partitionCount*d.partitionLength!=total){
        d.error="digest length/table mismatch"; return d;
    }
    if(d.partitionCount>4096 || d.partitionLength<0x84 || d.partitionLength>4096){d.error="digest partition table invalid";return d;}
    for(quint32 i=0;i<d.partitionCount;i++){
        const int o=32+int(i)*int(d.partitionLength);
        Partition p;
        p.label=fixed(b,o,0x20); p.fileName=fixed(b,o+0x20,0x20);
        p.allowRead=b.at(o+0x40)!=0; p.allowWrite=b.at(o+0x44)!=0;
        p.startSector=u64(b,o+0x54); p.sectors=u64(b,o+0x5C);
        QByteArray hash=b.mid(o+0x64,0x20); p.hashHex=QString::fromLatin1(hash.toHex());
        d.partitions.append(p);
    }
    d.valid=true; return d;
}
}
Digest parse(const QByteArray& data){
    const QByteArray elfMagic("\x7f""ELF",4);
    const QByteArray mbnMagic=QByteArray::fromHex("1A00000006000000");
    if(data.startsWith(elfMagic)){
        if(data.size()<0x9A){Digest d;d.error="ELF too small";return d;}
        const quint16 len=qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()+0x98));
        return parseBuf(data.mid(0xE8,len));
    }
    if(data.startsWith(mbnMagic)){
        if(data.size()<0x18){Digest d;d.error="MBN too small";return d;}
        const quint32 len=u32(data,0x14);
        return parseBuf(data.mid(0xA8,int(len)));
    }
    Digest d; d.error="not an Oplus digest MBN/ELF (magic mismatch)"; return d;
}
}
