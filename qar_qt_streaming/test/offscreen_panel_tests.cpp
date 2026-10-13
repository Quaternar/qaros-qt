// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// A widget shown OFF-SCREEN (Qt::WA_DontShowOnScreen, offscreen_panel.hpp) has to
// stream and take AR input exactly like a visible one, because the feature that uses
// it - a second copy of RViz's Displays panel that never appears on the desktop -
// has no other life than in the headset. These tests pin both halves:
//
//  * the WINDOW STATE show_offscreen leaves behind (the attributes, the flag, the
//    fixed size, the primary-screen position), and
//  * that the ordinary capture and input paths work on such a window as they do
//    on a visible one: a frame is captured, an AR tap presses a button inside it,
//    an AR tap on a combo opens a list that reaches the stream, and a size that
//    can never change never rebuilds the sender.
//
// Headless under the offscreen platform plugin, which cannot show whether a window
// would reach a real desktop - that is the attribute assertions here plus the live
// run. What it does show is whether Qt asked the platform to map a native window:
// QWindow::isVisible() is the platform-independent witness of that, and the popup
// assertions below use it. Nothing here asserts on rendered text: the plugin
// rasterizes no glyphs.

#include <gtest/gtest.h>

#include "qar_qt_streaming/offscreen_panel.hpp"
#include "qar_qt_streaming/offscreen_popup_guard.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/qt_widget_capture.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QPalette>
#include <QPoint>
#include <QPointer>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QScrollBar>
#include <QSize>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using qar_qt_streaming::clamp_offscreen_side;
using qar_qt_streaming::collect_panel_overlays;
using qar_qt_streaming::is_dialog_window;
using qar_qt_streaming::is_offscreen_panel;
using qar_qt_streaming::kDefaultOffscreenHeight;
using qar_qt_streaming::kDefaultOffscreenWidth;
using qar_qt_streaming::kMaxOffscreenSide;
using qar_qt_streaming::kMinOffscreenSide;
using qar_qt_streaming::OffscreenSizeChoice;
using qar_qt_streaming::PanelCloseMode;
using qar_ar_core::PanelPointerSample;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::parse_offscreen_size;
using qar_ar_core::PointerGesture;
using qar_ar_core::PointerPhase;
using qar_qt_streaming::QtPanelStream;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_qt_streaming::QtWidgetCapture;
using qar_qt_streaming::show_offscreen;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

/// A top-level widget whose whole area is one QPushButton, shown OFF-SCREEN.
class OffscreenButtonPanel
{
public:
  explicit OffscreenButtonPanel(int width = 300, int height = 200)
  {
    widget_ = new QWidget();
    auto * layout = new QVBoxLayout(widget_);
    layout->setContentsMargins(0, 0, 0, 0);
    button_ = new QPushButton("Go", widget_);
    layout->addWidget(button_);
    QObject::connect(button_, &QPushButton::clicked, [this]() { ++clicks_; });
    EXPECT_TRUE(show_offscreen(widget_, width, height));
    widget_->ensurePolished();
    widget_->layout()->activate();
    QCoreApplication::processEvents();
  }

  ~OffscreenButtonPanel() { delete widget_; }

  QWidget * widget() const { return widget_; }
  QPushButton * button() const { return button_; }
  int clicks() const { return clicks_; }

private:
  QWidget * widget_ = nullptr;
  QPushButton * button_ = nullptr;
  int clicks_ = 0;
};

/// The same panel, shown the ordinary way: the control for "an off-screen widget
/// behaves like a visible one".
class VisibleButtonPanel
{
public:
  VisibleButtonPanel()
  {
    widget_ = new QWidget();
    auto * layout = new QVBoxLayout(widget_);
    layout->setContentsMargins(0, 0, 0, 0);
    button_ = new QPushButton("Go", widget_);
    layout->addWidget(button_);
    QObject::connect(button_, &QPushButton::clicked, [this]() { ++clicks_; });
    widget_->resize(300, 200);
    widget_->show();
    widget_->ensurePolished();
    widget_->layout()->activate();
    QCoreApplication::processEvents();
  }

  ~VisibleButtonPanel() { delete widget_; }

  QWidget * widget() const { return widget_; }
  int clicks() const { return clicks_; }

private:
  QWidget * widget_ = nullptr;
  QPushButton * button_ = nullptr;
  int clicks_ = 0;
};

/// A black off-screen panel holding one combo box: black, so that the colour of a
/// popup composited over it is the only thing that can be non-black.
class OffscreenComboPanel
{
public:
  OffscreenComboPanel()
  {
    widget_ = new QWidget();
    widget_->setAutoFillBackground(true);
    QPalette palette = widget_->palette();
    palette.setColor(QPalette::Window, Qt::black);
    widget_->setPalette(palette);
    combo_ = new QComboBox(widget_);
    combo_->addItems({"zero", "one", "two", "three"});
    combo_->setGeometry(kComboX, kComboY, kComboW, kComboH);
    EXPECT_TRUE(show_offscreen(widget_, kWidth, kHeight));
    widget_->ensurePolished();
    QCoreApplication::processEvents();
  }

