// SPDX-FileCopyrightText: 2026 MediaGun
// SPDX-License-Identifier: LGPL-2.0-or-later

#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QtTest>

// The plugin as Qt loads it, from the build tree only: on macOS Qt's own HEIC plugin (qmacheif)
// must not answer instead.
class TestQHeif : public QObject
{
    Q_OBJECT

private:
    static QString data(const QString &name) { return QStringLiteral(QHEIF_TEST_DATA "/") + name; }

private slots:
    void initTestCase()
    {
        QCoreApplication::setLibraryPaths({ QStringLiteral(QHEIF_PLUGIN_DIR) });
    }

    void formats()
    {
        const auto formats = QImageReader::supportedImageFormats();
        for (const char *format : { "heic", "heif", "hif", "avif" }) {
            QVERIFY2(formats.contains(format), format);
        }
        const auto mimeTypes = QImageReader::supportedMimeTypes();
        QVERIFY(mimeTypes.contains("image/heic"));
        QVERIFY(mimeTypes.contains("image/heif"));
        QVERIFY(mimeTypes.contains("image/avif"));
    }

    void read_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QByteArray>("format");
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("alpha");

        QTest::newRow("HEIC") << "rainbow-451x461.heic" << QByteArray("heif") << QSize(451, 461) << false;
        QTest::newRow("HEIC with alpha") << "with-alpha-512x512.heic" << QByteArray("heif") << QSize(512, 512) << true;
        QTest::newRow("AVIF") << "example.avif" << QByteArray("avif") << QSize(800, 533) << false;
        QTest::newRow("AVIF, compact format, with alpha") << "simple_osm_tile_alpha.avif" << QByteArray("avif") << QSize(256, 256) << true;
    }

    void read()
    {
        QFETCH(QString, file);
        QFETCH(QByteArray, format);
        QFETCH(QSize, size);
        QFETCH(bool, alpha);

        QImageReader reader(data(file));
        QVERIFY2(reader.canRead(), qPrintable(reader.errorString()));
        QCOMPARE(reader.format(), format);
        QCOMPARE(reader.size(), size);

        const QImage image = reader.read();
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));
        QCOMPARE(image.size(), size);
        QCOMPARE(image.hasAlphaChannel(), alpha);
        QVERIFY(image.colorSpace().isValid());
    }

    // By content, whatever the file is called: phones and messengers rename files.
    void readByContent()
    {
        QFile file(data(QStringLiteral("rainbow-451x461.heic")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QBuffer buffer;
        buffer.setData(file.readAll());

        QImageReader reader(&buffer);
        QCOMPARE(reader.format(), QByteArray("heif"));
        QCOMPARE(reader.read().size(), QSize(451, 461));
    }

    // libheif applies the rotation and mirroring stored in the file, and Qt mustn't apply them again.
    void storedTransformationApplied()
    {
        QImageReader reader(data(QStringLiteral("clap_cropped_irot_imir.avif")));
        reader.setAutoTransform(true);
        QCOMPARE(reader.transformation(), QImageIOHandler::TransformationNone);
        const QSize size = reader.size();
        const QImage image = reader.read();
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));
        QCOMPARE(image.size(), size);
    }

    void truncatedFile()
    {
        QFile file(data(QStringLiteral("rainbow-451x461.heic")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QBuffer buffer;
        buffer.setData(file.read(200)); // the file type and part of the image description

        QImageReader reader(&buffer, "heic");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^qheif: can't read the file")));
        QVERIFY(reader.read().isNull());
    }

    void notHeif()
    {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        QImage(4, 4, QImage::Format_RGB32).save(&buffer, "png");
        buffer.close();

        QImageReader reader(&buffer, "heic");
        QVERIFY(reader.read().isNull());
    }
};

QTEST_GUILESS_MAIN(TestQHeif)
#include "tst_qheif.moc"
