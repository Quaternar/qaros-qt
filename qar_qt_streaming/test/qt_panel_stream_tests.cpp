// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The per-panel capture/submit cycle and the manager's bookkeeping, driven
// against real widgets (offscreen platform) and the recording
// MockQarPanelSenderAdapter.

#include <gtest/gtest.h>

#include "qar_qt_streaming/offscreen_panel.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QPalette>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using qar_ar_core::PanelPointerSample;
using qar_ar_core::PointerGesture;
using qar_ar_core::PointerPhase;
using qar_qt_streaming::PanelFrame;
using qar_qt_streaming::PanelPointerEvent;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStream;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_ar_core::Status;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;
using qar_qt_streaming::test_support::RecordingPanelInputSink;

namespace
{

class Fixture
{
public:
  Fixture()
  {
    widget_ = new QWidget();
    auto * layout = new QVBoxLayout(widget_);
    // A solid colour block, NOT a text label: the offscreen platform plugin
    // rasterizes no glyphs on this host, so a text change would not alter a
    // single pixel and every dirty-check assertion would be vacuous. A palette
    // fill exercises exactly the same render() path without a font engine.
    block_ = new QWidget(widget_);
    block_->setAutoFillBackground(true);
    set_block_colour(Qt::red);
    layout->addWidget(block_);
    widget_->resize(200, 120);
    // show() is required, not cosmetic: QWidget::render() draws only children
    // that are isVisible(), and visibility is inherited. Under the offscreen
    // platform plugin this creates no window — nothing appears on any desktop.
    widget_->show();
    widget_->ensurePolished();
    widget_->layout()->activate();
    QCoreApplication::processEvents();
  }

  ~Fixture() { delete widget_; }

  QWidget * widget() const { return widget_; }

  /// Change the rendered content, so the next capture must come out dirty.
  void mutate() { set_block_colour(Qt::blue); }

private:
  void set_block_colour(Qt::GlobalColor colour)
  {
    QPalette palette = block_->palette();
    palette.setColor(QPalette::Window, colour);
    block_->setPalette(palette);
  }

  QWidget * widget_ = nullptr;
  QWidget * block_ = nullptr;
};

PanelStreamOptions
options_no_pacing()
{
  PanelStreamOptions options;
  options.display_name = "Test Panel";
  options.max_fps = 0;          // no pacing: every tick captures
  options.keepalive_seconds = 0.0;  // isolate the dirty check from the keepalive
  return options;
}

}  // namespace

TEST(PanelStream, FirstTickCreatesTheSenderAndSubmitsAFrame)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  EXPECT_TRUE(stream.tick(0.0));
  EXPECT_EQ(factory->create_calls(), 1u);
  ASSERT_NE(factory->latest(), nullptr);
  EXPECT_EQ(factory->latest()->submit_calls(), 1u);
  EXPECT_EQ(stream.diagnostics().frames_submitted, 1u);
}

TEST(PanelStream, TheSenderIsToldTheDerivedBufferSizeAndPhysicalSize)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  const auto & desc = factory->latest()->desc();
  EXPECT_EQ(desc.buffer_width, 200u);
  EXPECT_EQ(desc.buffer_height, 120u);
  EXPECT_EQ(desc.display_name, "Test Panel");
  // 200 logical px * kDefaultMetersPerLogicalPx (0.0012 m/px).
  EXPECT_NEAR(desc.placement.size.width_meters, 0.24f, 1e-4f);
}

TEST(PanelStreamHeight, TheSenderIsToldTheVideoInsideTheFrameAndTheWidthIsUnchanged)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  // 200 x 120 px: a buffer of 200 x 120 and a panel 0.24 m wide. The default frame leaves the
  // video the width inside its two borders of 0.003 m, 0.234 m, which is 0.234 * 120 / 200 =
  // 0.1404 m tall; the title bar and the border above it, 0.066 m, are on top of it, and the
  // border of 0.003 m and the fit margin of 0.003 m below it: 0.2124 m.
  const auto & desc = factory->latest()->desc();
  EXPECT_NEAR(desc.placement.size.width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(desc.placement.size.height_meters, 0.2124f, 1e-6f);
  EXPECT_NEAR(stream.placement().size.height_meters, 0.2124f, 1e-6f);
  EXPECT_NEAR(stream.diagnostics().width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(stream.diagnostics().height_meters, 0.2124f, 1e-6f);
  EXPECT_FLOAT_EQ(desc.placement.size.content_scale, 1.0f);
  // The pixels the adapter normalises a reported contact against do not know the frame.
  EXPECT_EQ(desc.content_width, 200u);
  EXPECT_EQ(desc.content_height, 120u);
}

TEST(PanelStreamHeight, NoFrameSendsTheVideoAlone)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.ar_frame = PanelFrame{0.0, 0.0, 0.0, 0.0, 0.0};
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  const auto & desc = factory->latest()->desc();
  EXPECT_NEAR(desc.placement.size.width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(desc.placement.size.height_meters, 0.144f, 1e-6f);
  EXPECT_EQ(desc.buffer_width, 200u);
  EXPECT_EQ(desc.buffer_height, 120u);
}

TEST(PanelStreamHeight, TheSizeLineNamesTheVideoTheFourInsetsAndTheFitMarginOfTheFrame)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  const std::string line = stream.diagnostics().last_geometry_summary;
  EXPECT_NE(line.find("content 200x120 px, buffer 200x120 px, 0.240000x0.212400 m"),
    std::string::npos) << line;
  const std::string tail =
    " (video 0.234000 x 0.140400 m inside a frame of top 0.066000, bottom 0.003000, "
    "left 0.003000, right 0.003000 m, fit margin 0.003000 m)";
  ASSERT_GE(line.size(), tail.size()) << line;
  EXPECT_EQ(line.substr(line.size() - tail.size()), tail) << line;
}

TEST(PanelStreamHeight, AStatedFrameIsInThePanelAndInTheSizeLineWithAllFourInsetsAndTheMargin)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.ar_frame = PanelFrame{0.05, 0.02, 0.01, 0.04, 0.005};
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  // The video is 0.24 - 0.05 = 0.19 m wide and 0.114 m tall; the vertical part of the frame is
  // 0.07 m and the fit margin 0.005 m.
  const auto & desc = factory->latest()->desc();
  EXPECT_NEAR(desc.placement.size.width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(desc.placement.size.height_meters, 0.189f, 1e-5f);
  const std::string line = stream.diagnostics().last_geometry_summary;
  const std::string tail =
    " (video 0.190000 x 0.114000 m inside a frame of top 0.050000, bottom 0.020000, "
    "left 0.010000, right 0.040000 m, fit margin 0.005000 m)";
  ASSERT_GE(line.size(), tail.size()) << line;
  EXPECT_EQ(line.substr(line.size() - tail.size()), tail) << line;
}

TEST(PanelStreamHeight, TheDialogPanelKeepsTheVideoHeightAndIsFittedToTheFrame)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.dialog_panel = true;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  const float predicted = qar_qt_streaming::ar_panel_height_m(*fixture.widget(), options);
  QtPanelStream stream("dialog:Add", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  // 200 x 120 px: 0.24 m wide, a video of 0.144 m at that width. The area inside the default
  // frame, less the fit margin, is 0.144 - 0.069 - 0.003 = 0.072 m tall, which a video of this
  // shape fills at 0.072 / 0.6 = 0.12 m, so the panel is 0.12 + 0.006 = 0.126 m wide and keeps
  // the 0.144 m of height of its video.
  const auto & desc = factory->latest()->desc();
  EXPECT_NEAR(desc.placement.size.width_meters, 0.126f, 1e-6f);
  EXPECT_NEAR(desc.placement.size.height_meters, 0.144f, 1e-6f);
  EXPECT_FLOAT_EQ(predicted, desc.placement.size.height_meters);
  EXPECT_NEAR(stream.diagnostics().width_meters, 0.126f, 1e-6f);
  // The buffer, the content and the zoom are those of the widget.
  EXPECT_EQ(desc.buffer_width, 200u);
  EXPECT_EQ(desc.buffer_height, 120u);
  EXPECT_EQ(desc.content_width, 200u);
  EXPECT_EQ(desc.content_height, 120u);
  EXPECT_FLOAT_EQ(desc.placement.size.content_scale, 1.0f);
  // The size line says which rule applied: the video, the frame, and the width before and after.
  const std::string line = stream.diagnostics().last_geometry_summary;
  const std::string tail =
    " (video 0.120000 x 0.072000 m inside a frame of top 0.066000, bottom 0.003000, "
    "left 0.003000, right 0.003000 m, fit margin 0.003000 m "
    "(a dialog: width 0.240000 -> 0.126000 m for the frame))";
  ASSERT_GE(line.size(), tail.size()) << line;
  EXPECT_EQ(line.substr(line.size() - tail.size()), tail) << line;
  EXPECT_NE(line.find("0.126000x0.144000 m"), std::string::npos) << line;
}

