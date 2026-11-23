/*
    HEIC/HEIF and AVIF reading for QImage, through libheif.

    Based on the HEIF plugin of KImageFormats / qt-heic-image-plugin
    (https://github.com/novomesk/qt-heic-image-plugin): writing, AVCI, HEJ2 and checks for older
    libheif and Qt versions removed; AVIF added.

    SPDX-FileCopyrightText: 2020 Sirius Bakke <sirius@bakke.co>
    SPDX-FileCopyrightText: 2021 Daniel Novomesky <dnovomesky@gmail.com>
    SPDX-FileCopyrightText: 2026 MediaGun

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "qheifhandler.h"

#include <libheif/heif.h>

#include <QColorSpace>
#include <QDebug>
#include <QMutex>
#include <QPointF>
#include <QSysInfo>
#include <QtEndian>

#include <cstring>
#include <limits>

namespace {

QMutex &libheifMutex()
{
    static QMutex mutex;
    return mutex;
}

// libheif is initialised while it's used, and released after: the plugin can be unloaded.
struct LibheifUse
{
    LibheifUse()
    {
        QMutexLocker locker(&libheifMutex());
        heif_init(nullptr);
    }
    ~LibheifUse()
    {
        QMutexLocker locker(&libheifMutex());
        heif_deinit();
    }
    Q_DISABLE_COPY_MOVE(LibheifUse)
};

// Frees the libheif objects of one decode.
struct Decode
{
    heif_context *context = heif_context_alloc();
    heif_image_handle *handle = nullptr;
    heif_image *image = nullptr;

    Decode() = default;
    ~Decode()
    {
        if (image) {
            heif_image_release(image);
        }
        if (handle) {
            heif_image_handle_release(handle);
        }
        heif_context_free(context);
    }
    Q_DISABLE_COPY_MOVE(Decode)
};

quint32 brandAt(const char *data, int offset)
{
    return qFromBigEndian<quint32>(data + offset);
}

constexpr quint32 fourcc(const char (&s)[5])
{
    return (quint32(quint8(s[0])) << 24) | (quint32(quint8(s[1])) << 16) | (quint32(quint8(s[2])) << 8) | quint32(quint8(s[3]));
}

// The colour space the file declares (an ICC profile or CICP values), for the decoded image.
void applyColorSpace(heif_image_handle *handle, bool hasAlpha, int bitDepth, QImage &image)
{
    const heif_color_profile_type profileType = heif_image_handle_get_color_profile_type(handle);

    if (profileType == heif_color_profile_type_prof || profileType == heif_color_profile_type_rICC) {
        const size_t size = heif_image_handle_get_raw_color_profile_size(handle);
        if (size == 0 || size >= size_t(std::numeric_limits<int>::max())) {
            qWarning("qheif: ICC profile empty or too large");
            return;
        }
        QByteArray icc(qsizetype(size), 0);
        if (heif_image_handle_get_raw_color_profile(handle, icc.data()).code) {
            qWarning("qheif: ICC profile can't be read");
            return;
        }
        QColorSpace colorSpace = QColorSpace::fromIccProfile(icc);
        if (!colorSpace.isValid()) {
            qWarning("qheif: ICC profile not supported by Qt");
        } else if (colorSpace.colorModel() == QColorSpace::ColorModel::Cmyk) {
            qWarning("qheif: CMYK ICC profile in a HEIF image, ignored");
            colorSpace = QColorSpace();
        } else if (colorSpace.colorModel() == QColorSpace::ColorModel::Gray) {
            if (hasAlpha) {
                // Qt has no grey format with alpha: an RGB colour space with the profile's white point
                // and transfer function.
                QPointF whitePoint = colorSpace.whitePoint();
                if (whitePoint.isNull()) {
                    whitePoint = QPointF(0.3127f, 0.329f);
                }
                QColorSpace::TransferFunction transfer = colorSpace.transferFunction();
                if (transfer == QColorSpace::TransferFunction::Custom) {
                    transfer = QColorSpace::TransferFunction::SRgb;
                }
                colorSpace = QColorSpace(whitePoint, QPointF(0.64f, 0.33f), QPointF(0.3f, 0.6f), QPointF(0.15f, 0.06f), transfer,
                                         colorSpace.gamma());
            } else {
                image.convertTo(bitDepth > 8 ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8);
            }
        }
        image.setColorSpace(colorSpace);
        return;
    }

    if (profileType != heif_color_profile_type_nclx) {
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        return;
    }

    heif_color_profile_nclx *nclx = nullptr;
    if (heif_image_handle_get_nclx_color_profile(handle, &nclx).code || !nclx) {
        qWarning("qheif: CICP colour information can't be read");
        return;
    }

    QColorSpace::TransferFunction transfer = QColorSpace::TransferFunction::SRgb;
    float gamma = 0.0f;
    switch (nclx->transfer_characteristics) {
    case 4:
        transfer = QColorSpace::TransferFunction::Gamma;
        gamma = 2.2f;
        break;
    case 5:
        transfer = QColorSpace::TransferFunction::Gamma;
        gamma = 2.8f;
        break;
    case 8:
        transfer = QColorSpace::TransferFunction::Linear;
        break;
    case 2:
    case 13:
        transfer = QColorSpace::TransferFunction::SRgb;
        break;
    case 16:
        transfer = QColorSpace::TransferFunction::St2084;
        break;
    case 18:
        transfer = QColorSpace::TransferFunction::Hlg;
        break;
    default:
        qWarning("qheif: CICP transfer characteristics %d not supported, shown as sRGB", int(nclx->transfer_characteristics));
        break;
    }

    switch (nclx->color_primaries) {
    case 1:
    case 2:
        image.setColorSpace(QColorSpace(QColorSpace::Primaries::SRgb, transfer, gamma));
        break;
    case 12:
        image.setColorSpace(QColorSpace(QColorSpace::Primaries::DciP3D65, transfer, gamma));
        break;
    default:
        image.setColorSpace(QColorSpace(QPointF(nclx->color_primary_white_x, nclx->color_primary_white_y),
                                        QPointF(nclx->color_primary_red_x, nclx->color_primary_red_y),
                                        QPointF(nclx->color_primary_green_x, nclx->color_primary_green_y),
                                        QPointF(nclx->color_primary_blue_x, nclx->color_primary_blue_y), transfer, gamma));
        break;
    }
    heif_nclx_color_profile_free(nclx);

    if (!image.colorSpace().isValid()) {
        qWarning("qheif: invalid colour space from CICP values");
    }
}

} // namespace

QHeifHandler::Kind QHeifHandler::kindOf(const QByteArray &header)
{
    // ISO BMFF: a box [size:4][type:4], first the "ftyp" box: [major brand:4][minor version:4]
    // [compatible brands:4 each].
    if (header.size() < 16) {
        return Kind::None;
    }
    const char *data = header.constData();
    if (std::memcmp(data + 4, "ftyp", 4) != 0) {
        return Kind::None;
    }

    const quint32 boxSize = brandAt(data, 0);
    const int end = int(qMin<quint32>(boxSize, quint32(header.size())));
    // The compatible brands; in the compact format (mif3, a "mini" box) the minor version is the
    // codec's brand.
    const auto hasCompatible = [&](quint32 brand) {
        for (int offset = 12; offset + 4 <= end; offset += 4) {
            if (brandAt(data, offset) == brand) {
                return true;
            }
        }
        return false;
    };

    switch (brandAt(data, 8)) {
    case fourcc("avif"):
    case fourcc("avis"):
        return Kind::Avif;
    case fourcc("heic"):
    case fourcc("heix"):
    case fourcc("heim"):
    case fourcc("heis"):
    case fourcc("hevc"):
    case fourcc("hevx"):
        return Kind::Heif;
    case fourcc("mif1"):
    case fourcc("mif2"):
    case fourcc("mif3"):
    case fourcc("msf1"):
        // A generic HEIF brand: the compatible brands tell the codec.
        return hasCompatible(fourcc("avif")) || hasCompatible(fourcc("avis")) ? Kind::Avif : Kind::Heif;
    default:
        return Kind::None;
    }
}

bool QHeifHandler::canDecode(Kind kind)
{
    static const auto available = [] {
        LibheifUse use;
        return std::pair { heif_have_decoder_for_format(heif_compression_HEVC) != 0,
                           heif_have_decoder_for_format(heif_compression_AV1) != 0 };
    }();

    switch (kind) {
    case Kind::Heif:
        return available.first;
    case Kind::Avif:
        return available.second;
    case Kind::None:
        break;
    }
    return false;
}

bool QHeifHandler::canRead() const
{
    if (m_state != State::NotDecoded) {
        return m_state == State::Decoded;
    }
    if (!device()) {
        return false;
    }

    switch (kindOf(device()->peek(headerSize))) {
    case Kind::Heif:
        setFormat("heif");
        return true;
    case Kind::Avif:
        setFormat("avif");
        return true;
    case Kind::None:
        break;
    }
    return false;
}

bool QHeifHandler::read(QImage *image)
{
    if (!ensureDecoded()) {
        return false;
    }
    *image = m_image;
    return true;
}

QVariant QHeifHandler::option(ImageOption option) const
{
    if (option == Size && const_cast<QHeifHandler *>(this)->ensureDecoded()) {
        return m_image.size();
    }
    return {};
}

bool QHeifHandler::supportsOption(ImageOption option) const
{
    // No ImageTransformation: libheif already applies the rotation and mirroring the file stores.
    return option == Size;
}

bool QHeifHandler::ensureDecoded()
{
    if (m_state != State::NotDecoded) {
        return m_state == State::Decoded;
    }
    m_state = State::Error;

    const QByteArray buffer = device()->readAll();
    if (kindOf(buffer.left(headerSize)) == Kind::None) {
        return false;
    }

    LibheifUse use;
    Decode decode;

    // iPhone photos can have more children per box than libheif's default limit allows.
    if (heif_security_limits *limits = heif_context_get_security_limits(decode.context)) {
        static constexpr uint32_t relaxedChildrenLimit = 360;
        if (limits->max_children_per_box > 0 && limits->max_children_per_box < relaxedChildrenLimit) {
            limits->max_children_per_box = relaxedChildrenLimit;
        }
    }

    heif_error error = heif_context_read_from_memory_without_copy(decode.context, buffer.constData(), size_t(buffer.size()), nullptr);
    if (error.code) {
        qWarning() << "qheif: can't read the file:" << error.message;
        return false;
    }

    // The primary image: the photo itself, not a thumbnail, a depth map, or another burst frame.
    error = heif_context_get_primary_image_handle(decode.context, &decode.handle);
    if (error.code) {
        qWarning() << "qheif: no primary image:" << error.message;
        return false;
    }

    const int handleWidth = heif_image_handle_get_width(decode.handle);
    const int handleHeight = heif_image_handle_get_height(decode.handle);
    static constexpr int dimensionLimit = 0xffff;
    if (handleWidth < 1 || handleHeight < 1 || handleWidth > dimensionLimit || handleHeight > dimensionLimit) {
        qWarning() << "qheif: invalid image size" << handleWidth << "x" << handleHeight;
        return false;
    }

    const int bitDepth = heif_image_handle_get_luma_bits_per_pixel(decode.handle);
    if (bitDepth != 8 && bitDepth != 10 && bitDepth != 12 && bitDepth != 16) {
        qWarning() << "qheif: unsupported bit depth" << bitDepth;
        return false;
    }
    const bool hasAlpha = heif_image_handle_has_alpha_channel(decode.handle);
    const bool littleEndian = QSysInfo::ByteOrder == QSysInfo::LittleEndian;

    heif_chroma chroma;
    QImage::Format format;
    if (bitDepth == 8) {
        chroma = hasAlpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB;
        format = hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    } else if (hasAlpha) {
        chroma = littleEndian ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBBAA_BE;
        format = QImage::Format_RGBA64;
    } else {
        chroma = littleEndian ? heif_chroma_interleaved_RRGGBB_LE : heif_chroma_interleaved_RRGGBB_BE;
        format = QImage::Format_RGBX64;
    }

    heif_decoding_options *options = heif_decoding_options_alloc();
    options->strict_decoding = 1;
    error = heif_decode_image(decode.handle, &decode.image, heif_colorspace_RGB, chroma, options);
    if (error.code == heif_error_Invalid_input && error.subcode == heif_suberror_Unknown_NCLX_matrix_coefficients && !decode.image) {
        // Some phones (Xiaomi) write invalid colour values: decode again, leniently.
        qWarning("qheif: invalid matrix coefficients, decoding without strict checks");
        options->strict_decoding = 0;
        error = heif_decode_image(decode.handle, &decode.image, heif_colorspace_RGB, chroma, options);
    }
    heif_decoding_options_free(options);
    if (error.code) {
        qWarning() << "qheif: can't decode the image:" << error.message;
        return false;
    }

    const int width = heif_image_get_width(decode.image, heif_channel_interleaved);
    const int height = heif_image_get_height(decode.image, heif_channel_interleaved);
    int stride = 0;
    const uint8_t *source = heif_image_get_plane_readonly(decode.image, heif_channel_interleaved, &stride);
    if (width < 1 || height < 1 || !source || stride <= 0) {
        qWarning("qheif: decoded image has no pixels");
        return false;
    }

    // Through allocateImage(), to respect QImageReader::allocationLimit().
    QImage image;
    if (!QImageIOHandler::allocateImage(QSize(width, height), format, &image)) {
        qWarning() << "qheif: can't allocate a" << width << "x" << height << "image";
        return false;
    }

    if (bitDepth == 8) {
        const size_t rowBytes = size_t(width) * (hasAlpha ? 4 : 3);
        for (int y = 0; y < height; ++y) {
            std::memcpy(image.scanLine(y), source + size_t(y) * size_t(stride), rowBytes);
        }
    } else {
        // 10, 12 or 16 bits per channel, in 16-bit words: scaled to 16 bits, alpha 0xffff if none.
        const int channels = hasAlpha ? 4 : 3;
        const quint32 maxValue = (1u << bitDepth) - 1;
        for (int y = 0; y < height; ++y) {
            const auto *from = reinterpret_cast<const quint16 *>(source + size_t(y) * size_t(stride));
            auto *to = reinterpret_cast<quint16 *>(image.scanLine(y));
            for (int x = 0; x < width; ++x) {
                for (int c = 0; c < channels; ++c) {
                    const quint32 value = from[c] & maxValue;
                    to[c] = bitDepth == 16 ? quint16(value) : quint16((value * 65535u + maxValue / 2) / maxValue);
                }
                if (!hasAlpha) {
                    to[3] = 0xffff;
                }
                from += channels;
                to += 4;
            }
        }
    }

    applyColorSpace(decode.handle, hasAlpha, bitDepth, image);

    m_image = image;
    m_state = State::Decoded;
    return true;
}

QImageIOPlugin::Capabilities QHeifPlugin::capabilities(QIODevice *device, const QByteArray &format) const
{
    if (format == "heic" || format == "heif" || format == "hif") {
        return QHeifHandler::canDecode(QHeifHandler::Kind::Heif) ? CanRead : Capabilities();
    }
    if (format == "avif") {
        return QHeifHandler::canDecode(QHeifHandler::Kind::Avif) ? CanRead : Capabilities();
    }
    if (!format.isEmpty() || !device || !device->isOpen() || !device->isReadable()) {
        return {};
    }

    const QHeifHandler::Kind kind = QHeifHandler::kindOf(device->peek(QHeifHandler::headerSize));
    return kind != QHeifHandler::Kind::None && QHeifHandler::canDecode(kind) ? CanRead : Capabilities();
}

QImageIOHandler *QHeifPlugin::create(QIODevice *device, const QByteArray &format) const
{
    auto *handler = new QHeifHandler;
    handler->setDevice(device);
    handler->setFormat(format);
    return handler;
}