  ~OffscreenComboPanel()
  {
    combo_->hidePopup();
    delete widget_;
  }

  QWidget * widget() const { return widget_; }
  QComboBox * combo() const { return combo_; }

  static constexpr int kWidth = 300;
  static constexpr int kHeight = 200;
  static constexpr int kComboX = 20;
  static constexpr int kComboY = 20;
  static constexpr int kComboW = 160;
  static constexpr int kComboH = 28;

private:
  QWidget * widget_ = nullptr;
  QComboBox * combo_ = nullptr;
};

PanelStreamOptions
options_at()
{
  PanelStreamOptions options;
  options.max_fps = 0;
  options.keepalive_seconds = 0.0;
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  options.placement.pose.position = QarVector3{0.0f, 0.0f, 0.0f};
  return options;
}

QtPanelStreamingConfig
make_config(
  std::shared_ptr<MockQarPanelSenderFactory> factory,
  PanelCloseMode mode = PanelCloseMode::Destroy)
{
  QtPanelStreamingConfig config;
  config.panel_sender_factory = std::move(factory);
  config.clock = std::make_shared<ManualClock>();
  config.close_mode = mode;
  return config;
}

/// One AR tap already resolved to panel coordinates - the per-panel path.
PanelPointerSample
tap(const std::string & key, float u, float v, std::uint32_t pointer_id = 1)
{
  PanelPointerSample sample;
  sample.pointer_id = pointer_id;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_panel_uv = true;
  sample.panel_key = key;
  sample.u = u;
  sample.v = v;
  return sample;
}

void
paint_widget(QWidget * widget, const QColor & colour)
{
  widget->setAutoFillBackground(true);
  QPalette palette = widget->palette();
  palette.setColor(QPalette::Window, colour);
  widget->setPalette(palette);
}

/// Alpha of one pixel of a tightly packed RGBA8888 frame.
std::uint8_t
alpha_at(const qar_qt_streaming::test_support::RecordedPanelFrame & frame, int x, int y)
{
  return frame.rgba[(static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)) * 4u + 3u];
}

bool
is_black(const qar_qt_streaming::test_support::RecordedPanelFrame & frame, int x, int y)
{
  const std::size_t i = (static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)) * 4u;
  return frame.rgba[i] == 0 && frame.rgba[i + 1] == 0 && frame.rgba[i + 2] == 0 &&
         frame.rgba[i + 3] == 255;
}

}  // namespace

// ---------------------------------------------------------------------------
// The size parser (QAR_PANEL_HIDDEN_DISPLAYS_SIZE)
// ---------------------------------------------------------------------------

TEST(OffscreenSizeParse, TheShippedDefaultAndBoundsArePinned)
{
  // The operator-visible numbers, pinned where a refactor would trip over them.
  EXPECT_EQ(kDefaultOffscreenWidth, 480);
  EXPECT_EQ(kDefaultOffscreenHeight, 720);
  EXPECT_EQ(kMinOffscreenSide, 200);
  EXPECT_EQ(kMaxOffscreenSide, 2000);
  EXPECT_EQ(clamp_offscreen_side(0), 200);
  EXPECT_EQ(clamp_offscreen_side(199), 200);
  EXPECT_EQ(clamp_offscreen_side(200), 200);
  EXPECT_EQ(clamp_offscreen_side(1234), 1234);
  EXPECT_EQ(clamp_offscreen_side(2000), 2000);
  EXPECT_EQ(clamp_offscreen_side(2001), 2000);
  EXPECT_EQ(clamp_offscreen_side(-5), 200);
}

TEST(OffscreenSizeParse, UnsetIsTheDefaultWithoutAWarning)
{
  const OffscreenSizeChoice choice = parse_offscreen_size(nullptr);
  EXPECT_EQ(choice.width, 480);
  EXPECT_EQ(choice.height, 720);
  EXPECT_FALSE(choice.explicitly_chosen);
  EXPECT_FALSE(choice.clamped);
  EXPECT_TRUE(choice.warning.empty());
}

TEST(OffscreenSizeParse, EmptyAndBlankMeanUnsetToo)
{
  for (const char * text : {"", " ", "\t", "  \r\n "}) {
    const OffscreenSizeChoice choice = parse_offscreen_size(text);
    EXPECT_EQ(choice.width, 480) << "'" << text << "'";
    EXPECT_EQ(choice.height, 720) << "'" << text << "'";
    EXPECT_FALSE(choice.explicitly_chosen) << "'" << text << "'";
    EXPECT_TRUE(choice.warning.empty()) << "a variable set to nothing is not a typo";
  }
}

