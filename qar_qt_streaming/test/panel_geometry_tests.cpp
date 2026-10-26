// Copyright 2026 QuaternAR
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

#include <cmath>
#include <limits>
#include <string>

using qar_qt_streaming::align_up;
using qar_qt_streaming::compute_panel_geometry;
using qar_qt_streaming::FrameNarrowing;
using qar_qt_streaming::PanelFrame;
using qar_qt_streaming::PanelGeometry;
using qar_qt_streaming::PanelGeometryInput;

namespace
{

/// No frame at all: a panel exactly as large as its video.
PanelFrame
no_frame()
{
  return PanelFrame{0.0, 0.0, 0.0, 0.0, 0.0};
}

/// A frame that is a title bar of @p top_m and nothing else: no fit margin either.
PanelFrame
top_frame(double top_m)
{
  return PanelFrame{top_m, 0.0, 0.0, 0.0, 0.0};
}

PanelGeometryInput
make_input(std::uint32_t w, std::uint32_t h, double dpr)
{
  PanelGeometryInput input;
  input.logical_width = w;
  input.logical_height = h;
  input.device_pixel_ratio = dpr;
  return input;
}

/// The input of a dialog's panel: it fits its width to @p frame.
PanelGeometryInput
make_dialog_input(std::uint32_t w, std::uint32_t h, const PanelFrame & frame)
{
  PanelGeometryInput input = make_input(w, h, 1.0);
  input.frame = frame;
  input.narrow_for_frame = true;
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
  input.frame = no_frame();  // the video alone

  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;

  EXPECT_NEAR(geometry.width_meters, 0.40f, 1e-5f);
  EXPECT_NEAR(geometry.height_meters, 0.20f, 1e-5f);
}

TEST(PanelGeometryFrame, TheDefaultFrameIsTheOneMeasuredOnTheVisualizer)
{
  // The title bar of about 0.063 m and a border of about 0.003 m around the video on every side.
  EXPECT_DOUBLE_EQ(qar_qt_streaming::kDefaultArFrameTopM, 0.066);
  EXPECT_DOUBLE_EQ(qar_qt_streaming::kDefaultArFrameBottomM, 0.003);
  EXPECT_DOUBLE_EQ(qar_qt_streaming::kDefaultArFrameLeftM, 0.003);
  EXPECT_DOUBLE_EQ(qar_qt_streaming::kDefaultArFrameRightM, 0.003);
  EXPECT_DOUBLE_EQ(qar_qt_streaming::kDefaultArFitMarginM, 0.003);
  const PanelFrame frame;
  EXPECT_DOUBLE_EQ(frame.top_m, 0.066);
  EXPECT_DOUBLE_EQ(frame.bottom_m, 0.003);
  EXPECT_DOUBLE_EQ(frame.left_m, 0.003);
  EXPECT_DOUBLE_EQ(frame.right_m, 0.003);
  EXPECT_DOUBLE_EQ(frame.fit_margin_m, 0.003);
  // The two parts that the sizes are made of; the fit margin is a part of neither.
  EXPECT_DOUBLE_EQ(frame.vertical_m(), 0.069);
  EXPECT_DOUBLE_EQ(frame.horizontal_m(), 0.006);
  const PanelFrame stated{0.01, 0.02, 0.04, 0.08, 0.005};
  EXPECT_DOUBLE_EQ(stated.vertical_m(), 0.03);
  EXPECT_DOUBLE_EQ(stated.horizontal_m(), 0.12);
  EXPECT_DOUBLE_EQ(stated.fit_margin_m, 0.005);
  // The input starts with it, and a panel fits its height unless it is asked to fit its width.
  EXPECT_DOUBLE_EQ(PanelGeometryInput{}.frame.top_m, 0.066);
  EXPECT_DOUBLE_EQ(PanelGeometryInput{}.frame.right_m, 0.003);
  EXPECT_DOUBLE_EQ(PanelGeometryInput{}.frame.fit_margin_m, 0.003);
  EXPECT_FALSE(PanelGeometryInput{}.narrow_for_frame);
  EXPECT_DOUBLE_EQ(qar_qt_streaming::kMinNarrowedWidthShare, 0.5);
}

TEST(PanelGeometryFrame, ThisQarPanelIsTheVideoInsideTheBorderUnderTheTitleBar)
{
  // The real numbers of "This QAR panel": a 475 x 421 px window has a content of 475 x 421 px in
  // a buffer of 476 x 422 px (aligned up to even), and is 0.570 m wide at 1.2 mm per px. QarOS
  // draws the BUFFER at the width the border leaves, 0.570 - 2 * 0.003 = 0.564 m, so its video is
  // 0.564 * 422 / 476 = 0.500017 m tall, and the panel is that plus the 0.066 m of the title bar
  // and the top border, the 0.003 m of the bottom border and the 0.003 m fit margin: 0.572017 m.
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(make_input(475, 421, 1.0), geometry, error)) << error;

