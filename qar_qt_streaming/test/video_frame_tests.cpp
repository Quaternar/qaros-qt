// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// video_frame.hpp against tiny, hand-built buffers: every encoding decodes to
// an exact expected RGBA (or, for the lossy YUV/JPEG paths, a value close
// enough to prove the layout was read correctly), every rejection path names
// what was wrong, and the downscale/aspect-fit contracts are pinned.

#include <gtest/gtest.h>

#include "qar_qt_streaming/video_frame.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageWriter>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using qar_qt_streaming::aspect_fit_rect;
using qar_qt_streaming::decode_compressed_image;
using qar_qt_streaming::decode_raw_image;
using qar_qt_streaming::RawImageView;

namespace
{

/// Owns the byte buffer a RawImageView borrows, so the view stays valid for
/// the lifetime of the test.
struct Buffer
{
  std::vector<std::uint8_t> bytes;

  RawImageView view(
    std::uint32_t width, std::uint32_t height, std::uint32_t step, std::string encoding,
    bool is_bigendian = false) const
  {
    RawImageView v;
    v.width = width;
    v.height = height;
    v.step = step;
    v.encoding = std::move(encoding);
    v.is_bigendian = is_bigendian;
    v.data = bytes.data();
    v.size = bytes.size();
    return v;
  }
};

void
expect_pixel(
  const QImage & img, int x, int y, int r, int g, int b, int a = 255, const char * note = "")
{
  const QColor c = img.pixelColor(x, y);
  EXPECT_EQ(c.red(), r) << "red at (" << x << ", " << y << ") " << note;
  EXPECT_EQ(c.green(), g) << "green at (" << x << ", " << y << ") " << note;
  EXPECT_EQ(c.blue(), b) << "blue at (" << x << ", " << y << ") " << note;
  EXPECT_EQ(c.alpha(), a) << "alpha at (" << x << ", " << y << ") " << note;
}

void
append_u16(std::vector<std::uint8_t> & out, std::uint16_t value, bool is_bigendian)
{
  const std::uint8_t hi = static_cast<std::uint8_t>(value >> 8);
  const std::uint8_t lo = static_cast<std::uint8_t>(value & 0xFF);
  if (is_bigendian) {
    out.push_back(hi);
    out.push_back(lo);
  } else {
    out.push_back(lo);
    out.push_back(hi);
  }
}

void
append_f32(std::vector<std::uint8_t> & out, float value)
{
  std::uint8_t raw[4];
  std::memcpy(raw, &value, sizeof(raw));
  out.insert(out.end(), raw, raw + 4);
}

}  // namespace

// ---------------------------------------------------------------------------
// 8-bit colour encodings
// ---------------------------------------------------------------------------

TEST(RawImageDecode, Rgb8ExactPixels)
{
  Buffer buf;
  buf.bytes = {
    255, 0, 0, /**/ 0, 255, 0,
    0, 0, 255, /**/ 255, 255, 255,
  };
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 2, 6, "rgb8"), QSize(), out, &error)) << error;
  ASSERT_EQ(out.format(), QImage::Format_RGBA8888);
  expect_pixel(out, 0, 0, 255, 0, 0);
  expect_pixel(out, 1, 0, 0, 255, 0);
  expect_pixel(out, 0, 1, 0, 0, 255);
  expect_pixel(out, 1, 1, 255, 255, 255);
}

TEST(RawImageDecode, Bgr8ExactPixels)
{
  Buffer buf;
  // Same four colours as the rgb8 test, byte-swapped into B, G, R order.
  buf.bytes = {
    0, 0, 255, /**/ 0, 255, 0,
    255, 0, 0, /**/ 255, 255, 255,
  };
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 2, 6, "bgr8"), QSize(), out, &error)) << error;
  expect_pixel(out, 0, 0, 255, 0, 0);
  expect_pixel(out, 1, 0, 0, 255, 0);
  expect_pixel(out, 0, 1, 0, 0, 255);
  expect_pixel(out, 1, 1, 255, 255, 255);
}