TEST(PanelStreamHeight, ADialogPanelWhoseFrameIsNotBelowItsVideoIsNotFramedAndSaysSo)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.ar_frame = PanelFrame{0.30, 0.0, 0.0, 0.0, 0.0};  // more than the 0.144 m of its video
  options.dialog_panel = true;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("dialog:Add", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  const auto & desc = factory->latest()->desc();
  EXPECT_NEAR(desc.placement.size.width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(desc.placement.size.height_meters, 0.144f, 1e-6f);
  const std::string line = stream.diagnostics().last_geometry_summary;
  const std::string tail =
    " (video 0.240000 x 0.144000 m inside a frame of top 0.000000, bottom 0.000000, "
    "left 0.000000, right 0.000000 m, fit margin 0.000000 m "
    "(a dialog: width 0.240000 -> 0.240000 m for the frame, "
    "not applied because the frame and its fit margin are not below the video height))";
  ASSERT_GE(line.size(), tail.size()) << line;
  EXPECT_EQ(line.substr(line.size() - tail.size()), tail) << line;
}

TEST(PanelStreamHeight, ADialogPanelWhoseFrameIsBelowItsVideoOnlyWithoutTheMarginIsNotFramed)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  // 0.14 m is below the 0.144 m of the video of the dialog, 0.14 + 0.01 m is not.
  options.ar_frame = PanelFrame{0.14, 0.0, 0.0, 0.0, 0.01};
  options.dialog_panel = true;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("dialog:Add", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  const auto & desc = factory->latest()->desc();
  EXPECT_NEAR(desc.placement.size.width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(desc.placement.size.height_meters, 0.144f, 1e-6f);
  const std::string line = stream.diagnostics().last_geometry_summary;
  const std::string tail =
    " (video 0.240000 x 0.144000 m inside a frame of top 0.000000, bottom 0.000000, "
    "left 0.000000, right 0.000000 m, fit margin 0.000000 m "
    "(a dialog: width 0.240000 -> 0.240000 m for the frame, "
    "not applied because the frame and its fit margin are not below the video height))";
  ASSERT_GE(line.size(), tail.size()) << line;
  EXPECT_EQ(line.substr(line.size() - tail.size()), tail) << line;
}

TEST(PanelStreamHeight, ADialogPanelIsNeverNarrowerThanHalfAndSaysSo)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.ar_frame = PanelFrame{0.10, 0.0, 0.0, 0.0, 0.0};  // would leave 0.044 m of its 0.144 m
  options.dialog_panel = true;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("dialog:Add", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  const auto & desc = factory->latest()->desc();
  EXPECT_NEAR(desc.placement.size.width_meters, 0.12f, 1e-6f);
  EXPECT_NEAR(desc.placement.size.height_meters, 0.144f, 1e-6f);
  const std::string line = stream.diagnostics().last_geometry_summary;
  const std::string tail =
    " (video 0.120000 x 0.072000 m inside a frame of top 0.100000, bottom 0.000000, "
    "left 0.000000, right 0.000000 m, fit margin 0.000000 m "
    "(a dialog: width 0.240000 -> 0.120000 m for the frame, limited to half of the width))";
  ASSERT_GE(line.size(), tail.size()) << line;
  EXPECT_EQ(line.substr(line.size() - tail.size()), tail) << line;
}

TEST(PanelStreamHeight, ADialogPanelThatTheFrameWouldWidenKeepsItsWidthAndSaysSo)
{
  Fixture fixture;
  fixture.widget()->resize(200, 500);
  QCoreApplication::processEvents();
  PanelStreamOptions options = options_no_pacing();
  options.ar_frame = PanelFrame{0.063, 0.0, 0.0, 0.030, 0.003};
  options.dialog_panel = true;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("dialog:Add", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  // 200 x 500 px: 0.24 m wide, a video of 0.6 m. A frame with a strip of 0.030 m at the right
  // asks for 0.030 + (0.6 - 0.063 - 0.003) / 2.5 = 0.2436 m, more than the widget asks for, so
  // the width stays.
  const auto & desc = factory->latest()->desc();
  EXPECT_NEAR(desc.placement.size.width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(desc.placement.size.height_meters, 0.6f, 1e-6f);
  const std::string line = stream.diagnostics().last_geometry_summary;
  const std::string tail =
    " (video 0.210000 x 0.525000 m inside a frame of top 0.063000, bottom 0.000000, "
    "left 0.000000, right 0.030000 m, fit margin 0.003000 m "
    "(a dialog: width 0.240000 -> 0.240000 m for the frame, not made wider than the widget asks))";
  ASSERT_GE(line.size(), tail.size()) << line;
  EXPECT_EQ(line.substr(line.size() - tail.size()), tail) << line;
}

TEST(PanelStreamHeight, ThePanelThatIsNotADialogKeepsItsWidthAndAddsTheFrameAndMarginToItsHeight)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.ar_frame = PanelFrame{0.30, 0.0, 0.0, 0.0, 0.01};
  ASSERT_FALSE(options.dialog_panel);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("dock:Views", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_NEAR(factory->latest()->desc().placement.size.width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(factory->latest()->desc().placement.size.height_meters, 0.454f, 1e-5f);
  EXPECT_EQ(stream.diagnostics().last_geometry_summary.find("(a dialog"), std::string::npos);
}

TEST(PanelStreamHeight, TheHitPlacementIsTheVideoInsideTheFrame)
{
  Fixture fixture;
  PanelStreamOptions without = options_no_pacing();
  without.ar_frame = PanelFrame{0.0, 0.0, 0.0, 0.0, 0.0};
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream a("a", fixture.widget(), options_no_pacing(), factory);
  QtPanelStream b("b", fixture.widget(), without, factory);
  ASSERT_TRUE(a.tick(0.0));
  ASSERT_TRUE(b.tick(0.0));

  // The panel that is sent is 0.24 x 0.2124 m with the default frame and 0.24 x 0.144 m without...
  EXPECT_NEAR(a.placement().size.width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(a.placement().size.height_meters, 0.2124f, 1e-6f);
  EXPECT_NEAR(b.placement().size.height_meters, 0.144f, 1e-6f);
  // ...while the pointer hit tests work on the video, which fills the 200 x 120 px buffer: 0.234 x
  // 0.1404 m inside the frame with its borders of 0.003 m, and the whole panel without it.
  EXPECT_NEAR(a.hit_placement().size.width_meters, 0.234f, 1e-6f);
  EXPECT_NEAR(a.hit_placement().size.height_meters, 0.1404f, 1e-6f);
  EXPECT_NEAR(b.hit_placement().size.width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(b.hit_placement().size.height_meters, 0.144f, 1e-6f);
  // With the default frame the borders left and right are equal, so the middle of the video is the
  // middle of the panel, and its top edge is the 0.066 m of the title bar and the border and half
  // of the 0.003 m of the fit margin below the top edge of the panel: 0.0675 m.
  EXPECT_NEAR(a.hit_placement().pose.position.x, a.placement().pose.position.x, 1e-6f);
  EXPECT_NEAR(a.hit_placement().pose.position.y, a.placement().pose.position.y - 0.0675f, 1e-6f);
  EXPECT_FLOAT_EQ(a.hit_placement().pose.position.z, a.placement().pose.position.z);
  // The video ends the other half of the margin and the border of 0.003 m above the bottom edge
  // of the panel: 0.0045 m.
  EXPECT_NEAR(
    (a.hit_placement().pose.position.y - a.hit_placement().size.height_meters) -
    (a.placement().pose.position.y - a.placement().size.height_meters), 0.0045f, 1e-6f);
  // Without a frame the rectangle is the panel.
  EXPECT_FLOAT_EQ(b.hit_placement().pose.position.x, b.placement().pose.position.x);
  EXPECT_FLOAT_EQ(b.hit_placement().pose.position.y, b.placement().pose.position.y);
}

TEST(PanelStreamHeight, TheHitPlacementOfADialogIsItsVideoInsideTheFrameToo)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.dialog_panel = true;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("dialog:Add", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  // The panel that is sent is 0.126 x 0.144 m and its video, 0.12 m wide, is 0.072 m tall: the
  // frame is the 0.066 m above it and the border of 0.003 m under it, and the fit margin of
  // 0.003 m, half of it above the video and half under it.
  const qar_ar_core::PanelPlacement sent = stream.placement();
  const qar_ar_core::PanelPlacement hit = stream.hit_placement();
  EXPECT_NEAR(sent.size.width_meters, 0.126f, 1e-6f);
  EXPECT_NEAR(sent.size.height_meters, 0.144f, 1e-6f);
  EXPECT_NEAR(hit.size.width_meters, 0.12f, 1e-6f);
  EXPECT_NEAR(hit.size.height_meters, 0.072f, 1e-6f);
  EXPECT_NEAR(hit.pose.position.x, sent.pose.position.x, 1e-6f);
  EXPECT_NEAR(hit.pose.position.y, sent.pose.position.y - 0.0675f, 1e-6f);
  EXPECT_FLOAT_EQ(hit.pose.position.z, sent.pose.position.z);
  // The video ends half of the margin and the border above the bottom edge of the panel.
  EXPECT_NEAR(
    (hit.pose.position.y - hit.size.height_meters) -
    (sent.pose.position.y - sent.size.height_meters), 0.0045f, 1e-6f);
}

TEST(PanelStreamHeight, TheHitPlacementIsCentredUnderTheTitleBarWhenThePanelIsTallerThanItNeeds)
{
  Fixture fixture;
  fixture.widget()->resize(200, 500);
  QCoreApplication::processEvents();
  PanelStreamOptions options = options_no_pacing();
  options.ar_frame = PanelFrame{0.063, 0.0, 0.0, 0.030, 0.003};
  options.dialog_panel = true;  // the frame would widen it, so its width stays 0.24 m
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("dialog:Add", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  // The panel is 0.24 x 0.6 m; the video, 0.21 m wide, is 0.525 m tall, 0.012 m less than the
  // 0.537 m under the title bar. QarOS centres it there: 0.006 m of band above it and below it.
  const qar_ar_core::PanelPlacement sent = stream.placement();
  const qar_ar_core::PanelPlacement hit = stream.hit_placement();
  EXPECT_NEAR(sent.size.height_meters, 0.6f, 1e-6f);
  EXPECT_NEAR(hit.size.height_meters, 0.525f, 1e-5f);
  EXPECT_NEAR(hit.pose.position.y, sent.pose.position.y - (0.063f + 0.006f), 1e-5f);
  EXPECT_NEAR(
    hit.pose.position.y - hit.size.height_meters,
    sent.pose.position.y - sent.size.height_meters + 0.006f, 1e-5f);
}

TEST(PanelStreamHeight, TheHitPlacementStartsAtTheLeftInsetOfThePanel)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.ar_frame = PanelFrame{0.0, 0.0, 0.02, 0.01, 0.0};
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  // The video is 0.24 - 0.03 = 0.21 m wide and starts 0.02 m right of the left edge of the panel.
  const qar_ar_core::PanelPlacement sent = stream.placement();
  const qar_ar_core::PanelPlacement hit = stream.hit_placement();
  EXPECT_NEAR(hit.size.width_meters, 0.21f, 1e-6f);
  // The pose is the top centre and +X runs to the viewer's left, so the left edge of the
  // rectangle is its centre plus half of its width, and it is 0.02 m from the left edge of the
  // panel.
  EXPECT_NEAR(
    hit.pose.position.x + 0.5f * hit.size.width_meters,
    sent.pose.position.x + 0.5f * sent.size.width_meters - 0.02f, 1e-6f);
  EXPECT_NEAR(hit.pose.position.y, sent.pose.position.y, 1e-6f);
}

TEST(PanelStreamHeight, TheHitPlacementStartsAtTheLeftEdgeOfThePanelWhenThePaddingIsOnTheRight)
{
  Fixture fixture;
  fixture.widget()->resize(201, 121);
  QCoreApplication::processEvents();
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  // 201 x 121 px is a content of that size in a buffer of 202 x 122 px: QarOS draws the whole
  // buffer at the width the borders leave of the panel (0.2412 - 0.006 = 0.2352 m), the widget is
  // the top left part of it, so the widget covers 201/202 of the width of the video and 121/202
  // of that width as height.
  const auto & desc = factory->latest()->desc();
  ASSERT_EQ(desc.buffer_width, 202u);
  ASSERT_EQ(desc.buffer_height, 122u);
  ASSERT_EQ(desc.content_width, 201u);
  ASSERT_EQ(desc.content_height, 121u);
  const qar_ar_core::PanelPlacement sent = stream.placement();
  const qar_ar_core::PanelPlacement hit = stream.hit_placement();
  const float video_w_m = sent.size.width_meters - 0.006f;
  EXPECT_NEAR(hit.size.width_meters, video_w_m * 201.0f / 202.0f, 1e-6f);
  EXPECT_NEAR(hit.size.height_meters, video_w_m * 121.0f / 202.0f, 1e-6f);
  // The pose is the top centre and +X runs to the viewer's left, so the left edge of the rectangle
  // is its centre plus half of its width, and it is the border of 0.003 m from the left edge of
  // the panel.
  EXPECT_NEAR(
    hit.pose.position.x + 0.5f * hit.size.width_meters,
    sent.pose.position.x + 0.5f * sent.size.width_meters - 0.003f, 1e-6f);
  // The panel is as tall as its frame asks for, so the top edge is the title bar with its border
  // and half of the fit margin below the top.
  EXPECT_NEAR(hit.pose.position.y, sent.pose.position.y - 0.0675f, 1e-6f);
}

TEST(PanelStreamHeight, TheHitPlacementMovesAlongTheTiltedPanelsOwnAxes)
{
  // Turned a quarter turn about the room's Z axis: the panel's -Y (down the panel) is then the
  // room's +X and the panel's +X (to the viewer's left) is the room's +Y.
  const float half_sqrt = 0.70710678f;
  struct Case
  {
    const char * name;
    PanelFrame frame;
    float step_x_m;  ///< the room's X: down the panel, the top inset
    float step_y_m;  ///< the room's Y: to the viewer's left of the panel's middle
  };
  const Case cases[] = {
    {"a title bar of 0.20 m", PanelFrame{0.20, 0.0, 0.0, 0.0, 0.0}, 0.20f, 0.0f},
    {"a strip of 0.03 m on the right", PanelFrame{0.0, 0.0, 0.0, 0.03, 0.0}, 0.0f, 0.015f},
    {"a strip of 0.03 m on the left", PanelFrame{0.0, 0.0, 0.03, 0.0, 0.0}, 0.0f, -0.015f},
  };
  for (const Case & c : cases) {
    Fixture fixture;
    PanelStreamOptions options = options_no_pacing();
    options.ar_frame = c.frame;
    options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, half_sqrt, half_sqrt};
    options.placement.pose.position = QarVector3{1.0f, 2.0f, 3.0f};
    auto factory = std::make_shared<MockQarPanelSenderFactory>();
    QtPanelStream stream("p", fixture.widget(), options, factory);
    ASSERT_TRUE(stream.tick(0.0)) << c.name;

    const qar_ar_core::PanelPlacement hit = stream.hit_placement();
    EXPECT_NEAR(hit.pose.position.x, 1.0f + c.step_x_m, 1e-5f) << c.name;
    EXPECT_NEAR(hit.pose.position.y, 2.0f + c.step_y_m, 1e-5f) << c.name;
    EXPECT_NEAR(hit.pose.position.z, 3.0f, 1e-5f) << c.name;
    EXPECT_FLOAT_EQ(hit.pose.orientation.z, half_sqrt) << c.name;
    EXPECT_FLOAT_EQ(hit.pose.orientation.w, half_sqrt) << c.name;
  }
}

TEST(PanelStreamHeight, TheHitPlacementOfATiltedDialogMovesAlongItsOwnVerticalAxisToo)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.dialog_panel = true;
  // 0.144 m of video: a width of 0.24 * 0.104 / 0.144, with no horizontal part in the frame.
  options.ar_frame = PanelFrame{0.04, 0.0, 0.0, 0.0, 0.0};
  const float half_sqrt = 0.70710678f;
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, half_sqrt, half_sqrt};
  options.placement.pose.position = QarVector3{1.0f, 2.0f, 3.0f};
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("dialog:Add", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  // The 0.04 m title bar down the panel: along the room's +X after the quarter turn.
  const qar_ar_core::PanelPlacement hit = stream.hit_placement();
  EXPECT_NEAR(stream.placement().size.width_meters, 0.24f * 0.104f / 0.144f, 1e-5f);
  EXPECT_NEAR(hit.pose.position.x, 1.0f + 0.04f, 1e-5f);
  EXPECT_NEAR(hit.pose.position.y, 2.0f, 1e-5f);
  EXPECT_NEAR(hit.pose.position.z, 3.0f, 1e-5f);
}

TEST(PanelStreamHeight, AWidgetThatHasNoSizeYetHasNoPanelHeight)
{
  QWidget never_shown;
  never_shown.resize(0, 0);
  EXPECT_FLOAT_EQ(qar_qt_streaming::ar_panel_height_m(never_shown, PanelStreamOptions{}), 0.0f);
}

TEST(PanelStreamHeight, TheHeightAHostPredictsIsTheHeightTheStreamSends)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.ar_frame = PanelFrame{0.15, 0.0, 0.0, 0.0, 0.0};
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  const float predicted = qar_qt_streaming::ar_panel_height_m(*fixture.widget(), options);
  QtPanelStream stream("panel", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_NEAR(predicted, 0.294f, 1e-5f);
  EXPECT_FLOAT_EQ(predicted, factory->latest()->desc().placement.size.height_meters);
}

TEST(PanelStream, AnUnchangedWidgetIsCapturedButNotSubmitted)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_FALSE(stream.tick(1.0));
  EXPECT_FALSE(stream.tick(2.0));

  EXPECT_EQ(factory->latest()->submit_calls(), 1u);
  EXPECT_EQ(stream.diagnostics().frames_captured, 3u);
  EXPECT_EQ(stream.diagnostics().frames_skipped_unchanged, 2u);
  // The skipped ticks must not have reserved a send cycle either: a reserved
  // cycle has to be followed by a show_frame.
  EXPECT_EQ(factory->latest()->begin_calls(), 1u);
}

TEST(PanelStream, EditingTheWidgetSubmitsAgain)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  ASSERT_TRUE(stream.tick(0.0));
  ASSERT_FALSE(stream.tick(1.0));

  fixture.mutate();
  EXPECT_TRUE(stream.tick(2.0));
  EXPECT_EQ(factory->latest()->submit_calls(), 2u);
}

TEST(PanelStream, KeepaliveResendsAnUnchangedFrameOnSchedule)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.keepalive_seconds = 1.0;

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options, factory);

  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_FALSE(stream.tick(0.5));  // unchanged and keepalive not due
  EXPECT_TRUE(stream.tick(1.5));   // unchanged but keepalive due
  EXPECT_EQ(factory->latest()->submit_calls(), 2u);
}

TEST(PanelStream, TheFpsCapShedsTicks)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.max_fps = 10;  // one capture per 100 ms

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options, factory);

  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_FALSE(stream.tick(0.05));
  EXPECT_EQ(stream.diagnostics().frames_skipped_paced, 1u);
  // The shed tick must not even have captured — that repaint is the cost the
  // cap exists to avoid.
  EXPECT_EQ(stream.diagnostics().frames_captured, 1u);

  fixture.mutate();
  EXPECT_TRUE(stream.tick(0.2));
}