  EXPECT_EQ(geometry.content_width, 475u);
  EXPECT_EQ(geometry.content_height, 421u);
  EXPECT_EQ(geometry.buffer_width, 476u);
  EXPECT_EQ(geometry.buffer_height, 422u);
  EXPECT_NEAR(geometry.width_meters, 0.570f, 1e-6f);
  EXPECT_NEAR(geometry.video_width_meters, 0.564f, 1e-6f);
  EXPECT_NEAR(geometry.video_height_meters, 0.500017f, 1e-6f);
  EXPECT_NEAR(geometry.height_meters, 0.572017f, 1e-6f);
  // The parts add up to the height that is sent, whatever the numbers are.
  EXPECT_NEAR(geometry.height_meters,
    geometry.video_height_meters + geometry.frame.vertical_m() + geometry.frame.fit_margin_m,
    1e-6f);
  EXPECT_NEAR(geometry.width_meters, geometry.video_width_meters + geometry.frame.horizontal_m(),
    1e-6f);
  EXPECT_DOUBLE_EQ(geometry.frame.top_m, 0.066);
  EXPECT_DOUBLE_EQ(geometry.frame.right_m, 0.003);
  EXPECT_DOUBLE_EQ(geometry.frame.fit_margin_m, 0.003);
  // A panel keeps the width its widget asks for.
  EXPECT_FLOAT_EQ(geometry.nominal_width_meters, geometry.width_meters);
  EXPECT_EQ(geometry.narrowing, qar_qt_streaming::FrameNarrowing::None);
}

TEST(PanelGeometryFrame, TheOtherPanelsOfTheLogs)
{
  struct Case
  {
    std::uint32_t w, h;
    float width_m, video_w_m, video_h_m, panel_m;
  };
  // Views (475 x 266 px, buffer 476 x 266), the Displays copy (480 x 400 px) and a window of
  // 500 x 988 px: each panel is as wide as its widget asks (1.2 mm per px), its video is 0.006 m
  // narrower for the border, and the panel is that video's height plus the 0.069 m of the title
  // bar and the borders above and below and the 0.003 m fit margin tall.
  const Case cases[] = {
    {475, 266, 0.570f, 0.564f, 0.315176f, 0.387176f},
    {480, 400, 0.576f, 0.570f, 0.475000f, 0.547000f},
    {500, 988, 0.600f, 0.594f, 1.173744f, 1.245744f},
  };
  for (const Case & c : cases) {
    PanelGeometry geometry;
    std::string error;
    ASSERT_TRUE(compute_panel_geometry(make_input(c.w, c.h, 1.0), geometry, error)) << error;
    EXPECT_NEAR(geometry.width_meters, c.width_m, 1e-6f) << c.w << "x" << c.h;
    EXPECT_NEAR(geometry.video_width_meters, c.video_w_m, 1e-6f) << c.w << "x" << c.h;
    EXPECT_NEAR(geometry.video_height_meters, c.video_h_m, 1e-6f) << c.w << "x" << c.h;
    EXPECT_NEAR(geometry.height_meters, c.panel_m, 1e-6f) << c.w << "x" << c.h;
  }
}