TEST(RawImageDecode, Mono8ExactPixels)
{
  Buffer buf;
  buf.bytes = {0, 64, 128, 255};
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 2, 2, "mono8"), QSize(), out, &error)) << error;
  expect_pixel(out, 0, 0, 0, 0, 0);
  expect_pixel(out, 1, 0, 64, 64, 64);
  expect_pixel(out, 0, 1, 128, 128, 128);
  expect_pixel(out, 1, 1, 255, 255, 255);
}

TEST(RawImageDecode, Uc1IsAnAliasForMono8)
{
  Buffer buf;
  buf.bytes = {10, 200};
  QImage mono8;
  QImage uc1;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 2, "mono8"), QSize(), mono8, &error)) << error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 2, "8UC1"), QSize(), uc1, &error)) << error;
  EXPECT_EQ(mono8, uc1);
}

TEST(RawImageDecode, Uc3IsAnAliasForBgr8)
{
  Buffer buf;
  buf.bytes = {1, 2, 3, 4, 5, 6};
  QImage bgr8;
  QImage uc3;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 6, "bgr8"), QSize(), bgr8, &error)) << error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 6, "8UC3"), QSize(), uc3, &error)) << error;
  EXPECT_EQ(bgr8, uc3);
}

TEST(RawImageDecode, Rgba8ForcesOpaqueRegardlessOfSourceAlpha)
{
  Buffer buf;
  buf.bytes = {
    255, 0, 0, 0, /**/ 0, 255, 0, 128,
  };
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 8, "rgba8"), QSize(), out, &error)) << error;
  expect_pixel(out, 0, 0, 255, 0, 0, 255);
  expect_pixel(out, 1, 0, 0, 255, 0, 255);
}

TEST(RawImageDecode, Bgra8ExactPixelsAndOpaque)
{
  Buffer buf;
  // B, G, R, A -- decodes to red then green, alpha forced regardless of input.
  buf.bytes = {
    0, 0, 255, 5, /**/ 0, 255, 0, 250,
  };
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 8, "bgra8"), QSize(), out, &error)) << error;
  expect_pixel(out, 0, 0, 255, 0, 0, 255);
  expect_pixel(out, 1, 0, 0, 255, 0, 255);
}

TEST(RawImageDecode, Uc4IsAnAliasForBgra8)
{
  Buffer buf;
  buf.bytes = {9, 8, 7, 6, 5, 4, 3, 2};
  QImage bgra8;
  QImage uc4;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 8, "bgra8"), QSize(), bgra8, &error)) << error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 8, "8UC4"), QSize(), uc4, &error)) << error;
  EXPECT_EQ(bgra8, uc4);
}

TEST(RawImageDecode, StepPaddingIsHonoured)
{
  Buffer buf;
  // rgb8, width 2 (6 real bytes/row) but an 8-byte step: 2 padding bytes per
  // row, deliberately given a value ("0xEE") that would corrupt the next
  // pixel if step were ignored.
  buf.bytes = {
    255, 0, 0, 0, 255, 0, 0xEE, 0xEE,
    0, 0, 255, 255, 255, 255, 0xEE, 0xEE,
  };
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 2, 8, "rgb8"), QSize(), out, &error)) << error;
  expect_pixel(out, 0, 0, 255, 0, 0);
  expect_pixel(out, 1, 0, 0, 255, 0);
  expect_pixel(out, 0, 1, 0, 0, 255);
  expect_pixel(out, 1, 1, 255, 255, 255);
}

// ---------------------------------------------------------------------------
// 16-bit encodings
// ---------------------------------------------------------------------------

TEST(RawImageDecode, Mono16LittleEndianShiftsHighByte)
{
  Buffer buf;
  append_u16(buf.bytes, 0x1000, false);
  append_u16(buf.bytes, 0xFF00, false);
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 4, "mono16", false), QSize(), out, &error)) << error;
  expect_pixel(out, 0, 0, 0x10, 0x10, 0x10);
  expect_pixel(out, 1, 0, 0xFF, 0xFF, 0xFF);
}

TEST(RawImageDecode, Mono16BigEndianShiftsHighByte)
{
  Buffer buf;
  append_u16(buf.bytes, 0x1000, true);
  append_u16(buf.bytes, 0xFF00, true);
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 4, "mono16", true), QSize(), out, &error)) << error;
  expect_pixel(out, 0, 0, 0x10, 0x10, 0x10);
  expect_pixel(out, 1, 0, 0xFF, 0xFF, 0xFF);
}

