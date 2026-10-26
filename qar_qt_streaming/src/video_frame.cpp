// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/video_frame.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QImageReader>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace qar_qt_streaming
{

namespace
{

constexpr std::uint8_t kOpaqueAlpha = 255;

/// Only PNG and JPEG are decoded, and the first bytes of the payload decide which of the two
/// it is: the 8-byte PNG signature, or the 3 bytes with which every JPEG stream starts (the
/// start-of-image marker and the prefix of the next marker). The format string that comes with
/// the payload is used in messages only, never to pick a codec. The other formats that a Qt
/// plugin can read are refused, because for some of them the size in the header is not the
/// size that is allocated: a GIF allocates its logical screen, and an ICO file decodes an
/// embedded PNG at the size of that PNG. The cap below could not bound them.
constexpr std::uint8_t kPngSignature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr std::uint8_t kJpegSignature[] = {0xFF, 0xD8, 0xFF};

/// A compressed image of a few hundred bytes can declare any size, and the decoder
/// allocates up to 8 bytes per pixel for the declared size (4 for the usual 8-bit image)
/// and a second buffer follows for the conversion. decode_compressed_image therefore refuses
/// an image whose declared pixel count exceeds a cap before it decodes anything. Without a
/// size limit the cap is kMaxCompressedPixels. With one it is the area of the limit times
/// kCompressedAreaFactor, never below kMinCompressedPixels (so that an ordinary camera
/// frame is accepted for any panel) and never above kMaxCompressedPixels.
constexpr std::int64_t kMaxCompressedPixels = 64LL * 1000 * 1000;
constexpr std::int64_t kMinCompressedPixels = 16LL * 1000 * 1000;
constexpr std::int64_t kCompressedAreaFactor = 16;

bool
is_usable_limit(QSize max_size)
{
  return max_size.isValid() && max_size.width() > 0 && max_size.height() > 0;
}

/// The declared pixel count above which a compressed image is refused, for @p max_size.
std::int64_t
compressed_pixel_cap(QSize max_size)
{
  if (!is_usable_limit(max_size)) {
    return kMaxCompressedPixels;
  }
  const std::int64_t area = static_cast<std::int64_t>(max_size.width()) * max_size.height();
  return std::min(
    kMaxCompressedPixels, std::max(kMinCompressedPixels, area * kCompressedAreaFactor));
}

/// The name under which QImageReader knows the format of the compressed @p data, decided from
/// its first bytes alone: "png" or "jpeg", or nullptr for any other payload.
const char *
compressed_format_of(const std::uint8_t * data, std::size_t size)
{
  if (size >= sizeof(kPngSignature) &&
    std::memcmp(data, kPngSignature, sizeof(kPngSignature)) == 0)
  {
    return "png";
  }
  if (size >= sizeof(kJpegSignature) &&
    std::memcmp(data, kJpegSignature, sizeof(kJpegSignature)) == 0)
  {
    return "jpeg";
  }
  return nullptr;
}

/// Scale @p img down to fit inside @p max_size (aspect preserved, smooth); an
/// image that already fits, or a @p max_size that means "no limit", leaves it
/// unchanged. Never upscales.
QImage
clamp_to_max_size(const QImage & img, QSize max_size)
{
  if (!is_usable_limit(max_size)) {
    return img;
  }
  if (img.width() <= max_size.width() && img.height() <= max_size.height()) {
    return img;
  }
  return img.scaled(max_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

/// Force full opacity on a `Format_RGBA8888` image, in place.
void
force_opaque(QImage & img)
{
  for (int y = 0; y < img.height(); ++y) {
    std::uint8_t * row = img.scanLine(y);
    for (int x = 0; x < img.width(); ++x) {
      row[x * 4 + 3] = kOpaqueAlpha;
    }
  }
}

std::uint16_t
read_u16(const std::uint8_t * p, bool is_bigendian)
{
  return is_bigendian
    ? static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1])
    : static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[1]) << 8) | p[0]);
}