TEST(PanelStream, NotReadyIsSkippedRatherThanTreatedAsAnError)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  ASSERT_TRUE(stream.tick(0.0));
  factory->latest()->set_ready(false);
  fixture.mutate();

  EXPECT_FALSE(stream.tick(1.0));
  EXPECT_EQ(stream.diagnostics().frames_skipped_not_ready, 1u);
  EXPECT_TRUE(stream.diagnostics().last_error.empty());
}

TEST(PanelStream, AResizeRebuildsTheSenderBecauseTheLayoutIsFixedAtCreation)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_EQ(factory->create_calls(), 1u);

  fixture.widget()->resize(320, 200);
  fixture.widget()->layout()->activate();
  // A real resize still rebuilds — but only once it has SETTLED, so it is ticked
  // past both gates (>= resize_settle_seconds AND >= resize_settle_ticks). The
  // hysteresis tests below are where that rule is pinned down.
  stream.tick(1.0);
  stream.tick(1.3);
  ASSERT_TRUE(stream.tick(1.6));

  EXPECT_EQ(factory->create_calls(), 2u);
  EXPECT_EQ(factory->created().front()->close_calls(), 1u) << "the old sender must be closed";
  EXPECT_EQ(factory->latest()->desc().buffer_width, 320u);
}

// --- Resize hysteresis -----------------------------------------------------
//
// Rebuilding a panel is not a redraw: it retires the click/hover subscription,
// closes the QarOS panel, destroys the video sender and builds all three again
// (and the real input source spends two never-reused callback slots doing it).
// The pathological case is a status label INSIDE the streamed widget that
// grows, which widens the dock, which changes the capture buffer, which
// rebuilds the panel, which rewrites the status label — and the panel spends
// the session rebuilding instead of streaming. These pin the rule that stops it.

