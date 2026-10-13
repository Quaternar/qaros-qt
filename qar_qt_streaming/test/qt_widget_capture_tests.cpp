// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Exercises the REAL Qt capture path (QWidget::render into a reusable QImage)
// against a real widget tree, headless via the offscreen platform plugin. The
// dirty check tested here is what keeps an idle control panel off the network.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_widget_capture.hpp"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QCoreApplication>
#include <QPalette>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <chrono>
#include <cstring>
#include <ostream>
#include <vector>

using qar_qt_streaming::hash_pixels;
using qar_ar_core::PanelPixels;
using qar_qt_streaming::QtWidgetCapture;

namespace
{

/// A small widget tree whose rendered content can be changed on demand.
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

}  // namespace

TEST(HashPixels, IdenticalBuffersHashEqual)
{
  const std::vector<std::uint8_t> a(64 * 4, 0x7f);
  const std::vector<std::uint8_t> b(64 * 4, 0x7f);
  EXPECT_EQ(hash_pixels(a.data(), 16, 4, 64), hash_pixels(b.data(), 16, 4, 64));
}

TEST(HashPixels, OneChangedByteChangesTheHash)
{
  std::vector<std::uint8_t> a(64 * 4, 0x7f);
  const std::uint64_t before = hash_pixels(a.data(), 16, 4, 64);
  a[37] = 0x80;
  EXPECT_NE(before, hash_pixels(a.data(), 16, 4, 64));
}

TEST(HashPixels, StridePaddingIsNotHashed)
{
  // Two buffers whose PIXELS agree but whose scanline padding differs must hash
  // the same, or an unchanged panel would look dirty every tick.
  constexpr std::uint32_t width = 4;
  constexpr std::uint32_t height = 3;
  constexpr std::uint32_t stride = width * 4 + 8;  // 8 bytes of padding per row

  std::vector<std::uint8_t> a(stride * height, 0x11);
  std::vector<std::uint8_t> b(stride * height, 0x11);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t i = width * 4; i < stride; ++i) {
      b[y * stride + i] = 0xee;  // different padding only
    }
  }
  EXPECT_EQ(hash_pixels(a.data(), width, height, stride), hash_pixels(b.data(), width, height, stride));
}

TEST(HashPixels, NullBufferIsHandled)
{
  EXPECT_NO_THROW(hash_pixels(nullptr, 4, 4, 16));
}

TEST(WidgetCapture, ProducesABufferOfTheRequestedSize)
{
  Fixture fixture;
  QtWidgetCapture capture(fixture.widget());

  ASSERT_TRUE(capture.capture(200, 120, 1.0)) << capture.last_error();

  const PanelPixels pixels = capture.pixels();
  EXPECT_EQ(pixels.width, 200u);
  EXPECT_EQ(pixels.height, 120u);
  EXPECT_NE(pixels.data, nullptr);
  EXPECT_GE(pixels.stride_bytes, 200u * 4u);
}

TEST(WidgetCapture, FirstCaptureAlwaysCountsAsChanged)
{
  Fixture fixture;
  QtWidgetCapture capture(fixture.widget());

  ASSERT_TRUE(capture.capture(200, 120, 1.0));
  EXPECT_TRUE(capture.content_changed());
}

TEST(WidgetCapture, RecapturingAnUntouchedWidgetReportsNoChange)
{
  Fixture fixture;
  QtWidgetCapture capture(fixture.widget());

  ASSERT_TRUE(capture.capture(200, 120, 1.0));
  ASSERT_TRUE(capture.capture(200, 120, 1.0));
  EXPECT_FALSE(capture.content_changed());
}

TEST(WidgetCapture, EditingTheWidgetMakesTheNextCaptureDirty)
{
  Fixture fixture;
  QtWidgetCapture capture(fixture.widget());

  ASSERT_TRUE(capture.capture(200, 120, 1.0));
  ASSERT_TRUE(capture.capture(200, 120, 1.0));
  ASSERT_FALSE(capture.content_changed());

  fixture.mutate();
  ASSERT_TRUE(capture.capture(200, 120, 1.0));
  EXPECT_TRUE(capture.content_changed());
}

TEST(WidgetCapture, InvalidateForcesTheNextCaptureToCountAsChanged)
{
  Fixture fixture;
  QtWidgetCapture capture(fixture.widget());

  ASSERT_TRUE(capture.capture(200, 120, 1.0));
  ASSERT_TRUE(capture.capture(200, 120, 1.0));
  ASSERT_FALSE(capture.content_changed());

  capture.invalidate();
  ASSERT_TRUE(capture.capture(200, 120, 1.0));
  EXPECT_TRUE(capture.content_changed());
}

TEST(WidgetCapture, AResizeCountsAsChangedRatherThanComparingDifferentShapes)
{
  Fixture fixture;
  QtWidgetCapture capture(fixture.widget());

  ASSERT_TRUE(capture.capture(200, 120, 1.0));
  ASSERT_TRUE(capture.capture(240, 120, 1.0));
  EXPECT_TRUE(capture.content_changed());
  EXPECT_EQ(capture.pixels().width, 240u);
}

TEST(WidgetCapture, HiDpiCaptureFillsTheLargerBufferWithRealContent)
{
  Fixture fixture;
  QtWidgetCapture capture(fixture.widget());

  // At dpr 2 the same logical widget must paint across the whole 400x240 buffer,
  // not sit 1:1 in the top-left corner. Sample a pixel in the far half that a
  // 1:1 render would have left at the fill colour.
  ASSERT_TRUE(capture.capture(400, 240, 2.0)) << capture.last_error();
  const PanelPixels pixels = capture.pixels();
  ASSERT_NE(pixels.data, nullptr);

  const std::uint8_t * probe = pixels.data + static_cast<std::size_t>(200) * pixels.stride_bytes + 300 * 4;
  EXPECT_NE(probe[3], 0) << "bottom-right quadrant was never painted: dpr was ignored";
}

