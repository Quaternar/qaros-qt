// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__FRAME_LUMINANCE_HPP_
#define QAR_QT_STREAMING__FRAME_LUMINANCE_HPP_

/// @file frame_luminance.hpp
/// @brief How bright a captured panel frame is, from a sparse sample of its pixels.
///
/// Qt-free and header-only, so it is exactly testable on a plain byte buffer. A panel that
/// shows black in AR is either not receiving the pixels (a receiver problem) or sending black
/// ones (a capture problem); the mean luminance of the frame that was submitted tells the two
/// apart in one number, without copying the frame or keeping it. @ref frame_brightness adds the
/// luminance of the top rows and of the rest, and the alpha, from the same sparse sample.

#include "qar_ar_core/panel_types.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace qar_qt_streaming
{

/// Sampling stride, in pixels, of @ref mean_luminance: every 8th pixel in x and in y. A
/// 1920 x 1080 frame is read at about 32,000 pixels, which costs microseconds.
constexpr std::uint32_t kLuminanceSampleStep = 8;

/// @brief Mean of 0.299 R + 0.587 G + 0.114 B, 0 (black) to 255 (white), over every
/// @p step-th pixel in x and in y of an R8G8B8A8 frame (byte order red, green, blue, alpha).
/// Alpha is ignored. The rows are @c stride_bytes apart.
///
/// @return 0 for a frame without data or with a zero dimension, or with @p step 0.
inline double
mean_luminance(const qar_ar_core::PanelPixels & pixels, std::uint32_t step = kLuminanceSampleStep)
{
  if (pixels.data == nullptr || pixels.width == 0 || pixels.height == 0 || step == 0) {
    return 0.0;
  }
  double sum = 0.0;
  std::uint64_t count = 0;
  for (std::uint32_t y = 0; y < pixels.height; y += step) {
    const std::uint8_t * row = pixels.data + static_cast<std::size_t>(y) * pixels.stride_bytes;
    for (std::uint32_t x = 0; x < pixels.width; x += step) {
      const std::uint8_t * pixel = row + static_cast<std::size_t>(x) * 4u;
      sum += 0.299 * pixel[0] + 0.587 * pixel[1] + 0.114 * pixel[2];
      ++count;
    }
  }
  return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

/// Fraction of the rows, counted from the top, that @ref frame_brightness reports apart.
constexpr double kBrightnessTopFraction = 0.15;

/// What @ref frame_brightness reports for a quantity it could not measure.
constexpr double kBrightnessNotMeasured = -1.0;

/// @brief Brightness and opacity of one frame, each 0 to 255, from the same sparse sample.
struct FrameBrightness
{
  /// Mean luminance over every sampled pixel; the value @ref mean_luminance returns.
  double all = kBrightnessNotMeasured;
  /// ... over the sampled pixels in the top rows, and over those in all the other rows. A frame
  /// with a bright strip and a black page reads high in @c top and near 0 in @c rest.
  double top = kBrightnessNotMeasured;
  double rest = kBrightnessNotMeasured;
  /// Mean alpha over every sampled pixel.
  double alpha = kBrightnessNotMeasured;
};

/// @brief The luminance of a frame as a whole, of its top rows and of the rest, and its alpha,
/// from every @p step-th pixel in x and in y of an R8G8B8A8 frame (byte order red, green, blue,
/// alpha; rows @c stride_bytes apart), in one pass.
///
/// The top rows are the first @p top_fraction of the frame's rows, at least one: a row belongs
/// to the top when its index is below that count. The luminance is the one of
/// @ref mean_luminance, so @c all equals it exactly for the same @p step.
///
/// @return @ref kBrightnessNotMeasured in every member for a frame without data, with a zero
/// dimension, or with @p step 0; in @c rest when no sampled row lies below the top rows.
inline FrameBrightness
frame_brightness(
  const qar_ar_core::PanelPixels & pixels, double top_fraction = kBrightnessTopFraction,
  std::uint32_t step = kLuminanceSampleStep)
{
  FrameBrightness out;
  if (pixels.data == nullptr || pixels.width == 0 || pixels.height == 0 || step == 0) {
    return out;
  }
  // Whole rows, at least one, never more than the frame has.
  std::uint32_t top_rows = 1;
  if (top_fraction > 0.0) {
    const double wanted = top_fraction * static_cast<double>(pixels.height);
    top_rows = wanted >= static_cast<double>(pixels.height) ? pixels.height :
      std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(wanted));
  }
  double sum_all = 0.0;
  double sum_top = 0.0;
  double sum_rest = 0.0;
  double sum_alpha = 0.0;
  std::uint64_t count_top = 0;
  std::uint64_t count_rest = 0;
  for (std::uint32_t y = 0; y < pixels.height; y += step) {
    const std::uint8_t * row = pixels.data + static_cast<std::size_t>(y) * pixels.stride_bytes;
    const bool in_top = y < top_rows;
    for (std::uint32_t x = 0; x < pixels.width; x += step) {
      const std::uint8_t * pixel = row + static_cast<std::size_t>(x) * 4u;
      const double luminance = 0.299 * pixel[0] + 0.587 * pixel[1] + 0.114 * pixel[2];
      sum_all += luminance;
      sum_alpha += pixel[3];
      if (in_top) {
        sum_top += luminance;
        ++count_top;
      } else {
        sum_rest += luminance;
        ++count_rest;
      }
    }
  }
  const std::uint64_t count = count_top + count_rest;
  if (count == 0) {
    return out;
  }
  out.all = sum_all / static_cast<double>(count);
  out.alpha = sum_alpha / static_cast<double>(count);
  if (count_top > 0) {
    out.top = sum_top / static_cast<double>(count_top);
  }
  if (count_rest > 0) {
    out.rest = sum_rest / static_cast<double>(count_rest);
  }
  return out;
}

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__FRAME_LUMINANCE_HPP_