TEST(RawImageDecode, Uc16NormalisesOverTheNonZeroRange)
{
  Buffer buf;
  // 0 (no return), 100 (the non-zero min -> 0), 250 (mid), 400 (the max -> 255).
  for (const std::uint16_t v : {0, 100, 250, 400}) {
    append_u16(buf.bytes, v, false);
  }
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(4, 1, 8, "16UC1"), QSize(), out, &error)) << error;
  expect_pixel(out, 0, 0, 0, 0, 0, 255, "zero is always black");
  expect_pixel(out, 1, 0, 0, 0, 0, 255, "the non-zero minimum normalises to 0 too");
  expect_pixel(out, 2, 0, 128, 128, 128);
  expect_pixel(out, 3, 0, 255, 255, 255);
}

TEST(RawImageDecode, Uc16AllZeroFrameIsBlack)
{
  Buffer buf;
  for (int i = 0; i < 4; ++i) {
    append_u16(buf.bytes, 0, false);
  }
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 2, 4, "16UC1"), QSize(), out, &error)) << error;
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 2; ++x) {
      expect_pixel(out, x, y, 0, 0, 0);
    }
  }
}

TEST(RawImageDecode, Fc32NormalisesOverTheFiniteRange)
{
  Buffer buf;
  append_f32(buf.bytes, std::numeric_limits<float>::quiet_NaN());
  append_f32(buf.bytes, -1.0f);
  append_f32(buf.bytes, 1.0f);
  append_f32(buf.bytes, std::numeric_limits<float>::infinity());
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(4, 1, 16, "32FC1"), QSize(), out, &error)) << error;
  expect_pixel(out, 0, 0, 0, 0, 0, 255, "NaN is black");
  expect_pixel(out, 1, 0, 0, 0, 0, 255, "the finite minimum normalises to 0");
  expect_pixel(out, 2, 0, 255, 255, 255, 255, "the finite maximum normalises to 255");
  expect_pixel(out, 3, 0, 0, 0, 0, 255, "+Inf is black");
}

TEST(RawImageDecode, Fc32AllNonFiniteFrameIsBlack)
{
  Buffer buf;
  append_f32(buf.bytes, std::numeric_limits<float>::quiet_NaN());
  append_f32(buf.bytes, -std::numeric_limits<float>::infinity());
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 8, "32FC1"), QSize(), out, &error)) << error;
  expect_pixel(out, 0, 0, 0, 0, 0);
  expect_pixel(out, 1, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// YUV 4:2:2 -- both spellings of each byte layout.
//
// Chroma is held neutral (U = V = 128) so the expected RGB is exactly the Y
// value with no rounding in the BT.601 matrix to work through by hand; this
// still pins the byte ORDER, which is the only thing that differs between the
// two layouts / four encoding names.
// ---------------------------------------------------------------------------

TEST(RawImageDecode, UyvyLayoutBothEncodingNames)
{
  Buffer buf;
  buf.bytes = {128, 200, 128, 50};  // U, Y0, V, Y1
  QImage out;
  std::string error;
  for (const char * encoding : {"yuv422", "uyvy"}) {
    ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 4, encoding), QSize(), out, &error))
      << encoding << ": " << error;
    expect_pixel(out, 0, 0, 200, 200, 200);
    expect_pixel(out, 1, 0, 50, 50, 50);
  }
}

TEST(RawImageDecode, YuyvLayoutBothEncodingNames)
{
  Buffer buf;
  buf.bytes = {200, 128, 50, 128};  // Y0, U, Y1, V
  QImage out;
  std::string error;
  for (const char * encoding : {"yuyv", "yuv422_yuy2"}) {
    ASSERT_TRUE(decode_raw_image(buf.view(2, 1, 4, encoding), QSize(), out, &error))
      << encoding << ": " << error;
    expect_pixel(out, 0, 0, 200, 200, 200);
    expect_pixel(out, 1, 0, 50, 50, 50);
  }
}

