// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// VideoPanelWidget: a stand-alone AR video panel's content (variant a),
// captured through the real QtWidgetCapture path exactly like every other
// widget in this suite (see qt_widget_capture_tests.cpp's Fixture). Built
// like panel_tombstone.hpp's placeholder -- parentless, WA_DontShowOnScreen,
// already show()n -- so no window is ever mapped under the offscreen platform
// plugin.

#include <gtest/gtest.h>

#include "qar_qt_streaming/video_panel_widget.hpp"

#include "qar_qt_streaming/qt_widget_capture.hpp"

#include <QColor>
#include <QImage>

#include <memory>

using qar_qt_streaming::LatestVideoFrame;
using qar_ar_core::PanelPixels;
using qar_qt_streaming::QtWidgetCapture;
using qar_qt_streaming::VideoPanelWidget;

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

QImage
solid_frame(int width, int height, const QColor & colour)
{
  QImage img(width, height, QImage::Format_RGBA8888);
  img.fill(colour);
  return img;
}

}  // namespace

TEST(VideoPanelWidget, InitialSizeFollowsSixteenByNineFromLogicalWidth)
{
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget default_width(source, QStringLiteral("cam"));
  EXPECT_EQ(default_width.width(), 640);
  EXPECT_EQ(default_width.height(), 360);

  VideoPanelWidget custom_width(source, QStringLiteral("cam"), 320);
  EXPECT_EQ(custom_width.width(), 320);
  EXPECT_EQ(custom_width.height(), 180);
}

TEST(VideoPanelWidget, NoFrameCaptureIsNotUniformlyBlack)
{
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget widget(source, QStringLiteral("cam"), 160);
  QtWidgetCapture capture(&widget);

  ASSERT_TRUE(capture.capture(widget.width(), widget.height(), 1.0)) << capture.last_error();
  const QColor centre = pixel_at(capture.pixels(), widget.width() / 2, widget.height() / 2);
  EXPECT_NE(centre, QColor(0, 0, 0, 255))
    << "the waiting state must be visually distinct from letterbox black, "
    << "since the status text alone rasterizes nothing under this platform";
}

TEST(VideoPanelWidget, PublishedFrameShowsAtTheCentre)
{
  auto source = std::make_shared<LatestVideoFrame>();
  // Exactly the default widget's own 16:9 aspect -- no letterbox bars are
  // expected here.
  VideoPanelWidget widget(source, QStringLiteral("cam"));
  source->publish(solid_frame(160, 90, Qt::red));

  QtWidgetCapture capture(&widget);
  ASSERT_TRUE(capture.capture(widget.width(), widget.height(), 1.0)) << capture.last_error();
  const QColor centre = pixel_at(capture.pixels(), widget.width() / 2, widget.height() / 2);
  EXPECT_EQ(centre, QColor(255, 0, 0, 255));
}

TEST(VideoPanelWidget, MismatchedAspectFrameLetterboxesWithBlackBars)
{
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget widget(source, QStringLiteral("cam"), 200);  // 200 x 112
  source->publish(solid_frame(100, 100, Qt::red));              // square -> side bars

  QtWidgetCapture capture(&widget);
  ASSERT_TRUE(capture.capture(widget.width(), widget.height(), 1.0)) << capture.last_error();
  const PanelPixels pixels = capture.pixels();

  EXPECT_EQ(pixel_at(pixels, widget.width() / 2, widget.height() / 2), QColor(255, 0, 0, 255))
    << "the frame itself, at the centre";
  EXPECT_EQ(pixel_at(pixels, 1, widget.height() / 2), QColor(0, 0, 0, 255))
    << "the left letterbox bar";
  EXPECT_EQ(pixel_at(pixels, widget.width() - 2, widget.height() / 2), QColor(0, 0, 0, 255))
    << "the right letterbox bar";
}

TEST(VideoPanelWidget, PaintedSequenceTracksTheLatestFrame)
{
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget widget(source, QStringLiteral("cam"), 160);
  QtWidgetCapture capture(&widget);

  ASSERT_TRUE(capture.capture(widget.width(), widget.height(), 1.0));
  EXPECT_EQ(widget.painted_sequence(), source->sequence());
  EXPECT_EQ(widget.painted_sequence(), 0u);

  source->publish(solid_frame(160, 90, Qt::blue));
  ASSERT_TRUE(capture.capture(widget.width(), widget.height(), 1.0));
  EXPECT_EQ(widget.painted_sequence(), 1u);
  EXPECT_EQ(widget.painted_sequence(), source->sequence());
}

TEST(VideoPanelWidget, SyncToFrameReturnsFalseWithNoFrameYet)
{
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget widget(source, QStringLiteral("cam"), 640);
  EXPECT_FALSE(widget.sync_to_frame());
  EXPECT_EQ(widget.width(), 640);
  EXPECT_EQ(widget.height(), 360);
}

TEST(VideoPanelWidget, SyncToFrameIgnoresACloseEnoughAspect)
{
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget widget(source, QStringLiteral("cam"), 640);  // 640 x 360
  source->publish(solid_frame(160, 90, Qt::red));                // identical aspect
  EXPECT_FALSE(widget.sync_to_frame());
  EXPECT_EQ(widget.width(), 640);
  EXPECT_EQ(widget.height(), 360);
}

TEST(VideoPanelWidget, SyncToFrameResizesHeightWhenAspectDrifts)
{
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget widget(source, QStringLiteral("cam"), 640);  // 640 x 360, aspect 16:9
  source->publish(solid_frame(100, 100, Qt::red));               // aspect 1:1 -- well past 1%

  EXPECT_TRUE(widget.sync_to_frame());
  EXPECT_EQ(widget.width(), 640) << "width is kept; only height follows the new aspect";
  EXPECT_EQ(widget.height(), 640);

  // Settled: calling again with the same frame must not resize further.
  EXPECT_FALSE(widget.sync_to_frame());
}
