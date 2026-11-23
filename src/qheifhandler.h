/*
    HEIC/HEIF and AVIF reading for QImage, through libheif.

    Based on the HEIF plugin of KImageFormats / qt-heic-image-plugin
    (https://github.com/novomesk/qt-heic-image-plugin).

    SPDX-FileCopyrightText: 2020 Sirius Bakke <sirius@bakke.co>
    SPDX-FileCopyrightText: 2021 Daniel Novomesky <dnovomesky@gmail.com>
    SPDX-FileCopyrightText: 2026 MediaGun

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef QHEIFHANDLER_H
#define QHEIFHANDLER_H

#include <QByteArray>
#include <QImage>
#include <QImageIOHandler>
#include <QImageIOPlugin>

class QHeifHandler : public QImageIOHandler
{
public:
    enum class Kind { None, Heif, Avif };

    bool canRead() const override;
    bool read(QImage *image) override;

    QVariant option(ImageOption option) const override;
    bool supportsOption(ImageOption option) const override;

    // The file's kind, from the start of the file (its "ftyp" box).
    static Kind kindOf(const QByteArray &header);
    // Whether libheif was built with a decoder for the kind.
    static bool canDecode(Kind kind);

    // Bytes kindOf() needs.
    static constexpr int headerSize = 64;

private:
    bool ensureDecoded();

    enum class State { NotDecoded, Decoded, Error };

    State m_state = State::NotDecoded;
    QImage m_image;
};

class QHeifPlugin : public QImageIOPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QImageIOHandlerFactoryInterface_iid FILE "qheif.json")

public:
    Capabilities capabilities(QIODevice *device, const QByteArray &format) const override;
    QImageIOHandler *create(QIODevice *device, const QByteArray &format = QByteArray()) const override;
};

#endif // QHEIFHANDLER_H