TEST(WidgetCapture, AHiddenWidgetStillCapturesButIsFlagged)
{
  // Qt renders only VISIBLE children, so a hidden panel streams its background
  // and nothing else. That must be reported, not silently sent as a blank panel.
  QWidget widget;
  auto * layout = new QVBoxLayout(&widget);
  auto * block = new QWidget(&widget);
  block->setAutoFillBackground(true);
  layout->addWidget(block);
  widget.resize(200, 120);
  widget.ensurePolished();
  widget.layout()->activate();

  QtWidgetCapture capture(&widget);
  ASSERT_TRUE(capture.capture(200, 120, 1.0)) << capture.last_error();
  EXPECT_FALSE(capture.widget_visible());
}

TEST(WidgetCapture, AVisibleWidgetIsFlaggedVisible)
{
  Fixture fixture;
  QtWidgetCapture capture(fixture.widget());
  ASSERT_TRUE(capture.capture(200, 120, 1.0));
  EXPECT_TRUE(capture.widget_visible());
}

TEST(WidgetCapture, RejectsDegenerateArguments)
{
  Fixture fixture;
  QtWidgetCapture capture(fixture.widget());

  EXPECT_FALSE(capture.capture(0, 120, 1.0));
  EXPECT_FALSE(capture.capture(200, 0, 1.0));
  EXPECT_FALSE(capture.capture(200, 120, 0.0));
  EXPECT_FALSE(capture.last_error().empty());
}

TEST(WidgetCapture, WithoutAWidgetItFailsCleanly)
{
  QtWidgetCapture capture(nullptr);
  EXPECT_FALSE(capture.capture(200, 120, 1.0));
  EXPECT_FALSE(capture.last_error().empty());
}

// ---------------------------------------------------------------------------
// Popup compositing.
//
// A combo box list, a menu and a tooltip are separate top-level windows, so the
// panel's own render() never contains them and the AR operator would watch a
// combo do nothing. These exercise the real thing: real Qt::Popup windows (and a real
// QComboBox list) opened over a real panel, asserted on colour blocks and
// geometry — never on text, since the offscreen platform plugin on this host
// rasterizes no glyphs.
// ---------------------------------------------------------------------------

namespace
{

struct Rgba
{
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 0;

  bool operator==(const Rgba & other) const
  {
    return r == other.r && g == other.g && b == other.b && a == other.a;
  }

  bool operator!=(const Rgba & other) const { return !(*this == other); }
};

/// Printed by gtest when an expectation fails, so a wrong pixel says what it is.
std::ostream & operator<<(std::ostream & os, const Rgba & c)
{
  return os << "rgba(" << int(c.r) << ", " << int(c.g) << ", " << int(c.b) << ", " << int(c.a) <<
         ")";
}

const Rgba kPanelBlack{0, 0, 0, 255};
const Rgba kPopupMagenta{255, 0, 255, 255};

Rgba
pixel_at(const PanelPixels & pixels, int x, int y)
{
  const std::uint8_t * px = pixels.data + static_cast<std::size_t>(y) * pixels.stride_bytes +
    static_cast<std::size_t>(x) * 4u;
  return Rgba{px[0], px[1], px[2], px[3]};
}

/// A tightly packed copy, so two captures can be compared after the buffer has
/// been reused for the next frame.
std::vector<std::uint8_t>
snapshot(const PanelPixels & pixels)
{
  const std::size_t row = static_cast<std::size_t>(pixels.width) * 4u;
  std::vector<std::uint8_t> out(row * pixels.height);
  for (std::uint32_t y = 0; y < pixels.height; ++y) {
    std::memcpy(
      out.data() + y * row, pixels.data + static_cast<std::size_t>(y) * pixels.stride_bytes, row);
  }
  return out;
}

void
paint_widget(QWidget * widget, const QColor & colour)
{
  widget->setAutoFillBackground(true);
  QPalette palette = widget->palette();
  palette.setColor(QPalette::Window, colour);
  widget->setPalette(palette);
}

/// A shown top-level panel at a known screen position, able to open popups that
/// belong to its widget tree. The position matters: a popup is placed in GLOBAL
/// coordinates, so a panel at the origin would hide a whole class of mapping
/// bugs.
class PopupPanel
{
public:
  explicit PopupPanel(int width = 240, int height = 160, int x = 100, int y = 80)
  {
    widget_ = new QWidget();
    widget_->resize(width, height);
    widget_->move(x, y);
    paint_widget(widget_, Qt::black);
    widget_->show();
    widget_->ensurePolished();
    QCoreApplication::processEvents();
  }

  ~PopupPanel() { delete widget_; }

  QWidget * widget() const { return widget_; }

  /// Open a popup window owned by this panel's tree, its top-left at @p local
  /// panel coordinates. A plain Qt::Popup widget rather than a styled control:
  /// the point here is the capture geometry, and a flat colour block is the only
  /// thing this host can render deterministically.
  QWidget * open_popup(const QPoint & local, const QSize & size, const QColor & colour)
  {
    auto * popup = new QWidget(widget_, Qt::Popup);
    paint_widget(popup, colour);
    popup->resize(size);
    popup->move(widget_->mapToGlobal(local));
    popup->show();
    QCoreApplication::processEvents();
    // move() places the window FRAME, while mapToGlobal(0, 0) reports the
    // CONTENT origin, and the offscreen platform plugin puts a 2 px frame
    // between the two. Correct for it, so the pixel expectations below can be
    // written against the panel coordinate the test actually asked for. (The
    // capture reads the content origin, which is the correct one on either a
    // frameless desktop popup or this platform's synthetic frame.)
    const QPoint landed = local_origin_of(popup);
    if (landed != local) {
      popup->move(popup->pos() + (local - landed));
      QCoreApplication::processEvents();
    }
    return popup;
  }

  /// Where the popup actually ended up, in panel coordinates.
  QPoint local_origin_of(QWidget * popup) const
  {
    return widget_->mapFromGlobal(popup->mapToGlobal(QPoint(0, 0)));
  }

  static void close(QWidget * popup)
  {
    popup->hide();
    QCoreApplication::processEvents();
    delete popup;
    QCoreApplication::processEvents();
  }

private:
  QWidget * widget_ = nullptr;
};

}  // namespace

