// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/ar_readability.hpp"

#include <algorithm>
#include <cmath>

namespace qar_qt_streaming
{

double
subtended_arcmin(double length_m, double distance_m)
{
  if (!(distance_m > 0.0)) {
    return 0.0;
  }
  // Exact rather than small-angle: it costs nothing here, and it keeps the
  // function honest for a caller that asks about a whole panel up close.
  return 2.0 * std::atan(length_m / (2.0 * distance_m)) / kRadiansPerArcmin;
}

double
ceil_to_half(double value)
{
  return std::ceil(value * 2.0) / 2.0;
}

ReadabilityProfile
readability_preset(ReadabilityPreset preset)
{
  ReadabilityProfile profile;
  switch (preset) {
    case ReadabilityPreset::Near:
      profile.viewing_distance_m = 0.40;
      break;
    case ReadabilityPreset::ArmsLength:
      profile.viewing_distance_m = 0.65;
      break;
    case ReadabilityPreset::WallMounted:
      profile.viewing_distance_m = 2.0;
      break;
  }
  return profile;
}

bool
recommend_readability(
  const ReadabilityProfile & profile, ReadabilityRecommendation & out, std::string & out_error)
{
  const auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
  if (!positive(profile.viewing_distance_m)) {
    out_error = "viewing_distance_m must be finite and positive";
    return false;
  }
  if (!positive(profile.text_height_arcmin)) {
    out_error = "text_height_arcmin must be finite and positive";
    return false;
  }
  if (!positive(profile.device_pixels_per_degree)) {
    out_error = "device_pixels_per_degree must be finite and positive";
    return false;
  }
  if (!positive(profile.supersample)) {
    out_error = "supersample must be finite and positive";
    return false;
  }
  if (!positive(profile.body_font_px)) {
    out_error = "body_font_px must be finite and positive";
    return false;
  }
  if (!positive(profile.cap_height_ratio) || profile.cap_height_ratio > 1.0) {
    out_error = "cap_height_ratio must be in (0, 1]";
    return false;
  }
  if (!std::isfinite(profile.max_render_scale) || profile.max_render_scale < 1.0) {
    out_error = "max_render_scale must be finite and >= 1";
    return false;
  }

  // The capitals of the body font must subtend the target angle, so each
  // logical pixel subtends target / cap_height_px.
  const double cap_height_px = profile.cap_height_ratio * profile.body_font_px;
  const double arcmin_per_logical_px = profile.text_height_arcmin / cap_height_px;

  // Small-angle: at these sizes (a few arc-minutes) the exact form agrees to
  // better than 1e-6, and the linear form is what a reader can check by hand.
  const double meters_per_logical_px =
    profile.viewing_distance_m * arcmin_per_logical_px * kRadiansPerArcmin;

  // One source pixel must not be larger than one device pixel on the retina,
  // or the device resamples detail that was never captured. Half steps: a finer
  // quantisation would not be visible, a coarser one (integers) would cost up
  // to 4/2.25 = 78 % more pixels than needed.
  const double arcmin_per_device_px = 60.0 / profile.device_pixels_per_degree;
  const double raw_scale = profile.supersample * arcmin_per_logical_px / arcmin_per_device_px;
  const double render_scale = std::clamp(ceil_to_half(raw_scale), 1.0, profile.max_render_scale);

  // The reading floor is the ISO minimum, unless the device (or the eye) cannot
  // even resolve the strokes of a letter that small: a Snellen capital is five
  // strokes tall, and a stroke can be no finer than the coarser of the two.
  const double stroke_floor_arcmin = std::max(kAcuityArcmin, arcmin_per_device_px);
  const double cap_floor_arcmin =
    std::max(kMinimumReadingArcmin, kSnellenStrokesPerCap * stroke_floor_arcmin);
  const double min_legible_font_px =
    profile.body_font_px * cap_floor_arcmin / profile.text_height_arcmin;

  out.meters_per_logical_px = meters_per_logical_px;
  out.render_scale = render_scale;
  out.min_legible_font_px = min_legible_font_px;
  out.arcmin_per_logical_px = arcmin_per_logical_px;
  out.arcmin_per_device_px = arcmin_per_device_px;
  out_error.clear();
  return true;
}

}  // namespace qar_qt_streaming
