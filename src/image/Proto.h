#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace gf::img::proto {

/// 最小 protobuf wire-format 读解析器（只实现 payload/AVB 等用到的子集）。
struct Field {
    quint32 number = 0;
    int wire = 0;
    quint64 varint = 0;   // wire 0
    quint64 fixed64 = 0;  // wire 1
    quint32 fixed32 = 0;  // wire 5
    QByteArray bytes;     // wire 2
};

class Reader {
public:
    explicit Reader(const QByteArray& data, qsizetype offset = 0, qsizetype length = -1);

    /// 读取下一个字段；到达末尾返回 false。格式非法时抛 std::runtime_error。
    bool next(Field& out);
    bool atEnd() const { return m_pos >= m_end; }
    qsizetype position() const { return m_pos; }
    qsizetype remaining() const { return m_end - m_pos; }

private:
    QByteArray m_data;
    qsizetype m_pos = 0;
    qsizetype m_end = 0;
};

quint64 readVarint(const QByteArray& data, qsizetype* pos, qsizetype end);

void writeVarint(QByteArray& out, quint64 value);
void writeTag(QByteArray& out, quint32 number, int wire);
void writeVarintField(QByteArray& out, quint32 number, quint64 value);
void writeFixed32Field(QByteArray& out, quint32 number, quint32 value);
void writeFixed64Field(QByteArray& out, quint32 number, quint64 value);
void writeBytesField(QByteArray& out, quint32 number, const QByteArray& value);
void writeStringField(QByteArray& out, quint32 number, const QString& value);

} // namespace gf::img::proto
