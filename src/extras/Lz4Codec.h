#pragma once

#include <QByteArray>
#include <QString>

namespace gf::extras::lz4 {

/// LZ4 container variants used by Android boot ramdisks.
enum class Frame {
    Unknown = 0,
    RawBlock,   // 裸 LZ4 block，无帧头
    Legacy,     // 旧版帧（magic 0x184C2102，Android mkbootimg 常用）
    Modern,     // 标准帧（magic 0x184D2204）
};

QString frameName(Frame frame);
Frame detect(const QByteArray& data);

/// 解压 LZ4 数据（自动识别帧格式）。损坏时抛 std::runtime_error（中文）。
QByteArray decompress(const QByteArray& data);
/// 解压单个 LZ4 block（无帧头）。
QByteArray decompressBlock(const QByteArray& block);

/// 生成 LZ4 block（贪心哈希匹配，合法格式；不可压缩内容会原样以字面量输出）。
QByteArray compressBlock(const QByteArray& data);
/// 按指定帧格式压缩；Frame::Unknown/RawBlock 按裸 block 处理，Legacy/Modern 生成帧。
QByteArray compress(const QByteArray& data, Frame frame);

} // namespace gf::extras::lz4