TEST(PopupOwnership, APopupOfThisPanelIsOwned)
{
  PopupPanel panel;
  QWidget * popup = panel.open_popup(QPoint(10, 10), QSize(40, 30), Qt::magenta);
  EXPECT_TRUE(qar_qt_streaming::panel_tree_owns_popup(panel.widget(), popup));
  PopupPanel::close(popup);
}

TEST(PopupOwnership, APopupOfAnotherPanelIsNotOwned)
{
  PopupPanel mine;
  PopupPanel theirs(240, 160, 600, 400);
  QWidget * popup = theirs.open_popup(QPoint(10, 10), QSize(40, 30), Qt::magenta);

  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_popup(mine.widget(), popup));
  EXPECT_TRUE(qar_qt_streaming::panel_tree_owns_popup(theirs.widget(), popup));
  PopupPanel::close(popup);
}

TEST(PopupOwnership, AParentlessPopupBelongsToNobody)
{
  PopupPanel panel;
  auto * orphan = new QWidget(nullptr, Qt::Popup);
  orphan->resize(40, 30);
  orphan->show();
  QCoreApplication::processEvents();

  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_popup(panel.widget(), orphan));
  PopupPanel::close(orphan);
}

TEST(PopupOwnership, NullArgumentsAndSelfAreRejected)
{
  PopupPanel panel;
  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_popup(nullptr, panel.widget()));
  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_popup(panel.widget(), nullptr));
  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_popup(panel.widget(), panel.widget()));
}

TEST(PopupOwnership, CollectFindsOnlyThePopupsOfTheGivenTree)
{
  PopupPanel mine;
  PopupPanel theirs(240, 160, 600, 400);
  QWidget * ours = mine.open_popup(QPoint(10, 10), QSize(40, 30), Qt::magenta);
  QWidget * strangers = theirs.open_popup(QPoint(10, 10), QSize(40, 30), Qt::cyan);

  const std::vector<QWidget *> collected = qar_qt_streaming::collect_panel_popups(mine.widget());
  ASSERT_EQ(collected.size(), 1u);
  EXPECT_EQ(collected.front(), ours);

  PopupPanel::close(strangers);
  PopupPanel::close(ours);
  EXPECT_TRUE(qar_qt_streaming::collect_panel_popups(mine.widget()).empty());
}

TEST(PopupCapture, AnOpenPopupLandsInTheExpectedRegion)
{
  PopupPanel panel;
  QtWidgetCapture capture(panel.widget());
  ASSERT_TRUE(capture.capture(240, 160, 1.0)) << capture.last_error();
  EXPECT_EQ(capture.composited_popup_count(), 0u);
  EXPECT_EQ(pixel_at(capture.pixels(), 60, 70), kPanelBlack);

  constexpr int kX = 40;
  constexpr int kY = 50;
  constexpr int kW = 80;
  constexpr int kH = 40;
  QWidget * popup = panel.open_popup(QPoint(kX, kY), QSize(kW, kH), Qt::magenta);
  ASSERT_EQ(panel.local_origin_of(popup), QPoint(kX, kY)) << "the popup did not land where asked";

  ASSERT_TRUE(capture.capture(240, 160, 1.0)) << capture.last_error();
  EXPECT_TRUE(capture.content_changed()) << "an opening popup must mark the panel dirty";
  EXPECT_EQ(capture.composited_popup_count(), 1u);
  EXPECT_FALSE(capture.popup_clipped());

  const PanelPixels pixels = capture.pixels();
  EXPECT_EQ(pixel_at(pixels, kX, kY), kPopupMagenta) << "the popup's own top-left corner";
  EXPECT_EQ(pixel_at(pixels, kX + kW - 1, kY + kH - 1), kPopupMagenta) << "its bottom-right corner";
  EXPECT_EQ(pixel_at(pixels, kX - 1, kY + 2), kPanelBlack) << "one pixel left of the popup";
  EXPECT_EQ(pixel_at(pixels, kX + kW, kY + 2), kPanelBlack) << "one pixel right of the popup";
  EXPECT_EQ(pixel_at(pixels, kX + 2, kY - 1), kPanelBlack) << "one pixel above the popup";
  EXPECT_EQ(pixel_at(pixels, kX + 2, kY + kH), kPanelBlack) << "one pixel below the popup";

  PopupPanel::close(popup);
}

TEST(PopupCapture, TheCompositeHonoursTheRenderScale)
{
  // The capture ratio is dpr x the render scale, and the popup must ride it
  // exactly as the base widget does — at ratio 2 a popup at panel (40, 50) is at
  // device (80, 100), twice the size, and NOT a 1:1 block in the wrong corner.
  PopupPanel panel;
  QtWidgetCapture capture(panel.widget());

  QWidget * popup = panel.open_popup(QPoint(40, 50), QSize(80, 40), Qt::magenta);
  ASSERT_EQ(panel.local_origin_of(popup), QPoint(40, 50));
  ASSERT_TRUE(capture.capture(480, 320, 2.0)) << capture.last_error();
  EXPECT_EQ(capture.composited_popup_count(), 1u);

  const PanelPixels pixels = capture.pixels();
  EXPECT_EQ(pixel_at(pixels, 80, 100), kPopupMagenta) << "scaled top-left";
  EXPECT_EQ(pixel_at(pixels, 80 + 160 - 1, 100 + 80 - 1), kPopupMagenta) << "scaled bottom-right";
  EXPECT_EQ(pixel_at(pixels, 79, 102), kPanelBlack) << "one device pixel left of the scaled popup";
  EXPECT_EQ(pixel_at(pixels, 80 + 160, 102), kPanelBlack) << "one device pixel past its right edge";

  PopupPanel::close(popup);
}

TEST(PopupCapture, ClosingThePopupRestoresTheBaseImage)
{
  PopupPanel panel;
  QtWidgetCapture capture(panel.widget());
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  const std::vector<std::uint8_t> base = snapshot(capture.pixels());

  QWidget * popup = panel.open_popup(QPoint(40, 50), QSize(80, 40), Qt::magenta);
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  ASSERT_TRUE(capture.content_changed());
  EXPECT_NE(snapshot(capture.pixels()), base);

  PopupPanel::close(popup);
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  EXPECT_TRUE(capture.content_changed()) << "a closing popup must mark the panel dirty too";
  EXPECT_EQ(capture.composited_popup_count(), 0u);
  EXPECT_EQ(snapshot(capture.pixels()), base) << "the panel did not come back to what it was";
}