/// Bytes per pixel for every encoding this decoder accepts, or 0 if unknown.
std::size_t
bytes_per_pixel(const std::string & encoding)
{
  if (encoding == "rgb8" || encoding == "bgr8" || encoding == "8UC3") {return 3;}
  if (encoding == "rgba8" || encoding == "bgra8" || encoding == "8UC4") {return 4;}
  if (encoding == "mono8" || encoding == "8UC1") {return 1;}
  if (encoding == "mono16" || encoding == "16UC1") {return 2;}
  if (encoding == "32FC1") {return 4;}
  if (encoding == "yuv422" || encoding == "uyvy" || encoding == "yuyv" ||
    encoding == "yuv422_yuy2")
  {
    return 2;
  }
  return 0;
}

bool
validate_common(const RawImageView & in, std::size_t bpp, std::string * error)
{
  if (bpp == 0) {
    if (error) {*error = "unknown encoding: " + in.encoding;}
    return false;
  }
  if (in.width == 0 || in.height == 0) {
    if (error) {*error = "zero width or height";}
    return false;
  }
  const std::size_t min_step = static_cast<std::size_t>(in.width) * bpp;
  if (in.step < min_step) {
    if (error) {
      *error = "step (" + std::to_string(in.step) + ") is smaller than width * bytes_per_pixel (" +
        std::to_string(min_step) + ") for encoding " + in.encoding;
    }
    return false;
  }
  const std::size_t min_size = static_cast<std::size_t>(in.step) * in.height;
  if (in.size < min_size) {
    if (error) {
      *error = "buffer size (" + std::to_string(in.size) + ") is smaller than step * height (" +
        std::to_string(min_size) + ")";
    }
    return false;
  }
  if (in.data == nullptr) {
    if (error) {*error = "null image data";}
    return false;
  }
  return true;
}

std::uint8_t
clamp_byte(double v)
{
  return static_cast<std::uint8_t>(std::lround(std::min(255.0, std::max(0.0, v))));
}

/// ITU-R BT.601 YCbCr -> RGB, full range.
void
yuv_to_rgb(int y, int u, int v, std::uint8_t & r, std::uint8_t & g, std::uint8_t & b)
{
  const double yf = static_cast<double>(y);
  const double uf = static_cast<double>(u) - 128.0;
  const double vf = static_cast<double>(v) - 128.0;
  r = clamp_byte(yf + 1.402 * vf);
  g = clamp_byte(yf - 0.344136 * uf - 0.714136 * vf);
  b = clamp_byte(yf + 1.772 * uf);
}

void
write_gray(std::uint8_t * dst, std::uint8_t v)
{
  dst[0] = v;
  dst[1] = v;
  dst[2] = v;
  dst[3] = kOpaqueAlpha;
}

}  // namespace

QRect
aspect_fit_rect(QSize source, QRect bounds)
{
  if (source.isEmpty() || bounds.isEmpty()) {
    return QRect();
  }
  const QSize fitted = source.scaled(bounds.size(), Qt::KeepAspectRatio);
  QRect result(QPoint(0, 0), fitted);
  result.moveCenter(bounds.center());
  return result;
}

