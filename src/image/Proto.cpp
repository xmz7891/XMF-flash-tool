#include "Proto.h"

#include "ImageCommon.h"

namespace gf::img::proto {

namespace {
constexpr int kWireVarint = 0;
constexpr int kWireFixed64 = 1;
constexpr int kWireLength = 2;
constexpr int kWireFixed32 = 5;
} // namespace

quint64 readVarint(const QByteArray& data, qsizetype* pos, qsizetype end)
{
    quint64 value = 0;
    int shift = 0;
    qsizetype p = *pos;
    while (true) {
        if (p >= end)
            fail(QStringLiteral("protobuf 数据不完整（varint 截断）"));
        const quint8 byte = quint8(data.at(p++));
        value |= quint64(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0)
            break;
        shift += 7;
        if (shift >= 64)
            fail(QStringLiteral("protobuf varint 溢出"));
    }
    *pos = p;
    return value;
}

Reader::Reader(const QByteArray& data, qsizetype offset, qsizetype length)
    : m_data(data)
{
    if (offset < 0 || offset > m_data.size())
        fail(QStringLiteral("protobuf 读取偏移越界"));
    m_pos = offset;
    m_end = length < 0 ? m_data.size() : qMin<qsizetype>(m_data.size(), offset + length);
}

bool Reader::next(Field& out)
{
    if (m_pos >= m_end)
        return false;
    const quint64 key = readVarint(m_data, &m_pos, m_end);
    out = Field{};
    out.number = quint32(key >> 3);
    out.wire = int(key & 0x7);
    switch (out.wire) {
    case kWireVarint:
        out.varint = readVarint(m_data, &m_pos, m_end);
        break;
    case kWireFixed64: {
        if (m_end - m_pos < 8)
            fail(QStringLiteral("protobuf fixed64 数据截断"));
        quint64 v = 0;
        for (int i = 0; i < 8; ++i)
            v |= quint64(quint8(m_data.at(m_pos + i))) << (8 * i);
        out.fixed64 = v;
        m_pos += 8;
        break;
    }
    case kWireFixed32: {
        if (m_end - m_pos < 4)
            fail(QStringLiteral("protobuf fixed32 数据截断"));
        quint32 v = 0;
        for (int i = 0; i < 4; ++i)
            v |= quint32(quint8(m_data.at(m_pos + i))) << (8 * i);
        out.fixed32 = v;
        m_pos += 4;
        break;
    }
    case kWireLength: {
        const quint64 len = readVarint(m_data, &m_pos, m_end);
        if (len > quint64(m_end - m_pos))
            fail(QStringLiteral("protobuf 长度前缀超出剩余数据"));
        out.bytes = m_data.mid(m_pos, qsizetype(len));
        m_pos += qsizetype(len);
        break;
    }
    default:
        fail(QStringLiteral("不支持 protobuf wire 类型 %1").arg(out.wire));
    }
    return true;
}

void writeVarint(QByteArray& out, quint64 value)
{
    do {
        quint8 byte = quint8(value & 0x7F);
        value >>= 7;
        if (value)
            byte |= 0x80;
        out.append(char(byte));
    } while (value);
}

void writeTag(QByteArray& out, quint32 number, int wire)
{
    writeVarint(out, (quint64(number) << 3) | quint64(wire));
}

void writeVarintField(QByteArray& out, quint32 number, quint64 value)
{
    writeTag(out, number, kWireVarint);
    writeVarint(out, value);
}

void writeFixed32Field(QByteArray& out, quint32 number, quint32 value)
{
    writeTag(out, number, kWireFixed32);
    for (int i = 0; i < 4; ++i)
        out.append(char((value >> (8 * i)) & 0xFF));
}

void writeFixed64Field(QByteArray& out, quint32 number, quint64 value)
{
    writeTag(out, number, kWireFixed64);
    for (int i = 0; i < 8; ++i)
        out.append(char((value >> (8 * i)) & 0xFF));
}

void writeBytesField(QByteArray& out, quint32 number, const QByteArray& value)
{
    writeTag(out, number, kWireLength);
    writeVarint(out, quint64(value.size()));
    out.append(value);
}

void writeStringField(QByteArray& out, quint32 number, const QString& value)
{
    writeBytesField(out, number, value.toUtf8());
}

} // namespace gf::img::proto
