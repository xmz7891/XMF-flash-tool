#include <QtTest>

#include "TestRegistry.h"

#include "ElfFormat.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace {

void put16(QByteArray& out, qsizetype at, quint16 value)
{
    out[at] = char(value & 0xFF);
    out[at + 1] = char((value >> 8) & 0xFF);
}

void put32(QByteArray& out, qsizetype at, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        out[at + i] = char((value >> (8 * i)) & 0xFF);
}

void put64(QByteArray& out, qsizetype at, quint64 value)
{
    for (int i = 0; i < 8; ++i)
        out[at + i] = char((value >> (8 * i)) & 0xFF);
}

/// 生成一个带 symtab/strtab 的最小 ELF64（AArch64）。
QByteArray buildElf64()
{
    const QByteArray strtab = QByteArray("\0main\0helper\0", 13);
    const qsizetype strtabOffset = 256;
    const qsizetype symtabOffset = 272;
    const QByteArray elfSize = QByteArray(qsizetype(symtabOffset + 24 * 3), '\0');
    QByteArray elf = elfSize;

    // ELF header
    elf[0] = char(0x7F);
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = char(2); // ELF64
    elf[5] = char(1); // little endian
    elf[6] = char(1); // version
    put16(elf, 16, 2);    // ET_EXEC
    put16(elf, 18, 183);  // EM_AARCH64
    put32(elf, 20, 1);    // version
    put64(elf, 24, 0x400000); // entry
    put64(elf, 32, 0);    // phoff
    put64(elf, 40, 64);   // shoff
    put32(elf, 48, 0);    // flags
    put16(elf, 52, 64);   // ehsize
    put16(elf, 54, 56);   // phentsize
    put16(elf, 56, 0);    // phnum
    put16(elf, 58, 64);   // shentsize
    put16(elf, 60, 3);    // shnum
    put16(elf, 62, 0);    // shstrndx

    // section 1: .strtab
    const qsizetype sh1 = 64 + 64;
    put32(elf, sh1 + 4, 3);                 // SHT_STRTAB
    put64(elf, sh1 + 24, strtabOffset);     // sh_offset
    put64(elf, sh1 + 32, quint64(strtab.size())); // sh_size
    put64(elf, sh1 + 48, 1);                // addralign

    // section 2: .symtab
    const qsizetype sh2 = 64 + 128;
    put32(elf, sh2 + 4, 2);                 // SHT_SYMTAB
    put64(elf, sh2 + 24, symtabOffset);
    put64(elf, sh2 + 32, 24 * 3);
    put32(elf, sh2 + 40, 1);                // sh_link -> strtab
    put32(elf, sh2 + 44, 1);                // sh_info
    put64(elf, sh2 + 48, 8);                // addralign
    put64(elf, sh2 + 56, 24);               // entsize

    elf.replace(strtabOffset, strtab.size(), strtab);

    auto writeSymbol = [&](qsizetype index, quint32 nameOffset, quint8 info, quint64 value,
                           quint64 size) {
        const qsizetype at = symtabOffset + index * 24;
        put32(elf, at + 0, nameOffset);
        elf[at + 4] = char(info);
        elf[at + 5] = char(0);
        put16(elf, at + 6, 1); // shndx
        put64(elf, at + 8, value);
        put64(elf, at + 16, size);
    };
    writeSymbol(0, 0, 0, 0, 0);
    writeSymbol(1, 1, (1 << 4) | 2, 0x400100, 32); // GLOBAL FUNC main
    writeSymbol(2, 6, (0 << 4) | 2, 0x400200, 8);  // LOCAL FUNC helper
    return elf;
}