TEST(PanelStream, AResizeSmallerThanTheThresholdIsAbsorbedWithoutARebuild)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_EQ(factory->create_calls(), 1u);

  // 4 px, under the 8 px default threshold: not worth a rebuild however long it
  // is held.
  fixture.widget()->resize(204, 120);
  fixture.widget()->layout()->activate();
  for (int i = 1; i <= 20; ++i) {
    stream.tick(static_cast<double>(i));
  }

  EXPECT_EQ(factory->create_calls(), 1u) << "a few pixels must not rebuild the panel";
  EXPECT_GT(stream.diagnostics().resizes_ignored_small, 0u);
  EXPECT_FALSE(stream.diagnostics().resize_pending);
  // ...and the panel keeps streaming at the layout its live sender was built
  // for, so the captured buffer can never mismatch the sender.
  EXPECT_EQ(stream.diagnostics().buffer_width, 200u);
  EXPECT_EQ(factory->latest()->desc().buffer_width, 200u);
}

TEST(PanelStream, AnOscillatingWidthNeverSettlesSoTheSenderIsNeverRebuilt)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  ASSERT_EQ(factory->create_calls(), 1u);

  // The regression itself: the width flips between two SIGNIFICANT sizes every
  // 0.25 s — the rate the status text inside the widget was rewritten. Neither
  // candidate is ever held long enough to be adopted, so nothing is rebuilt,
  // which is the whole point: each rebuild is what rewrote the text that
  // changed the width.
  double now = 0.0;
  for (int i = 0; i < 40; ++i) {
    fixture.widget()->resize((i % 2) == 0 ? 260 : 200, 120);
    fixture.widget()->layout()->activate();
    now += 0.25;
    stream.tick(now);
  }

  EXPECT_EQ(factory->create_calls(), 1u) << "an oscillating size must not churn the sender";
  EXPECT_EQ(stream.diagnostics().adapter_rebuilds, 1u);
  EXPECT_EQ(factory->created().front()->close_calls(), 0u) << "the live sender stays live";
}

TEST(PanelStream, ASustainedResizeIsAdoptedOnlyOnceBothSettleGatesArePassed)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  fixture.widget()->resize(320, 200);
  fixture.widget()->layout()->activate();

  // Tick 1: the candidate has only just been seen. Neither gate is passed.
  stream.tick(1.0);
  EXPECT_EQ(factory->create_calls(), 1u);
  EXPECT_TRUE(stream.diagnostics().resize_pending);
  EXPECT_EQ(stream.diagnostics().pending_buffer_width, 320u);

  // Enough SECONDS but only two ticks: still not adopted. The tick gate is what
  // stops one stray sample from a slowly ticked panel counting as "held".
  stream.tick(2.0);
  EXPECT_EQ(factory->create_calls(), 1u) << "two ticks is not resize_settle_ticks";

  // Third consecutive tick on the same size, well past the settle seconds.
  ASSERT_TRUE(stream.tick(3.0));
  EXPECT_EQ(factory->create_calls(), 2u);
  EXPECT_FALSE(stream.diagnostics().resize_pending);
  EXPECT_EQ(stream.diagnostics().adapter_rebuilds, 2u);
  EXPECT_EQ(factory->latest()->desc().buffer_width, 320u);
}

TEST(PanelStream, EnoughTicksButNotEnoughSecondsDoesNotAdoptEither)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  fixture.widget()->resize(320, 200);
  fixture.widget()->layout()->activate();

  // Ten ticks inside 0.1 s — a 60 Hz GUI thread during a splitter drag. The
  // seconds gate is the one that matters there.
  for (int i = 1; i <= 10; ++i) {
    stream.tick(0.01 * static_cast<double>(i));
  }
  EXPECT_EQ(factory->create_calls(), 1u);
  EXPECT_TRUE(stream.diagnostics().resize_pending);

  // Hold the same size past resize_settle_seconds and it goes through.
  ASSERT_TRUE(stream.tick(1.0));
  EXPECT_EQ(factory->create_calls(), 2u);
}

TEST(PanelStream, WhileAResizeSettlesTheSubmittedFrameStillMatchesTheLiveSender)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  auto first = factory->latest();

  fixture.widget()->resize(320, 200);
  fixture.widget()->layout()->activate();
  fixture.mutate();
  stream.tick(0.1);

  // The sender's buffer layout is fixed at creation, so a frame captured at the
  // widget's NEW size would be rejected by the real adapter (it compares the
  // sizes before copying). Everything submitted before the resize is adopted
  // must still be the size the live sender was built for.
  ASSERT_FALSE(first->frames().empty());
  for (const auto & frame : first->frames()) {
    EXPECT_EQ(frame.width, first->desc().buffer_width);
    EXPECT_EQ(frame.height, first->desc().buffer_height);
  }
  // The AR hit test normalises against the same committed size, so it is pinned
  // too rather than running ahead of the panel the operator is pointing at.
  EXPECT_EQ(stream.logical_width(), 200u);
}

TEST(PanelStream, ZeroingTheHysteresisRestoresRebuildOnAnyChange)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  PanelStreamOptions options = options_no_pacing();
  options.resize_threshold_px = 0;
  options.resize_settle_seconds = 0.0;
  options.resize_settle_ticks = 0;
  QtPanelStream stream("panel", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  fixture.widget()->resize(202, 120);
  fixture.widget()->layout()->activate();
  ASSERT_TRUE(stream.tick(1.0));

  EXPECT_EQ(factory->create_calls(), 2u) << "a host that opts out gets a rebuild on every change";
  EXPECT_EQ(factory->latest()->desc().buffer_width, 202u);
}

TEST(PanelStream, TheNotReadyStreakCountsConsecutiveUnreservedTicksAndResetsOnASubmit)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  // The keepalive is what keeps an idle panel ASKING for a send cycle, which is
  // exactly the situation the streak has to describe.
  PanelStreamOptions options = options_no_pacing();
  options.keepalive_seconds = 1.0;
  QtPanelStream stream("panel", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 0u);

  // "Nothing has attached to this panel's stream yet" is a STATE, not an error,
  // and it is the one the operator has to be able to see: the adapter does not
  // print it every frame, so this streak is what says it is still happening.
  factory->latest()->set_ready(false);
  for (int i = 1; i <= 3; ++i) {
    EXPECT_FALSE(stream.tick(static_cast<double>(i)));
  }
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 3u);
  EXPECT_EQ(stream.diagnostics().frames_skipped_not_ready, 3u);
  EXPECT_TRUE(stream.diagnostics().last_error.empty()) << "not-ready is not a failure";

  factory->latest()->set_ready(true);
  EXPECT_TRUE(stream.tick(10.0));
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 0u);
}

