// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Snippets of API.md for the readability of a panel, its geometry and the hit test. The functions are compiled and
// linked and never run; their parameters stand for what the host owns.

// The library headers come first: panel_pointer_state.hpp declares a method named emit(), which the emit macro of Qt breaks.
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

namespace qqs = qar_qt_streaming;

void snippet_readability(qqs::PanelStreamOptions & options)
{
  // [qar-snippet:readability]
  qqs::ReadabilityProfile profile = qqs::readability_preset(qqs::ReadabilityPreset::ArmsLength);   // 0.65 m
  profile.body_font_px = 14.0;               // the body font that the widgets use
  profile.device_pixels_per_degree = 47.0;   // the resolution of the headset

  qqs::ReadabilityRecommendation recommendation;
  std::string error;
  if (qqs::recommend_readability(profile, recommendation, error)) {
    qqs::apply_readability(recommendation, options);   // the metres per logical pixel and the render scale
    std::printf("%.3f mm per logical px, render scale %.1f, no text under %.1f px\n",
      recommendation.meters_per_logical_px * 1000.0, recommendation.render_scale,
      recommendation.min_legible_font_px);
  } else {
    std::printf("profile refused: %s\n", error.c_str());
  }
  // [qar-snippet-end:readability]
}

void snippet_geometry()
{
  // [qar-snippet:geometry]
  qqs::PanelGeometryInput input;
  input.logical_width = 420;           // QWidget::width()
  input.logical_height = 200;          // QWidget::height()
  input.device_pixel_ratio = 1.5;      // QWidget::devicePixelRatioF()
  input.render_scale = 2.0;            // the supersample of the readability recommendation

  qqs::PanelGeometry geometry;
  std::string error;
  if (qqs::compute_panel_geometry(input, geometry, error)) {
    // The ratio is 1.5 * 2.0 = 3.0: a buffer of 1260 x 600 pixels, a panel of 0.504 x 0.240 m and a content scale of 1.0.
    std::printf("buffer %u x %u px, panel %.3f x %.3f m, content scale %.1f\n",
      static_cast<unsigned>(geometry.buffer_width), static_cast<unsigned>(geometry.buffer_height),
      static_cast<double>(geometry.width_meters), static_cast<double>(geometry.height_meters),
      static_cast<double>(geometry.content_scale));
  } else {
    std::printf("no geometry: %s\n", error.c_str());   // for example a widget that is not laid out yet
  }
  // [qar-snippet-end:geometry]
}

void snippet_hit_test(const qar_ar_core::PanelPlacement & placement)
{
  // [qar-snippet:hit_test]
  const QarVector3 aim{0.1f, 1.45f, -1.0f};                     // a point in room space
  const qqs::PanelHit hit = qqs::hit_test_panel_point(placement, aim);

  std::int32_t x = 0;
  std::int32_t y = 0;
  if (hit.valid && qqs::panel_uv_to_widget_px(hit, 420, 200, x, y)) {   // false outside the panel
    std::printf("widget pixel (%d, %d), %.2f m from the plane of the panel\n", x, y, static_cast<double>(hit.distance_m));
  }

  // A ray instead of a point: the hit is where the ray meets the plane, and distance_m is how far along the ray that is.
  const qqs::PanelHit along =
    qqs::intersect_panel_ray(placement, QarVector3{0.0f, 1.5f, 0.0f}, QarVector3{0.0f, 0.0f, -1.0f});
  std::printf("ray: valid %d, inside %d\n", along.valid ? 1 : 0, along.inside ? 1 : 0);
  // [qar-snippet-end:hit_test]
}