QString findWorkspaceSample()
{
    QString base = QDir::currentPath();
    for (int depth = 0; depth < 5; ++depth) {
        const QStringList dirs = {
            gf::img::joinPath(base, QStringLiteral("_research/extract/package_mtk_edl-master/DA/Common")),
            gf::img::joinPath(base, QStringLiteral("_research/extract/package_mtk_edl-master/DA")),
            gf::img::joinPath(base, QStringLiteral("_research/extract/package_mtk_edl-master/PreLoader")),
        };
        for (const QString& dir : dirs) {
            if (!QFileInfo::exists(dir))
                continue;
            QDirIterator it(dir, {QStringLiteral("*.bin"), QStringLiteral("*.melf")}, QDir::Files,
                            QDirIterator::Subdirectories);
            int checked = 0;
            while (it.hasNext() && checked < 60) {
                const QString path = it.next();
                ++checked;
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly))
                    continue;
                const QByteArray head = file.read(4);
                if (head.size() == 4 && quint8(head.at(0)) == 0x7F && head.at(1) == 'E' &&
                    head.at(2) == 'L' && head.at(3) == 'F')
                    return path;
            }
        }
        QDir up(base);
        if (!up.cdUp())
            break;
        base = up.absolutePath();
    }
    return {};
}

} // namespace

class TestElf : public QObject
{
    Q_OBJECT

private slots:
    void parseMinimalElf64()
    {
        const QByteArray elf = buildElf64();
        QVERIFY(gf::img::elf::isElf(elf));
        const gf::img::elf::Info info = gf::img::elf::parse(elf);
        QVERIFY(info.valid);
        QCOMPARE(info.elfClass, 64);
        QVERIFY(!info.bigEndian);
        QCOMPARE(info.type, quint16(2));
        QCOMPARE(info.machine, quint16(183));
        QCOMPARE(info.entry, quint64(0x400000));
        QVERIFY(info.machineText().contains(QStringLiteral("AArch64")));
        QVERIFY(info.typeText().contains(QStringLiteral("EXEC")));
        QCOMPARE(info.sections.size(), 3);
        QCOMPARE(info.sections.at(1).type, 3u);
        QCOMPARE(info.sections.at(2).type, 2u);
        QCOMPARE(info.sections.at(2).link, 1u);

        QCOMPARE(info.symbols.size(), 3);
        bool foundMain = false;
        bool foundHelper = false;
        for (const auto& symbol : info.symbols) {
            if (symbol.name == QStringLiteral("main")) {
                foundMain = true;
                QCOMPARE(symbol.value, quint64(0x400100));
                QCOMPARE(symbol.size, quint64(32));
                QCOMPARE(symbol.bindText(), QStringLiteral("GLOBAL"));
                QCOMPARE(symbol.typeText(), QStringLiteral("FUNC"));
            } else if (symbol.name == QStringLiteral("helper")) {
                foundHelper = true;
                QCOMPARE(symbol.bindText(), QStringLiteral("LOCAL"));
            }
        }
        QVERIFY(foundMain);
        QVERIFY(foundHelper);

        const QStringList lines = gf::img::elf::describe(info);
        QVERIFY(lines.size() >= 4);
        QVERIFY(!lines.first().isEmpty());
    }

    void symbolsFromFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = gf::img::joinPath(dir.path(), QStringLiteral("fake.elf"));
        gf::img::writeFileOrThrow(path, buildElf64());
        const QVector<gf::img::elf::Symbol> symbols = gf::img::elf::readSymbols(path);
        QCOMPARE(symbols.size(), 3);
        QCOMPARE(gf::img::elf::readSymbols(path, 2).size(), 2);
    }

    void realDaElfSmoke()
    {
        const QString sample = findWorkspaceSample();
        if (sample.isEmpty())
            QSKIP("工作区没有可用的 MTK DA ELF 样本，跳过真实样本冒烟");
        const QByteArray data = gf::img::readFileOrThrow(sample);
        QVERIFY(gf::img::elf::isElf(data));
        const gf::img::elf::Info info = gf::img::elf::parse(data);
        QVERIFY(info.valid);
        QVERIFY(info.elfClass == 32 || info.elfClass == 64);
        QVERIFY(!info.machineText().isEmpty());
        QVERIFY(!info.segments.isEmpty());
    }

    void rejectsNonElf()
    {
        bool threw = false;
        try {
            gf::img::elf::parse(QByteArray(64, '\0'));
        } catch (const std::exception& e) {
            threw = QString::fromUtf8(e.what()).contains(QStringLiteral("ELF"));
        }
        QVERIFY(threw);
        QVERIFY(!gf::img::elf::isElf(QByteArray("PK\x03\x04", 4)));
    }
};

#include "test_elf.moc"
GF_TEST_CLASS(TestElf)
