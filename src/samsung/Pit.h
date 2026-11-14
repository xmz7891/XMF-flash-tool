#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
namespace gf::samsung {
struct PitEntry { quint32 binaryType=0, deviceType=0, partitionId=0, attributes=0, updateAttributes=0, startBlock=0, blockCount=0, fileOffset=0, fileSize=0; QString name, flashFile, fotaFile; quint64 byteSize() const; };
struct PitInfo { bool valid=false; QString error, project, cpuBl; quint16 luCount=0; QList<PitEntry> entries; };
PitInfo parsePit(const QByteArray& data);
QString describePit(const PitInfo& info);
QString deviceTypeText(quint32 type);
}
