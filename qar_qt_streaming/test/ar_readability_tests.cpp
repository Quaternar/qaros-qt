// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The readability arithmetic. Every expected number below is worked by
// hand in ar_readability.hpp's file comment; the tests pin the code to it.

#include <gtest/gtest.h>

#include "qar_qt_streaming/ar_readability.hpp"
#include "qar_qt_streaming/panel_geometry.hpp"

#include <cmath>
#include <string>

using qar_qt_streaming::ceil_to_half;
using qar_qt_streaming::compute_panel_geometry;
using qar_qt_streaming::kRadiansPerArcmin;
using qar_qt_streaming::PanelGeometry;
using qar_qt_streaming::PanelGeometryInput;
using qar_qt_streaming::readability_preset;
using qar_qt_streaming::ReadabilityPreset;
using qar_qt_streaming::ReadabilityProfile;
using qar_qt_streaming::ReadabilityRecommendation;
using qar_qt_streaming::recommend_readability;
using qar_qt_streaming::subtended_arcmin;

namespace
{

ReadabilityRecommendation
recommend(const ReadabilityProfile & profile)
{
  ReadabilityRecommendation out;
  std::string error;
  EXPECT_TRUE(recommend_readability(profile, out, error)) << error;
  return out;
}

}  // namespace

TEST(Readability, OneArcminuteIsPiOver10800Radians)
{
  EXPECT_NEAR(kRadiansPerArcmin, 3.14159265358979323846 / 10800.0, 1e-15);
}

TEST(Readability, SubtendedAngleMatchesTheSmallAngleFormula)
{
  // 1 mm at 1 m: 1e-3 rad = 3.4377 arc-minutes.
  EXPECT_NEAR(subtended_arcmin(0.001, 1.0), 1e-3 / kRadiansPerArcmin, 1e-6);
  EXPECT_DOUBLE_EQ(subtended_arcmin(0.001, 0.0), 0.0);
}

TEST(Readability, CeilToHalfRoundsUpToHalfSteps)
{
  EXPECT_DOUBLE_EQ(ceil_to_half(1.0), 1.0);
  EXPECT_DOUBLE_EQ(ceil_to_half(1.01), 1.5);
  EXPECT_DOUBLE_EQ(ceil_to_half(1.5), 1.5);
  EXPECT_DOUBLE_EQ(ceil_to_half(1.96), 2.0);
  EXPECT_DOUBLE_EQ(ceil_to_half(2.51), 3.0);
}

TEST(Readability, ArmsLengthPresetMatchesTheWorkedExample)
{
  const ReadabilityRecommendation r = recommend(readability_preset(ReadabilityPreset::ArmsLength));

  // 22 arcmin / (0.70 * 12 px) = 2.619 arcmin per logical px.
  EXPECT_NEAR(r.arcmin_per_logical_px, 22.0 / 8.4, 1e-9);
  // 60 / 45 ppd = 1.333 arcmin per device px.
  EXPECT_NEAR(r.arcmin_per_device_px, 60.0 / 45.0, 1e-9);
  // 0.65 m * 2.619 * 2.90888e-4 = 4.95e-4 m: half a millimetre per logical px.
  EXPECT_NEAR(r.meters_per_logical_px, 0.65 * (22.0 / 8.4) * kRadiansPerArcmin, 1e-9);
  EXPECT_NEAR(r.meters_per_logical_px, 4.952e-4, 1e-6);
  // 2.619 / 1.333 = 1.96 -> 2.0.
  EXPECT_DOUBLE_EQ(r.render_scale, 2.0);
  // 12 px * 16 / 22 = 8.73 px.
  EXPECT_NEAR(r.min_legible_font_px, 12.0 * 16.0 / 22.0, 1e-9);
}

TEST(Readability, PhysicalSizeScalesWithDistanceButRenderScaleDoesNot)
{
  // ("near"/"far" are Windows macros; hence the names.)
  const ReadabilityRecommendation close_up = recommend(readability_preset(ReadabilityPreset::Near));
  const ReadabilityRecommendation across_room =
    recommend(readability_preset(ReadabilityPreset::WallMounted));

  // 2.0 m / 0.4 m: the wall panel is five times the size, pixel for pixel...
  EXPECT_NEAR(across_room.meters_per_logical_px / close_up.meters_per_logical_px, 5.0, 1e-9);
  // ...and needs exactly the same pixel density, because the profile fixes the
  // ANGULAR size of the text.
  EXPECT_DOUBLE_EQ(across_room.render_scale, close_up.render_scale);
  EXPECT_DOUBLE_EQ(across_room.min_legible_font_px, close_up.min_legible_font_px);
}

