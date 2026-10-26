// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The brightness of what a panel sends: frame_brightness on synthetic buffers, and the
// diagnostics a stream fills from the frame it has just submitted (at most once a second).

#include <gtest/gtest.h>

#include "qar_qt_streaming/frame_luminance.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QPalette>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <vector>

using qar_qt_streaming::FrameBrightness;
using qar_qt_streaming::frame_brightness;
using qar_qt_streaming::kBrightnessNotMeasured;
using qar_qt_streaming::mean_luminance;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStream;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

/// An R8G8B8A8 frame the test can paint pixel by pixel, rows `stride_bytes` apart.
struct Frame
{
  Frame(std::uint32_t w, std::uint32_t h, std::uint32_t stride_bytes_in, std::uint8_t r,
    std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
  : width(w), height(h), stride_bytes(stride_bytes_in),
    bytes(static_cast<std::size_t>(stride_bytes_in) * h, 0)
  {
    paint_rows(0, h, r, g, b, a);
  }

  void paint_rows(std::uint32_t first, std::uint32_t end, std::uint8_t r, std::uint8_t g,
    std::uint8_t b, std::uint8_t a = 255)
  {
    for (std::uint32_t y = first; y < end && y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        std::uint8_t * p = bytes.data() + static_cast<std::size_t>(y) * stride_bytes + x * 4u;
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
      }
    }
  }

  qar_ar_core::PanelPixels pixels() const
  {
    qar_ar_core::PanelPixels out;
    out.data = bytes.data();
    out.width = width;
    out.height = height;
    out.stride_bytes = stride_bytes;
    return out;
  }

  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t stride_bytes;
  std::vector<std::uint8_t> bytes;
};

/// A 200 x 120 panel: a strip of one colour on top, the page below it in another, so the
/// brightness of the two can differ. The strip is 24 px high (20 % of the panel).
class StripedWidget
{
public:
  StripedWidget(QColor strip, QColor page)
  {
    widget_ = new QWidget();
    widget_->setAutoFillBackground(true);
    set_colour(widget_, page);
    strip_ = new QWidget(widget_);
    strip_->setAutoFillBackground(true);
    set_colour(strip_, strip);
    strip_->setGeometry(0, 0, 200, 24);
    widget_->resize(200, 120);
    widget_->show();
    widget_->ensurePolished();
    QCoreApplication::processEvents();
  }

  ~StripedWidget() { delete widget_; }

  QWidget * get() const { return widget_; }

  void recolour_page(QColor page)
  {
    set_colour(widget_, page);
    QCoreApplication::processEvents();
  }

private:
  static void set_colour(QWidget * widget, QColor colour)
  {
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, colour);
    widget->setPalette(palette);
  }

  QWidget * widget_ = nullptr;
  QWidget * strip_ = nullptr;
};

PanelStreamOptions
options_no_pacing()
{
  PanelStreamOptions options;
  options.max_fps = 0;              // every tick captures
  options.keepalive_seconds = 0.0;  // only a changed frame is sent
  return options;
}

}  // namespace

// --- frame_brightness ---------------------------------------------------------

TEST(FrameBrightness, AWhiteFrameReadsWhiteEverywhereAndOpaque)
{
  const FrameBrightness b = frame_brightness(Frame(64, 64, 256, 255, 255, 255).pixels());
  EXPECT_NEAR(b.all, 255.0, 1e-9);
  EXPECT_NEAR(b.top, 255.0, 1e-9);
  EXPECT_NEAR(b.rest, 255.0, 1e-9);
  EXPECT_NEAR(b.alpha, 255.0, 1e-9);
}

TEST(FrameBrightness, ABlackOpaqueFrameIsBlackButNotTransparent)
{
  const FrameBrightness b = frame_brightness(Frame(64, 64, 256, 0, 0, 0).pixels());
  EXPECT_NEAR(b.all, 0.0, 1e-12);
  EXPECT_NEAR(b.top, 0.0, 1e-12);
  EXPECT_NEAR(b.rest, 0.0, 1e-12);
  EXPECT_NEAR(b.alpha, 255.0, 1e-9) << "black is a colour; transparent is a different fault";
}

TEST(FrameBrightness, ATransparentFrameReadsAlphaZeroAndKeepsItsColour)
{
  const FrameBrightness b = frame_brightness(Frame(64, 64, 256, 200, 100, 50, 0).pixels());
  EXPECT_NEAR(b.alpha, 0.0, 1e-12);
  EXPECT_NEAR(b.all, 124.2, 1e-9) << "0.299 * 200 + 0.587 * 100 + 0.114 * 50";
  EXPECT_NEAR(b.top, 124.2, 1e-9);
  EXPECT_NEAR(b.rest, 124.2, 1e-9);
}