TEST(PopupCapture, AStillOpenPopupDoesNotResendEveryTick)
{
  // The composite must not defeat the dirty check: an open, untouched list is
  // still an idle panel and belongs off the network.
  PopupPanel panel;
  QtWidgetCapture capture(panel.widget());
  QWidget * popup = panel.open_popup(QPoint(40, 50), QSize(80, 40), Qt::magenta);

  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  EXPECT_FALSE(capture.content_changed());

  PopupPanel::close(popup);
}

TEST(PopupCapture, AMovingPopupIsTrackedPerTick)
{
  // A long list scrolls and re-places itself; the composite reads the geometry
  // every tick rather than remembering where it opened.
  PopupPanel panel;
  QtWidgetCapture capture(panel.widget());
  QWidget * popup = panel.open_popup(QPoint(20, 20), QSize(60, 30), Qt::magenta);
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  ASSERT_EQ(pixel_at(capture.pixels(), 22, 22), kPopupMagenta);

  popup->move(panel.widget()->mapToGlobal(QPoint(120, 90)));
  QCoreApplication::processEvents();
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  EXPECT_TRUE(capture.content_changed());
  EXPECT_EQ(pixel_at(capture.pixels(), 122, 92), kPopupMagenta) << "not drawn at the new place";
  EXPECT_EQ(pixel_at(capture.pixels(), 22, 22), kPanelBlack) << "still drawn at the old place";

  PopupPanel::close(popup);
}

TEST(PopupCapture, APopupOverTheEdgeIsClippedToThePanel)
{
  // Documented behaviour: clip, never shift. The AR hit test hands a panel point
  // to the popup where Qt really put it, so a shifted picture would aim every
  // click at the wrong item.
  PopupPanel panel;
  QtWidgetCapture capture(panel.widget());
  QWidget * popup = panel.open_popup(QPoint(200, 140), QSize(100, 60), Qt::magenta);
  ASSERT_EQ(panel.local_origin_of(popup), QPoint(200, 140));

  ASSERT_TRUE(capture.capture(240, 160, 1.0)) << capture.last_error();
  EXPECT_EQ(capture.composited_popup_count(), 1u) << "the visible part must still be drawn";
  EXPECT_TRUE(capture.popup_clipped()) << "the cut must be reported, not silent";

  const PanelPixels pixels = capture.pixels();
  EXPECT_EQ(pixel_at(pixels, 200, 140), kPopupMagenta) << "the part inside the panel";
  EXPECT_EQ(pixel_at(pixels, 239, 159), kPopupMagenta) << "right up to the last pixel";
  EXPECT_EQ(pixel_at(pixels, 199, 140), kPanelBlack) << "nothing shifted inward";
  EXPECT_EQ(pixel_at(pixels, 200, 139), kPanelBlack);

  PopupPanel::close(popup);
}

TEST(PopupCapture, APopupEntirelyOffThePanelDrawsNothing)
{
  PopupPanel panel;
  QtWidgetCapture capture(panel.widget());
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  const std::vector<std::uint8_t> base = snapshot(capture.pixels());

  QWidget * popup = panel.open_popup(QPoint(400, 20), QSize(100, 60), Qt::magenta);
  ASSERT_TRUE(capture.capture(240, 160, 1.0)) << capture.last_error();
  EXPECT_EQ(capture.composited_popup_count(), 0u);
  EXPECT_TRUE(capture.popup_clipped());
  EXPECT_EQ(snapshot(capture.pixels()), base) << "nothing may be drawn outside the buffer";

  PopupPanel::close(popup);
}

TEST(PopupCapture, AnUnrelatedPanelsPopupIsNeverComposited)
{
  // Two panels streaming at once must not bleed into each other, and RViz has
  // menus of its own that are nobody's panel content.
  PopupPanel mine;
  PopupPanel theirs(240, 160, 100, 80);  // deliberately the SAME screen position
  QtWidgetCapture capture(mine.widget());
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  const std::vector<std::uint8_t> base = snapshot(capture.pixels());

  QWidget * popup = theirs.open_popup(QPoint(40, 50), QSize(80, 40), Qt::magenta);
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  EXPECT_EQ(capture.composited_popup_count(), 0u) << "somebody else's popup was composited";
  EXPECT_FALSE(capture.content_changed());
  EXPECT_EQ(snapshot(capture.pixels()), base);

  // ... and the panel that DOES own it still gets it.
  QtWidgetCapture theirs_capture(theirs.widget());
  ASSERT_TRUE(theirs_capture.capture(240, 160, 1.0));
  EXPECT_EQ(theirs_capture.composited_popup_count(), 1u);

  PopupPanel::close(popup);
}

TEST(PopupCapture, CompositingCanBeTurnedOff)
{
  PopupPanel panel;
  QtWidgetCapture capture(panel.widget());
  capture.set_composite_popups(false);
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  const std::vector<std::uint8_t> base = snapshot(capture.pixels());

  QWidget * popup = panel.open_popup(QPoint(40, 50), QSize(80, 40), Qt::magenta);
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  EXPECT_EQ(capture.composited_popup_count(), 0u);
  EXPECT_EQ(snapshot(capture.pixels()), base);

  PopupPanel::close(popup);
}

