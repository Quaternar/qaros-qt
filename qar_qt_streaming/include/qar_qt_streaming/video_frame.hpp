// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__VIDEO_FRAME_HPP_
#define QAR_QT_STREAMING__VIDEO_FRAME_HPP_

/// @file video_frame.hpp
/// @brief ROS-free decoding of a camera frame into a displayable QImage.
///
/// This is the core both video-panel variants share: (a) a stand-alone AR
/// panel for a video stream, and (b) substituting a live video frame for the
/// uncapturable native window of a widget (see qt_widget_capture.hpp). Both
/// need the SAME decode step, and neither is allowed to depend on ROS —
/// a host that does link rclcpp, such as an RViz plugin, converts a real
/// `sensor_msgs::msg::Image`/`CompressedImage` into @ref RawImageView / raw
/// bytes and hands them here.
///
/// `RawImageView` mirrors `sensor_msgs::msg::Image` field-for-field (see
/// sensor_msgs' `image_encodings.hpp` for the encoding name constants used
/// below) so binding the real message is a field copy, exactly the pattern
/// `panel_input_source.hpp` already uses for gesture events.
///
/// ## Thread safety
///
/// These functions touch only `QImage` (never `QPixmap`, `QFont` or any other
/// Qt type whose implementation assumes the GUI thread), so they are safe to
/// call from a worker thread. That is the intended use: decode off the GUI
/// thread, then hand the result to @ref LatestVideoFrame, which the GUI thread
/// only ever reads.

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>

#include <cstddef>
#include <cstdint>
#include <string>

namespace qar_qt_streaming
{

/// @brief ROS-free mirror of `sensor_msgs::msg::Image`. Borrowed: @ref data
/// must outlive the call it is passed to.
struct RawImageView
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  /// Bytes per row, INCLUDING any padding — may exceed `width * bytes_per_pixel`.
  std::uint32_t step = 0;
  /// A `sensor_msgs` image_encodings name: `rgb8`, `bgr8`, `rgba8`, `bgra8`,
  /// `mono8`, `8UC1`, `8UC3`, `8UC4`, `mono16`, `16UC1`, `32FC1`, `yuv422`,
  /// `uyvy`, `yuyv`, `yuv422_yuy2`. Anything else is rejected.
  std::string encoding;
  /// Byte order of any encoding wider than one byte per sample (`mono16`,
  /// `16UC1`). Ignored for `32FC1`, which sensor_msgs always publishes
  /// host/little-endian.
  bool is_bigendian = false;
  const std::uint8_t * data = nullptr;
  std::size_t size = 0;
};

/// @brief Decode a raw camera frame into an opaque `Format_RGBA8888` image.
///
/// The result is scaled DOWN to fit inside @p max_size when it is larger,
/// preserving aspect ratio with a smooth filter; it is never scaled UP. An
/// invalid or empty @p max_size (default-constructed `QSize()`, or either
/// dimension <= 0) means "no limit".
///
/// See the class file comment for the per-encoding rules (normalisation for
/// depth-style `16UC1`/`32FC1`, byte order for 16-bit samples, YUV 4:2:2
/// layouts). @p in.step and @p in.size are validated against @p in.width /
/// @p in.height / the encoding's bytes-per-pixel before any byte is read.
///
/// @param[out] error When non-null and this returns false, set to a specific,
///        human-readable reason (which argument was wrong, and what it was).
/// @return false on any validation failure or allocation failure; @p out is
///         left untouched.
bool decode_raw_image(const RawImageView & in, QSize max_size, QImage & out, std::string * error);

/// @brief Decode a `sensor_msgs::msg::CompressedImage`-style payload (PNG or
/// JPEG) into the same opaque `Format_RGBA8888` contract as
/// @ref decode_raw_image, via `QImageReader`.
///
/// Only PNG and JPEG are decoded. The first bytes of the payload decide which of
/// the two it is, and @p format is used only in messages, never to pick a codec.
/// Any other payload is refused, whatever image plugins Qt has: for some of the
/// formats that Qt can read (GIF, ICO) the size in the header is not the size that
/// is allocated, so the cap below could not bound them.
///
/// The payload is not trusted. The declared size is read from the header before
/// anything is decoded, and an image is refused when it declares more pixels than a
/// cap: 64 million without a size limit; with one, 16 times the area of @p max_size,
/// but at least 16 million and at most 64 million. A payload of more than INT_MAX
/// bytes is refused as well. An image that is larger than @p max_size is scaled
/// while it is read.
///
/// @param format `CompressedImage::format`, e.g. `"jpeg"`, `"png"`, or
///        `"bgr8; jpeg compressed bgr8"`. May be empty. It appears in the error
///        messages and has no other effect.
bool decode_compressed_image(
  const std::string & format,
  const std::uint8_t * data,
  std::size_t size,
  QSize max_size,
  QImage & out,
  std::string * error);

/// @brief The largest rectangle with @p source's aspect ratio that fits inside
/// @p bounds, centred in it. An empty @p source or @p bounds yields an empty
/// (default-constructed) `QRect`.
QRect aspect_fit_rect(QSize source, QRect bounds);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__VIDEO_FRAME_HPP_