TEST(OffscreenSizeParse, AWellFormedSizeIsTakenAsWritten)
{
  struct Case
  {
    const char * text;
    int width;
    int height;
  };
  for (const Case & c : {
      Case{"480x720", 480, 720}, Case{"800X600", 800, 600}, Case{"640x360", 640, 360},
      Case{" 640 x 360 ", 640, 360}, Case{"\t320\tX\t900\r\n", 320, 900},
      Case{"200x2000", 200, 2000}, Case{"2000x200", 2000, 200}, Case{"0480x0720", 480, 720},
      Case{"1000x1000", 1000, 1000}})
  {
    const OffscreenSizeChoice choice = parse_offscreen_size(c.text);
    EXPECT_EQ(choice.width, c.width) << "'" << c.text << "'";
    EXPECT_EQ(choice.height, c.height) << "'" << c.text << "'";
    EXPECT_TRUE(choice.explicitly_chosen) << "'" << c.text << "'";
    EXPECT_FALSE(choice.clamped) << "'" << c.text << "'";
    EXPECT_TRUE(choice.warning.empty()) << "'" << c.text << "'";
  }
}

TEST(OffscreenSizeParse, EachSideIsClampedAndTheClampIsReported)
{
  struct Case
  {
    const char * text;
    int width;
    int height;
  };
  for (const Case & c : {
      Case{"100x5000", 200, 2000}, Case{"199x720", 200, 720}, Case{"480x199", 480, 200},
      Case{"2001x720", 2000, 720}, Case{"480x2001", 480, 2000}, Case{"0x0", 200, 200},
      Case{"1x1", 200, 200}, Case{"9999x9999", 2000, 2000}})
  {
    const OffscreenSizeChoice choice = parse_offscreen_size(c.text, 480, 720, "SIZE_VAR");
    EXPECT_EQ(choice.width, c.width) << "'" << c.text << "'";
    EXPECT_EQ(choice.height, c.height) << "'" << c.text << "'";
    EXPECT_TRUE(choice.explicitly_chosen) << "a well-formed size, merely out of range";
    EXPECT_TRUE(choice.clamped) << "'" << c.text << "'";
    ASSERT_FALSE(choice.warning.empty()) << "a clamp is not silent: '" << c.text << "'";
    EXPECT_NE(choice.warning.find("SIZE_VAR"), std::string::npos) << choice.warning;
    EXPECT_NE(choice.warning.find(c.text), std::string::npos) << "the value as typed";
    EXPECT_NE(choice.warning.find("200..2000"), std::string::npos) << "the bounds";
    EXPECT_NE(
      choice.warning.find(std::to_string(c.width) + "x" + std::to_string(c.height)),
      std::string::npos) << "what is used instead: " << choice.warning;
  }
}

TEST(OffscreenSizeParse, AnythingThatIsNotASizeFallsBackToTheDefaultWithOneWarningNamingIt)
{
  for (const char * text : {"480", "480x", "x720", "-480x720", "480x-720", "+480x720",
      "480.5x720", "480x720.0", "480x720x3", "480*720", "480,720", "480 720", "480 by 720",
      "wide", "off", "0x", "x", "xx", "480xx720", "0x1F0x2D0", "480x720px", "480x 720 7"})
  {
    const OffscreenSizeChoice choice = parse_offscreen_size(text, 480, 720, "SIZE_VAR");
    EXPECT_EQ(choice.width, 480) << "'" << text << "'";
    EXPECT_EQ(choice.height, 720) << "'" << text << "'";
    EXPECT_FALSE(choice.explicitly_chosen) << "'" << text << "'";
    EXPECT_FALSE(choice.clamped) << "'" << text << "'";
    ASSERT_FALSE(choice.warning.empty()) << "'" << text << "' must be reported, not guessed at";
    EXPECT_NE(choice.warning.find("SIZE_VAR"), std::string::npos) << choice.warning;
    EXPECT_NE(choice.warning.find(text), std::string::npos) << "the value as typed";
    EXPECT_NE(choice.warning.find("WIDTHxHEIGHT"), std::string::npos) << "what is accepted";
    EXPECT_NE(choice.warning.find("480x720"), std::string::npos) << "what is used";
  }
}

TEST(OffscreenSizeParse, ANumberTooLongForAnIntSaturatesInsteadOfOverflowing)
{
  const OffscreenSizeChoice choice = parse_offscreen_size("99999999999999999999999x720");
  EXPECT_EQ(choice.width, 2000);
  EXPECT_EQ(choice.height, 720);
  EXPECT_TRUE(choice.explicitly_chosen);
  EXPECT_TRUE(choice.clamped);

  const OffscreenSizeChoice both = parse_offscreen_size("4294967296x2147483648");
  EXPECT_EQ(both.width, 2000) << "2^32 must not wrap to 0 and clamp UP to 200";
  EXPECT_EQ(both.height, 2000);
}