TEST(PopupCapture, ARealComboBoxListReachesTheStream)
{
  // The AR operator taps a combo on the panel: the list opens as a window of its
  // own, and the streamed image has to change with it.
  PopupPanel panel;
  auto * combo = new QComboBox(panel.widget());
  combo->addItems({"zero", "one", "two", "three"});
  combo->setGeometry(20, 20, 160, 28);
  combo->show();
  QCoreApplication::processEvents();

  QtWidgetCapture capture(panel.widget());
  ASSERT_TRUE(capture.capture(240, 160, 1.0)) << capture.last_error();
  const std::vector<std::uint8_t> closed = snapshot(capture.pixels());

  combo->showPopup();
  QCoreApplication::processEvents();
  QWidget * list = QApplication::activePopupWidget();
  ASSERT_NE(list, nullptr) << "the combo did not open a popup";
  // Paint the real list a known colour: this host rasterizes no glyphs, so the
  // block is the only thing that can be asserted on.
  paint_widget(list, Qt::magenta);
  QCoreApplication::processEvents();

  ASSERT_TRUE(capture.capture(240, 160, 1.0)) << capture.last_error();
  EXPECT_TRUE(capture.content_changed()) << "the open list never reached the stream";
  EXPECT_EQ(capture.composited_popup_count(), 1u);
  EXPECT_NE(snapshot(capture.pixels()), closed);

  // Qt puts the list against the combo, so its top-left is inside the panel.
  // The assertion is on PLACEMENT: the panel's black is gone where the list is
  // and still there one pixel outside it. Not on an exact colour — a real combo
  // container paints its frame in a style-derived shade of the palette, not the
  // flat fill a bare Qt::Popup widget gives.
  const QPoint local = panel.local_origin_of(list);
  ASSERT_GE(local.x(), 1);
  ASSERT_GE(local.y(), 0);
  ASSERT_LT(local.x(), 240);
  ASSERT_LT(local.y(), 160 - 4);
  EXPECT_NE(pixel_at(capture.pixels(), local.x() + 2, local.y() + 2), kPanelBlack)
    << "the list did not paint where Qt put it";
  EXPECT_EQ(pixel_at(capture.pixels(), local.x() - 1, local.y() + 2), kPanelBlack)
    << "the list bled past its own left edge";

  combo->hidePopup();
  QCoreApplication::processEvents();
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  EXPECT_TRUE(capture.content_changed());
  EXPECT_EQ(capture.composited_popup_count(), 0u);
  EXPECT_EQ(snapshot(capture.pixels()), closed) << "closing the list must restore the panel";
}

TEST(QtWidgetCapture, PopupGlobalOriginIsTheWidgetGeometryForAPopupKeptOffScreen)
{
  // A tooltip-class window: it takes no focus and enters no popup mode, so it can sit open
  // for the whole test.
  QWidget kept_off(nullptr, Qt::ToolTip);
  kept_off.setAttribute(Qt::WA_DontShowOnScreen, true);
  kept_off.setGeometry(40, 50, 100, 60);
  kept_off.show();
  QCoreApplication::processEvents();
  ASSERT_TRUE(kept_off.isVisible());

  // Qt never moves the native window of such a widget after creating it, so once that window
  // has had a geometry event its position can disagree with the widget geometry; the widget
  // geometry is what the popup shows at. Give the native window another position: whether or
  // not Qt reads it back, the helper answers the widget geometry.
  if (QWindow * window = kept_off.windowHandle()) {
    window->setGeometry(QRect(400, 500, 100, 60));
    QCoreApplication::processEvents();
  }
  EXPECT_EQ(qar_qt_streaming::popup_global_origin(&kept_off), QPoint(40, 50));

  // The same kind of window without the attribute keeps Qt's own mapping, unchanged.
  QWidget ordinary(nullptr, Qt::ToolTip);
  ordinary.setGeometry(40, 50, 100, 60);
  ordinary.show();
  QCoreApplication::processEvents();
  ASSERT_FALSE(ordinary.testAttribute(Qt::WA_DontShowOnScreen));
  EXPECT_EQ(qar_qt_streaming::popup_global_origin(&ordinary), ordinary.mapToGlobal(QPoint(0, 0)));

  EXPECT_EQ(qar_qt_streaming::popup_global_origin(nullptr), QPoint());
}

// ---------------------------------------------------------------------------
// Dialogs and the capture.
//
// A modal dialog (Qt::Dialog, normally driven by QDialog::exec) is a top-level
// window like a popup, but it is usually BIGGER than the panel that spawned it
// and centred on its parent, so the popup rule of "clip at the panel edge"
// would leave the operator staring at the middle of a box whose Cancel button
// is off the panel. The capture therefore does not draw a dialog at all: the
// dialog gets a panel of its own (QtPanelStreamManager), and these tests pin
// that the owner's capture neither composites it nor moves it.
//
// exec() is never called here — it would spin a nested event loop and hang the
// test runner. show() puts the same window on the same screen with the same
// geometry, which is all the capture path looks at; setModal(true) + show() is
// how the modality predicates below are exercised without a nested loop.
// ---------------------------------------------------------------------------

namespace
{

const Rgba kDialogCyan{0, 255, 255, 255};

/// A dialog owned by @p parent's widget tree, painted a flat colour.
QDialog *
open_dialog(QWidget * parent, const QSize & size, const QColor & colour, bool modal = false)
{
  auto * dialog = new QDialog(parent);
  paint_widget(dialog, colour);
  dialog->resize(size);
  dialog->setModal(modal);
  dialog->show();
  QCoreApplication::processEvents();
  return dialog;
}

void
close_dialog(QDialog * dialog)
{
  dialog->hide();
  QCoreApplication::processEvents();
  delete dialog;
  QCoreApplication::processEvents();
}

}  // namespace

TEST(DialogOwnership, AnOwnedDialogIsCollectedOnlyWhenDialogsAreAskedFor)
{
  PopupPanel panel;
  QDialog * dialog = open_dialog(panel.widget(), QSize(300, 200), Qt::cyan);

  // The popup scan must not have started returning dialogs: it is what the
  // tooltip/menu path uses and its fast path assumes popup-or-nothing.
  EXPECT_TRUE(qar_qt_streaming::collect_panel_popups(panel.widget()).empty());

  qar_qt_streaming::PanelOverlayOptions options;
  options.popups = false;
  options.tooltips = false;
  const auto overlays = qar_qt_streaming::collect_panel_overlays(panel.widget(), options);
  ASSERT_EQ(overlays.size(), 1u);
  EXPECT_EQ(overlays.front(), dialog);
  EXPECT_TRUE(qar_qt_streaming::is_dialog_window(dialog));
  close_dialog(dialog);
}

