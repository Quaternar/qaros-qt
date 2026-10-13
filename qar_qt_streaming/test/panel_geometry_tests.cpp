// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The DPI contract: buffer pixels follow the device pixel ratio, physical
// metres follow LOGICAL pixels, and effective_pixel_ratio carries the ratio
// for diagnostics. content_scale is what QarOS receives and is always 1.0 (a ZOOM per
// qar_streaming.h - see panel_geometry.cpp). Get this wrong and AR panels are
// blurry, the wrong physical size, or - content_scale != 1.0 - hover/click
// land off the content they are visibly over.

#include <gtest/gtest.h>

#include "qar_qt_streaming/panel_geometry.hpp"

using qar_qt_streaming::align_up;
using qar_qt_streaming::compute_panel_geometry;
using qar_qt_streaming::PanelGeometry;
using qar_qt_streaming::PanelGeometryInput;

namespace
{

PanelGeometryInput
make_input(std::uint32_t w, std::uint32_t h, double dpr)
{
  PanelGeometryInput input;
  input.logical_width = w;
  input.logical_height = h;
  input.device_pixel_ratio = dpr;
  return input;
}

}  // namespace

TEST(AlignUp, RoundsUpToTheNextMultiple)
{
  EXPECT_EQ(align_up(0, 2), 0u);
  EXPECT_EQ(align_up(1, 2), 2u);
  EXPECT_EQ(align_up(2, 2), 2u);
  EXPECT_EQ(align_up(3, 2), 4u);
  EXPECT_EQ(align_up(17, 4), 20u);
}

TEST(AlignUp, AlignmentOfZeroOrOneIsIdentity)
{
  EXPECT_EQ(align_up(7, 0), 7u);
  EXPECT_EQ(align_up(7, 1), 7u);
}

TEST(PanelGeometry, AtRatioOneBufferMatchesLogicalSize)
{
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(make_input(400, 600, 1.0), geometry, error)) << error;

  EXPECT_EQ(geometry.buffer_width, 400u);
  EXPECT_EQ(geometry.buffer_height, 600u);
  EXPECT_FLOAT_EQ(geometry.content_scale, 1.0f);
}

TEST(PanelGeometry, HiDpiGrowsTheBufferButNotThePhysicalSize)
{
  PanelGeometry at_one;
  PanelGeometry at_two;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(make_input(400, 600, 1.0), at_one, error)) << error;
  ASSERT_TRUE(compute_panel_geometry(make_input(400, 600, 2.0), at_two, error)) << error;

  // Twice the pixels...
  EXPECT_EQ(at_two.buffer_width, 800u);
  EXPECT_EQ(at_two.buffer_height, 1200u);
  // ...at exactly the same physical size. This is the whole point: a HiDPI
  // display must make the panel SHARPER, never BIGGER.
  EXPECT_FLOAT_EQ(at_two.width_meters, at_one.width_meters);
  EXPECT_FLOAT_EQ(at_two.height_meters, at_one.height_meters);
  // content_scale is what QarOS receives, and it is a ZOOM (see the file
  // comment) - always 1.0, never the ratio. The ratio itself still reaches
  // effective_pixel_ratio and, via that, the buffer size asserted above.
  EXPECT_FLOAT_EQ(at_two.content_scale, 1.0f);
  EXPECT_DOUBLE_EQ(at_two.effective_pixel_ratio, 2.0);
}

TEST(PanelGeometry, PhysicalSizeFollowsLogicalPixelsAndTheConfiguredScale)
{
  PanelGeometryInput input = make_input(500, 250, 1.0);
  input.meters_per_logical_px = 0.0008;

  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;

  EXPECT_NEAR(geometry.width_meters, 0.40f, 1e-5f);
  EXPECT_NEAR(geometry.height_meters, 0.20f, 1e-5f);
}

TEST(PanelGeometry, FractionalRatioRoundsAndAlignsToAnEvenBuffer)
{
  // 401 * 1.25 = 501.25 -> 501 -> aligned up to 502. Odd dimensions are rejected
  // by the video encoders the QarOS runtime uses, so the alignment is load-bearing.
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(make_input(401, 401, 1.25), geometry, error)) << error;

  EXPECT_EQ(geometry.buffer_width % 2u, 0u);
  EXPECT_EQ(geometry.buffer_height % 2u, 0u);
  EXPECT_EQ(geometry.buffer_width, 502u);
}

TEST(PanelGeometry, ClampsAnOversizedWidgetByReducingTheRatioNotThePanelSize)
{
  PanelGeometryInput input = make_input(4000, 1000, 2.0);
  input.max_buffer_dimension = 4096;

  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;

  EXPECT_LE(geometry.buffer_width, 4096u);
  EXPECT_LE(geometry.buffer_height, 4096u);
  // The ratio was cut from 2.0 to ~1.024, and that is reported honestly...
  EXPECT_LT(geometry.effective_pixel_ratio, 2.0);
  // ...in effective_pixel_ratio - content_scale is what is SENT, and that is
  // always 1.0 (a ZOOM, per the file comment), clamped or not.
  EXPECT_FLOAT_EQ(geometry.content_scale, 1.0f);
  // ...while the physical size, which depends only on logical px, is untouched.
  EXPECT_NEAR(geometry.width_meters, static_cast<float>(4000 * 0.0012), 1e-4f);
}