TEST(PanelGeometryFrame, NoFrameIsTheVideoAloneAndOnlyTheMetresChange)
{
  PanelGeometryInput with = make_input(475, 421, 1.0);
  with.frame = PanelFrame{0.063, 0.0, 0.0, 0.030, 0.003};
  PanelGeometryInput without = with;
  without.frame = no_frame();

  PanelGeometry a;
  PanelGeometry b;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(with, a, error)) << error;
  ASSERT_TRUE(compute_panel_geometry(without, b, error)) << error;

  // No frame: the panel is exactly as large as its video (the buffer-based 0.50534 m).
  EXPECT_FLOAT_EQ(b.video_width_meters, b.width_meters);
  EXPECT_FLOAT_EQ(b.height_meters, b.video_height_meters);
  EXPECT_NEAR(b.height_meters, 0.505336f, 1e-5f);
  // The frame changes the height by its vertical part and the fit margin less the share of the
  // video that its horizontal part takes from the width, and touches nothing else: not the width
  // of the panel, not a buffer or content pixel, not the zoom that is sent. The mapping of a
  // reported contact uses the content size and never the metres.
  EXPECT_NEAR(
    a.height_meters - b.height_meters, 0.063f + 0.003f - 0.030f * 422.0f / 476.0f, 1e-6f);
  EXPECT_FLOAT_EQ(a.width_meters, b.width_meters);
  EXPECT_EQ(a.buffer_width, b.buffer_width);
  EXPECT_EQ(a.buffer_height, b.buffer_height);
  EXPECT_EQ(a.content_width, b.content_width);
  EXPECT_EQ(a.content_height, b.content_height);
  EXPECT_FLOAT_EQ(a.content_scale, b.content_scale);
  EXPECT_DOUBLE_EQ(a.effective_pixel_ratio, b.effective_pixel_ratio);
}

TEST(PanelGeometryFrame, AStatedFrameIsUsedAsStated)
{
  PanelGeometryInput input = make_input(500, 250, 1.0);  // 0.60 m wide, a buffer of 2:1
  input.frame = PanelFrame{0.05, 0.02, 0.01, 0.04, 0.005};
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;
  EXPECT_NEAR(geometry.width_meters, 0.60f, 1e-6f);
  EXPECT_NEAR(geometry.video_width_meters, 0.55f, 1e-6f);   // 0.60 - (0.01 + 0.04)
  EXPECT_NEAR(geometry.video_height_meters, 0.275f, 1e-6f);
  EXPECT_NEAR(geometry.height_meters, 0.35f, 1e-6f);        // 0.275 + (0.05 + 0.02) + 0.005
  EXPECT_DOUBLE_EQ(geometry.frame.top_m, 0.05);
  EXPECT_DOUBLE_EQ(geometry.frame.bottom_m, 0.02);
  EXPECT_DOUBLE_EQ(geometry.frame.left_m, 0.01);
  EXPECT_DOUBLE_EQ(geometry.frame.right_m, 0.04);
  EXPECT_DOUBLE_EQ(geometry.frame.fit_margin_m, 0.005);
}

TEST(PanelGeometryFrame, EachInsetHasItsOwnEffectOnThePanel)
{
  // 500 x 250 px: 0.60 m wide, a buffer of 2:1, so a video 0.60 m wide is 0.30 m tall.
  struct Case
  {
    const char * name;
    PanelFrame frame;
    float video_w_m, video_h_m, panel_h_m;
  };
  const Case cases[] = {
    {"none", no_frame(), 0.60f, 0.30f, 0.30f},
    {"top", PanelFrame{0.01, 0.0, 0.0, 0.0, 0.0}, 0.60f, 0.30f, 0.31f},
    {"bottom", PanelFrame{0.0, 0.02, 0.0, 0.0, 0.0}, 0.60f, 0.30f, 0.32f},
    {"left", PanelFrame{0.0, 0.0, 0.04, 0.0, 0.0}, 0.56f, 0.28f, 0.28f},
    {"right", PanelFrame{0.0, 0.0, 0.0, 0.08, 0.0}, 0.52f, 0.26f, 0.26f},
    {"fit margin", PanelFrame{0.0, 0.0, 0.0, 0.0, 0.004}, 0.60f, 0.30f, 0.304f},
  };
  for (const Case & c : cases) {
    PanelGeometryInput input = make_input(500, 250, 1.0);
    input.frame = c.frame;
    PanelGeometry geometry;
    std::string error;
    ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;
    EXPECT_NEAR(geometry.width_meters, 0.60f, 1e-6f) << c.name;
    EXPECT_NEAR(geometry.video_width_meters, c.video_w_m, 1e-6f) << c.name;
    EXPECT_NEAR(geometry.video_height_meters, c.video_h_m, 1e-6f) << c.name;
    EXPECT_NEAR(geometry.height_meters, c.panel_h_m, 1e-6f) << c.name;
  }
}