TEST(FrameBrightness, AlphaIsTheMeanOverTheSampledPixels)
{
  // 64 x 64 sampled every 8th pixel: 8 rows of 8. Half of the sampled rows opaque, half not.
  Frame frame(64, 64, 256, 10, 10, 10, 255);
  frame.paint_rows(32, 64, 10, 10, 10, 0);
  EXPECT_NEAR(frame_brightness(frame.pixels()).alpha, 127.5, 1e-9);
}

TEST(FrameBrightness, TheTopRowsAreMeasuredApartFromTheRest)
{
  // A bright strip over a black page: the top 15 % of 64 rows is 9 rows, so the sampled rows 0
  // and 8 are the top and the other six are the rest. The strip is 12 rows high.
  Frame frame(64, 64, 256, 0, 0, 0);
  frame.paint_rows(0, 12, 255, 255, 255);
  const FrameBrightness b = frame_brightness(frame.pixels());
  EXPECT_NEAR(b.top, 255.0, 1e-9);
  EXPECT_NEAR(b.rest, 0.0, 1e-12) << "rows 16 and below are black";
  EXPECT_NEAR(b.all, 255.0 * 2.0 / 8.0, 1e-9) << "two of the eight sampled rows are white";

  // The other way round: a black strip over a bright page.
  Frame inverse(64, 64, 256, 255, 255, 255);
  inverse.paint_rows(0, 12, 0, 0, 0);
  const FrameBrightness c = frame_brightness(inverse.pixels());
  EXPECT_NEAR(c.top, 0.0, 1e-12);
  EXPECT_NEAR(c.rest, 255.0, 1e-9);
}

TEST(FrameBrightness, AtLeastOneRowIsTheTopWhateverTheFractionIs)
{
  Frame frame(64, 64, 256, 255, 255, 255);
  frame.paint_rows(1, 64, 0, 0, 0);  // only row 0 is white
  // A fraction that rounds to no row still gives the top one row.
  const FrameBrightness none = frame_brightness(frame.pixels(), 0.0);
  EXPECT_NEAR(none.top, 255.0, 1e-9);
  EXPECT_NEAR(none.rest, 0.0, 1e-12);
  const FrameBrightness tiny = frame_brightness(frame.pixels(), 0.001);
  EXPECT_NEAR(tiny.top, 255.0, 1e-9);
  EXPECT_NEAR(tiny.rest, 0.0, 1e-12);
}

TEST(FrameBrightness, AFrameShorterThanTheStepHasNoRest)
{
  // Only row 0 is sampled, and it is the top: nothing below it was measured.
  const FrameBrightness b = frame_brightness(Frame(40, 4, 160, 255, 255, 255).pixels());
  EXPECT_NEAR(b.all, 255.0, 1e-9);
  EXPECT_NEAR(b.top, 255.0, 1e-9);
  EXPECT_EQ(b.rest, kBrightnessNotMeasured);
  EXPECT_NEAR(b.alpha, 255.0, 1e-9);
}

TEST(FrameBrightness, AFractionOfOneMakesEveryRowTheTop)
{
  Frame frame(64, 64, 256, 90, 90, 90);
  const FrameBrightness b = frame_brightness(frame.pixels(), 1.0);
  EXPECT_NEAR(b.top, 90.0, 1e-9);
  EXPECT_EQ(b.rest, kBrightnessNotMeasured);
  EXPECT_NEAR(frame_brightness(frame.pixels(), 7.5).top, 90.0, 1e-9) << "more than all is all";
}

TEST(FrameBrightness, AllIsExactlyTheMeanLuminanceOfTheSameSample)
{
  // A frame with a different colour in every sampled pixel, so that a different order of
  // summing or a different sample could not give the same number.
  Frame frame(70, 50, 320, 0, 0, 0);
  for (std::uint32_t y = 0; y < frame.height; ++y) {
    for (std::uint32_t x = 0; x < frame.width; ++x) {
      std::uint8_t * p = frame.bytes.data() + static_cast<std::size_t>(y) * 320u + x * 4u;
      p[0] = static_cast<std::uint8_t>((x * 7 + y * 3) & 0xFF);
      p[1] = static_cast<std::uint8_t>((x * 5 + y * 11) & 0xFF);
      p[2] = static_cast<std::uint8_t>((x * 13 + y * 2) & 0xFF);
      p[3] = static_cast<std::uint8_t>((x + y) & 0xFF);
    }
  }
  for (const std::uint32_t step : {1u, 3u, 8u}) {
    EXPECT_EQ(
      frame_brightness(frame.pixels(), 0.15, step).all, mean_luminance(frame.pixels(), step))
      << "step " << step;
  }
}

