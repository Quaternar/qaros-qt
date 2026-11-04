// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Widgets at a fractional device pixel ratio. The ctest entries of this suite run the test
// executable with QT_SCALE_FACTOR=1.25 and 1.5 (and the environment variable that makes
// test_main.cpp let a fractional ratio through); a plain run sees the ratio of the platform
// plugin, which is 1.0, and the same expectations hold with that ratio.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_panel_input_injector.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/qt_widget_capture.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"
#include "qar_qt_streaming/qt_compat.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QPalette>
#include <QPointF>
#include <QScreen>
#include <QWidget>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace seam = qar_qt_streaming::seam;

namespace qt_compat = qar_qt_streaming::qt_compat;
using qar_qt_streaming::seam::PanelPixels;
using qar_qt_streaming::seam::PanelPointerSample;
using qar_qt_streaming::seam::PointerGesture;
using qar_qt_streaming::seam::PointerPhase;
using qar_qt_streaming::PanelFrame;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::PointerAction;
using qar_qt_streaming::PointerActionKind;
using qar_qt_streaming::QtPanelInputInjector;
using qar_qt_streaming::QtPanelStream;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_qt_streaming::QtWidgetCapture;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

/// The ratio this run is meant to have: QT_SCALE_FACTOR, else 1.0.
double
expected_ratio()
{
  bool ok = false;
  const double value = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(&ok);
  return ok && value > 0.0 ? value : 1.0;
}

std::uint32_t
scaled(int logical, double ratio)
{
  return static_cast<std::uint32_t>(std::lround(logical * ratio));
}

std::uint32_t
align_even(std::uint32_t value)
{
  return value + (value % 2u);
}

void
fill_with(QWidget * widget, const QColor & colour)
{
  widget->setAutoFillBackground(true);
  QPalette palette = widget->palette();
  palette.setColor(QPalette::Window, colour);
  widget->setPalette(palette);
}

/// A top-level widget of @p width x @p height logical pixels.
class ShownWidget
{
public:
  ShownWidget(int width, int height)
  {
    widget_ = new QWidget();
    widget_->resize(width, height);
  }

  ~ShownWidget() {delete widget_;}

  ShownWidget(const ShownWidget &) = delete;
  ShownWidget & operator=(const ShownWidget &) = delete;

  /// Call once the children are in place.
  void show()
  {
    widget_->show();
    widget_->ensurePolished();
    QCoreApplication::processEvents();
  }

  QWidget * widget() const {return widget_;}

private:
  QWidget * widget_ = nullptr;
};

/// Counts presses and records the position of the last mouse event it gets.
class MouseProbe : public QWidget
{
public:
  using QWidget::QWidget;

  int presses = 0;
  QPointF last_position;

protected:
  void mousePressEvent(QMouseEvent * event) override
  {
    ++presses;
    last_position = qt_compat::mouse_local_position(*event);
  }

  void mouseMoveEvent(QMouseEvent * event) override
  {
    last_position = qt_compat::mouse_local_position(*event);
  }
};

PanelStreamOptions
options_no_pacing()
{
  PanelStreamOptions options;
  options.max_fps = 0;
  options.keepalive_seconds = 0.0;
  options.placement.pose.orientation = seam::Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
  options.placement.pose.position = seam::Vector3{0.0f, 0.0f, 0.0f};
  return options;
}

}  // namespace

TEST(FractionalDpr, TheEnvironmentGivesTheWidgetsTheRequestedPixelRatio)
{
  ShownWidget panel(200, 120);
  panel.show();
  const double ratio = expected_ratio();
  EXPECT_NEAR(panel.widget()->devicePixelRatioF(), ratio, 1e-9)
    << "QT_SCALE_FACTOR and QT_FONT_DPI=96 are the settings of the ctest entries";
  ASSERT_NE(QGuiApplication::primaryScreen(), nullptr);
  EXPECT_NEAR(QGuiApplication::primaryScreen()->devicePixelRatio(), ratio, 1e-9);
  // The ratio is not rounded to a whole number.
  if (ratio != std::floor(ratio)) {
    EXPECT_NE(panel.widget()->devicePixelRatioF(), std::round(ratio));
  }
}

TEST(FractionalDpr, TheBufferSizeIsTheLogicalSizeTimesTheRatioRoundedAndAligned)
{
  // 201 x 121 logical pixels give half-pixel products at 1.5 and fractions at 1.25.
  ShownWidget panel(201, 121);
  panel.show();
  const double ratio = panel.widget()->devicePixelRatioF();
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", panel.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  const auto & desc = factory->latest()->desc();
  EXPECT_EQ(desc.buffer_width, align_even(scaled(201, ratio)));
  EXPECT_EQ(desc.buffer_height, align_even(scaled(121, ratio)));
  // The physical size follows the logical size alone.
  EXPECT_NEAR(desc.placement.size.width_meters, 201 * 0.0012f, 1e-4f);
}