TEST(PanelGeometryFrame, AFrameThatLeavesNoWidthIsNotApplied)
{
  // 20 x 100 px: 0.024 m wide, which a strip of 0.030 m at the right would take entirely. The
  // panel is then its video alone, 0.024 m wide and five times that tall, and the fit margin is
  // dropped with the rest of the frame.
  PanelGeometryInput input = make_input(20, 100, 1.0);
  input.frame = PanelFrame{0.063, 0.0, 0.0, 0.030, 0.003};
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;
  EXPECT_NEAR(geometry.width_meters, 0.024f, 1e-6f);
  EXPECT_NEAR(geometry.video_width_meters, 0.024f, 1e-6f);
  EXPECT_NEAR(geometry.video_height_meters, 0.120f, 1e-6f);
  EXPECT_NEAR(geometry.height_meters, 0.120f, 1e-6f);
  EXPECT_DOUBLE_EQ(geometry.frame.vertical_m(), 0.0);
  EXPECT_DOUBLE_EQ(geometry.frame.horizontal_m(), 0.0);
  EXPECT_DOUBLE_EQ(geometry.frame.fit_margin_m, 0.0);
}

TEST(PanelGeometryDialog, TheAddDialogKeepsTheHeightOfItsVideoAndIsFittedToTheFrame)
{
  // The real numbers of the Add dialog: 500 x 988 px in a buffer of the same size, 0.600 m wide
  // at 1.2 mm per px, so its video is 1.1856 m tall. The area inside the frame, less the fit
  // margin, is 1.1856 - 0.069 - 0.003 = 1.1136 m tall, a video fills it at 1.1136 / 1.976 =
  // 0.563563 m, the panel is that wide plus the 0.006 m of the borders, 0.569563 m, and the fit
  // margin is what the video leaves of the 1.1856 - 0.069 m under the title bar and above the
  // bottom border.
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(
    compute_panel_geometry(make_dialog_input(500, 988, PanelFrame{}), geometry, error)) << error;

  EXPECT_NEAR(geometry.nominal_width_meters, 0.600f, 1e-6f);
  EXPECT_NEAR(geometry.width_meters, 0.569563f, 1e-6f);
  EXPECT_NEAR(geometry.height_meters, 1.1856f, 1e-6f);
  EXPECT_NEAR(geometry.video_width_meters, 0.563563f, 1e-6f);
  EXPECT_NEAR(geometry.video_height_meters, 1.1136f, 1e-6f);
  EXPECT_EQ(geometry.narrowing, FrameNarrowing::Reduced);
  // The parts add up, as they do for a panel, and the area inside the frame, less the fit margin,
  // has the aspect ratio of the buffer.
  EXPECT_NEAR(
    geometry.height_meters,
    geometry.video_height_meters + geometry.frame.vertical_m() + geometry.frame.fit_margin_m,
    1e-6f);
  EXPECT_NEAR(
    geometry.width_meters, geometry.video_width_meters + geometry.frame.horizontal_m(), 1e-6f);
  EXPECT_NEAR(
    geometry.video_width_meters /
    (geometry.height_meters - geometry.frame.vertical_m() - geometry.frame.fit_margin_m),
    500.0f / 988.0f, 1e-5f);
  // The buffer and the content are those of the widget.
  EXPECT_EQ(geometry.buffer_width, 500u);
  EXPECT_EQ(geometry.buffer_height, 988u);
  EXPECT_EQ(geometry.content_width, 500u);
  EXPECT_EQ(geometry.content_height, 988u);
}