TEST(FrameBrightness, RowsAreStrideBytesApartAndThePaddingIsNeverRead)
{
  // 10 pixels wide, rows 64 bytes apart: the 24 bytes of padding on each row are white and
  // must not count.
  Frame frame(10, 10, 64, 100, 100, 100);
  for (std::uint32_t y = 0; y < 10; ++y) {
    for (std::uint32_t i = 40; i < 64; ++i) {
      frame.bytes[static_cast<std::size_t>(y) * 64u + i] = 255;
    }
  }
  const FrameBrightness b = frame_brightness(frame.pixels(), 0.15, 1);
  EXPECT_NEAR(b.all, 100.0, 1e-9);
  EXPECT_NEAR(b.alpha, 255.0, 1e-9);
}

TEST(FrameBrightness, AFrameWithoutDataOrSizeIsNotMeasured)
{
  const auto expect_nothing = [](const FrameBrightness & b, const char * what) {
      EXPECT_EQ(b.all, kBrightnessNotMeasured) << what;
      EXPECT_EQ(b.top, kBrightnessNotMeasured) << what;
      EXPECT_EQ(b.rest, kBrightnessNotMeasured) << what;
      EXPECT_EQ(b.alpha, kBrightnessNotMeasured) << what;
    };
  expect_nothing(frame_brightness(qar_ar_core::PanelPixels{}), "no data");
  Frame frame(16, 16, 64, 255, 255, 255);
  qar_ar_core::PanelPixels no_width = frame.pixels();
  no_width.width = 0;
  expect_nothing(frame_brightness(no_width), "no width");
  qar_ar_core::PanelPixels no_height = frame.pixels();
  no_height.height = 0;
  expect_nothing(frame_brightness(no_height), "no height");
  expect_nothing(frame_brightness(frame.pixels(), 0.15, 0), "a step of zero reads nothing");
}

// --- the diagnostics of a stream ------------------------------------------------

TEST(StreamBrightness, NothingIsMeasuredBeforeTheFirstSubmittedFrame)
{
  qar_ar_core::PanelStreamDiagnostics fresh;
  EXPECT_LT(fresh.luminance_all, 0.0);
  EXPECT_LT(fresh.luminance_top, 0.0);
  EXPECT_LT(fresh.luminance_rest, 0.0);
  EXPECT_LT(fresh.alpha_mean, 0.0);
  EXPECT_EQ(fresh.luminance_frame, 0u);
}

TEST(StreamBrightness, AWhiteWidgetFillsTheDiagnosticsWithWhite)
{
  StripedWidget widget(QColor(255, 255, 255), QColor(255, 255, 255));
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", widget.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  const auto & d = stream.diagnostics();
  EXPECT_NEAR(d.luminance_all, 255.0, 1e-9);
  EXPECT_NEAR(d.luminance_top, 255.0, 1e-9);
  EXPECT_NEAR(d.luminance_rest, 255.0, 1e-9);
  EXPECT_NEAR(d.alpha_mean, 255.0, 1e-9);
  EXPECT_EQ(d.luminance_frame, d.frames_submitted);
  EXPECT_EQ(d.luminance_frame, 1u);
}

TEST(StreamBrightness, ABlackWidgetFillsTheDiagnosticsWithBlackThatIsOpaque)
{
  StripedWidget widget(QColor(0, 0, 0), QColor(0, 0, 0));
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", widget.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  const auto & d = stream.diagnostics();
  EXPECT_NEAR(d.luminance_all, 0.0, 1e-12);
  EXPECT_NEAR(d.luminance_top, 0.0, 1e-12);
  EXPECT_NEAR(d.luminance_rest, 0.0, 1e-12);
  EXPECT_NEAR(d.alpha_mean, 255.0, 1e-9);
  EXPECT_EQ(d.luminance_frame, 1u);
}