TEST(PanelGeometry, NeverExceedsTheMaximumEvenAfterAlignment)
{
  // A max that is not itself a multiple of the alignment is where a naive
  // align-then-check would step over the limit.
  PanelGeometryInput input = make_input(1001, 1001, 1.0);
  input.max_buffer_dimension = 1001;
  input.buffer_alignment = 2;

  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;
  EXPECT_LE(geometry.buffer_width, 1001u);
  EXPECT_EQ(geometry.buffer_width % 2u, 0u);
}

TEST(PanelGeometry, RejectsAWidgetThatIsNotLaidOutYet)
{
  PanelGeometry geometry;
  std::string error;
  EXPECT_FALSE(compute_panel_geometry(make_input(0, 600, 1.0), geometry, error));
  EXPECT_FALSE(error.empty());

  EXPECT_FALSE(compute_panel_geometry(make_input(400, 0, 1.0), geometry, error));
  EXPECT_FALSE(error.empty());
}

TEST(PanelGeometry, RejectsNonsenseRatios)
{
  PanelGeometry geometry;
  std::string error;
  EXPECT_FALSE(compute_panel_geometry(make_input(400, 600, 0.0), geometry, error));
  EXPECT_FALSE(compute_panel_geometry(make_input(400, 600, -1.0), geometry, error));

  PanelGeometryInput nan_input = make_input(400, 600, std::nan(""));
  EXPECT_FALSE(compute_panel_geometry(nan_input, geometry, error));

  PanelGeometryInput bad_scale = make_input(400, 600, 1.0);
  bad_scale.meters_per_logical_px = 0.0;
  EXPECT_FALSE(compute_panel_geometry(bad_scale, geometry, error));
}

TEST(PanelGeometry, RenderScaleSupersamplesTheBufferButNotThePhysicalSize)
{
  PanelGeometryInput input = make_input(400, 600, 1.0);
  input.render_scale = 2.0;

  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;

  // The readability supersample rides the HiDPI invariant: more pixels, same
  // metres, and effective_pixel_ratio tells QarOS's diagnostics so - but
  // content_scale, what QarOS itself receives, stays 1.0 (a ZOOM; see the
  // file comment).
  EXPECT_EQ(geometry.buffer_width, 800u);
  EXPECT_EQ(geometry.buffer_height, 1200u);
  EXPECT_FLOAT_EQ(geometry.content_scale, 1.0f);
  EXPECT_DOUBLE_EQ(geometry.effective_pixel_ratio, 2.0);
  EXPECT_NEAR(geometry.width_meters, static_cast<float>(400 * 0.0012), 1e-5f);

  // It stacks with the display's own ratio...
  input.device_pixel_ratio = 1.5;
  ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;
  EXPECT_EQ(geometry.buffer_width, 1200u);
  EXPECT_FLOAT_EQ(geometry.content_scale, 1.0f);
  EXPECT_DOUBLE_EQ(geometry.effective_pixel_ratio, 3.0);

  // ...and is clamped by the encoder limit exactly like the ratio is.
  input.max_buffer_dimension = 1000;
  ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;
  EXPECT_LE(geometry.buffer_height, 1000u);
  EXPECT_LT(geometry.effective_pixel_ratio, 3.0);
}

TEST(PanelGeometry, RejectsANonsenseRenderScale)
{
  PanelGeometry geometry;
  std::string error;
  PanelGeometryInput input = make_input(400, 600, 1.0);
  input.render_scale = 0.0;
  EXPECT_FALSE(compute_panel_geometry(input, geometry, error));
  input.render_scale = std::nan("");
  EXPECT_FALSE(compute_panel_geometry(input, geometry, error));
}

TEST(PanelGeometry, ContentScaleSentToQarOsIsAlwaysOneRegardlessOfSupersampling)
{
  // QarGuiPanelSize::content_scale is a ZOOM (third_party/qar/include/
  // qar_streaming.h): "1.0 fits the content across the full panel surface,
  // 2.0 shows it at twice the size so only half of it fits". QarOS already
  // learns the raster's resolution from the video image itself, so this must
  // always be 1.0 - a zoom > 1 would crop the far edge, so contacts would reach
  // only part of the registered extent. The ratio itself is real and must still
  // reach effective_pixel_ratio (and, through it, the buffer) - just never
  // content_scale.
  struct Combo { double dpr; double render_scale; };
  const Combo combos[] = {
    {1.0, 1.0}, {2.0, 1.0}, {1.0, 2.0}, {1.5, 1.5}, {3.0, 1.0},
  };
  for (const auto & combo : combos) {
    PanelGeometryInput input = make_input(400, 300, combo.dpr);
    input.render_scale = combo.render_scale;

    PanelGeometry geometry;
    std::string error;
    ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;

    EXPECT_FLOAT_EQ(geometry.content_scale, 1.0f)
      << "dpr=" << combo.dpr << " render_scale=" << combo.render_scale;
    EXPECT_DOUBLE_EQ(geometry.effective_pixel_ratio, combo.dpr * combo.render_scale)
      << "supersampling must still reach the buffer, just never content_scale";
  }
}