TEST(PanelGeometryDialog, EachInsetHasItsOwnEffectOnTheWidth)
{
  // 500 x 250 px: 0.60 m wide, a buffer of 2:1, so a video of 0.30 m at that width. The height of
  // the dialog stays 0.30 m; its width is fh + (0.30 - fv - m) * 2, but never more than 0.60 m, so
  // a horizontal inset on its own (which would widen the dialog) leaves the width alone.
  struct Case
  {
    const char * name;
    PanelFrame frame;
    float width_m;
    FrameNarrowing narrowing;
  };
  const Case cases[] = {
    {"none", no_frame(), 0.60f, FrameNarrowing::Reduced},
    {"top", PanelFrame{0.05, 0.0, 0.0, 0.0, 0.0}, 0.50f, FrameNarrowing::Reduced},
    {"bottom", PanelFrame{0.0, 0.05, 0.0, 0.0, 0.0}, 0.50f, FrameNarrowing::Reduced},
    {"left", PanelFrame{0.0, 0.0, 0.01, 0.0, 0.0}, 0.60f, FrameNarrowing::None},
    {"right", PanelFrame{0.0, 0.0, 0.0, 0.01, 0.0}, 0.60f, FrameNarrowing::None},
    {"top and right", PanelFrame{0.05, 0.0, 0.0, 0.02, 0.0}, 0.52f, FrameNarrowing::Reduced},
    {"all four", PanelFrame{0.04, 0.01, 0.005, 0.015, 0.0}, 0.52f, FrameNarrowing::Reduced},
    {"fit margin", PanelFrame{0.0, 0.0, 0.0, 0.0, 0.01}, 0.58f, FrameNarrowing::Reduced},
    {"top and fit margin", PanelFrame{0.05, 0.0, 0.0, 0.0, 0.01}, 0.48f, FrameNarrowing::Reduced},
    {"the default frame", PanelFrame{}, 0.462f, FrameNarrowing::Reduced},
  };
  for (const Case & c : cases) {
    PanelGeometry geometry;
    std::string error;
    ASSERT_TRUE(compute_panel_geometry(make_dialog_input(500, 250, c.frame), geometry, error))
      << c.name << ": " << error;
    EXPECT_EQ(geometry.narrowing, c.narrowing) << c.name;
    EXPECT_NEAR(geometry.width_meters, c.width_m, 1e-5f) << c.name;
    EXPECT_NEAR(geometry.height_meters, 0.30f, 1e-6f) << c.name;
    const float video_w_m = c.width_m - static_cast<float>(c.frame.horizontal_m());
    EXPECT_NEAR(geometry.video_width_meters, video_w_m, 1e-5f) << c.name;
    EXPECT_NEAR(geometry.video_height_meters, 0.5f * video_w_m, 1e-5f) << c.name;
  }
}

TEST(PanelGeometryDialog, TheHeightIsTheOneOfTheVideoAloneWhateverTheFrameIs)
{
  PanelGeometry alone;
  std::string error;
  PanelGeometryInput none = make_input(500, 988, 1.0);
  none.frame = no_frame();
  ASSERT_TRUE(compute_panel_geometry(none, alone, error)) << error;
  const PanelFrame frames[] = {
    no_frame(), top_frame(0.03), PanelFrame{}, PanelFrame{0.2, 0.0, 0.0, 0.05, 0.0},
    PanelFrame{0.1, 0.1, 0.02, 0.02, 0.0}, top_frame(0.9), top_frame(2.0)};
  for (const PanelFrame & frame : frames) {
    PanelGeometry geometry;
    ASSERT_TRUE(compute_panel_geometry(make_dialog_input(500, 988, frame), geometry, error))
      << error;
    EXPECT_FLOAT_EQ(geometry.height_meters, alone.height_meters) << frame.top_m;
    EXPECT_LE(geometry.width_meters, geometry.nominal_width_meters + 1e-6f) << frame.top_m;
    EXPECT_GE(geometry.width_meters, 0.5f * geometry.nominal_width_meters - 1e-6f) << frame.top_m;
    if (geometry.narrowing == FrameNarrowing::Reduced) {
      EXPECT_NEAR(
        geometry.height_meters,
        geometry.video_height_meters + geometry.frame.vertical_m() + geometry.frame.fit_margin_m,
        1e-6f) << frame.top_m;
    }
  }
}