TEST(StreamBrightness, ABrightStripOverABlackPageIsToldApart)
{
  StripedWidget widget(QColor(255, 255, 255), QColor(0, 0, 0));
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", widget.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  // The strip is 24 px of 120: the sampled rows 0, 8 and 16 lie in the top 15 % (18 rows) and
  // are white; the page starts at row 24, so every other sampled row is black.
  const auto & d = stream.diagnostics();
  EXPECT_NEAR(d.luminance_top, 255.0, 1e-9);
  EXPECT_NEAR(d.luminance_rest, 0.0, 1e-12);
  EXPECT_GT(d.luminance_all, 0.0);
  EXPECT_LT(d.luminance_all, 255.0);
}

TEST(StreamBrightness, ItIsMeasuredAtMostOncePerSecond)
{
  StripedWidget widget(QColor(255, 255, 255), QColor(255, 255, 255));
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", widget.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  ASSERT_EQ(stream.diagnostics().luminance_frame, 1u);
  EXPECT_NEAR(stream.diagnostics().luminance_rest, 255.0, 1e-9);

  // Frames keep arriving with another page colour, and the diagnostics keep the old reading.
  widget.recolour_page(QColor(0, 0, 0));
  ASSERT_TRUE(stream.tick(0.3));
  widget.recolour_page(QColor(255, 255, 255));
  ASSERT_TRUE(stream.tick(0.6));
  widget.recolour_page(QColor(0, 0, 0));
  ASSERT_TRUE(stream.tick(0.9));
  EXPECT_EQ(stream.diagnostics().frames_submitted, 4u);
  EXPECT_EQ(stream.diagnostics().luminance_frame, 1u) << "no second reading inside a second";
  EXPECT_NEAR(stream.diagnostics().luminance_rest, 255.0, 1e-9);

  // A second after the reading, the next submitted frame is measured, and it is that frame.
  widget.recolour_page(QColor(255, 255, 255));
  ASSERT_TRUE(stream.tick(1.0));
  EXPECT_EQ(stream.diagnostics().frames_submitted, 5u);
  EXPECT_EQ(stream.diagnostics().luminance_frame, 5u);
  EXPECT_NEAR(stream.diagnostics().luminance_rest, 255.0, 1e-9);

  // The limit counts from the last reading, not from the first: a frame 0.9 s later is skipped.
  widget.recolour_page(QColor(0, 0, 0));
  ASSERT_TRUE(stream.tick(1.9));
  EXPECT_EQ(stream.diagnostics().luminance_frame, 5u);
  EXPECT_NEAR(stream.diagnostics().luminance_rest, 255.0, 1e-9);
  widget.recolour_page(QColor(255, 255, 255));
  ASSERT_TRUE(stream.tick(2.0));
  EXPECT_EQ(stream.diagnostics().luminance_frame, 7u);
}

TEST(StreamBrightness, OnlyAFrameThatWasSubmittedIsMeasured)
{
  StripedWidget widget(QColor(255, 255, 255), QColor(255, 255, 255));
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", widget.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  // Unchanged: captured and not submitted, so there is nothing new to measure, however late.
  EXPECT_FALSE(stream.tick(5.0));
  EXPECT_EQ(stream.diagnostics().luminance_frame, 1u);
  // Changed, a long time after the first reading: measured at once.
  widget.recolour_page(QColor(0, 0, 0));
  ASSERT_TRUE(stream.tick(6.0));
  EXPECT_EQ(stream.diagnostics().luminance_frame, 2u);
  EXPECT_NEAR(stream.diagnostics().luminance_rest, 0.0, 1e-12);
}

TEST(StreamBrightness, TheManagersSnapshotCarriesTheReading)
{
  StripedWidget widget(QColor(255, 255, 255), QColor(0, 0, 0));
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto clock = std::make_shared<qar_qt_streaming::test_support::ManualClock>();
  qar_qt_streaming::QtPanelStreamingConfig config;
  config.panel_sender_factory = factory;
  config.clock = clock;
  qar_qt_streaming::QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", widget.get(), options_no_pacing()).ok());
  EXPECT_LT(manager.diagnostics().panels.at(0).luminance_all, 0.0) << "nothing measured yet";
  clock->advance(0.1);
  ASSERT_EQ(manager.tick(), 1u);

  const qar_ar_core::PanelStreamDiagnostics & d = manager.diagnostics().panels.at(0);
  EXPECT_NEAR(d.luminance_top, 255.0, 1e-9);
  EXPECT_NEAR(d.luminance_rest, 0.0, 1e-12);
  EXPECT_NEAR(d.alpha_mean, 255.0, 1e-9);
  EXPECT_EQ(d.luminance_frame, 1u);
}