bool
decode_raw_image(const RawImageView & in, QSize max_size, QImage & out, std::string * error)
{
  const std::size_t bpp = bytes_per_pixel(in.encoding);
  if (!validate_common(in, bpp, error)) {
    return false;
  }

  const std::uint32_t w = in.width;
  const std::uint32_t h = in.height;

  QImage image(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGBA8888);
  if (image.isNull()) {
    if (error) {*error = "failed to allocate the decoded image";}
    return false;
  }

  if (in.encoding == "rgb8") {
    for (std::uint32_t y = 0; y < h; ++y) {
      const std::uint8_t * src = in.data + static_cast<std::size_t>(y) * in.step;
      std::uint8_t * dst = image.scanLine(static_cast<int>(y));
      for (std::uint32_t x = 0; x < w; ++x) {
        dst[x * 4 + 0] = src[x * 3 + 0];
        dst[x * 4 + 1] = src[x * 3 + 1];
        dst[x * 4 + 2] = src[x * 3 + 2];
        dst[x * 4 + 3] = kOpaqueAlpha;
      }
    }
  } else if (in.encoding == "bgr8" || in.encoding == "8UC3") {
    for (std::uint32_t y = 0; y < h; ++y) {
      const std::uint8_t * src = in.data + static_cast<std::size_t>(y) * in.step;
      std::uint8_t * dst = image.scanLine(static_cast<int>(y));
      for (std::uint32_t x = 0; x < w; ++x) {
        dst[x * 4 + 0] = src[x * 3 + 2];
        dst[x * 4 + 1] = src[x * 3 + 1];
        dst[x * 4 + 2] = src[x * 3 + 0];
        dst[x * 4 + 3] = kOpaqueAlpha;
      }
    }
  } else if (in.encoding == "rgba8") {
    for (std::uint32_t y = 0; y < h; ++y) {
      const std::uint8_t * src = in.data + static_cast<std::size_t>(y) * in.step;
      std::uint8_t * dst = image.scanLine(static_cast<int>(y));
      for (std::uint32_t x = 0; x < w; ++x) {
        dst[x * 4 + 0] = src[x * 4 + 0];
        dst[x * 4 + 1] = src[x * 4 + 1];
        dst[x * 4 + 2] = src[x * 4 + 2];
        dst[x * 4 + 3] = kOpaqueAlpha;  // opaque contract: source alpha is dropped
      }
    }
  } else if (in.encoding == "bgra8" || in.encoding == "8UC4") {
    for (std::uint32_t y = 0; y < h; ++y) {
      const std::uint8_t * src = in.data + static_cast<std::size_t>(y) * in.step;
      std::uint8_t * dst = image.scanLine(static_cast<int>(y));
      for (std::uint32_t x = 0; x < w; ++x) {
        dst[x * 4 + 0] = src[x * 4 + 2];
        dst[x * 4 + 1] = src[x * 4 + 1];
        dst[x * 4 + 2] = src[x * 4 + 0];
        dst[x * 4 + 3] = kOpaqueAlpha;
      }
    }
  } else if (in.encoding == "mono8" || in.encoding == "8UC1") {
    for (std::uint32_t y = 0; y < h; ++y) {
      const std::uint8_t * src = in.data + static_cast<std::size_t>(y) * in.step;
      std::uint8_t * dst = image.scanLine(static_cast<int>(y));
      for (std::uint32_t x = 0; x < w; ++x) {
        write_gray(dst + x * 4, src[x]);
      }
    }
  } else if (in.encoding == "mono16") {
    // Simple truncation to the high byte -- NOT a min/max normalisation; that
    // is what tells this apart from 16UC1 below.
    for (std::uint32_t y = 0; y < h; ++y) {
      const std::uint8_t * src = in.data + static_cast<std::size_t>(y) * in.step;
      std::uint8_t * dst = image.scanLine(static_cast<int>(y));
      for (std::uint32_t x = 0; x < w; ++x) {
        const std::uint16_t raw = read_u16(src + x * 2, in.is_bigendian);
        write_gray(dst + x * 4, static_cast<std::uint8_t>(raw >> 8));
      }
    }
  } else if (in.encoding == "16UC1") {
    // Depth-image style: normalise across the NON-ZERO values only, so the
    // (very common) "no return" zero does not wash out the real range.
    std::uint16_t min_v = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t max_v = 0;
    bool any_nonzero = false;
    for (std::uint32_t y = 0; y < h; ++y) {
      const std::uint8_t * src = in.data + static_cast<std::size_t>(y) * in.step;
      for (std::uint32_t x = 0; x < w; ++x) {
        const std::uint16_t raw = read_u16(src + x * 2, in.is_bigendian);
        if (raw != 0) {
          any_nonzero = true;
          min_v = std::min(min_v, raw);
          max_v = std::max(max_v, raw);
        }
      }
    }
    for (std::uint32_t y = 0; y < h; ++y) {
      const std::uint8_t * src = in.data + static_cast<std::size_t>(y) * in.step;
      std::uint8_t * dst = image.scanLine(static_cast<int>(y));
      for (std::uint32_t x = 0; x < w; ++x) {
        const std::uint16_t raw = read_u16(src + x * 2, in.is_bigendian);
        std::uint8_t v = 0;  // zero (and the all-zero frame) render black
        if (any_nonzero && raw != 0) {
          v = (max_v == min_v) ? std::uint8_t{255} : clamp_byte(
            static_cast<double>(raw - min_v) / static_cast<double>(max_v - min_v) * 255.0);
        }
        write_gray(dst + x * 4, v);
      }
    }
  } else if (in.encoding == "32FC1") {
    float min_v = std::numeric_limits<float>::max();
    float max_v = std::numeric_limits<float>::lowest();
    bool any_finite = false;
    for (std::uint32_t y = 0; y < h; ++y) {
      const std::uint8_t * src = in.data + static_cast<std::size_t>(y) * in.step;
      for (std::uint32_t x = 0; x < w; ++x) {
        float value = 0.0f;
        std::memcpy(&value, src + x * 4, sizeof(value));
        if (std::isfinite(value)) {
          any_finite = true;
          min_v = std::min(min_v, value);
          max_v = std::max(max_v, value);
        }
      }
    }
    for (std::uint32_t y = 0; y < h; ++y) {
      const std::uint8_t * src = in.data + static_cast<std::size_t>(y) * in.step;
      std::uint8_t * dst = image.scanLine(static_cast<int>(y));
      for (std::uint32_t x = 0; x < w; ++x) {
        float value = 0.0f;
        std::memcpy(&value, src + x * 4, sizeof(value));
        std::uint8_t v = 0;  // NaN/Inf, and the all-non-finite frame, render black
        if (any_finite && std::isfinite(value)) {
          v = (max_v == min_v) ? std::uint8_t{255} : clamp_byte(
            static_cast<double>(value - min_v) / static_cast<double>(max_v - min_v) * 255.0);
        }
        write_gray(dst + x * 4, v);
      }
    }
  } else if (
    in.encoding == "yuv422" || in.encoding == "uyvy" ||
    in.encoding == "yuyv" || in.encoding == "yuv422_yuy2")
  {
    // A macropixel is 4 bytes / 2 output pixels either way; only the byte
    // ORDER inside it differs. "yuv422" is the historical sensor_msgs name for
    // UYVY, kept alongside the explicit "uyvy" spelling; "yuv422_yuy2" is the
    // equally historical name for YUYV, alongside "yuyv".
    const bool uyvy_order = (in.encoding == "yuv422" || in.encoding == "uyvy");
    const std::uint32_t pair_count = w / 2;
    for (std::uint32_t y = 0; y < h; ++y) {
      const std::uint8_t * src = in.data + static_cast<std::size_t>(y) * in.step;
      std::uint8_t * dst = image.scanLine(static_cast<int>(y));
      for (std::uint32_t p = 0; p < pair_count; ++p) {
        const std::uint8_t * mp = src + static_cast<std::size_t>(p) * 4;
        int y0 = 0;
        int y1 = 0;
        int u = 0;
        int v = 0;
        if (uyvy_order) {
          u = mp[0]; y0 = mp[1]; v = mp[2]; y1 = mp[3];
        } else {
          y0 = mp[0]; u = mp[1]; y1 = mp[2]; v = mp[3];
        }
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        yuv_to_rgb(y0, u, v, r, g, b);
        std::uint8_t * out0 = dst + (p * 2) * 4;
        out0[0] = r; out0[1] = g; out0[2] = b; out0[3] = kOpaqueAlpha;
        yuv_to_rgb(y1, u, v, r, g, b);
        std::uint8_t * out1 = dst + (p * 2 + 1) * 4;
        out1[0] = r; out1[1] = g; out1[2] = b; out1[3] = kOpaqueAlpha;
      }
      if (w % 2 == 1) {
        // An odd width leaves one column outside any whole macropixel -- there
        // is no well-defined sample for it, so it is painted black rather than
        // reading past what @ref validate_common guaranteed is there.
        write_gray(dst + (w - 1) * 4, 0);
      }
    }
  } else {
    if (error) {*error = "unknown encoding: " + in.encoding;}
    return false;
  }

  out = clamp_to_max_size(image, max_size);
  if (error) {error->clear();}
  return true;
}