TEST(PanelGeometryDialog, AFrameOfZeroLeavesTheWidthAlone)
{
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(
    compute_panel_geometry(make_dialog_input(500, 988, no_frame()), geometry, error)) << error;
  EXPECT_FLOAT_EQ(geometry.width_meters, geometry.nominal_width_meters);
  EXPECT_NEAR(geometry.height_meters, 1.1856f, 1e-5f);
  EXPECT_FLOAT_EQ(geometry.video_height_meters, geometry.height_meters);
  EXPECT_EQ(geometry.narrowing, FrameNarrowing::Reduced);
}

TEST(PanelGeometryDialog, TheWidthIsNeverBelowHalfOfTheNominalOne)
{
  // 500 x 200 px: 0.600 m wide, a video of 0.24 m. A title bar of 0.20 m would leave 0.04 m of
  // it, which a video of this shape fills at a sixth of the width; the width stops at half,
  // 0.30 m, where the video is 0.12 m tall.
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(
    compute_panel_geometry(make_dialog_input(500, 200, top_frame(0.20)), geometry, error))
    << error;
  EXPECT_EQ(geometry.narrowing, FrameNarrowing::Limited);
  EXPECT_NEAR(geometry.nominal_width_meters, 0.60f, 1e-6f);
  EXPECT_NEAR(geometry.width_meters, 0.30f, 1e-6f);
  EXPECT_NEAR(geometry.height_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(geometry.video_width_meters, 0.30f, 1e-6f);
  EXPECT_NEAR(geometry.video_height_meters, 0.12f, 1e-6f);
}

TEST(PanelGeometryDialog, ExactlyHalfIsAReductionAndNotALimit)
{
  // 240 x 100 px: 0.288 m wide, a video of 0.12 m; a title bar of 0.06 m leaves exactly half of
  // it, so the width is half of the nominal one by the rule itself, and a rounding error must
  // not turn that into a limit.
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(
    compute_panel_geometry(make_dialog_input(240, 100, top_frame(0.06)), geometry, error))
    << error;
  EXPECT_EQ(geometry.narrowing, FrameNarrowing::Reduced);
  EXPECT_NEAR(geometry.width_meters, 0.144f, 1e-6f);
  EXPECT_NEAR(geometry.height_meters, 0.12f, 1e-6f);
  EXPECT_NEAR(geometry.video_height_meters, 0.06f, 1e-6f);
}

TEST(PanelGeometryDialog, AFrameWhoseVerticalPartAndMarginAreNotBelowTheVideoHeightIsNotApplied)
{
  // 500 x 100 px: a video of 0.12 m. A vertical part of 0.12 m or more cannot be taken from it,
  // whichever insets it is made of, and neither can a vertical part and a fit margin that add up
  // to that or more.
  const PanelFrame frames[] = {
    top_frame(0.12), top_frame(0.2), top_frame(1.0), PanelFrame{0.05, 0.07, 0.0, 0.03, 0.0},
    PanelFrame{0.0, 0.5, 0.01, 0.01, 0.0}, PanelFrame{0.115, 0.0, 0.0, 0.0, 0.01},
    PanelFrame{0.0, 0.0, 0.0, 0.0, 0.2}};
  for (const PanelFrame & frame : frames) {
    PanelGeometry geometry;
    std::string error;
    ASSERT_TRUE(compute_panel_geometry(make_dialog_input(500, 100, frame), geometry, error))
      << error;
    EXPECT_EQ(geometry.narrowing, FrameNarrowing::Skipped) << frame.vertical_m();
    EXPECT_FLOAT_EQ(geometry.width_meters, geometry.nominal_width_meters) << frame.vertical_m();
    EXPECT_NEAR(geometry.width_meters, 0.60f, 1e-6f) << frame.vertical_m();
    EXPECT_NEAR(geometry.height_meters, 0.12f, 1e-6f) << frame.vertical_m();
    EXPECT_NEAR(geometry.video_width_meters, 0.60f, 1e-6f) << frame.vertical_m();
    EXPECT_NEAR(geometry.video_height_meters, 0.12f, 1e-6f) << frame.vertical_m();
    EXPECT_DOUBLE_EQ(geometry.frame.vertical_m(), 0.0) << frame.vertical_m();
    EXPECT_DOUBLE_EQ(geometry.frame.horizontal_m(), 0.0) << frame.vertical_m();
    EXPECT_DOUBLE_EQ(geometry.frame.fit_margin_m, 0.0) << frame.vertical_m();
  }
}

TEST(PanelGeometryDialog, AFrameJustBelowTheVideoHeightWithoutTheMarginIsApplied)
{
  // 500 x 100 px: a video of 0.12 m. A title bar of 0.115 m is below it and is applied, and the
  // width is limited to half; the same title bar with a fit margin of 0.01 m is not.
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(
    compute_panel_geometry(make_dialog_input(500, 100, top_frame(0.115)), geometry, error))
    << error;
  EXPECT_EQ(geometry.narrowing, FrameNarrowing::Limited);
  EXPECT_NEAR(geometry.width_meters, 0.30f, 1e-6f);
  EXPECT_DOUBLE_EQ(geometry.frame.top_m, 0.115);
}

TEST(PanelGeometryDialog, AFrameThatWouldWidenTheDialogLeavesItsWidthAlone)
{
  // 400 x 1000 px: 0.48 m wide, a buffer of 2.5:1, a video of 1.2 m. A frame with a strip of
  // 0.030 m at the right asks for 0.030 + (1.2 - 0.063 - 0.003) / 2.5 = 0.4836 m, more than the
  // widget asks for: the width stays 0.48 m and the video, 0.45 m wide and 1.125 m tall, is a
  // little shorter than the 1.137 m under the title bar.
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(
    compute_panel_geometry(
      make_dialog_input(400, 1000, PanelFrame{0.063, 0.0, 0.0, 0.030, 0.003}), geometry, error))
    << error;
  EXPECT_EQ(geometry.narrowing, FrameNarrowing::None);
  EXPECT_NEAR(geometry.width_meters, 0.48f, 1e-6f);
  EXPECT_NEAR(geometry.height_meters, 1.2f, 1e-6f);
  EXPECT_NEAR(geometry.video_width_meters, 0.45f, 1e-6f);
  EXPECT_NEAR(geometry.video_height_meters, 1.125f, 1e-5f);
  EXPECT_GT(geometry.height_meters - geometry.frame.vertical_m(), geometry.video_height_meters);
}

TEST(PanelGeometryDialog, TheFittedWidthFollowsLogicalPixelsAndNotTheDisplayRatio)
{
  PanelGeometry at_one;
  PanelGeometry at_two;
  std::string error;
  PanelGeometryInput one = make_dialog_input(475, 421, PanelFrame{});
  PanelGeometryInput two = one;
  two.device_pixel_ratio = 2.0;
  ASSERT_TRUE(compute_panel_geometry(one, at_one, error)) << error;
  ASSERT_TRUE(compute_panel_geometry(two, at_two, error)) << error;
  EXPECT_GT(at_two.buffer_height, at_one.buffer_height);
  EXPECT_FLOAT_EQ(at_two.nominal_width_meters, at_one.nominal_width_meters);
  EXPECT_NEAR(at_two.width_meters, at_one.width_meters, 1e-3f);
  EXPECT_NEAR(at_two.height_meters, at_one.height_meters, 1e-3f);
  // The area inside the frame, less the fit margin, has the aspect ratio of the buffer (here 476
  // x 422 px).
  EXPECT_NEAR(
    at_one.video_width_meters /
    (at_one.height_meters - at_one.frame.vertical_m() - at_one.frame.fit_margin_m),
    476.0f / 422.0f, 1e-4f);
}

TEST(PanelGeometryDialog, ANonsenseFrameIsRejectedForADialogToo)
{
  PanelGeometry geometry;
  std::string error;
  EXPECT_FALSE(
    compute_panel_geometry(make_dialog_input(400, 600, top_frame(-0.01)), geometry, error));
  EXPECT_FALSE(
    compute_panel_geometry(make_dialog_input(400, 600, top_frame(std::nan(""))), geometry, error));
  EXPECT_TRUE(
    compute_panel_geometry(make_dialog_input(400, 600, no_frame()), geometry, error)) << error;
}

TEST(PanelGeometryFrame, TheBufferOfAClampedWidgetIsWhatTheVideoIs)
{
  // 4000 x 1000 px at ratio 2 is clamped to a 4096 px buffer: the video QarOS draws is that
  // buffer, so its aspect ratio is the buffer's (4096 x 1024 here), not the widget's, and its
  // width is the panel's less the frame's.
  PanelGeometryInput input = make_input(4000, 1000, 2.0);
  input.max_buffer_dimension = 4096;
  input.frame = PanelFrame{0.063, 0.0, 0.0, 0.030, 0.003};
  PanelGeometry geometry;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(input, geometry, error)) << error;
  const double video_width = 4000 * 0.0012 - 0.030;
  const double expected = video_width * static_cast<double>(geometry.buffer_height) /
    static_cast<double>(geometry.buffer_width);
  EXPECT_NEAR(geometry.video_height_meters, static_cast<float>(expected), 1e-4f);
  EXPECT_NEAR(geometry.video_width_meters, static_cast<float>(video_width), 1e-4f);
  EXPECT_NEAR(geometry.width_meters, static_cast<float>(4000 * 0.0012), 1e-4f);
}