TEST(DialogOwnership, AnotherPanelsDialogIsNotOurs)
{
  PopupPanel mine;
  PopupPanel theirs(240, 160, 600, 400);
  QDialog * dialog = open_dialog(theirs.widget(), QSize(120, 90), Qt::cyan);

  qar_qt_streaming::PanelOverlayOptions options;
  options.popups = false;
  options.tooltips = false;
  EXPECT_TRUE(qar_qt_streaming::collect_panel_overlays(mine.widget(), options).empty());
  EXPECT_EQ(qar_qt_streaming::collect_panel_overlays(theirs.widget(), options).size(), 1u);
  close_dialog(dialog);
}

TEST(DialogModality, OnlyAnOwnedModalWindowIsReportedAsBlocking)
{
  PopupPanel mine;
  PopupPanel theirs(240, 160, 600, 400);
  EXPECT_EQ(qar_qt_streaming::panel_owned_modal_window(mine.widget()), nullptr);

  QDialog * stranger = open_dialog(theirs.widget(), QSize(120, 90), Qt::cyan, true);
  EXPECT_EQ(qar_qt_streaming::panel_owned_modal_window(mine.widget()), nullptr);
  close_dialog(stranger);

  QDialog * ours = open_dialog(mine.widget(), QSize(120, 90), Qt::cyan, true);
  EXPECT_EQ(qar_qt_streaming::panel_owned_modal_window(mine.widget()), ours);
  close_dialog(ours);

  // Gone again once it is dismissed — the injector's block must not outlive it.
  EXPECT_EQ(qar_qt_streaming::panel_owned_modal_window(mine.widget()), nullptr);
}

TEST(DialogCapture, AnOwnedDialogIsNeverDrawnIntoItsPanel)
{
  // The panel neither composites the dialog nor docks the real window onto
  // itself: squeezed into a tall narrow dock, a dialog would show its top half
  // and no buttons. It gets a panel of its own instead
  // (QtPanelStreamManager) -- so as far as THIS panel's image is concerned an
  // owned dialog might as well not exist.
  PopupPanel panel;  // 240 x 160 at (100, 80)
  QDialog * dialog = open_dialog(panel.widget(), QSize(400, 300), Qt::cyan);
  const QPoint before_pos = dialog->pos();

  QtWidgetCapture capture(panel.widget());
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  QCoreApplication::processEvents();

  EXPECT_EQ(capture.composited_popup_count(), 0u);
  EXPECT_EQ(pixel_at(capture.pixels(), 2, 2), kPanelBlack);
  EXPECT_EQ(pixel_at(capture.pixels(), 120, 80), kPanelBlack);
  // And the operator's real window is left exactly where and as big as it was.
  // Moving somebody's desktop window to fit a rectangle it never asked for is
  // not something the capture does.
  EXPECT_EQ(dialog->pos(), before_pos);
  EXPECT_EQ(dialog->size(), QSize(400, 300));
  close_dialog(dialog);
}

TEST(DialogCapture, OpeningOrClosingADialogIsNotAChangeToTheOwnerPanel)
{
  PopupPanel panel;
  QtWidgetCapture capture(panel.widget());
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  const std::vector<std::uint8_t> bare = snapshot(capture.pixels());

  QDialog * dialog = open_dialog(panel.widget(), QSize(120, 90), Qt::cyan);
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  EXPECT_FALSE(capture.content_changed()) << "the dialog is not part of this frame";
  EXPECT_EQ(snapshot(capture.pixels()), bare);

  close_dialog(dialog);
  ASSERT_TRUE(capture.capture(240, 160, 1.0));
  EXPECT_FALSE(capture.content_changed());
  EXPECT_EQ(snapshot(capture.pixels()), bare);
}

TEST(DialogCapture, TheDialogsOwnCaptureDrawsTheDialog)
{
  // The other half of the same rule: what the owner panel stops drawing, the
  // dialog's own panel draws -- at its own size, with nothing clipped.
  PopupPanel panel;
  QDialog * dialog = open_dialog(panel.widget(), QSize(400, 300), Qt::cyan);

  QtWidgetCapture capture(dialog);
  ASSERT_TRUE(capture.capture(400, 300, 1.0));

  EXPECT_TRUE(capture.widget_visible());
  EXPECT_FALSE(capture.popup_clipped());
  EXPECT_EQ(pixel_at(capture.pixels(), 2, 2), kDialogCyan);
  EXPECT_EQ(pixel_at(capture.pixels(), 399 - 2, 299 - 2), kDialogCyan)
    << "the bottom-right corner -- where a dialog keeps its buttons";
  close_dialog(dialog);
}

TEST(DialogCapture, APopupOpenedInsideADialogBelongsToTheDialogsPanel)
{
  PopupPanel panel;
  QDialog * dialog = open_dialog(panel.widget(), QSize(240, 160), Qt::cyan);

  // Parented to the DIALOG. The parent walk from it reaches the panel too, but
  // the nearest dialog ancestor wins: the list drops inside the dialog, and the
  // dialog is its own AR panel, so that is the only image it may land in.
  auto * popup = new QWidget(dialog, Qt::Popup);
  paint_widget(popup, Qt::magenta);
  popup->resize(60, 40);
  popup->move(dialog->mapToGlobal(QPoint(20, 20)));
  popup->show();
  QCoreApplication::processEvents();
  const QPoint landed = dialog->mapFromGlobal(popup->mapToGlobal(QPoint(0, 0)));
  if (landed != QPoint(20, 20)) {
    popup->move(popup->pos() + (QPoint(20, 20) - landed));
    QCoreApplication::processEvents();
  }

  QtWidgetCapture owner(panel.widget());
  ASSERT_TRUE(owner.capture(240, 160, 1.0));
  EXPECT_EQ(owner.composited_popup_count(), 0u) << "it would land at a meaningless spot here";

  QtWidgetCapture theirs(dialog);
  ASSERT_TRUE(theirs.capture(240, 160, 1.0));
  EXPECT_EQ(theirs.composited_popup_count(), 1u);
  EXPECT_EQ(pixel_at(theirs.pixels(), 30, 30), kPopupMagenta);
  EXPECT_EQ(pixel_at(theirs.pixels(), 120, 120), kDialogCyan);

  PopupPanel::close(popup);
  close_dialog(dialog);
}