bool
decode_compressed_image(
  const std::string & format,
  const std::uint8_t * data,
  std::size_t size,
  QSize max_size,
  QImage & out,
  std::string * error)
{
  if (data == nullptr || size == 0) {
    if (error) {*error = "no compressed image data";}
    return false;
  }

  // Qt takes the length as an int: a larger payload would arrive truncated or negative.
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    if (error) {
      *error = "compressed image data is too large (" + std::to_string(size) + " bytes)";
    }
    return false;
  }

  const std::string hint = format.empty() ? std::string("unknown") : format;

  // The first bytes decide the format, whatever Qt plugins are installed and whatever the
  // hint says; every other payload is refused before a reader is made.
  const char * const kind = compressed_format_of(data, size);
  if (kind == nullptr) {
    if (error) {
      *error = "unsupported compressed image format: only PNG and JPEG are decoded "
        "(format hint: " + hint + ")";
    }
    return false;
  }

  const std::string failure = "failed to decode compressed image (format hint: " + hint + ")";
  QByteArray bytes = QByteArray::fromRawData(
    reinterpret_cast<const char *>(data), static_cast<int>(size));
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::ReadOnly)) {
    if (error) {*error = failure;}
    return false;
  }
  // The reader makes the handler of the format it is given and falls back to no other.
  QImageReader reader(&buffer, QByteArray(kind));
  reader.setAutoDetectImageFormat(false);

  // The declared size comes from the header, before a single pixel is decoded.
  const QSize declared = reader.size();
  if (!declared.isValid() || declared.isEmpty()) {
    if (error) {*error = failure;}
    return false;
  }
  const std::int64_t pixels = static_cast<std::int64_t>(declared.width()) * declared.height();
  const std::int64_t cap = compressed_pixel_cap(max_size);
  if (pixels > cap) {
    if (error) {
      *error = "compressed image declares " + std::to_string(declared.width()) + "x" +
        std::to_string(declared.height()) + " pixels, more than the limit of " +
        std::to_string(cap) + " pixels";
    }
    return false;
  }
  // An image that is larger than the limit is scaled by the reader, so that the
  // full-size RGBA copy below is never made.
  if (is_usable_limit(max_size) &&
    (declared.width() > max_size.width() || declared.height() > max_size.height()))
  {
    const QSize target = declared.scaled(max_size, Qt::KeepAspectRatio);
    if (!target.isEmpty()) {
      reader.setScaledSize(target);
    }
  }

  const QImage loaded = reader.read();
  if (loaded.isNull()) {
    if (error) {*error = failure;}
    return false;
  }

  QImage rgba = loaded.convertToFormat(QImage::Format_RGBA8888);
  if (rgba.isNull()) {
    if (error) {*error = "failed to convert the decoded image to RGBA8888";}
    return false;
  }
  force_opaque(rgba);
  out = clamp_to_max_size(rgba, max_size);
  if (error) {error->clear();}
  return true;
}

}  // namespace qar_qt_streaming
