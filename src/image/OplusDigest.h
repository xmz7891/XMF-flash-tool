#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
namespace gf::img::oplus {
struct Partition { QString label, fileName; bool allowRead=false, allowWrite=false; quint64 startSector=0, sectors=0; QString hashHex; };
struct Digest { bool valid=false; QString error; quint32 version=0, headerSize=0, partitionCount=0, partitionLength=0; QList<Partition> partitions; };
Digest parse(const QByteArray& data);
}