// ---------------------------------------------------------------------------
// Attributing a dialog that is parented ABOVE every streamed panel.
//
// With RViz's Displays dock streamed and Add tapped from AR, nothing would
// appear in the headset: rviz_common builds the box as
// `new NewObjectDialog(..., this)` with `this` the MAIN WINDOW, so the parent
// walk from the dialog runs dialog -> VisualizationFrame and never passes
// through the dock. "Same top-level window" would hand the one dialog to every
// dock at once; causation hands it to the panel that was clicked.
//
// These fixtures reproduce that parenting exactly: one window, two docks, and a
// dialog whose parent is the window.
// ---------------------------------------------------------------------------

namespace
{

using qar_qt_streaming::PanelOverlayAttribution;

/// Two dock-like panels inside ONE top-level window, plus dialogs parented to
/// that window. Resets the process-wide attribution state at both ends, so no
/// test inherits another's decisions.
class SharedWindow
{
public:
  SharedWindow()
  {
    PanelOverlayAttribution::instance().reset();
    window_ = new QWidget();
    window_->resize(600, 200);
    window_->move(70, 50);
    paint_widget(window_, Qt::darkGray);
    left_ = new QWidget(window_);
    left_->setGeometry(0, 0, 240, 160);
    paint_widget(left_, Qt::black);
    right_ = new QWidget(window_);
    right_->setGeometry(320, 0, 240, 160);
    paint_widget(right_, Qt::black);
    window_->show();
    window_->ensurePolished();
    QCoreApplication::processEvents();
    // Both are streaming, so both are known to attribution — which is what makes
    // "a dialog one of them parents can never be claimed by the other" true.
    PanelOverlayAttribution::instance().register_panel(left_);
    PanelOverlayAttribution::instance().register_panel(right_);
  }

  ~SharedWindow()
  {
    delete window_;
    QCoreApplication::processEvents();
    PanelOverlayAttribution::instance().reset();
  }

  QWidget * left() const { return left_; }
  QWidget * right() const { return right_; }

  /// A dialog parented to the WINDOW — the RViz parenting, unowned by either
  /// dock as far as the parent walk is concerned.
  QDialog * open_window_dialog(const QSize & size, const QColor & colour, bool modal = false)
  {
    auto * dialog = new QDialog(window_);
    paint_widget(dialog, colour);
    dialog->resize(size);
    dialog->setModal(modal);
    dialog->show();
    QCoreApplication::processEvents();
    return dialog;
  }

  /// What the injector does on a press/release/click: "this panel just took an
  /// activating AR action". The injector's own call is covered by
  /// qt_panel_input_injector_tests.cpp.
  static void ar_activation(QWidget * panel)
  {
    PanelOverlayAttribution::instance().note_panel_input(panel);
  }

private:
  QWidget * window_ = nullptr;
  QWidget * left_ = nullptr;
  QWidget * right_ = nullptr;
};

/// The dialog half of the scan both the capture and the injector run.
std::vector<QWidget *>
dialogs_of(QWidget * root)
{
  qar_qt_streaming::PanelOverlayOptions options;
  options.popups = false;
  options.tooltips = false;
  return qar_qt_streaming::collect_panel_overlays(root, options);
}

}  // namespace

TEST(DialogAttribution, TheParentWalkCannotReachADialogParentedToTheSharedWindow)
{
  // The motivating fact, pinned: neither dock is on the dialog's parent chain,
  // so the strict rule is right to refuse and something else has to answer.
  SharedWindow app;
  QDialog * dialog = app.open_window_dialog(QSize(200, 120), Qt::cyan);

  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_popup(app.left(), dialog));
  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_popup(app.right(), dialog));
  EXPECT_TRUE(dialogs_of(app.left()).empty()) << "nobody has clicked anything yet";
  close_dialog(dialog);
}

TEST(DialogAttribution, ItGoesToThePanelThatMostRecentlyTookInput)
{
  SharedWindow app;
  SharedWindow::ar_activation(app.left());
  QDialog * dialog = app.open_window_dialog(QSize(200, 120), Qt::cyan);

  EXPECT_TRUE(qar_qt_streaming::panel_tree_owns_overlay(app.left(), dialog));
  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_overlay(app.right(), dialog));

  const std::vector<QWidget *> ours = dialogs_of(app.left());
  ASSERT_EQ(ours.size(), 1u);
  EXPECT_EQ(ours.front(), dialog);
  EXPECT_TRUE(dialogs_of(app.right()).empty()) << "the other dock must not see it too";
  close_dialog(dialog);
}

TEST(DialogAttribution, LaterInputOnAnotherPanelDoesNotMoveIt)
{
  SharedWindow app;
  SharedWindow::ar_activation(app.left());
  QDialog * dialog = app.open_window_dialog(QSize(200, 120), Qt::cyan);
  ASSERT_EQ(dialogs_of(app.left()).size(), 1u);

  // The operator works the other panel while the dialog is still up — the exact
  // flip-flop this rule exists to prevent, since the dialog is mid-interaction.
  for (int i = 0; i < 3; ++i) {
    SharedWindow::ar_activation(app.right());
    EXPECT_EQ(dialogs_of(app.left()).size(), 1u) << "it left the panel that opened it";
    EXPECT_TRUE(dialogs_of(app.right()).empty()) << "it followed the clicks";
  }

  bool decided = false;
  EXPECT_EQ(PanelOverlayAttribution::instance().decided_owner(dialog, &decided), app.left());
  EXPECT_TRUE(decided);
  close_dialog(dialog);
}

TEST(DialogAttribution, WithNoRecentInputItBelongsToNobody)
{
  // A dialog the LOCAL operator opened with the desktop mouse. Safe by default:
  // it is attributed to nobody.
  SharedWindow app;
  QDialog * dialog = app.open_window_dialog(QSize(200, 120), Qt::cyan);

  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_overlay(app.left(), dialog));
  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_overlay(app.right(), dialog));
  bool decided = false;
  EXPECT_EQ(PanelOverlayAttribution::instance().decided_owner(dialog, &decided), nullptr);
  EXPECT_TRUE(decided) << "the refusal is a decision, not a question re-asked every tick";
  close_dialog(dialog);
}