// The three answers of a sender that is not ready are different faults, so the streak is also
// counted by cause: nothing reserved yet, nothing attached (604), a receiver that lost the stream
// and reconnects (603). The heartbeat prints the three counts as `nr=<no destination>/<604>/<603>`.
TEST(PanelStream, TheNotReadyStreakIsAlsoCountedByTheCauseTheSenderGives)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  PanelStreamOptions options = options_no_pacing();
  options.keepalive_seconds = 1.0;
  QtPanelStream stream("panel", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_EQ(qar_ar_core::not_ready_split_text(stream.diagnostics()), "0/0/0");

  factory->latest()->set_ready(false);
  for (int i = 1; i <= 2; ++i) {
    EXPECT_FALSE(stream.tick(static_cast<double>(i)));
  }
  factory->latest()->set_not_ready_cause(qar_ar_core::PanelNotReadyCause::NoActiveTransfers);
  for (int i = 3; i <= 5; ++i) {
    EXPECT_FALSE(stream.tick(static_cast<double>(i)));
  }
  factory->latest()->set_not_ready_cause(qar_ar_core::PanelNotReadyCause::Reconnecting);
  EXPECT_FALSE(stream.tick(6.0));

  const auto & d = stream.diagnostics();
  EXPECT_EQ(d.not_ready_streak, 6u);
  EXPECT_EQ(d.not_ready_streak_no_destination, 2u) << "the default cause";
  EXPECT_EQ(d.not_ready_streak_no_transfers, 3u);
  EXPECT_EQ(d.not_ready_streak_reconnecting, 1u);
  EXPECT_EQ(qar_ar_core::not_ready_split_text(d), "2/3/1");

  factory->latest()->set_ready(true);
  EXPECT_TRUE(stream.tick(10.0));
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 0u);
  EXPECT_EQ(qar_ar_core::not_ready_split_text(stream.diagnostics()), "0/0/0")
    << "a frame that lands ends the streak and every count of it";
}

TEST(PanelStream, ANotReadyTickCountsOnlyForTheCauseOfThatTick)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  PanelStreamOptions options = options_no_pacing();
  options.keepalive_seconds = 1.0;
  QtPanelStream stream("panel", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));
  factory->latest()->set_ready(false);

  factory->latest()->set_not_ready_cause(qar_ar_core::PanelNotReadyCause::Reconnecting);
  EXPECT_FALSE(stream.tick(1.0));
  EXPECT_EQ(qar_ar_core::not_ready_split_text(stream.diagnostics()), "0/0/1");
  factory->latest()->set_not_ready_cause(qar_ar_core::PanelNotReadyCause::NoActiveTransfers);
  EXPECT_FALSE(stream.tick(2.0));
  EXPECT_EQ(qar_ar_core::not_ready_split_text(stream.diagnostics()), "0/1/1")
    << "the earlier tick keeps its count, the new one goes to its own cause";
}

TEST(PanelStream, ANotYetLaidOutWidgetIsRefusedWithoutTouchingTheFactory)
{
  QWidget widget;
  widget.resize(0, 0);

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", &widget, options_no_pacing(), factory);

  EXPECT_FALSE(stream.tick(0.0));
  EXPECT_EQ(factory->create_calls(), 0u);
  EXPECT_FALSE(stream.diagnostics().last_error.empty());
}

TEST(PanelStream, AFactoryRefusalIsReportedAndRetriedNextTick)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_fails(true, "session not ready");
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  EXPECT_FALSE(stream.tick(0.0));
  EXPECT_EQ(stream.diagnostics().last_error, "session not ready");
  EXPECT_FALSE(stream.diagnostics().active);

  factory->set_fails(false);
  EXPECT_TRUE(stream.tick(1.0));
  EXPECT_TRUE(stream.diagnostics().active);
}

TEST(PanelStream, ClosingClosesTheSenderAndIsIdempotent)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  stream.close();
  stream.close();
  EXPECT_EQ(factory->latest()->close_calls(), 1u);
  EXPECT_FALSE(stream.diagnostics().active);
}

TEST(PanelStream, ClosingReturnsWhatTheAdapterDidAndNothingTheSecondTime)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  const qar_ar_core::PanelCloseReport first = stream.close();
  EXPECT_TRUE(first.input_unsubscribed);
  EXPECT_EQ(first.panel, qar_ar_core::PanelCloseOutcome::Closed);
  EXPECT_TRUE(first.sender_destroyed);
  EXPECT_FALSE(factory->latest()->last_close_panel_closed_in_ar());

  const qar_ar_core::PanelCloseReport second = stream.close();
  EXPECT_FALSE(second.input_unsubscribed);
  EXPECT_EQ(second.panel, qar_ar_core::PanelCloseOutcome::NoPanel);
  EXPECT_FALSE(second.sender_destroyed);
  EXPECT_EQ(factory->latest()->sender_destroy_calls(), 1u);
}

TEST(PanelStream, ClosingAStreamTheOperatorClosedInArTellsTheAdapterToSkipClosePanel)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  stream.suspend_capture();  // what the manager does when the runtime reports CLOSED
  const qar_ar_core::PanelCloseReport report = stream.close();

  EXPECT_TRUE(factory->latest()->last_close_panel_closed_in_ar());
  EXPECT_EQ(report.panel, qar_ar_core::PanelCloseOutcome::AlreadyClosedInAr);
  EXPECT_EQ(factory->latest()->close_panel_calls(), 0u);
  EXPECT_TRUE(report.sender_destroyed);
}

TEST(PanelStream, AResizeRebuildStillClosesThePanelBecauseNothingClosedItInAr)
{
  // The one mid-session close that is not a window closing: the resize rebuild
  // closes the old panel and sender before asking for new ones. It never passes the
  // AR-closed flag (a panel closed in AR is not captured, so never rebuilt).
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  fixture.widget()->resize(320, 200);
  fixture.widget()->layout()->activate();
  stream.tick(1.0);
  stream.tick(1.3);
  ASSERT_TRUE(stream.tick(1.6));  // settled: the sender is rebuilt

  ASSERT_EQ(factory->created().size(), 2u);
  const auto old_adapter = factory->created().front();
  EXPECT_EQ(old_adapter->close_calls(), 1u);
  EXPECT_EQ(old_adapter->close_panel_calls(), 1u);
  EXPECT_FALSE(old_adapter->last_close_panel_closed_in_ar());
  EXPECT_EQ(factory->created().back()->close_calls(), 0u);
}

TEST(PanelStream, SetPoseIsForwardedToTheLiveSender)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  QarPose pose{};
  pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  pose.position = QarVector3{1.0f, 2.0f, 3.0f};
  stream.set_pose(pose);

  EXPECT_EQ(factory->latest()->placement_calls(), 1u);
  EXPECT_FLOAT_EQ(factory->latest()->placement().pose.position.y, 2.0f);
  EXPECT_FLOAT_EQ(stream.placement().pose.position.z, 3.0f);
}

TEST(PanelStream, SubmittedPixelsMatchTheCapturedBuffer)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  ASSERT_EQ(factory->latest()->frames().size(), 1u);
  const auto & frame = factory->latest()->frames().front();
  EXPECT_EQ(frame.width, 200u);
  EXPECT_EQ(frame.height, 120u);
  EXPECT_EQ(frame.rgba.size(), 200u * 120u * 4u);
}

// --- manager ---------------------------------------------------------------

namespace
{

QtPanelStreamingConfig
make_config(std::shared_ptr<MockQarPanelSenderFactory> factory, std::shared_ptr<ManualClock> clock)
{
  QtPanelStreamingConfig config;
  config.panel_sender_factory = std::move(factory);
  config.clock = std::move(clock);
  // A frame that is the same above and below the video and has nothing at the sides: the middle
  // of the panel is the middle of the widget, which the routing tests rely on. The tests of the
  // frame itself state theirs.
  config.ar_frame = PanelFrame{0.03, 0.03, 0.0, 0.0, 0.0};
  return config;
}

}  // namespace

TEST(PanelManager, RefusesConstructionWithoutAFactory)
{
  QtPanelStreamingConfig config;
  EXPECT_THROW(QtPanelStreamManager{config}, std::invalid_argument);
}

