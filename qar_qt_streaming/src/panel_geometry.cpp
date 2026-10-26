// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/panel_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace qar_qt_streaming
{

std::uint32_t
align_up(std::uint32_t value, std::uint32_t alignment)
{
  if (alignment <= 1) {
    return value;
  }
  const std::uint32_t remainder = value % alignment;
  if (remainder == 0) {
    return value;
  }
  // Saturate rather than wrap: callers clamp against max_buffer_dimension anyway,
  // and a wrapped 0 would look like a legal "empty" buffer.
  const std::uint32_t pad = alignment - remainder;
  if (value > UINT32_MAX - pad) {
    return value - remainder;
  }
  return value + pad;
}

bool
compute_panel_geometry(
  const PanelGeometryInput & input, PanelGeometry & out_geometry, std::string & out_error)
{
  if (input.logical_width == 0 || input.logical_height == 0) {
    out_error = "widget has a zero logical dimension (not laid out yet?)";
    return false;
  }
  if (!std::isfinite(input.device_pixel_ratio) || input.device_pixel_ratio <= 0.0) {
    out_error = "device_pixel_ratio must be finite and positive";
    return false;
  }
  if (!std::isfinite(input.meters_per_logical_px) || input.meters_per_logical_px <= 0.0) {
    out_error = "meters_per_logical_px must be finite and positive";
    return false;
  }
  if (!std::isfinite(input.render_scale) || input.render_scale <= 0.0) {
    out_error = "render_scale must be finite and positive";
    return false;
  }
  const auto inset_ok = [](double inset_m) {return std::isfinite(inset_m) && inset_m >= 0.0;};
  if (!inset_ok(input.frame.top_m) || !inset_ok(input.frame.bottom_m) ||
    !inset_ok(input.frame.left_m) || !inset_ok(input.frame.right_m) ||
    !inset_ok(input.frame.fit_margin_m))
  {
    out_error = "every inset and the fit margin of the frame must be finite and not negative";
    return false;
  }
  const std::uint32_t max_dim =
    input.max_buffer_dimension == 0 ? kDefaultMaxBufferDimension : input.max_buffer_dimension;
  if (max_dim < input.buffer_alignment) {
    out_error = "max_buffer_dimension is smaller than buffer_alignment";
    return false;
  }

  // Clamp the ratio first, so the buffer stays within the encoder's limits while
  // the PHYSICAL size (which is derived from logical px alone) is untouched. An
  // oversized widget then streams at reduced sharpness instead of failing. The
  // readability supersample is just more ratio, and is clamped the same way.
  double ratio = input.device_pixel_ratio * input.render_scale;
  const double max_ratio_w = static_cast<double>(max_dim) / input.logical_width;
  const double max_ratio_h = static_cast<double>(max_dim) / input.logical_height;
  ratio = std::min({ratio, max_ratio_w, max_ratio_h});
  if (ratio <= 0.0) {
    out_error = "widget is too large to stream even at ratio 1.0";
    return false;
  }

  const auto scaled = [ratio](std::uint32_t logical) {
    const double v = std::lround(static_cast<double>(logical) * ratio);
    return static_cast<std::uint32_t>(std::max(1.0, v));
  };

  const std::uint32_t content_w = scaled(input.logical_width);
  const std::uint32_t content_h = scaled(input.logical_height);
  std::uint32_t width = align_up(content_w, input.buffer_alignment);
  std::uint32_t height = align_up(content_h, input.buffer_alignment);

  // align_up can push a just-at-the-limit dimension one step over it; step back
  // rather than exceed a hard encoder bound.
  const std::uint32_t aligned_max =
    input.buffer_alignment <= 1 ? max_dim : (max_dim / input.buffer_alignment) * input.buffer_alignment;
  width = std::min(width, aligned_max);
  height = std::min(height, aligned_max);
  if (width == 0 || height == 0) {
    out_error = "computed buffer size collapsed to zero";
    return false;
  }

  out_geometry.buffer_width = width;
  out_geometry.buffer_height = height;
  // Never past the buffer: the max-dimension clamp above can shrink the buffer
  // below the widget's scaled size, and the widget is then simply cropped.
  out_geometry.content_width = std::min(content_w, width);
  out_geometry.content_height = std::min(content_h, height);
  // QarOS draws the video into the panel inside a frame of its own, so the area it draws into must
  // be a hair taller than the video: the BUFFER (the buffer is what the video is, padding
  // included). A video taller than the area is scrolled, behind a scroll track that narrows it,
  // so the area is the video plus the fit margin. A panel keeps its width and is made as tall as
  // the video at the width the frame leaves it needs, plus the margin; a dialog keeps the height
  // of its video at the width it asks for and is given the width the frame and the margin need
  // for that height. The nominal width stays logical px * metres per px, so a HiDPI display or a
  // supersample does not change it; the buffer enters the heights only as an aspect ratio. See
  // panel_geometry.hpp.
  const PanelFrame no_frame{0.0, 0.0, 0.0, 0.0, 0.0};
  PanelFrame frame = input.frame;
  const double nominal_width_m =
    static_cast<double>(input.logical_width) * input.meters_per_logical_px;
  const double aspect = static_cast<double>(height) / static_cast<double>(width);
  double width_m = nominal_width_m;
  double height_m = 0.0;
  FrameNarrowing narrowing = FrameNarrowing::None;
  if (input.narrow_for_frame) {
    // The height of the dialog is the one of its video at the width it asks for.
    height_m = nominal_width_m * aspect;
    if (frame.vertical_m() + frame.fit_margin_m >= height_m) {
      narrowing = FrameNarrowing::Skipped;
      frame = no_frame;
    } else {
      // The video must be (height - fv - m) tall and, at the aspect ratio of the buffer, that
      // wide.
      const double fitted_m = frame.horizontal_m() +
        (height_m - frame.vertical_m() - frame.fit_margin_m) / aspect;
      // The tolerances are for rounding: a width of exactly one half is not Limited, and one of
      // exactly the nominal width is a reduction by nothing.
      if (fitted_m < kMinNarrowedWidthShare * nominal_width_m - 1e-9) {
        width_m = kMinNarrowedWidthShare * nominal_width_m;
        narrowing = FrameNarrowing::Limited;
      } else if (fitted_m <= nominal_width_m + 1e-9) {
        width_m = std::min(fitted_m, nominal_width_m);
        narrowing = FrameNarrowing::Reduced;
      }
      // Else the frame would widen the panel: the width the widget asks for is kept, and the
      // video is a little shorter than the area, which leaves a thin band above and below it.
    }
    if (frame.horizontal_m() >= width_m) {
      frame = no_frame;
    }
  } else {
    if (frame.horizontal_m() >= nominal_width_m) {
      frame = no_frame;
    }
    height_m = frame.vertical_m() + (nominal_width_m - frame.horizontal_m()) * aspect +
      frame.fit_margin_m;
  }
  const double video_width_m = width_m - frame.horizontal_m();
  out_geometry.width_meters = static_cast<float>(width_m);
  out_geometry.nominal_width_meters = static_cast<float>(nominal_width_m);
  out_geometry.video_width_meters = static_cast<float>(video_width_m);
  out_geometry.video_height_meters = static_cast<float>(video_width_m * aspect);
  out_geometry.frame = frame;
  out_geometry.height_meters = static_cast<float>(height_m);
  out_geometry.narrowing = narrowing;
  // QarGuiPanelSize::content_scale (third_party/qar/include/qar_streaming.h) is
  // documented as a ZOOM: "1.0 fits the content across the full panel surface,
  // 2.0 shows it at twice the size so only half of it fits, and 0.5 shows it at
  // half size." `ratio` here is a RESOLUTION (our HiDPI/readability
  // supersample), not a zoom, and QarOS already learns the raster's resolution
  // from the video image itself. Sending the ratio would be a zoom > 1: contacts
  // would reach only part of the registered extent (hover/click landing offset).
  // So QarOS is always sent 1.0 here; the supersample ratio is reported
  // separately, diagnostics-only, in effective_pixel_ratio below. See
  // panel_geometry.hpp.
  out_geometry.content_scale = 1.0f;
  out_geometry.effective_pixel_ratio = ratio;
  out_error.clear();
  return true;
}

}  // namespace qar_qt_streaming