TEST(RawImageDecode, YuvOddWidthTrailingColumnIsBlack)
{
  Buffer buf;
  buf.bytes = {128, 200, 128, 50, /* trailing odd column, no full macropixel */ 77, 77};
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(3, 1, 6, "uyvy"), QSize(), out, &error)) << error;
  expect_pixel(out, 0, 0, 200, 200, 200);
  expect_pixel(out, 1, 0, 50, 50, 50);
  expect_pixel(out, 2, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Rejections
// ---------------------------------------------------------------------------

TEST(RawImageDecode, RejectsUnknownEncoding)
{
  Buffer buf;
  buf.bytes = {0, 0, 0, 0};
  QImage out;
  std::string error;
  EXPECT_FALSE(decode_raw_image(buf.view(2, 2, 2, "made_up_encoding"), QSize(), out, &error));
  EXPECT_NE(error.find("made_up_encoding"), std::string::npos) << error;
}

TEST(RawImageDecode, RejectsZeroWidthOrHeight)
{
  Buffer buf;
  buf.bytes = {0, 0, 0, 0};
  QImage out;
  std::string error;
  EXPECT_FALSE(decode_raw_image(buf.view(0, 2, 2, "mono8"), QSize(), out, &error));
  EXPECT_FALSE(error.empty());
  error.clear();
  EXPECT_FALSE(decode_raw_image(buf.view(2, 0, 2, "mono8"), QSize(), out, &error));
  EXPECT_FALSE(error.empty());
}

TEST(RawImageDecode, RejectsStepSmallerThanWidthTimesBytesPerPixel)
{
  Buffer buf;
  buf.bytes = {0, 0, 0};
  QImage out;
  std::string error;
  EXPECT_FALSE(decode_raw_image(buf.view(2, 1, 1, "mono8"), QSize(), out, &error));
  EXPECT_FALSE(error.empty());
}

TEST(RawImageDecode, RejectsBufferSizeSmallerThanStepTimesHeight)
{
  Buffer buf;
  buf.bytes = {1, 2, 3};  // one row's worth, claiming two rows
  QImage out;
  std::string error;
  EXPECT_FALSE(decode_raw_image(buf.view(3, 2, 3, "mono8"), QSize(), out, &error));
  EXPECT_FALSE(error.empty());
}

TEST(RawImageDecode, RejectsNullData)
{
  RawImageView v;
  v.width = 2;
  v.height = 2;
  v.step = 2;
  v.encoding = "mono8";
  v.data = nullptr;
  // Large enough that step/size validation passes and the null check is the
  // one that actually fires.
  v.size = 4;
  QImage out;
  std::string error;
  EXPECT_FALSE(decode_raw_image(v, QSize(), out, &error));
  EXPECT_FALSE(error.empty());
}

// ---------------------------------------------------------------------------
// Downscale contract
// ---------------------------------------------------------------------------

TEST(RawImageDecode, DownscalesPreservingAspectRatio)
{
  Buffer buf;
  buf.bytes.assign(static_cast<std::size_t>(4000) * 1000, 128);
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(4000, 1000, 4000, "mono8"), QSize(1280, 720), out, &error))
    << error;
  EXPECT_EQ(out.width(), 1280);
  EXPECT_EQ(out.height(), 320);
}

TEST(RawImageDecode, SmallImageWithBiggerMaxStaysUnchanged)
{
  Buffer buf;
  buf.bytes = {1, 2, 3, 4, 5, 6, 7, 8};
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(4, 2, 4, "mono8"), QSize(1000, 1000), out, &error)) << error;
  EXPECT_EQ(out.width(), 4);
  EXPECT_EQ(out.height(), 2);
}

TEST(RawImageDecode, EmptyMaxSizeMeansNoLimit)
{
  Buffer buf;
  buf.bytes.assign(static_cast<std::size_t>(2000) * 10, 64);
  QImage out;
  std::string error;
  ASSERT_TRUE(decode_raw_image(buf.view(2000, 10, 2000, "mono8"), QSize(), out, &error)) << error;
  EXPECT_EQ(out.width(), 2000);
  EXPECT_EQ(out.height(), 10);
}

// ---------------------------------------------------------------------------
// Compressed images
// ---------------------------------------------------------------------------