TEST(PanelManager, AddsRemovesAndListsPanelsDeterministically)
{
  Fixture a;
  Fixture b;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));

  ASSERT_TRUE(manager.add_panel("zeta", a.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(manager.add_panel("alpha", b.widget(), options_no_pacing()).ok());

  const auto keys = manager.panel_keys();
  ASSERT_EQ(keys.size(), 2u);
  EXPECT_EQ(keys[0], "alpha") << "keys must come back ordered";
  EXPECT_EQ(keys[1], "zeta");
  EXPECT_TRUE(manager.has_panel("zeta"));

  manager.tick();

  // remove_panel follows the close mode, and the default is DESTROY: the panel's
  // AR panel is closed, its sender destroyed, and the key is simply gone.
  ASSERT_TRUE(manager.remove_panel("zeta").ok());
  EXPECT_FALSE(manager.has_panel("zeta"));
  EXPECT_FALSE(manager.is_tombstoned("zeta"));
  EXPECT_EQ(manager.panel_keys(), std::vector<std::string>{"alpha"});
  EXPECT_EQ(manager.remove_panel("zeta").status, Status::NotFound) << "a panel is closed once";
  EXPECT_EQ(manager.remove_panel("nothing-here").status, Status::NotFound);
}

TEST(PanelManager, RemovingAPanelInNoticeModeTombstonesIt)
{
  Fixture a;
  Fixture b;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
  config.close_mode = qar_qt_streaming::PanelCloseMode::Notice;  // the escape hatch
  QtPanelStreamManager manager(config);

  ASSERT_TRUE(manager.add_panel("zeta", a.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(manager.add_panel("alpha", b.widget(), options_no_pacing()).ok());

  // A sender has to exist before there is anything to protect: a panel removed
  // before its first tick never announced a stream, so it is simply erased (see
  // PanelLifetime.APanelThatNeverGotASenderIsErasedRatherThanTombstoned).
  manager.tick();

  // In Notice mode remove_panel TOMBSTONES: the key stays, showing the
  // closed-source notice, because its video sender must outlive the widget.
  ASSERT_TRUE(manager.remove_panel("zeta").ok());
  EXPECT_TRUE(manager.has_panel("zeta"));
  EXPECT_TRUE(manager.is_tombstoned("zeta"));
  EXPECT_TRUE(manager.remove_panel("zeta").ok()) << "tombstoning twice is a no-op";
  EXPECT_EQ(manager.remove_panel("nothing-here").status, Status::NotFound);
  for (const auto & adapter : factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 0u) << "no sender is destroyed mid-session in notice mode";
  }
}

TEST(PanelManager, RejectsDuplicateKeysAndBadArguments)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  EXPECT_EQ(
    manager.add_panel("p", fixture.widget(), options_no_pacing()).status, Status::AlreadyExists);
  EXPECT_EQ(manager.add_panel("", fixture.widget(), options_no_pacing()).status,
    Status::InvalidArgument);
  EXPECT_EQ(manager.add_panel("q", nullptr, options_no_pacing()).status, Status::InvalidArgument);
}

TEST(PanelManager, AddingAPanelDoesNotTouchQarOsUntilTheFirstTick)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  EXPECT_EQ(factory->create_calls(), 0u) << "add_panel must not block on the runtime";

  EXPECT_EQ(manager.tick(), 1u);
  EXPECT_EQ(factory->create_calls(), 1u);
}

TEST(PanelManager, TickReportsHowManyPanelsActuallySubmitted)
{
  Fixture a;
  Fixture b;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto clock = std::make_shared<ManualClock>();
  QtPanelStreamManager manager(make_config(factory, clock));

  ASSERT_TRUE(manager.add_panel("a", a.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(manager.add_panel("b", b.widget(), options_no_pacing()).ok());

  EXPECT_EQ(manager.tick(), 2u);
  clock->advance(1.0);
  EXPECT_EQ(manager.tick(), 0u) << "nothing changed, so nothing should be sent";
}

TEST(PanelManager, DiagnosticsSummariseEveryPanel)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto clock = std::make_shared<ManualClock>();
  QtPanelStreamManager manager(make_config(factory, clock));

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  manager.tick();
  clock->advance(1.0);
  manager.tick();

  const auto snapshot = manager.diagnostics();
  EXPECT_EQ(snapshot.panel_count, 1u);
  EXPECT_EQ(snapshot.ticks, 2u);
  EXPECT_TRUE(snapshot.streaming_active);
  ASSERT_EQ(snapshot.panels.size(), 1u);
  EXPECT_EQ(snapshot.panels[0].key, "p");
  EXPECT_EQ(snapshot.panels[0].frames_submitted, 1u);
  EXPECT_EQ(snapshot.panels[0].frames_skipped_unchanged, 1u);
}

TEST(PanelManager, ClearClosesEveryPanel)
{
  Fixture a;
  Fixture b;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));

  ASSERT_TRUE(manager.add_panel("a", a.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(manager.add_panel("b", b.widget(), options_no_pacing()).ok());
  manager.tick();

  manager.clear();
  EXPECT_TRUE(manager.panel_keys().empty());
  for (const auto & adapter : factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u);
  }
}

TEST(PanelManager, SetPanelPoseTargetsTheNamedPanel)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  manager.tick();

  QarPose pose{};
  pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  pose.position = QarVector3{0.0f, 1.5f, -2.0f};
  EXPECT_TRUE(manager.set_panel_pose("p", pose).ok());
  EXPECT_EQ(manager.set_panel_pose("missing", pose).status, Status::NotFound);
  EXPECT_FLOAT_EQ(factory->latest()->placement().pose.position.y, 1.5f);
}

TEST(PanelManager, RoutesARoomPointToThePanelPixelUnderIt)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto clock = std::make_shared<ManualClock>();
  RecordingPanelInputSink sink;

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.input_sink = &sink;
  QtPanelStreamManager manager(config);

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  // A tick is what derives the panel's physical size from the widget, so it must
  // happen before a hit test can mean anything.
  manager.tick();

  // The panel's pose sits on its TOP EDGE (QarOS panel space). The frame of this manager is the
  // same above and below the video, so the point that lands on the widget's centre is half the
  // height of the whole panel below it: 0.03 m of frame, the widget, 0.03 m of frame.
  float panel_height_m = 0.0f;
  for (const auto & p : manager.diagnostics().panels) {
    if (p.key == "p") { panel_height_m = p.height_meters; }
  }
  ASSERT_GT(panel_height_m, static_cast<float>(config.ar_frame.vertical_m()));
  EXPECT_TRUE(manager.route_room_point(
      QarVector3{0.0f, -0.5f * panel_height_m, 0.0f}, PanelPointerEvent::Kind::Click));
  ASSERT_EQ(sink.events().size(), 1u);
  EXPECT_EQ(sink.events()[0].panel_key, "p");
  EXPECT_EQ(sink.events()[0].kind, PanelPointerEvent::Kind::Click);
  EXPECT_EQ(sink.events()[0].widget_x, 100);
  EXPECT_EQ(sink.events()[0].widget_y, 60);

  // A point in the band above the widget and one in the band below it hit nothing. The widget
  // spans 0.144 m, from 0.03 m under the panel's top edge to 0.174 m under it.
  EXPECT_FALSE(manager.route_room_point(
      QarVector3{0.0f, -0.015f, 0.0f}, PanelPointerEvent::Kind::Click));
  EXPECT_FALSE(manager.route_room_point(
      QarVector3{0.0f, -0.189f, 0.0f}, PanelPointerEvent::Kind::Click));
  EXPECT_EQ(sink.events().size(), 1u);
  // ...and the points just inside the two edges of the widget land on its first and last row.
  EXPECT_TRUE(manager.route_room_point(
      QarVector3{0.0f, -0.0305f, 0.0f}, PanelPointerEvent::Kind::Click));
  EXPECT_TRUE(manager.route_room_point(
      QarVector3{0.0f, -0.1735f, 0.0f}, PanelPointerEvent::Kind::Click));
  ASSERT_EQ(sink.events().size(), 3u);
  EXPECT_EQ(sink.events()[1].widget_y, 0);
  EXPECT_EQ(sink.events()[2].widget_y, 119);
}

TEST(PanelManager, APointInThePaddingStripOrBesideThePanelHitsNothing)
{
  // 201 x 121 px is a content of that size in a buffer of 202 x 122 px. QarOS draws the buffer at
  // the sent width of the panel, so a column of the video is that width / 202 wide, the widget
  // covers the first 201 of them from the left edge of the panel, and the last one is padding.
  // The same holds for the panel of a dialog, whose width is reduced for the frame.
  for (const bool dialog : {false, true}) {
    Fixture fixture;
    fixture.widget()->resize(201, 121);
    QCoreApplication::processEvents();
    auto factory = std::make_shared<MockQarPanelSenderFactory>();
    RecordingPanelInputSink sink;
    QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
    config.input_sink = &sink;
    QtPanelStreamManager manager(config);
    PanelStreamOptions options = options_no_pacing();
    options.dialog_panel = dialog;
    ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options).ok());
    manager.tick();

    float width_m = 0.0f;
    float height_m = 0.0f;
    for (const auto & p : manager.diagnostics().panels) {
      if (p.key == "p") {
        width_m = p.width_meters;
        height_m = p.height_meters;
      }
    }
    ASSERT_GT(width_m, 0.0f);
    const float column_m = width_m / 202.0f;
    // +X runs to the viewer's left, so the left edge of the panel is at +width / 2; the middle of
    // the panel's height is inside the widget.
    const auto route = [&](float from_left_edge_m) {
        return manager.route_room_point(
          QarVector3{0.5f * width_m - from_left_edge_m, -0.5f * height_m, 0.0f},
          PanelPointerEvent::Kind::Click);
      };
    EXPECT_TRUE(route(0.5f * column_m)) << dialog;
    EXPECT_TRUE(route(200.5f * column_m)) << dialog;
    ASSERT_EQ(sink.events().size(), 2u) << dialog;
    EXPECT_EQ(sink.events()[0].widget_x, 0) << dialog;
    EXPECT_EQ(sink.events()[1].widget_x, 200) << dialog;
    EXPECT_FALSE(route(201.5f * column_m)) << dialog << ": the padding column";
    EXPECT_FALSE(route(-0.5f * column_m)) << dialog << ": left of the panel";
    EXPECT_FALSE(route(width_m + 0.5f * column_m)) << dialog << ": right of the panel";
    EXPECT_EQ(sink.events().size(), 2u) << dialog;
  }
}

