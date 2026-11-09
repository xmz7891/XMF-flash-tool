#include <QtTest>

#include <QByteArray>
#include <QImage>

#include "TestRegistry.h"
#include "image/BootLogo.h"

class TestBootLogo : public QObject {
    Q_OBJECT

private slots:
    void findsEmbeddedPng()
    {
        QByteArray blob = QByteArrayLiteral("VENDOR-HEADER-0123456789");
        blob += QByteArrayLiteral("\x89PNG\r\n\x1a\n");
        blob += QByteArray("fake-png-body-0123456789", 24);
        blob += QByteArrayLiteral("IEND");
        blob += QByteArray("----", 4);
        blob += QByteArrayLiteral("TRAILER");

        const auto images = gf::img::findEmbeddedImages(blob);
        QVERIFY(!images.isEmpty());
        QCOMPARE(images.first().format, QStringLiteral("png"));
        QCOMPARE(images.first().offset, qint64(blob.indexOf("\x89PNG\r\n\x1a\n")));
        QVERIFY(images.first().data.startsWith(QByteArrayLiteral("\x89PNG")));
        QVERIFY(images.first().data.contains("IEND"));
    }

    void findsEmbeddedJpeg()
    {
        QByteArray blob = QByteArray(64, '\0');
        blob += QByteArrayLiteral("\xFF\xD8\xFF\xE0");
        blob += QByteArray(128, '\x11');
        const auto images = gf::img::findEmbeddedImages(blob);
        QVERIFY(!images.isEmpty());
        QCOMPARE(images.first().format, QStringLiteral("jpeg"));
        QVERIFY(images.first().offset >= 64);
    }

    void decodesRgb565()
    {
        // 2x2 pixels: red, green, blue, white (little endian RGB565)
        const quint16 pixels[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
        QByteArray data(reinterpret_cast<const char*>(pixels), sizeof(pixels));
        const QImage image = gf::img::decodeRawBitmap(data, 2, 2, 0, 16);
        QVERIFY(!image.isNull());
        QCOMPARE(image.width(), 2);
        QCOMPARE(image.height(), 2);
        QCOMPARE(QColor(image.pixel(0, 0)).red(), 255);
        QCOMPARE(QColor(image.pixel(0, 0)).green(), 0);
        QCOMPARE(QColor(image.pixel(1, 0)).green(), 255);
        QCOMPARE(QColor(image.pixel(0, 1)).blue(), 255);
        QCOMPARE(QColor(image.pixel(1, 1)), QColor(Qt::white));
    }

    void encodesRgb565()
    {
        QImage image(2, 2, QImage::Format_RGB32);
        image.setPixelColor(0, 0, QColor(Qt::red));
        image.setPixelColor(1, 0, QColor(Qt::green));
        image.setPixelColor(0, 1, QColor(Qt::blue));
        image.setPixelColor(1, 1, QColor(Qt::white));
        const QByteArray raw = gf::img::encodeRawBitmap(image, 16);
        QCOMPARE(raw.size(), 8);
        const QImage back = gf::img::decodeRawBitmap(raw, 2, 2, 0, 16);
        QCOMPARE(QColor(back.pixel(0, 0)), QColor(Qt::red));
        QCOMPARE(QColor(back.pixel(1, 0)), QColor(Qt::green));
        QCOMPARE(QColor(back.pixel(0, 1)), QColor(Qt::blue));
        QCOMPARE(QColor(back.pixel(1, 1)), QColor(Qt::white));
    }

    void encodesBgr24()
    {
        QImage image(1, 1, QImage::Format_RGB32);
        image.setPixelColor(0, 0, QColor(Qt::red));
        const QByteArray raw = gf::img::encodeRawBitmap(image, 24);
        QCOMPARE(raw.size(), 3);
        QCOMPARE(quint8(raw.at(0)), quint8(0));   // B
        QCOMPARE(quint8(raw.at(1)), quint8(0));   // G
        QCOMPARE(quint8(raw.at(2)), quint8(255)); // R
    }

    void rejectsShortBuffer()
    {
        QByteArray data(16, '\0');
        QVERIFY(gf::img::decodeRawBitmap(data, 8, 8, 0, 16).isNull());
    }

    void decodes24Bit()
    {
        QByteArray data;
        data += char(0x00); data += char(0x00); data += char(0xFF); // red
        const QImage image = gf::img::decodeRawBitmap(data, 1, 1, 0, 24);
        QVERIFY(!image.isNull());
        QCOMPARE(QColor(image.pixel(0, 0)), QColor(255, 0, 0));
    }
};

GF_TEST_CLASS(TestBootLogo)

#include "test_bootlogo.moc"