namespace
{

QByteArray
encode(const QImage & img, const char * format)
{
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  const bool saved = img.save(&buffer, format);
  buffer.close();
  return saved ? bytes : QByteArray();
}

std::uint32_t
crc32_of(const QByteArray & bytes)
{
  std::uint32_t crc = 0xFFFFFFFFu;
  for (const char c : bytes) {
    crc ^= static_cast<std::uint8_t>(c);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
  }
  return ~crc;
}

void
append_u32(QByteArray & out, std::uint32_t value)
{
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.append(static_cast<char>((value >> shift) & 0xFFu));
  }
}

/// The signature and the IHDR chunk of an 8-bit RGBA PNG that declares @p width x @p height,
/// then the header of an IDAT chunk with nothing valid behind it: Qt can read the declared
/// size from this much, and there is no picture to decode.
QByteArray
crafted_png_header(std::uint32_t width, std::uint32_t height)
{
  QByteArray png("\x89PNG\r\n\x1a\n", 8);
  QByteArray ihdr("IHDR", 4);
  append_u32(ihdr, width);
  append_u32(ihdr, height);
  ihdr.append(static_cast<char>(8));  // bit depth
  ihdr.append(static_cast<char>(6));  // colour type: RGBA
  ihdr.append(QByteArray(3, '\0'));   // compression, filter, interlace
  append_u32(png, 13);
  png.append(ihdr);
  append_u32(png, crc32_of(ihdr));
  append_u32(png, 16);
  png.append("IDAT", 4);
  png.append(QByteArray(16, '\0'));
  return png;
}

}  // namespace

TEST(CompressedImageDecode, PngRoundTripsExactlyAndForcesOpaque)
{
  QImage source(3, 2, QImage::Format_RGBA8888);
  source.setPixelColor(0, 0, QColor(255, 0, 0, 40));  // translucent on purpose
  source.setPixelColor(1, 0, QColor(0, 255, 0, 255));
  source.setPixelColor(2, 0, QColor(0, 0, 255, 255));
  source.setPixelColor(0, 1, QColor(255, 255, 0, 255));
  source.setPixelColor(1, 1, QColor(0, 255, 255, 255));
  source.setPixelColor(2, 1, QColor(255, 255, 255, 255));

  const QByteArray png = encode(source, "PNG");
  ASSERT_FALSE(png.isEmpty());

  QImage out;
  std::string error;
  ASSERT_TRUE(
    decode_compressed_image(
      "png", reinterpret_cast<const std::uint8_t *>(png.constData()),
      static_cast<std::size_t>(png.size()), QSize(), out, &error))
    << error;
  ASSERT_EQ(out.size(), source.size());
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      const QColor expected = source.pixelColor(x, y);
      const QColor actual = out.pixelColor(x, y);
      EXPECT_EQ(actual.red(), expected.red()) << x << "," << y;
      EXPECT_EQ(actual.green(), expected.green()) << x << "," << y;
      EXPECT_EQ(actual.blue(), expected.blue()) << x << "," << y;
      EXPECT_EQ(actual.alpha(), 255) << "must be forced opaque at " << x << "," << y;
    }
  }
}

TEST(CompressedImageDecode, JpegApproximatesWhenSupported)
{
  if (!QImageWriter::supportedImageFormats().contains("jpeg")) {
    GTEST_SKIP() << "no JPEG encoder available on this host";
  }
  QImage source(32, 32, QImage::Format_RGBA8888);
  source.fill(QColor(200, 40, 40));
  const QByteArray jpeg = encode(source, "JPEG");
  ASSERT_FALSE(jpeg.isEmpty());

  QImage out;
  std::string error;
  ASSERT_TRUE(
    decode_compressed_image(
      "jpeg", reinterpret_cast<const std::uint8_t *>(jpeg.constData()),
      static_cast<std::size_t>(jpeg.size()), QSize(), out, &error))
    << error;
  const QColor c = out.pixelColor(16, 16);
  EXPECT_NEAR(c.red(), 200, 20);
  EXPECT_NEAR(c.green(), 40, 20);
  EXPECT_NEAR(c.blue(), 40, 20);
  EXPECT_EQ(c.alpha(), 255);
}