TEST(OffscreenSizeParse, TheCallersDefaultsAreUsedAndClampedToo)
{
  const OffscreenSizeChoice unset = parse_offscreen_size(nullptr, 640, 360);
  EXPECT_EQ(unset.width, 640);
  EXPECT_EQ(unset.height, 360);

  const OffscreenSizeChoice silly = parse_offscreen_size("nonsense", 50, 99999);
  EXPECT_EQ(silly.width, 200);
  EXPECT_EQ(silly.height, 2000);
  ASSERT_FALSE(silly.warning.empty());
  EXPECT_NE(silly.warning.find("200x2000"), std::string::npos)
    << "the warning names what is actually used: " << silly.warning;
}

TEST(OffscreenSizeParse, WithoutASourceNameTheWarningStillSaysWhatWasTyped)
{
  const OffscreenSizeChoice choice = parse_offscreen_size("  wide  ");
  ASSERT_FALSE(choice.warning.empty());
  EXPECT_NE(choice.warning.find("size='  wide  '"), std::string::npos) << choice.warning;
}

// ---------------------------------------------------------------------------
// The window state show_offscreen leaves behind
// ---------------------------------------------------------------------------

TEST(OffscreenPanel, TheAttributesAndTheFlagAreSetBeforeItIsShown)
{
  OffscreenButtonPanel panel;
  QWidget * widget = panel.widget();

  EXPECT_TRUE(widget->testAttribute(Qt::WA_DontShowOnScreen))
    << "without it the panel would appear on the operator's desktop";
  EXPECT_FALSE(widget->testAttribute(Qt::WA_QuitOnClose))
    << "a visible parentless window with this keeps RViz from quitting";
  EXPECT_TRUE(widget->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus))
    << "activateWindow() and the panel's own shortcuts must be inert";
  EXPECT_TRUE(is_offscreen_panel(widget));
}

TEST(OffscreenPanel, ItIsVisibleToQtWhileNotOnScreen)
{
  OffscreenButtonPanel panel;
  QWidget * widget = panel.widget();

  // isVisible() is the one thing the capture (QWidget::render paints only visible
  // children) and the injector (childAt skips hidden ones) both need.
  EXPECT_TRUE(widget->isVisible());
  EXPECT_TRUE(panel.button()->isVisible()) << "visibility is inherited by the children";
  EXPECT_TRUE(widget->testAttribute(Qt::WA_DontShowOnScreen));
  EXPECT_EQ(widget->childAt(widget->rect().center()), panel.button())
    << "the injector resolves its targets this way";
}

TEST(OffscreenPanel, ItStaysAPlainTopLevelWindowAndNeverADialogToolOrPopup)
{
  OffscreenButtonPanel panel;
  QWidget * widget = panel.widget();

  EXPECT_TRUE(widget->isWindow());
  EXPECT_EQ(widget->windowType(), Qt::Window)
    << "any other type could be mistaken for an overlay of some other panel";
  EXPECT_FALSE(is_dialog_window(widget));
  EXPECT_NE(widget->windowType(), Qt::Tool);
  EXPECT_NE(widget->windowType(), Qt::Popup);
  EXPECT_NE(widget->windowType(), Qt::ToolTip);
  EXPECT_EQ(widget->parentWidget(), nullptr);
}

TEST(OffscreenPanel, TheSizeIsFixedToTheRequestedLogicalPixels)
{
  OffscreenButtonPanel panel(320, 540);
  QWidget * widget = panel.widget();

  EXPECT_EQ(widget->width(), 320);
  EXPECT_EQ(widget->height(), 540);
  EXPECT_EQ(widget->minimumSize(), QSize(320, 540));
  EXPECT_EQ(widget->maximumSize(), QSize(320, 540));

  // Nothing the content does may move it: not a resize, not an adjustSize, not the
  // layout asking for more room.
  widget->resize(900, 900);
  widget->adjustSize();
  widget->layout()->invalidate();
  QCoreApplication::processEvents();
  EXPECT_EQ(widget->size(), QSize(320, 540));
}

TEST(OffscreenPanel, ItIsPlacedAtTheTopLeftOfThePrimaryScreen)
{
  const QScreen * screen = QGuiApplication::primaryScreen();
  ASSERT_NE(screen, nullptr);

  // Parked somewhere else first, so that landing on the screen's corner is the call's
  // doing and not where a fresh window happens to start.
  QWidget widget;
  widget.move(screen->availableGeometry().topLeft() + QPoint(137, 91));
  ASSERT_NE(widget.pos(), screen->availableGeometry().topLeft());

  ASSERT_TRUE(show_offscreen(&widget, 300, 200));
  EXPECT_EQ(widget.pos(), screen->availableGeometry().topLeft())
    << "a popup is placed against the desktop and mapped back through global coordinates";
}