TEST(PanelGeometryFrame, TheVideoHeightDoesNotDependOnTheDisplayRatioBeyondRounding)
{
  PanelGeometry at_one;
  PanelGeometry at_two;
  std::string error;
  ASSERT_TRUE(compute_panel_geometry(make_input(475, 421, 1.0), at_one, error)) << error;
  ASSERT_TRUE(compute_panel_geometry(make_input(475, 421, 2.0), at_two, error)) << error;
  EXPECT_GT(at_two.buffer_height, at_one.buffer_height);
  // A HiDPI display makes the panel sharper, not bigger: the metres agree to the rounding of the
  // buffer (here under a millimetre).
  EXPECT_NEAR(at_two.video_height_meters, at_one.video_height_meters, 1e-3f);
  EXPECT_NEAR(at_two.height_meters, at_one.height_meters, 1e-3f);
  EXPECT_FLOAT_EQ(at_two.width_meters, at_one.width_meters);
  EXPECT_FLOAT_EQ(at_two.video_width_meters, at_one.video_width_meters);
}

TEST(PanelGeometryFrame, RejectsANonsenseFrame)
{
  // Every inset and the fit margin on its own: negative, not a number and infinite are refused,
  // 0 is not.
  const double bad[] = {-0.01, std::nan(""), std::numeric_limits<double>::infinity()};
  for (int inset = 0; inset < 5; ++inset) {
    for (const double value : bad) {
      PanelFrame frame = no_frame();
      (inset == 0 ? frame.top_m : inset == 1 ? frame.bottom_m : inset == 2 ? frame.left_m :
        inset == 3 ? frame.right_m : frame.fit_margin_m) = value;
      PanelGeometry geometry;
      std::string error;
      PanelGeometryInput input = make_input(400, 600, 1.0);
      input.frame = frame;
      EXPECT_FALSE(compute_panel_geometry(input, geometry, error)) << inset << " " << value;
      EXPECT_FALSE(error.empty()) << inset << " " << value;
    }
  }
  PanelGeometry geometry;
  std::string error;
  PanelGeometryInput input = make_input(400, 600, 1.0);
  input.frame = no_frame();
  EXPECT_TRUE(compute_panel_geometry(input, geometry, error)) << error;
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