TEST(PanelManagerHeight, TheManagersFrameIsAppliedToEveryPanelInPlaceOfTheOptions)
{
  Fixture fixture;
  Fixture other;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
  config.ar_frame = PanelFrame{0.20, 0.0, 0.0, 0.0, 0.0};
  QtPanelStreamManager manager(config);

  PanelStreamOptions options = options_no_pacing();
  options.ar_frame = PanelFrame{0.50, 0.0, 0.0, 0.0, 0.0};  // the manager's value wins
  const float predicted = manager.ar_panel_height_m(*fixture.widget(), options);
  ASSERT_TRUE(manager.add_panel("a", fixture.widget(), options).ok());
  ASSERT_TRUE(manager.add_panel("b", other.widget(), options_no_pacing()).ok());
  manager.tick();

  ASSERT_EQ(factory->created().size(), 2u);
  for (const auto & adapter : factory->created()) {
    EXPECT_NEAR(adapter->desc().placement.size.height_meters, 0.144f + 0.20f, 1e-5f);
  }
  // What the host is told before the panel exists is what the panel is sent.
  EXPECT_NEAR(predicted, 0.344f, 1e-5f);
  EXPECT_FLOAT_EQ(predicted, factory->created()[0]->desc().placement.size.height_meters);
}

TEST(PanelManagerHeight, TheDefaultManagerGivesTheFrameOfTheVisualizer)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
  config.ar_frame = PanelFrame{};  // what a manager that is not told otherwise has
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("a", fixture.widget(), options_no_pacing()).ok());
  manager.tick();
  // 200 x 120 px: 0.24 m wide, the video 0.234 m wide inside the borders and 0.6 times that tall,
  // under a title bar and border of 0.066 m, above a border of 0.003 m and with a fit margin of
  // 0.003 m.
  EXPECT_NEAR(factory->latest()->desc().placement.size.width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(factory->latest()->desc().placement.size.height_meters, 0.2124f, 1e-6f);
  EXPECT_NEAR(manager.ar_panel_height_m(*fixture.widget(), options_no_pacing()), 0.2124f, 1e-6f);
}

TEST(PanelManagerHeight, TheManagerMakesTheDialogPanelNarrowerAndKeepsItsHeight)
{
  Fixture fixture;
  Fixture other;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
  config.ar_frame = PanelFrame{0.05, 0.0, 0.0, 0.0, 0.0};
  QtPanelStreamManager manager(config);

  PanelStreamOptions dialog_options = options_no_pacing();
  dialog_options.dialog_panel = true;
  const float predicted = manager.ar_panel_height_m(*fixture.widget(), dialog_options);
  ASSERT_TRUE(manager.add_panel("dialog:Add", fixture.widget(), dialog_options).ok());
  ASSERT_TRUE(manager.add_panel("dock:Views", other.widget(), options_no_pacing()).ok());
  manager.tick();

  // The same 200 x 120 px (0.24 m wide, a video of 0.144 m) with the manager's 0.05 m of frame:
  // the dialog keeps the 0.144 m of height and is 0.24 * 0.094 / 0.144 = 0.15667 m wide, the
  // other panel keeps the width and is 0.194 m tall.
  ASSERT_EQ(factory->created().size(), 2u);
  EXPECT_NEAR(factory->created()[0]->desc().placement.size.height_meters, 0.144f, 1e-5f);
  EXPECT_NEAR(factory->created()[0]->desc().placement.size.width_meters, 0.156667f, 1e-5f);
  EXPECT_NEAR(factory->created()[1]->desc().placement.size.height_meters, 0.194f, 1e-5f);
  EXPECT_NEAR(factory->created()[1]->desc().placement.size.width_meters, 0.24f, 1e-6f);
  EXPECT_NEAR(predicted, 0.144f, 1e-5f);
}

TEST(PanelManager, ARoomPointThatMissesEveryPanelNotifiesNobody)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  RecordingPanelInputSink sink;

  QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
  config.input_sink = &sink;
  QtPanelStreamManager manager(config);

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  manager.tick();

  EXPECT_FALSE(manager.route_room_point(QarVector3{9.0f, 0.0f, 0.0f}, PanelPointerEvent::Kind::Hover));
  EXPECT_TRUE(sink.events().empty());
}

TEST(PanelManager, RoutesARoomRayThroughTheSameSeam)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  RecordingPanelInputSink sink;

  QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
  config.input_sink = &sink;
  QtPanelStreamManager manager(config);

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  manager.tick();

  // The ray goes through the middle of the panel (half its height below the pose on its top
  // edge), which is the middle of the widget: QarOS centres the video in the panel.
  float panel_height_m = 0.0f;
  for (const auto & p : manager.diagnostics().panels) {
    if (p.key == "p") { panel_height_m = p.height_meters; }
  }
  ASSERT_GT(panel_height_m, 0.0f);
  EXPECT_TRUE(manager.route_room_ray(
      QarVector3{0.0f, -0.5f * panel_height_m, 1.0f}, QarVector3{0.0f, 0.0f, -1.0f},
      PanelPointerEvent::Kind::Click));
  ASSERT_EQ(sink.events().size(), 1u);
  EXPECT_EQ(sink.events()[0].widget_x, 100);
  EXPECT_EQ(sink.events()[0].widget_y, 60);
  // The ray through the pose, on the top edge of the panel, meets the band of the frame above the
  // widget and lands on no pixel.
  EXPECT_FALSE(manager.route_room_ray(
      QarVector3{0.0f, 0.0f, 1.0f}, QarVector3{0.0f, 0.0f, -1.0f}, PanelPointerEvent::Kind::Click));
  EXPECT_EQ(sink.events().size(), 1u);
}

TEST(PanelManager, RoutingWithoutASinkIsSafe)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  manager.tick();
  float panel_height_m = 0.0f;
  for (const auto & p : manager.diagnostics().panels) {
    if (p.key == "p") { panel_height_m = p.height_meters; }
  }
  ASSERT_GT(panel_height_m, 0.0f);
  EXPECT_TRUE(manager.route_room_point(
      QarVector3{0.0f, -0.5f * panel_height_m, 0.0f}, PanelPointerEvent::Kind::Click));
}


// ---------------------------------------------------------------------------
// A panel QarOS is asking for and we are not serving must be VISIBLE.
//
// The shape being modelled: a GUI panel is created, QarOS is told its content
// is stream <id> and starts asking for it, and our sender never reports a ready
// destination — so we submit nothing, forever, and without a report nothing
// would say so. The test double's `set_senders_start_not_ready` is exactly
// `try_begin_frame` returning success with out_ready == false, which is how the
// runtime reports MEDIA_STREAMING_NO_ACTIVE_TRANSFERS (604).
// ---------------------------------------------------------------------------
namespace
{

class RecordingDiagnostics : public qar_ar_core::PanelDiagnosticsSink
{
public:
  struct Entry
  {
    qar_ar_core::DiagnosticSeverity severity;
    std::string category;
    std::string message;
    std::string panel_key;
  };

  void on_message(
    qar_ar_core::DiagnosticSeverity severity,
    const std::string & category,
    const std::string & message,
    const std::string & panel_key) override
  {
    messages.push_back(Entry{severity, category, message, panel_key});
  }

  /// The first entry whose message contains @p needle, or nullptr.
  const Entry * find(const std::string & needle) const
  {
    for (const Entry & entry : messages) {
      if (entry.message.find(needle) != std::string::npos) {
        return &entry;
      }
    }
    return nullptr;
  }

  std::size_t count(const std::string & needle) const
  {
    std::size_t n = 0;
    for (const Entry & entry : messages) {
      if (entry.message.find(needle) != std::string::npos) {
        ++n;
      }
    }
    return n;
  }

  std::vector<Entry> messages;
};

/// Pacing off, keepalive ON (the shipped default): an unserved panel must keep
/// ATTEMPTING a submit every tick, otherwise "not ready" would really mean
/// "we never asked".
PanelStreamOptions
options_unpaced_with_keepalive()
{
  PanelStreamOptions options;
  options.max_fps = 0;
  options.keepalive_seconds = 1.0;
  return options;
}

}  // namespace

TEST(PanelStream, TheStreamIdIsRecordedWhenTheSenderIsBuilt)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  ASSERT_TRUE(stream.tick(0.0));
  // Whatever the backend calls its stream, the panel carries it — that pairing
  // is the whole point: the runtime names streams and nothing else.
  EXPECT_EQ(stream.diagnostics().stream_id, factory->latest()->stream_id_text());
  EXPECT_FALSE(stream.diagnostics().stream_id.empty());
}