TEST(OffscreenPanel, TheSidesAreClampedToTheBounds)
{
  OffscreenButtonPanel tiny(10, 99999);
  EXPECT_EQ(tiny.widget()->width(), kMinOffscreenSide);
  EXPECT_EQ(tiny.widget()->height(), kMaxOffscreenSide);
}

TEST(OffscreenPanel, AChildWidgetOrNothingIsRefusedAndLeftAlone)
{
  EXPECT_FALSE(show_offscreen(nullptr, 300, 200));

  QWidget parent;
  auto * child = new QWidget(&parent);
  EXPECT_FALSE(show_offscreen(child, 300, 200)) << "a child has no window to keep off the desktop";
  EXPECT_FALSE(child->testAttribute(Qt::WA_DontShowOnScreen));
  EXPECT_FALSE(child->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
  EXPECT_FALSE(is_offscreen_panel(child));
  EXPECT_FALSE(is_offscreen_panel(nullptr));
}

TEST(OffscreenPanel, AnOrdinaryWindowIsNotMistakenForOne)
{
  VisibleButtonPanel panel;
  EXPECT_FALSE(is_offscreen_panel(panel.widget()));
  EXPECT_TRUE(panel.widget()->testAttribute(Qt::WA_QuitOnClose))
    << "the control: an ordinary window DOES keep the application alive";
}

TEST(OffscreenPanel, ShowingItAgainBringsAHiddenOneBack)
{
  OffscreenButtonPanel panel;
  QWidget * widget = panel.widget();
  widget->hide();
  QCoreApplication::processEvents();
  ASSERT_FALSE(widget->isVisible());

  ASSERT_TRUE(show_offscreen(widget, 300, 200));
  QCoreApplication::processEvents();
  EXPECT_TRUE(widget->isVisible());
  EXPECT_TRUE(is_offscreen_panel(widget)) << "and it is still not on the desktop";
  EXPECT_EQ(widget->size(), QSize(300, 200));
}

TEST(OffscreenPanel, ShowingItNeverTakesTheActiveWindowFromTheMainWindow)
{
  // What this pins is the SHOW: a window shown the ordinary way becomes the active
  // window, and showing the off-screen panel (a Displays panel is created while the
  // operator is working in the main window) must leave the main window active.
  //
  // What it cannot pin is the explicit activateWindow() RViz's own panels end an
  // action with ("force keyboard focus back on the main window"): this host's
  // offscreen platform plugin does not act on that call for ANY window (the control
  // below shows only the show-time activation). On Windows the call reaches
  // QWindow::requestActivate(), which refuses a window carrying
  // Qt::WindowDoesNotAcceptFocus (Qt5Gui.dll 5.15.2 contains the warning it
  // prints); that part is only observable in a running RViz.
  QWidget main_window;
  main_window.resize(200, 100);
  main_window.show();
  QCoreApplication::processEvents();
  QApplication::setActiveWindow(&main_window);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::activeWindow(), &main_window);

  {
    QWidget ordinary;
    ordinary.resize(200, 100);
    ordinary.show();
    QCoreApplication::processEvents();
    EXPECT_EQ(QApplication::activeWindow(), &ordinary)
      << "the control: an ordinary window becomes the active one when shown";
    QApplication::setActiveWindow(&main_window);
    QCoreApplication::processEvents();
  }
  ASSERT_EQ(QApplication::activeWindow(), &main_window);

  OffscreenButtonPanel panel;
  EXPECT_NE(QApplication::activeWindow(), panel.widget());
  EXPECT_FALSE(panel.widget()->isActiveWindow());
  EXPECT_EQ(QApplication::activeWindow(), &main_window) << "showing it took the active window";

  panel.widget()->activateWindow();
  QCoreApplication::processEvents();
  EXPECT_FALSE(panel.widget()->isActiveWindow());
  EXPECT_EQ(QApplication::activeWindow(), &main_window);
}

// ---------------------------------------------------------------------------
// Streaming it: the ordinary capture and input paths
// ---------------------------------------------------------------------------

TEST(OffscreenStream, ItIsCapturedAndSubmittedLikeAnyVisibleWidget)
{
  OffscreenButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("off", panel.widget(), options_at(), factory);

  ASSERT_TRUE(stream.tick(0.0)) << stream.diagnostics().last_error;
  EXPECT_TRUE(stream.diagnostics().widget_visible)
    << "an off-screen widget must not be reported as a hidden (blank) one";
  EXPECT_EQ(stream.diagnostics().adapter_rebuilds, 1u);
  ASSERT_NE(factory->latest(), nullptr);
  const auto & desc = factory->latest()->desc();
  EXPECT_EQ(desc.buffer_width, 300u) << "the logical size, at a device pixel ratio of 1";
  EXPECT_EQ(desc.buffer_height, 200u);
  ASSERT_EQ(factory->latest()->frames().size(), 1u);
  // The panel's own paint reached the buffer: the window background is opaque.
  EXPECT_EQ(alpha_at(factory->latest()->frames().front(), 150, 100), 255);
  EXPECT_EQ(alpha_at(factory->latest()->frames().front(), 5, 5), 255);
}

