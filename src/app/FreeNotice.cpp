#include "FreeNotice.h"
#include "FreeNoticePrivate.h"

#include <QCryptographicHash>
#include <QByteArray>

namespace gf::notice {
namespace {
constexpr char kMagic[] = "XMFN";
constexpr int kNonceSize = 16;
constexpr int kDigestSize = 16;
constexpr int kHeaderSize = 4 + kNonceSize + kDigestSize;

QByteArray streamFor(const QByteArray& key, const QByteArray& nonce, int size)
{
    QByteArray stream;
    stream.reserve(size);
    quint32 counter = 0;
    while (stream.size() < size) {
        QByteArray seed = key + nonce;
        seed.append(char((counter >> 24) & 0xFF));
        seed.append(char((counter >> 16) & 0xFF));
        seed.append(char((counter >> 8) & 0xFF));
        seed.append(char(counter & 0xFF));
        stream.append(QCryptographicHash::hash(seed, QCryptographicHash::Sha256));
        ++counter;
    }
    return stream.left(size);
}

bool equalConstantTime(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    return diff == 0;
}
} // namespace

bool verify()
{
    const QByteArray payload = private_data::payload();
    if (payload.size() <= kHeaderSize || payload.left(4) != QByteArray(kMagic, 4))
        return false;
    const QByteArray nonce = payload.mid(4, kNonceSize);
    const QByteArray expected = payload.mid(4 + kNonceSize, kDigestSize);
    const QByteArray cipher = payload.mid(kHeaderSize);
    const QByteArray actual = QCryptographicHash::hash(cipher, QCryptographicHash::Sha256).left(kDigestSize);
    if (!equalConstantTime(expected, actual) || !equalConstantTime(expected, private_data::digest()))
        return false;
    const QByteArray key = QByteArrayLiteral("XMF-FREE-XIAOMAI-2026-V1");
    const QByteArray stream = streamFor(key, nonce, cipher.size());
    QByteArray plain;
    plain.resize(cipher.size());
    for (int i = 0; i < cipher.size(); ++i)
        plain[i] = char(cipher.at(i) ^ stream.at(i));
    const QString text = QString::fromUtf8(plain);
    return text.contains(QStringLiteral("永久免费")) && text.contains(QStringLiteral("小麦"))
        && text.contains(QStringLiteral("永不收费"));
}

QString text()
{
    if (!verify())
        return QString();
    const QByteArray payload = private_data::payload();
    const QByteArray nonce = payload.mid(4, kNonceSize);
    const QByteArray cipher = payload.mid(kHeaderSize);
    const QByteArray key = QByteArrayLiteral("XMF-FREE-XIAOMAI-2026-V1");
    const QByteArray stream = streamFor(key, nonce, cipher.size());
    QByteArray plain;
    plain.resize(cipher.size());
    for (int i = 0; i < cipher.size(); ++i)
        plain[i] = char(cipher.at(i) ^ stream.at(i));
    return QString::fromUtf8(plain);
}
} // namespace gf::notice