TEST(CompressedImageDecode, RejectsGarbageBytes)
{
  const std::vector<std::uint8_t> junk = {1, 2, 3, 4, 5, 6, 7, 8};
  QImage out;
  std::string error;
  EXPECT_FALSE(decode_compressed_image("png", junk.data(), junk.size(), QSize(), out, &error));
  EXPECT_FALSE(error.empty());

  // A header that declares 60000 x 60000 pixels (about 14 GB as RGBA) is refused before
  // anything is decoded, with a size limit and without one.
  const QByteArray huge = crafted_png_header(60000, 60000);
  const auto * huge_data = reinterpret_cast<const std::uint8_t *>(huge.constData());
  for (const QSize limit : {QSize(), QSize(1920, 1080)}) {
    error.clear();
    EXPECT_FALSE(
      decode_compressed_image(
        "png", huge_data, static_cast<std::size_t>(huge.size()), limit, out, &error));
    EXPECT_NE(error.find("60000x60000"), std::string::npos) << error;
    EXPECT_NE(error.find("more than the limit"), std::string::npos) << error;
  }

  // The cap follows the size limit. 17 million pixels are over the cap of a small limit
  // (16 million at least), and under the 64 million that apply without a limit, where the
  // header passes and the missing picture is what fails.
  const QByteArray wide = crafted_png_header(17000, 1000);
  const auto * wide_data = reinterpret_cast<const std::uint8_t *>(wide.constData());
  error.clear();
  EXPECT_FALSE(
    decode_compressed_image(
      "png", wide_data, static_cast<std::size_t>(wide.size()), QSize(100, 100), out, &error));
  EXPECT_NE(error.find("more than the limit"), std::string::npos) << error;
  error.clear();
  EXPECT_FALSE(
    decode_compressed_image(
      "png", wide_data, static_cast<std::size_t>(wide.size()), QSize(), out, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(error.find("more than the limit"), std::string::npos) << error;

  // A payload length that does not fit Qt's int is refused before a byte of it is read: the
  // pointer is passed on and never dereferenced.
  if constexpr (sizeof(std::size_t) > sizeof(int)) {
    error.clear();
    EXPECT_FALSE(
      decode_compressed_image(
        "png", junk.data(), static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1,
        QSize(), out, &error));
    EXPECT_NE(error.find("too large"), std::string::npos) << error;
  }

  // Only PNG and JPEG are decoded, and the first bytes decide, so every other format that Qt
  // can read is refused with one message, with a size limit and without one, and no image
  // plugin is needed to see it. The GIF has a logical screen of 16383 x 16383 pixels and one
  // 1 x 1 frame (its header reports the frame); the ICO file wraps a PNG that declares
  // 60000 x 60000 pixels; the last three payloads are shorter than a signature. The format
  // hint says png for all of them, which changes nothing.
  const QByteArray embedded_png = crafted_png_header(60000, 60000);
  QByteArray ico = QByteArray::fromHex("000001000100" "0000000001002000");
  for (int shift = 0; shift < 32; shift += 8) {
    ico.append(static_cast<char>((embedded_png.size() >> shift) & 0xFF));
  }
  ico.append(QByteArray::fromHex("16000000"));  // the image starts at offset 22
  ico.append(embedded_png);
  struct Crafted
  {
    const char * name;
    QByteArray bytes;
  };
  const std::vector<Crafted> refused = {
    {"GIF", QByteArray("GIF89a") + QByteArray::fromHex(
        "ff3fff3f000000" "2c000000000100010000" "0202440100" "3b")},
    {"ICO", ico},
    {"BMP", QByteArray("BM") + QByteArray::fromHex(
        "3a0000000000000036000000" "28000000" "01000000" "01000000" "0100" "1800"
        "00000000" "04000000" "00000000" "00000000" "00000000" "00000000" "0000ff00")},
    {"TIFF", QByteArray::fromHex("49492a00" "08000000" "0000" "00000000")},
    {"WebP", QByteArray("RIFF") + QByteArray::fromHex("04000000") + QByteArray("WEBP")},
    {"seven bytes of a PNG signature", QByteArray::fromHex("89504e470d0a1a")},
    {"two bytes of a JPEG signature", QByteArray::fromHex("ffd8")},
    {"one byte", QByteArray::fromHex("ff")},
  };
  for (const Crafted & item : refused) {
    const auto * data = reinterpret_cast<const std::uint8_t *>(item.bytes.constData());
    for (const QSize limit : {QSize(), QSize(1920, 1080)}) {
      error.clear();
      EXPECT_FALSE(
        decode_compressed_image(
          "png", data, static_cast<std::size_t>(item.bytes.size()), limit, out, &error))
        << item.name;
      EXPECT_NE(error.find("only PNG and JPEG"), std::string::npos) << item.name << ": " << error;
    }
  }

  // A JPEG signature passes the format check, and the junk behind it is an ordinary decode
  // failure, not a refusal of the format.
  const QByteArray bad_jpeg = QByteArray::fromHex("ffd8ffe0") + QByteArray(64, '\0');
  const auto * bad_jpeg_data = reinterpret_cast<const std::uint8_t *>(bad_jpeg.constData());
  for (const QSize limit : {QSize(), QSize(1920, 1080)}) {
    error.clear();
    EXPECT_FALSE(
      decode_compressed_image(
        "jpeg", bad_jpeg_data, static_cast<std::size_t>(bad_jpeg.size()), limit, out, &error));
    EXPECT_NE(error.find("failed to decode compressed image"), std::string::npos) << error;
    EXPECT_EQ(error.find("only PNG and JPEG"), std::string::npos) << error;
  }
}

TEST(CompressedImageDecode, RejectsEmptyData)
{
  QImage out;
  std::string error;
  EXPECT_FALSE(decode_compressed_image("png", nullptr, 0, QSize(), out, &error));
  EXPECT_FALSE(error.empty());
}

TEST(CompressedImageDecode, DownscalesLikeRawDecode)
{
  QImage source(800, 200, QImage::Format_RGBA8888);
  source.fill(Qt::darkGray);
  const QByteArray png = encode(source, "PNG");
  ASSERT_FALSE(png.isEmpty());

  QImage out;
  std::string error;
  ASSERT_TRUE(
    decode_compressed_image(
      "png", reinterpret_cast<const std::uint8_t *>(png.constData()),
      static_cast<std::size_t>(png.size()), QSize(200, 200), out, &error))
    << error;
  EXPECT_EQ(out.width(), 200);
  EXPECT_EQ(out.height(), 50);
}

// ---------------------------------------------------------------------------
// aspect_fit_rect
// ---------------------------------------------------------------------------

TEST(AspectFitRect, WiderSourceLetterboxesTopAndBottom)
{
  const QRect bounds(0, 0, 200, 200);
  const QRect result = aspect_fit_rect(QSize(400, 100), bounds);
  EXPECT_EQ(result.width(), 200);
  EXPECT_EQ(result.height(), 50);
  EXPECT_EQ(result.center(), bounds.center());
}

TEST(AspectFitRect, TallerSourceLetterboxesLeftAndRight)
{
  const QRect bounds(0, 0, 200, 200);
  const QRect result = aspect_fit_rect(QSize(100, 400), bounds);
  EXPECT_EQ(result.width(), 50);
  EXPECT_EQ(result.height(), 200);
  EXPECT_EQ(result.center(), bounds.center());
}

TEST(AspectFitRect, EqualAspectFillsBoundsExactly)
{
  const QRect bounds(10, 20, 50, 50);
  const QRect result = aspect_fit_rect(QSize(100, 100), bounds);
  EXPECT_EQ(result.width(), 50);
  EXPECT_EQ(result.height(), 50);
  EXPECT_EQ(result.center(), bounds.center());
}

TEST(AspectFitRect, EmptySourceYieldsEmptyRect)
{
  EXPECT_TRUE(aspect_fit_rect(QSize(), QRect(0, 0, 100, 100)).isEmpty());
  EXPECT_TRUE(aspect_fit_rect(QSize(0, 50), QRect(0, 0, 100, 100)).isEmpty());
}

TEST(AspectFitRect, EmptyBoundsYieldsEmptyRect)
{
  EXPECT_TRUE(aspect_fit_rect(QSize(100, 50), QRect()).isEmpty());
}