TEST(DialogAttribution, AnActivationOlderThanTheWindowDoesNotCount)
{
  SharedWindow app;
  PanelOverlayAttribution::instance().set_window_ms(0.0);
  SharedWindow::ar_activation(app.left());
  // Provably past the window without sleeping: spin until the steady clock has
  // actually advanced, so the age is strictly positive.
  const auto marked = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() == marked) {
  }

  QDialog * dialog = app.open_window_dialog(QSize(200, 120), Qt::cyan);
  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_overlay(app.left(), dialog));
  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_overlay(app.right(), dialog));
  close_dialog(dialog);
}

TEST(DialogAttribution, AStructurallyOwnedDialogIsNeverStolen)
{
  // One panel is clicked; the OTHER panel's own child dialog opens. The parent
  // walk owns it, and causation must not claim it — otherwise both panels would
  // adopt the same window.
  SharedWindow app;
  SharedWindow::ar_activation(app.right());

  auto * dialog = new QDialog(app.left());
  paint_widget(dialog, Qt::cyan);
  dialog->resize(120, 90);
  dialog->show();
  QCoreApplication::processEvents();

  EXPECT_TRUE(qar_qt_streaming::panel_tree_owns_overlay(app.left(), dialog));
  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_overlay(app.right(), dialog));
  EXPECT_EQ(dialogs_of(app.left()).size(), 1u);
  EXPECT_TRUE(dialogs_of(app.right()).empty());
  close_dialog(dialog);
}

TEST(DialogAttribution, AReshownDialogIsAttributedAfresh)
{
  // The decision lasts as long as the SHOWING. A dialog object RViz keeps alive
  // and shows again was opened by whoever clicked the second time.
  SharedWindow app;
  SharedWindow::ar_activation(app.left());
  QDialog * dialog = app.open_window_dialog(QSize(200, 120), Qt::cyan);
  ASSERT_EQ(dialogs_of(app.left()).size(), 1u);

  dialog->hide();
  QCoreApplication::processEvents();
  ASSERT_TRUE(dialogs_of(app.left()).empty());

  SharedWindow::ar_activation(app.right());
  dialog->show();
  QCoreApplication::processEvents();
  EXPECT_TRUE(dialogs_of(app.left()).empty());
  EXPECT_EQ(dialogs_of(app.right()).size(), 1u);
  close_dialog(dialog);
}

TEST(DialogAttribution, OnlyTheAttributedPanelIsBlockedByItsModalDialog)
{
  SharedWindow app;
  SharedWindow::ar_activation(app.left());
  QDialog * dialog = app.open_window_dialog(QSize(200, 120), Qt::cyan, true);

  // Nobody is blocked by a dialog nobody is drawing: the input path reads
  // attributions, it never takes one, so a modal window no panel has adopted
  // yet cannot swallow AR clicks it is invisible to.
  EXPECT_EQ(qar_qt_streaming::panel_owned_modal_window(app.left()), nullptr);

  // The sweep's scan is the thing that decides, because promoting the dialog to
  // a panel is what puts it in front of the operator.
  ASSERT_EQ(dialogs_of(app.left()).size(), 1u);

  // From there the injector's modality rule follows attribution: the panel that
  // can SEE the dialog is the one that must not be clickable behind it.
  EXPECT_EQ(qar_qt_streaming::panel_owned_modal_window(app.left()), dialog);
  EXPECT_EQ(qar_qt_streaming::panel_owned_modal_window(app.right()), nullptr);
  close_dialog(dialog);
}

TEST(DialogAttribution, AttributionCanBeTurnedOff)
{
  SharedWindow app;
  SharedWindow::ar_activation(app.left());
  QDialog * dialog = app.open_window_dialog(QSize(200, 120), Qt::cyan);

  qar_qt_streaming::PanelOverlayOptions strict;
  strict.popups = false;
  strict.tooltips = false;
  strict.attribute_by_input = false;
  EXPECT_TRUE(qar_qt_streaming::collect_panel_overlays(app.left(), strict).empty())
    << "opted out, so only the parent walk answers";
  EXPECT_EQ(dialogs_of(app.left()).size(), 1u) << "and the default still attributes";
  close_dialog(dialog);
}

TEST(DialogCapture, AnAttributedDialogIsDrawnIntoNoPanelAtAll)
{
  // Attribution answers "whose dialog is this" -- exclusively, and once -- and
  // the answer means "give it a panel of its own, beside that owner"
  // (QtPanelStreamManager), so NEITHER panel's image changes when the Add box
  // opens.
  SharedWindow app;
  QtWidgetCapture left(app.left());
  QtWidgetCapture right(app.right());
  ASSERT_TRUE(left.capture(240, 160, 1.0));
  ASSERT_TRUE(right.capture(240, 160, 1.0));
  const std::vector<std::uint8_t> bare_left = snapshot(left.pixels());
  const std::vector<std::uint8_t> bare_right = snapshot(right.pixels());

  SharedWindow::ar_activation(app.left());
  QDialog * dialog = app.open_window_dialog(QSize(400, 300), Qt::cyan);
  const QPoint before_pos = dialog->pos();

  // Exclusive: exactly one panel owns it, and it is the one that was clicked.
  ASSERT_EQ(dialogs_of(app.left()).size(), 1u);
  EXPECT_TRUE(dialogs_of(app.right()).empty());

  ASSERT_TRUE(left.capture(240, 160, 1.0));
  ASSERT_TRUE(right.capture(240, 160, 1.0));
  EXPECT_FALSE(left.content_changed());
  EXPECT_EQ(snapshot(left.pixels()), bare_left);
  EXPECT_FALSE(right.content_changed());
  EXPECT_EQ(snapshot(right.pixels()), bare_right);
  EXPECT_EQ(dialog->pos(), before_pos) << "nobody docks the operator's window any more";
  EXPECT_EQ(dialog->size(), QSize(400, 300));
  close_dialog(dialog);
}
