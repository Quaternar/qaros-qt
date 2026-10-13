// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Region substitution (variant b): showing a LatestVideoFrame's live video in
// place of a region QWidget::render() cannot paint -- RViz's RenderPanel,
// which embeds a native window via QWidget::createWindowContainer. Exercised
// at two levels: QtWidgetCapture directly, and the QtPanelStreamManager
// passthrough.
//
// Include order note: qt_panel_stream_manager.hpp (via panel_pointer_state.hpp)
// declares a method literally named `emit`, which only parses because no real
// Qt header has defined Qt's `emit` keyword-macro yet at that point -- see the
// ordering comment in qt_panel_stream.hpp. So the qar_qt_streaming project
// headers come first here, and plain Qt headers (<QLabel> etc.) after.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QLabel>
#include <QPalette>
#include <QWidget>

#include <memory>
#include <utility>

using qar_qt_streaming::LatestVideoFrame;
using qar_ar_core::PanelPixels;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_qt_streaming::QtWidgetCapture;
using qar_ar_core::Status;

namespace
{

QColor
pixel_at(const PanelPixels & p, int x, int y)
{
  const QImage view(
    p.data, static_cast<int>(p.width), static_cast<int>(p.height),
    static_cast<int>(p.stride_bytes), QImage::Format_RGBA8888);
  return view.pixelColor(x, y);
}

void
paint_widget(QWidget * widget, const QColor & colour)
{
  widget->setAutoFillBackground(true);
  QPalette palette = widget->palette();
  palette.setColor(QPalette::Window, colour);
  widget->setPalette(palette);
}

QImage
solid_frame(int width, int height, const QColor & colour)
{
  QImage img(width, height, QImage::Format_RGBA8888);
  img.fill(colour);
  return img;
}

/// A 400x300 panel: a yellow QLabel filling the top half (ordinary Qt content
/// that must be left alone) and a plain black QWidget filling the bottom half
/// (stands in for RViz's RenderPanel -- the thing this feature substitutes
/// video into).
class RegionFixture
{
public:
  RegionFixture()
  {
    parent_ = new QWidget();
    parent_->resize(400, 300);
    paint_widget(parent_, Qt::black);

    label_ = new QLabel(parent_);
    label_->setGeometry(0, 0, 400, 150);
    paint_widget(label_, Qt::yellow);

    render_panel_ = new QWidget(parent_);
    render_panel_->setGeometry(0, 150, 400, 150);
    paint_widget(render_panel_, Qt::black);

    parent_->show();
    parent_->ensurePolished();
    QCoreApplication::processEvents();
  }

  ~RegionFixture() { delete parent_; }

  QWidget * parent() const { return parent_; }
  QWidget * render_panel() const { return render_panel_; }
  QLabel * label() const { return label_; }

private:
  QWidget * parent_ = nullptr;
  QLabel * label_ = nullptr;
  QWidget * render_panel_ = nullptr;
};

}  // namespace

TEST(RegionSubstitution, SolidGreenSourceFillsTheRegionLabelUntouched)
{
  RegionFixture fixture;
  QtWidgetCapture capture(fixture.parent());

  auto source = std::make_shared<LatestVideoFrame>();
  source->publish(solid_frame(400, 150, Qt::green));  // exactly the region's own size
  capture.set_region_source(fixture.render_panel(), source);

  ASSERT_TRUE(capture.capture(400, 300, 1.0)) << capture.last_error();
  EXPECT_EQ(capture.substituted_region_count(), 1u);

  const PanelPixels pixels = capture.pixels();
  EXPECT_EQ(pixel_at(pixels, 200, 200), QColor(0, 255, 0, 255)) << "inside the render panel";
  EXPECT_EQ(pixel_at(pixels, 200, 75), QColor(255, 255, 0, 255)) << "the label must be untouched";
}

TEST(RegionSubstitution, NewFramePublishedMarksContentChanged)
{
  RegionFixture fixture;
  QtWidgetCapture capture(fixture.parent());
  auto source = std::make_shared<LatestVideoFrame>();
  source->publish(solid_frame(400, 150, Qt::green));
  capture.set_region_source(fixture.render_panel(), source);

  ASSERT_TRUE(capture.capture(400, 300, 1.0));
  ASSERT_TRUE(capture.capture(400, 300, 1.0));
  EXPECT_FALSE(capture.content_changed()) << "an unchanged substituted frame is still idle";

  source->publish(solid_frame(400, 150, Qt::blue));
  ASSERT_TRUE(capture.capture(400, 300, 1.0));
  EXPECT_TRUE(capture.content_changed());
  EXPECT_EQ(pixel_at(capture.pixels(), 200, 200), QColor(0, 0, 255, 255));
}