TEST(Readability, ACoarserDeviceNeedsLessSupersamplingAndAFinerOneMore)
{
  ReadabilityProfile coarse;
  coarse.device_pixels_per_degree = 25.0;  // consumer VR: 2.4 arcmin per px
  EXPECT_DOUBLE_EQ(recommend(coarse).render_scale, 1.5);  // 2.619 / 2.4 = 1.09 -> 1.5

  ReadabilityProfile fine;
  fine.device_pixels_per_degree = 90.0;  // 0.667 arcmin per px
  EXPECT_DOUBLE_EQ(recommend(fine).render_scale, 4.0);  // 3.93 -> 4.0

  fine.max_render_scale = 3.0;
  EXPECT_DOUBLE_EQ(recommend(fine).render_scale, 3.0);
}

TEST(Readability, SupersampleMultipliesTheRenderScale)
{
  ReadabilityProfile profile;
  profile.supersample = 1.5;
  EXPECT_DOUBLE_EQ(recommend(profile).render_scale, 3.0);  // 1.5 * 1.96 = 2.95 -> 3.0
}

TEST(Readability, RenderScaleNeverDropsBelowOne)
{
  ReadabilityProfile profile;
  profile.device_pixels_per_degree = 5.0;  // 12 arcmin per px: far coarser than the text
  EXPECT_DOUBLE_EQ(recommend(profile).render_scale, 1.0);
}

TEST(Readability, TheReadingFloorRisesWhenTheDeviceCannotResolveStrokes)
{
  // 10 ppd = 6 arcmin per px; five strokes of that is 30 arcmin, above the ISO
  // 16 arcmin minimum, so the device — not the standard — sets the floor.
  ReadabilityProfile profile;
  profile.device_pixels_per_degree = 10.0;
  EXPECT_NEAR(recommend(profile).min_legible_font_px, 12.0 * 30.0 / 22.0, 1e-9);
}

TEST(Readability, ALargerTextTargetMeansALargerPanel)
{
  ReadabilityProfile normal;
  ReadabilityProfile large;
  large.text_height_arcmin = 30.0;
  EXPECT_NEAR(
    recommend(large).meters_per_logical_px / recommend(normal).meters_per_logical_px,
    30.0 / 22.0, 1e-9);
}

TEST(Readability, RejectsNonsenseProfiles)
{
  ReadabilityRecommendation out;
  std::string error;

  ReadabilityProfile zero_distance;
  zero_distance.viewing_distance_m = 0.0;
  EXPECT_FALSE(recommend_readability(zero_distance, out, error));
  EXPECT_FALSE(error.empty());

  ReadabilityProfile nan_ppd;
  nan_ppd.device_pixels_per_degree = std::nan("");
  EXPECT_FALSE(recommend_readability(nan_ppd, out, error));

  ReadabilityProfile bad_cap;
  bad_cap.cap_height_ratio = 1.5;
  EXPECT_FALSE(recommend_readability(bad_cap, out, error));

  ReadabilityProfile bad_max;
  bad_max.max_render_scale = 0.5;
  EXPECT_FALSE(recommend_readability(bad_max, out, error));

  ReadabilityProfile bad_supersample;
  bad_supersample.supersample = -1.0;
  EXPECT_FALSE(recommend_readability(bad_supersample, out, error));
}

TEST(Readability, FeedsPanelGeometryWithoutTouchingThePhysicalSizeInvariant)
{
  const ReadabilityRecommendation r = recommend(readability_preset(ReadabilityPreset::ArmsLength));

  PanelGeometryInput input;
  input.logical_width = 500;
  input.logical_height = 300;
  input.device_pixel_ratio = 1.0;
  input.meters_per_logical_px = r.meters_per_logical_px;
  input.render_scale = r.render_scale;

  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;

  // Twice the pixels - in effective_pixel_ratio; the metres follow LOGICAL px.
  // content_scale is what QarOS receives and stays 1.0 (a ZOOM; see the
  // panel_geometry.hpp file comment) regardless of the supersample.
  EXPECT_EQ(geometry.buffer_width, 1000u);
  EXPECT_EQ(geometry.buffer_height, 600u);
  EXPECT_FLOAT_EQ(geometry.content_scale, 1.0f);
  EXPECT_DOUBLE_EQ(geometry.effective_pixel_ratio, r.render_scale);
  EXPECT_NEAR(geometry.width_meters, static_cast<float>(500.0 * r.meters_per_logical_px), 1e-6f);
  EXPECT_NEAR(geometry.width_meters, 0.2476f, 1e-3f);
}