TEST(PanelStream, AnUnservedPanelAccumulatesUnservedSecondsAndNoFrames)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_not_ready(true);
  QtPanelStream stream("panel", fixture.widget(), options_unpaced_with_keepalive(), factory);

  EXPECT_FALSE(stream.tick(0.0));
  EXPECT_FALSE(stream.tick(3.0));
  EXPECT_FALSE(stream.tick(9.0));

  const auto & d = stream.diagnostics();
  EXPECT_EQ(d.frames_submitted, 0u);
  // Every tick asked and was refused — so the silence is QarOS's, not ours.
  EXPECT_EQ(d.not_ready_streak, 3u);
  EXPECT_EQ(factory->latest()->begin_calls(), 3u);
  EXPECT_NEAR(d.unserved_seconds, 9.0, 1e-9);
}

TEST(PanelStream, TheUnservedClockResetsOnTheFirstFrameThatLands)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_not_ready(true);
  QtPanelStream stream("panel", fixture.widget(), options_unpaced_with_keepalive(), factory);

  ASSERT_FALSE(stream.tick(0.0));
  ASSERT_FALSE(stream.tick(4.0));
  ASSERT_GT(stream.diagnostics().unserved_seconds, 0.0);

  factory->latest()->set_ready(true);
  ASSERT_TRUE(stream.tick(5.0));
  EXPECT_DOUBLE_EQ(stream.diagnostics().unserved_seconds, 0.0);
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 0u);
}

TEST(PanelManager, WarnsOnceWhenAPanelHasHeldASenderWithoutServingIt)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_not_ready(true);
  RecordingDiagnostics diagnostics;
  auto clock = std::make_shared<ManualClock>();

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.diagnostics = &diagnostics;
  config.unserved_warn_seconds = 5.0;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_unpaced_with_keepalive()).ok());

  manager.tick();
  clock->advance(4.0);
  manager.tick();
  EXPECT_EQ(diagnostics.count("is not being served"), 0u) << "warned before the threshold";

  clock->advance(2.0);  // 6 s of holding a sender and submitting nothing
  manager.tick();
  const auto * warning = diagnostics.find("is not being served");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->severity, qar_ar_core::DiagnosticSeverity::Warning);
  EXPECT_EQ(warning->panel_key, "p");
  // The stream the headset is asking for has to be IN the line, or it cannot be
  // matched to the runtime's streamRequest/<id> traffic.
  EXPECT_NE(warning->message.find(factory->latest()->stream_id_text()), std::string::npos);

  // Latched: once per unserved run, not once per tick.
  clock->advance(10.0);
  manager.tick();
  manager.tick();
  EXPECT_EQ(diagnostics.count("is not being served"), 1u);
}

TEST(PanelManager, RetiringAPanelThatNeverServedAFrameIsAWarningNamingTheStream)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_not_ready(true);
  RecordingDiagnostics diagnostics;
  auto clock = std::make_shared<ManualClock>();

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.diagnostics = &diagnostics;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_unpaced_with_keepalive()).ok());
  manager.tick();
  const std::string stream_id = factory->latest()->stream_id_text();

  clock->advance(3.0);
  manager.tick();
  ASSERT_TRUE(manager.remove_panel("p").ok());

  const auto * retired = diagnostics.find("having served it NO frames");
  ASSERT_NE(retired, nullptr);
  EXPECT_EQ(retired->severity, qar_ar_core::DiagnosticSeverity::Warning);
  EXPECT_NE(retired->message.find(stream_id), std::string::npos);
}

TEST(PanelManager, RetiringAPanelThatDidServeReportsTheFrameCountAndStream)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  RecordingDiagnostics diagnostics;
  auto clock = std::make_shared<ManualClock>();

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.diagnostics = &diagnostics;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_unpaced_with_keepalive()).ok());
  manager.tick();
  const std::string stream_id = factory->latest()->stream_id_text();
  ASSERT_GT(factory->latest()->submit_calls(), 0u);

  ASSERT_TRUE(manager.remove_panel("p").ok());

  // The close line names the stream and the number of frames that stream
  // actually carried — and it is NOT the warning, which is for a stream that
  // served nothing.
  const auto * closed = diagnostics.find("closed 'p'");
  ASSERT_NE(closed, nullptr);
  EXPECT_EQ(closed->severity, qar_ar_core::DiagnosticSeverity::Info);
  EXPECT_NE(closed->message.find(stream_id), std::string::npos);
  EXPECT_NE(closed->message.find("after 1 frame(s)"), std::string::npos);
  EXPECT_EQ(diagnostics.count("having served it NO frames"), 0u);
}

TEST(PanelManager, TombstoningAPanelInNoticeModeReportsTheFrameCountAndStream)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  RecordingDiagnostics diagnostics;
  auto clock = std::make_shared<ManualClock>();

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.diagnostics = &diagnostics;
  config.close_mode = qar_qt_streaming::PanelCloseMode::Notice;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_unpaced_with_keepalive()).ok());
  manager.tick();
  const std::string stream_id = factory->latest()->stream_id_text();
  ASSERT_GT(factory->latest()->submit_calls(), 0u);

  ASSERT_TRUE(manager.remove_panel("p").ok());

  // The retirement line is a TOMBSTONE line — the stream is not removed, it
  // goes on showing the notice — but it still names the stream and the number
  // of frames that stream actually carried.
  const auto * retired = diagnostics.find("has closed after");
  ASSERT_NE(retired, nullptr);
  EXPECT_EQ(retired->severity, qar_ar_core::DiagnosticSeverity::Info);
  EXPECT_NE(retired->message.find(stream_id), std::string::npos);
  EXPECT_NE(retired->message.find("1 frame(s)"), std::string::npos);
  EXPECT_EQ(diagnostics.count("having served it NO frames"), 0u);
}

TEST(PanelManager, ZeroUnservedWarnSecondsDisablesTheReport)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_not_ready(true);
  RecordingDiagnostics diagnostics;
  auto clock = std::make_shared<ManualClock>();

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.diagnostics = &diagnostics;
  config.unserved_warn_seconds = 0.0;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_unpaced_with_keepalive()).ok());

  manager.tick();
  clock->advance(60.0);
  manager.tick();
  EXPECT_EQ(diagnostics.count("is not being served"), 0u);
}

// ---------------------------------------------------------------------------
// A popup opening or closing over a panel is logged once per edge.
//
// The line is how an operator tells "no list opened" from "a list opened and was kept off the
// desktop", so it must appear on the 0 -> N and N -> 0 edges only - never once per captured
// frame while the list stays open.
// ---------------------------------------------------------------------------

TEST(QtPanelStreamManager, APopupOpeningAndClosingIsLoggedOnceEach)
{
  // Declared before the manager, so the manager (and its streams) is gone before the widget.
  std::unique_ptr<QWidget> widget(new QWidget());
  auto * combo = new QComboBox(widget.get());
  combo->addItems({"zero", "one", "two"});
  combo->setGeometry(20, 20, 160, 28);
  ASSERT_TRUE(qar_qt_streaming::show_offscreen(widget.get(), 300, 200));
  widget->ensurePolished();
  QCoreApplication::processEvents();

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  RecordingDiagnostics diagnostics;
  QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
  config.diagnostics = &diagnostics;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("off", widget.get(), options_no_pacing()).ok());
  manager.tick();
  EXPECT_EQ(diagnostics.count("popup open over this panel"), 0u) << "nothing is open yet";
  EXPECT_EQ(diagnostics.count("popup closed"), 0u);

  // An AR tap in the middle of the combo opens its list.
  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_panel_uv = true;
  sample.panel_key = "off";
  sample.u = 100.0f / 300.0f;
  sample.v = 34.0f / 200.0f;
  ASSERT_TRUE(manager.handle_sample(sample));
  QCoreApplication::processEvents();
  ASSERT_NE(QApplication::activePopupWidget(), nullptr) << "the AR tap did not open the list";

  manager.tick();
  ASSERT_EQ(diagnostics.count("popup open over this panel"), 1u);
  const auto * opened = diagnostics.find("popup open over this panel");
  ASSERT_NE(opened, nullptr);
  EXPECT_EQ(opened->severity, qar_ar_core::DiagnosticSeverity::Info);
  EXPECT_EQ(opened->panel_key, "off");
  EXPECT_NE(opened->message.find("kept off the desktop"), std::string::npos) << opened->message;

  // Open and unchanged: the count does not move, so nothing more is said.
  manager.tick();
  manager.tick();
  manager.tick();
  EXPECT_EQ(diagnostics.count("popup open over this panel"), 1u);
  EXPECT_EQ(diagnostics.count("popup closed"), 0u);

  combo->hidePopup();
  QCoreApplication::processEvents();
  manager.tick();
  ASSERT_EQ(diagnostics.count("popup closed"), 1u);
  EXPECT_EQ(diagnostics.find("popup closed")->panel_key, "off");
  manager.tick();
  EXPECT_EQ(diagnostics.count("popup closed"), 1u);
  EXPECT_EQ(diagnostics.count("popup open over this panel"), 1u);
}