TEST(RegionSubstitution, DestroyedRegionIsIgnoredWithoutCrashing)
{
  RegionFixture fixture;
  QtWidgetCapture capture(fixture.parent());
  auto * doomed = new QWidget(fixture.parent());
  doomed->setGeometry(0, 150, 400, 150);

  auto source = std::make_shared<LatestVideoFrame>();
  source->publish(solid_frame(10, 10, Qt::green));
  capture.set_region_source(doomed, source);

  delete doomed;
  QCoreApplication::processEvents();

  EXPECT_TRUE(capture.capture(400, 300, 1.0)) << capture.last_error();
  EXPECT_EQ(capture.substituted_region_count(), 0u);
}

TEST(RegionSubstitution, RegionOutsideTheTreeIsIgnored)
{
  RegionFixture fixture;
  QWidget stranger;
  stranger.resize(50, 50);

  QtWidgetCapture capture(fixture.parent());
  auto source = std::make_shared<LatestVideoFrame>();
  source->publish(solid_frame(10, 10, Qt::green));
  capture.set_region_source(&stranger, source);

  EXPECT_TRUE(capture.capture(400, 300, 1.0)) << capture.last_error();
  EXPECT_EQ(capture.substituted_region_count(), 0u);
}

TEST(RegionSubstitution, WidgetItselfAsRegionSubstitutesTheWholeCapture)
{
  QWidget widget;
  widget.resize(100, 60);
  widget.show();
  widget.ensurePolished();
  QCoreApplication::processEvents();

  QtWidgetCapture capture(&widget);
  auto source = std::make_shared<LatestVideoFrame>();
  source->publish(solid_frame(100, 60, Qt::green));
  capture.set_region_source(&widget, source);

  ASSERT_TRUE(capture.capture(100, 60, 1.0)) << capture.last_error();
  EXPECT_EQ(capture.substituted_region_count(), 1u);
  EXPECT_EQ(pixel_at(capture.pixels(), 50, 30), QColor(0, 255, 0, 255));
}

TEST(RegionSubstitution, ClearRegionSourceStopsSubstitutingJustThatOne)
{
  RegionFixture fixture;
  QtWidgetCapture capture(fixture.parent());
  auto source = std::make_shared<LatestVideoFrame>();
  source->publish(solid_frame(400, 150, Qt::green));
  capture.set_region_source(fixture.render_panel(), source);
  ASSERT_TRUE(capture.capture(400, 300, 1.0));
  ASSERT_EQ(capture.substituted_region_count(), 1u);

  capture.clear_region_source(fixture.render_panel());
  ASSERT_TRUE(capture.capture(400, 300, 1.0));
  EXPECT_EQ(capture.substituted_region_count(), 0u);
  EXPECT_EQ(pixel_at(capture.pixels(), 200, 200), QColor(0, 0, 0, 255))
    << "back to the render panel's own (black) rendering";
}

TEST(RegionSubstitution, ClearRegionSourcesStopsAll)
{
  RegionFixture fixture;
  QtWidgetCapture capture(fixture.parent());
  auto source = std::make_shared<LatestVideoFrame>();
  source->publish(solid_frame(400, 150, Qt::green));
  capture.set_region_source(fixture.render_panel(), source);
  capture.clear_region_sources();

  ASSERT_TRUE(capture.capture(400, 300, 1.0));
  EXPECT_EQ(capture.substituted_region_count(), 0u);
}

// ---------------------------------------------------------------------------
// QtPanelStreamManager passthrough
// ---------------------------------------------------------------------------

namespace
{

QtPanelStreamingConfig
make_config(std::shared_ptr<qar_qt_streaming::test_support::MockQarPanelSenderFactory> factory)
{
  QtPanelStreamingConfig config;
  config.panel_sender_factory = std::move(factory);
  return config;
}

}  // namespace

TEST(PanelRegionSourceManager, SetOnUnknownKeyIsNotFound)
{
  auto factory = std::make_shared<qar_qt_streaming::test_support::MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));

  QWidget region;
  auto source = std::make_shared<LatestVideoFrame>();
  const auto result = manager.set_panel_region_source("no-such-panel", &region, source);
  EXPECT_EQ(result.status, Status::NotFound);
  EXPECT_FALSE(result.ok());
}

TEST(PanelRegionSourceManager, ClearOnUnknownKeyIsNotFound)
{
  auto factory = std::make_shared<qar_qt_streaming::test_support::MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));

  const auto result = manager.clear_panel_region_sources("no-such-panel");
  EXPECT_EQ(result.status, Status::NotFound);
  EXPECT_FALSE(result.ok());
}

TEST(PanelRegionSourceManager, SetAndClearOnAKnownKeySucceed)
{
  auto factory = std::make_shared<qar_qt_streaming::test_support::MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));

  RegionFixture fixture;
  PanelStreamOptions options;
  options.input.enabled = false;
  ASSERT_TRUE(manager.add_panel("cam", fixture.parent(), options).ok());

  auto source = std::make_shared<LatestVideoFrame>();
  EXPECT_TRUE(manager.set_panel_region_source("cam", fixture.render_panel(), source).ok());
  EXPECT_TRUE(manager.clear_panel_region_sources("cam").ok());
}