TEST(OffscreenStream, TheFixedSizeSurvivesFiftyNewRowsAndNeverRebuildsTheSender)
{
  auto * widget = new QWidget();
  auto * layout = new QVBoxLayout(widget);
  auto * tree = new QTreeWidget(widget);
  tree->setHeaderHidden(true);
  layout->addWidget(tree);
  ASSERT_TRUE(show_offscreen(widget, 300, 400));
  widget->ensurePolished();
  QCoreApplication::processEvents();

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("off", widget, options_at(), factory);
  ASSERT_TRUE(stream.tick(0.0)) << stream.diagnostics().last_error;
  ASSERT_EQ(stream.diagnostics().adapter_rebuilds, 1u);
  const std::uint32_t built_width = stream.diagnostics().buffer_width;
  const std::uint32_t built_height = stream.diagnostics().buffer_height;
  EXPECT_EQ(built_width, 300u);
  EXPECT_EQ(built_height, 400u);

  // The taller tree: fifty rows where there were none. Everything that could make a
  // normal window grow is tried as well.
  for (int i = 0; i < 50; ++i) {
    new QTreeWidgetItem(tree, QStringList(QString("display %1").arg(i)));
  }
  tree->expandAll();
  widget->layout()->invalidate();
  widget->adjustSize();
  widget->resize(500, 900);
  QCoreApplication::processEvents();

  // Long enough, and over enough ticks, for the resize hysteresis (0.5 s, 3 ticks) to
  // adopt a new size if the widget had one.
  for (int i = 1; i <= 30; ++i) {
    stream.tick(0.1 * i);
  }

  EXPECT_EQ(widget->size(), QSize(300, 400)) << "the surface never changes size";
  EXPECT_EQ(stream.diagnostics().adapter_rebuilds, 1u) << "so the sender is never rebuilt";
  EXPECT_EQ(factory->create_calls(), 1u);
  EXPECT_FALSE(stream.diagnostics().resize_pending);
  EXPECT_EQ(stream.diagnostics().buffer_width, built_width);
  EXPECT_EQ(stream.diagnostics().buffer_height, built_height);
  EXPECT_EQ(tree->topLevelItemCount(), 50);
  EXPECT_GT(tree->verticalScrollBar() != nullptr ? tree->verticalScrollBar()->maximum() : 0, 0)
    << "the extra rows scroll inside the fixed surface";

  delete widget;
}

TEST(OffscreenStream, TheControlThatCanBeResizedDoesRebuildTheSender)
{
  // Without this the test above could pass for the wrong reason (a stream that never
  // rebuilds on ANY size change).
  auto * widget = new QWidget();
  widget->resize(300, 400);
  widget->show();
  QCoreApplication::processEvents();

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("plain", widget, options_at(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  ASSERT_EQ(stream.diagnostics().adapter_rebuilds, 1u);

  widget->resize(500, 900);
  QCoreApplication::processEvents();
  for (int i = 1; i <= 30; ++i) {
    stream.tick(0.1 * i);
  }
  EXPECT_EQ(stream.diagnostics().adapter_rebuilds, 2u);
  EXPECT_EQ(stream.diagnostics().buffer_width, 500u);

  delete widget;
}

TEST(OffscreenStream, AnArTapPressesAButtonInsideIt)
{
  OffscreenButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("off", panel.widget(), options_at()).ok());
  manager.tick();  // derives the panel's logical size, which a tap is normalised against

  EXPECT_TRUE(manager.handle_sample(tap("off", 0.5f, 0.5f)));
  EXPECT_EQ(panel.clicks(), 1) << "the real QPushButton's clicked() ran";

  const auto snapshot = manager.diagnostics();
  ASSERT_EQ(snapshot.panels.size(), 1u);
  EXPECT_EQ(snapshot.panels[0].input_samples_received, 1u);
  EXPECT_GE(snapshot.panels[0].input_events_delivered, 2u);
  EXPECT_EQ(snapshot.panels[0].input_events_dropped, 0u);

  // The same tap off the panel is not a click.
  EXPECT_FALSE(manager.handle_sample(tap("off", 1.5f, 0.5f)));
  EXPECT_EQ(panel.clicks(), 1);
}

TEST(OffscreenStream, TheVisibleControlTakesTheSameTap)
{
  VisibleButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("vis", panel.widget(), options_at()).ok());
  manager.tick();
  EXPECT_TRUE(manager.handle_sample(tap("vis", 0.5f, 0.5f)));
  EXPECT_EQ(panel.clicks(), 1);
}

