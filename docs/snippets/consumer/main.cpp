// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Consumer program of the installed qar_qt_streaming package. It prints the version of the package
// and the targets it was linked with, and calls three functions of the library that use neither a
// widget nor QarOS: the parser of the close mode, the readability preset and the panel geometry.

#include <cstdio>
#include <string>

#include "qar_qt_streaming/ar_readability.hpp"
#include "qar_qt_streaming/panel_close_mode.hpp"
#include "qar_qt_streaming/panel_geometry.hpp"

namespace qqs = qar_qt_streaming;

int main()
{
  std::printf("qar_qt_streaming %s\n", CONSUMER_PACKAGE_VERSION);
  std::printf("targets: %s\n", CONSUMER_TARGETS);

  std::string error;
  qqs::ReadabilityRecommendation reading;
  qqs::recommend_readability(qqs::readability_preset(qqs::ReadabilityPreset::ArmsLength), reading, error);
  std::printf("arm's-length preset: %.2f mm per logical pixel, render scale %.1f\n",
    reading.meters_per_logical_px * 1000.0, reading.render_scale);

  qqs::PanelGeometryInput input;
  input.logical_width = 420;
  input.logical_height = 200;
  qqs::PanelGeometry geometry;
  qqs::compute_panel_geometry(input, geometry, error);
  std::printf("a 420 x 200 widget: buffer %u x %u px, panel %.3f x %.3f m\n",
    static_cast<unsigned>(geometry.buffer_width), static_cast<unsigned>(geometry.buffer_height),
    static_cast<double>(geometry.width_meters), static_cast<double>(geometry.height_meters));

  std::printf("close mode of \"notice\": %s\n", qqs::to_string(qqs::parse_panel_close_mode("notice").mode));
  return 0;
}