TEST(FractionalDpr, TheDiagnosticsNameTheDevicePixelRatio)
{
  ShownWidget panel(200, 120);
  panel.show();
  const double ratio = panel.widget()->devicePixelRatioF();
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", panel.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  EXPECT_NEAR(stream.diagnostics().device_pixel_ratio, ratio, 1e-9);
  EXPECT_NE(
    stream.diagnostics().last_geometry_summary.find("dpr " + std::to_string(ratio)),
    std::string::npos) << stream.diagnostics().last_geometry_summary;
}

TEST(FractionalDpr, TheCapturePaintsLogicalGeometryAcrossTheDevicePixelBuffer)
{
  // A white panel with a red stripe two logical pixels wide at x = 100.
  ShownWidget panel(200, 100);
  fill_with(panel.widget(), Qt::white);
  auto * stripe = new QWidget(panel.widget());
  stripe->setGeometry(100, 0, 2, 100);
  fill_with(stripe, Qt::red);
  panel.show();
  const double ratio = panel.widget()->devicePixelRatioF();

  const std::uint32_t width = scaled(200, ratio);
  const std::uint32_t height = scaled(100, ratio);
  QtWidgetCapture capture(panel.widget());
  ASSERT_TRUE(capture.capture(width, height, ratio)) << capture.last_error();
  const PanelPixels pixels = capture.pixels();
  ASSERT_EQ(pixels.width, width);
  ASSERT_EQ(pixels.height, height);

  const auto pixel_at = [&pixels](double logical_x, std::uint32_t y) {
      const auto x = static_cast<std::uint32_t>(std::floor(logical_x * pixels.width / 200.0));
      return pixels.data + y * pixels.stride_bytes + x * 4u;
    };
  const std::uint32_t y = height / 2;
  // The middle of the stripe (logical 101), then logical 98 and 104 on either side of it.
  const std::uint8_t * on_stripe = pixel_at(101.0, y);
  EXPECT_EQ(on_stripe[0], 255);
  EXPECT_EQ(on_stripe[1], 0);
  EXPECT_EQ(on_stripe[2], 0);
  const std::uint8_t * left = pixel_at(98.0, y);
  EXPECT_EQ(left[1], 255);
  const std::uint8_t * right = pixel_at(104.0, y);
  EXPECT_EQ(right[1], 255);
}

TEST(FractionalDpr, TheInjectorKeepsTheSubPixelPartOfAPanelPosition)
{
  ShownWidget panel(200, 120);
  auto * target = new MouseProbe(panel.widget());
  target->setGeometry(30, 20, 100, 60);
  target->setMouseTracking(true);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  PointerAction move;
  move.kind = PointerActionKind::Move;
  move.pointer_id = 1;
  move.x = 40.375;
  move.y = 25.625;
  ASSERT_TRUE(injector.deliver(move));
  EXPECT_NEAR(target->last_position.x(), 10.375, 1e-9);
  EXPECT_NEAR(target->last_position.y(), 5.625, 1e-9);

  PointerAction press = move;
  press.kind = PointerActionKind::Press;
  press.x = 90.25;
  press.y = 49.75;
  ASSERT_TRUE(injector.deliver(press));
  EXPECT_NEAR(target->last_position.x(), 60.25, 1e-9);
  EXPECT_NEAR(target->last_position.y(), 29.75, 1e-9);
  PointerAction release = press;
  release.kind = PointerActionKind::Release;
  injector.deliver(release);
}

TEST(FractionalDpr, AContactOnANarrowTargetPressesItAndItsNeighboursDoNot)
{
  ShownWidget panel(200, 120);
  auto * target = new MouseProbe(panel.widget());
  // Two pixels wide: from Qt 6.8 on, QWidget::childAt() never returns a child that is one pixel
  // wide or one pixel high.
  target->setGeometry(100, 0, 2, 120);
  panel.show();

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config;
  config.panel_sender_factory = factory;
  config.clock = std::make_shared<ManualClock>();
  config.ar_frame = PanelFrame{0.03, 0.03, 0.0, 0.0, 0.0};
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_no_pacing()).ok());
  manager.tick();

  std::uint32_t id = 1;
  const auto contact = [&](double x_px, double y_px) {
      PanelPointerSample sample;
      sample.pointer_id = id++;
      sample.gesture = PointerGesture::Click;
      sample.phase = PointerPhase::Instant;
      sample.has_panel_uv = true;
      sample.panel_key = "p";
      sample.u = static_cast<float>(x_px / 200.0);
      sample.v = static_cast<float>(y_px / 120.0);
      manager.handle_sample(sample);
    };
  // Two pixels to the left of the target, two to the right of it, and the target. A position
  // reaches the widgets rounded to a whole pixel, as the position of a mouse event does.
  contact(98.0, 60.0);
  contact(102.0, 60.0);
  EXPECT_EQ(target->presses, 0);
  contact(100.0, 60.0);
  EXPECT_EQ(target->presses, 1);
}