TEST(OffscreenStream, AnArTapOnAComboOpensAListThatReachesTheStream)
{
  OffscreenComboPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("off", panel.widget(), options_at()).ok());
  manager.tick();
  ASSERT_NE(factory->latest(), nullptr);
  ASSERT_EQ(factory->latest()->frames().size(), 1u) << "the closed combo, sent once";
  const std::vector<std::uint8_t> closed = factory->latest()->frames().back().rgba;
  ASSERT_EQ(QApplication::activePopupWidget(), nullptr);

  // Tap the middle of the combo.
  const float u = (OffscreenComboPanel::kComboX + OffscreenComboPanel::kComboW / 2.0f) /
    OffscreenComboPanel::kWidth;
  const float v = (OffscreenComboPanel::kComboY + OffscreenComboPanel::kComboH / 2.0f) /
    OffscreenComboPanel::kHeight;
  ASSERT_TRUE(manager.handle_sample(tap("off", u, v)));
  QCoreApplication::processEvents();

  QWidget * list = QApplication::activePopupWidget();
  ASSERT_NE(list, nullptr) << "the AR tap did not open the combo's list";
  EXPECT_TRUE(qar_qt_streaming::panel_tree_owns_popup(panel.widget(), list))
    << "the list belongs to the off-screen panel's tree";
  EXPECT_TRUE(qar_qt_streaming::is_offscreen_popup(list))
    << "the list must be kept off the desktop";
  EXPECT_FALSE(list->windowHandle() != nullptr && list->windowHandle()->isVisible())
    << "Qt mapped the list as a desktop window";
  // This host rasterizes no glyphs, so the list is given a known colour to be found.
  paint_widget(list, Qt::magenta);
  QCoreApplication::processEvents();

  // The capture composites it...
  QtWidgetCapture capture(panel.widget());
  ASSERT_TRUE(capture.capture(300, 200, 1.0)) << capture.last_error();
  EXPECT_EQ(capture.composited_popup_count(), 1u);

  // ...and so does the stream: the open list is a new frame.
  manager.tick();
  ASSERT_EQ(factory->latest()->frames().size(), 2u) << "the open list never reached the stream";
  const auto & open = factory->latest()->frames().back();
  EXPECT_NE(open.rgba, closed);

  // Where Qt put the list, in panel coordinates: painted there, and black beside it.
  const QPoint local =
    panel.widget()->mapFromGlobal(qar_qt_streaming::popup_global_origin(list));
  ASSERT_GE(local.x(), 1);
  ASSERT_GE(local.y(), 0);
  ASSERT_LT(local.x() + 4, OffscreenComboPanel::kWidth);
  ASSERT_LT(local.y() + 4, OffscreenComboPanel::kHeight);
  EXPECT_FALSE(is_black(open, local.x() + 2, local.y() + 2)) << "the list is not where Qt put it";
  EXPECT_TRUE(is_black(open, local.x() - 1, local.y() + 2)) << "the list bled past its left edge";

  panel.combo()->hidePopup();
  QCoreApplication::processEvents();
  manager.tick();
  ASSERT_EQ(factory->latest()->frames().size(), 3u);
  EXPECT_EQ(factory->latest()->frames().back().rgba, closed) << "closing the list restores the panel";
}

TEST(OffscreenStream, TheDialogSweepNeverTakesItForADialogOfAnotherPanel)
{
  OffscreenButtonPanel off;
  VisibleButtonPanel other;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("other", other.widget(), options_at()).ok());
  ASSERT_TRUE(manager.add_panel("off", off.widget(), options_at()).ok());
  manager.tick();

  // AR input on the OTHER panel is what primes dialog attribution by causation; with
  // the off-screen panel a visible top-level window, an overlay scan that mistook it
  // for a dialog would now hand it to 'other' as a panel of its own.
  ASSERT_TRUE(manager.handle_sample(tap("other", 0.5f, 0.5f)));
  manager.tick();
  manager.tick();

  EXPECT_TRUE(manager.dialog_panel_keys().empty());
  EXPECT_EQ(manager.panel_keys(), (std::vector<std::string>{"off", "other"}));
  EXPECT_TRUE(collect_panel_overlays(other.widget()).empty());
  EXPECT_TRUE(collect_panel_overlays(off.widget()).empty());
  EXPECT_FALSE(is_dialog_window(off.widget()));

  // The sweep itself is alive in this test: a real dialog parented into 'other' is
  // promoted - and the off-screen panel is still nobody's dialog afterwards.
  QDialog dialog(other.widget());
  dialog.setWindowTitle("Probe");
  dialog.resize(180, 100);
  dialog.show();
  QCoreApplication::processEvents();
  manager.tick();
  ASSERT_EQ(manager.dialog_panel_keys().size(), 1u);
  EXPECT_EQ(manager.dialog_panel_keys().front(), "dialog:Probe");
  EXPECT_TRUE(manager.has_panel("off"));
  EXPECT_FALSE(manager.is_tombstoned("off"));

  dialog.hide();
  QCoreApplication::processEvents();
  manager.tick();
  EXPECT_TRUE(manager.dialog_panel_keys().empty());
  EXPECT_TRUE(manager.has_panel("off")) << "retiring the dialog panel leaves the off-screen one";
  EXPECT_TRUE(off.widget()->isVisible());
}

TEST(OffscreenStream, StoppingTheStreamLeavesTheWidgetShownAndItCanBeStartedAgain)
{
  OffscreenButtonPanel panel;
  const QPointer<QWidget> guard(panel.widget());
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("off", panel.widget(), options_at()).ok());
  manager.tick();
  ASSERT_EQ(factory->create_calls(), 1u);

  // The Stop button: the generic close path. It tears down the STREAM and only that.
  ASSERT_TRUE(manager.close_panel("off", "stopped in RViz").ok());
  EXPECT_FALSE(manager.has_panel("off"));
  EXPECT_EQ(manager.diagnostics().panels_closed, 1u);
  ASSERT_FALSE(guard.isNull()) << "the widget must not be deleted by a close";
  EXPECT_TRUE(panel.widget()->isVisible()) << "nor hidden";
  EXPECT_TRUE(is_offscreen_panel(panel.widget()));
  EXPECT_EQ(factory->latest()->sender_destroy_calls(), 1u);

  // Start again: an ordinary fresh add onto the same widget, on a new sender.
  ASSERT_TRUE(manager.add_panel("off", panel.widget(), options_at()).ok());
  manager.tick();
  EXPECT_EQ(factory->create_calls(), 2u);
  EXPECT_TRUE(manager.handle_sample(tap("off", 0.5f, 0.5f)));
  EXPECT_EQ(panel.clicks(), 1) << "and it takes input again";
}

TEST(OffscreenStream, StoppingAndStartingInNoticeModeRevivesTheSameSlot)
{
  OffscreenButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, PanelCloseMode::Notice));
  ASSERT_TRUE(manager.add_panel("off", panel.widget(), options_at()).ok());
  manager.tick();

  ASSERT_TRUE(manager.close_panel("off", "stopped in RViz", "Streaming was stopped.").ok());
  EXPECT_TRUE(manager.is_tombstoned("off"));
  EXPECT_TRUE(panel.widget()->isVisible()) << "the notice replaces the capture, not the widget";
  EXPECT_TRUE(is_offscreen_panel(panel.widget()));

  ASSERT_TRUE(manager.add_panel("off", panel.widget(), options_at()).ok())
    << "Start on a tombstoned key revives it";
  EXPECT_FALSE(manager.is_tombstoned("off"));
  manager.tick();
  EXPECT_EQ(factory->create_calls(), 1u) << "the notice slot's sender is re-used";
  EXPECT_TRUE(manager.handle_sample(tap("off", 0.5f, 0.5f)));
  EXPECT_EQ(panel.clicks(), 1);
}

TEST(OffscreenStream, StartingAgainAfterTheOperatorClosedItInArNeedsTheStaleStreamClosedFirst)
{
  OffscreenButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("off", panel.widget(), options_at()).ok());
  manager.tick();

  // The operator closes it in AR. Capture stops, but the stream entry stays: its Qt
  // window never closes, so nothing would ever retire it.
  ASSERT_TRUE(manager.note_ar_panel_closed("off"));
  EXPECT_EQ(manager.diagnostics().ar_closed_panels, 1u);
  EXPECT_TRUE(manager.has_panel("off"));
  EXPECT_FALSE(manager.is_tombstoned("off"));

  // So Start cannot simply add: the key is still held by the stale stream...
  EXPECT_EQ(
    manager.add_panel("off", panel.widget(), options_at()).status,
    qar_ar_core::Status::AlreadyExists);

  // ...it clears that stream first (the AR panel is already closed, so
  // qar_gui_panels_close_panel is skipped, never called twice), then adds afresh.
  ASSERT_TRUE(manager.close_panel("off", "restarted").ok());
  EXPECT_EQ(manager.diagnostics().close_panel_skipped, 1u);
  EXPECT_TRUE(factory->created().front()->last_close_panel_closed_in_ar());
  ASSERT_TRUE(manager.add_panel("off", panel.widget(), options_at()).ok());
  manager.tick();
  EXPECT_EQ(factory->create_calls(), 2u);
  EXPECT_TRUE(manager.handle_sample(tap("off", 0.5f, 0.5f)));
  EXPECT_EQ(panel.clicks(), 1);
  EXPECT_TRUE(panel.widget()->isVisible());
}
