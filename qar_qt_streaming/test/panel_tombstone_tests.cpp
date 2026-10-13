// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The tombstone lifetime: a video sender is created once and destroyed only at
// session shutdown. A source window that closes leaves the stream running and
// showing a notice; an AR-side close stops capture and nothing else. The
// assertions are therefore mostly about the test double's close_calls() and
// create_calls(), because "what did we do to the sender" is the whole subject.
//
// That is the `notice` close mode (QAR_PANEL_CLOSE_MODE=notice), the runtime
// escape hatch; the default is `destroy` (see panel_close_tests.cpp). Every
// manager here is therefore PINNED to PanelCloseMode::Notice: these tests are
// about what the tombstone does, and must keep saying so whatever the default is.
//
// Nothing here asserts on rendered TEXT: the offscreen platform plugin
// rasterises no glyphs, so a notice and a blank rectangle can be byte-identical
// where text is the only difference. What is asserted is structure (a different
// widget is being captured, at the same buffer layout), counters, and the pure
// fit arithmetic — none of which needs a font engine.

#include <gtest/gtest.h>

#include "qar_qt_streaming/panel_tombstone.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QCoreApplication>
#include <QPalette>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>
#include <string>
#include <vector>

using qar_qt_streaming::PanelCloseMode;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::PanelTombstoneStyle;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_ar_core::Status;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

/// A streamable widget: a solid colour block, shown under the offscreen plugin
/// so render() draws its children. Same recipe as qt_panel_stream_tests.cpp.
class SourceWidget
{
public:
  explicit SourceWidget(int w = 200, int h = 120)
  {
    widget_ = new QWidget();
    auto * layout = new QVBoxLayout(widget_);
    auto * block = new QWidget(widget_);
    block->setAutoFillBackground(true);
    QPalette palette = block->palette();
    palette.setColor(QPalette::Window, Qt::red);
    block->setPalette(palette);
    layout->addWidget(block);
    widget_->resize(w, h);
    widget_->show();
    widget_->ensurePolished();
    widget_->layout()->activate();
    QCoreApplication::processEvents();
  }

  ~SourceWidget() { delete widget_; }

  QWidget * get() const { return widget_; }

private:
  QWidget * widget_ = nullptr;
};

PanelStreamOptions
options_no_pacing()
{
  PanelStreamOptions options;
  options.max_fps = 0;              // every tick captures
  options.keepalive_seconds = 0.0;  // isolate the dirty check
  return options;
}

/// A manager wired to a MockQarPanelSenderFactory and a manual clock, with dialog
/// promotion off (these tests are about lifetime, not about dialogs).
struct Harness
{
  Harness()
  {
    factory = std::make_shared<MockQarPanelSenderFactory>();
    clock = std::make_shared<ManualClock>();
    QtPanelStreamingConfig config;
    config.panel_sender_factory = factory;
    config.clock = clock;
    config.dialog_panels = false;
    config.close_mode = PanelCloseMode::Notice;
    manager = std::make_unique<QtPanelStreamManager>(config);
  }

  std::shared_ptr<MockQarPanelSenderFactory> factory;
  std::shared_ptr<ManualClock> clock;
  std::unique_ptr<QtPanelStreamManager> manager;
};

/// Total close() calls the factory has seen across every sender it minted.
std::uint32_t
total_close_calls(const MockQarPanelSenderFactory & factory)
{
  std::uint32_t total = 0;
  for (const auto & adapter : factory.created()) {
    total += adapter->close_calls();
  }
  return total;
}

}  // namespace

// --- the fit arithmetic -----------------------------------------------------

TEST(PanelTombstoneSizing, AWidePanelShowsTheNoticeAtTheBodySize)
{
  PanelTombstoneStyle style;  // 12 px body, 8.7 px floor
  // 600 logical px is far more than "Displays" needs at 12 px.
  EXPECT_DOUBLE_EQ(qar_qt_streaming::tombstone_body_font_px(600, 8, style), style.body_font_px);
  EXPECT_FALSE(qar_qt_streaming::tombstone_is_clipped(600, 8, style));
}

TEST(PanelTombstoneSizing, ANarrowPanelShrinksTheNoticeRatherThanCuttingIt)
{
  PanelTombstoneStyle style;
  // 100 px wide with a 20-character name: 100 * 0.86 / (20 * 0.55) = 7.8 px,
  // below the 12 px body size, so the notice is set smaller to fit.
  const double px = qar_qt_streaming::tombstone_body_font_px(100, 20, style);
  EXPECT_LT(px, style.body_font_px);
  EXPECT_GE(px, style.minimum_font_px);
}

TEST(PanelTombstoneSizing, TheNoticeIsNeverShrunkBelowTheReadingFloor)
{
  PanelTombstoneStyle style;
  // 60 px for a 40-character name cannot be fitted at any readable size.
  const double px = qar_qt_streaming::tombstone_body_font_px(60, 40, style);
  EXPECT_DOUBLE_EQ(px, style.minimum_font_px);
  // ...and that is reported rather than silently accepted.
  EXPECT_TRUE(qar_qt_streaming::tombstone_is_clipped(60, 40, style));
}

TEST(PanelTombstoneSizing, AZeroWidthPanelIsNotTreatedAsClipped)
{
  PanelTombstoneStyle style;
  EXPECT_DOUBLE_EQ(qar_qt_streaming::tombstone_body_font_px(0, 12, style), style.body_font_px);
  EXPECT_FALSE(qar_qt_streaming::tombstone_is_clipped(0, 12, style));
}

// --- the widget -------------------------------------------------------------

TEST(PanelTombstoneWidget, IsBuiltAtThePanelsSizeAndIsRenderableWithoutAWindow)
{
  qar_qt_streaming::PanelTombstoneText text;
  text.panel_name = "Displays";
  std::unique_ptr<QWidget> notice(
    qar_qt_streaming::make_panel_tombstone_widget(text, PanelTombstoneStyle{}, 240, 160));
  ASSERT_NE(notice, nullptr);
  EXPECT_EQ(notice->width(), 240);
  EXPECT_EQ(notice->height(), 160);
  // Visible in Qt's sense — which is what QWidget::render() needs of children —
  // while never mapped onto the desktop.
  EXPECT_TRUE(notice->isVisible());
  EXPECT_TRUE(notice->testAttribute(Qt::WA_DontShowOnScreen));
  // Structure, not glyphs: the name, the rule, the reason and the dismissal.
  EXPECT_EQ(notice->findChildren<QWidget *>().size() >= 4, true);
}

// --- the lifetime ------------------------------------------------------------

TEST(PanelLifetime, ClosingASourceWidgetTombstonesTheStreamAndDoesNotCloseTheSender)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.manager->tick(), 1u);
  ASSERT_EQ(h.factory->create_calls(), 1u);
  ASSERT_EQ(h.factory->latest()->close_calls(), 0u);

  ASSERT_TRUE(h.manager->tombstone_panel("dock", "the window was closed in RViz").ok());

  // THE assertion: the sender is untouched.
  EXPECT_EQ(h.factory->latest()->close_calls(), 0u);
  EXPECT_EQ(h.factory->create_calls(), 1u);
  // The panel is still there, and it is a notice.
  EXPECT_TRUE(h.manager->has_panel("dock"));
  EXPECT_TRUE(h.manager->is_tombstoned("dock"));
  EXPECT_EQ(h.manager->tombstoned_panel_keys(), std::vector<std::string>{"dock"});
}

TEST(PanelLifetime, TheTombstoneCapturesAndSubmitsAChangedFrameAtTheSameLayout)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();
  ASSERT_EQ(adapter->frames().size(), 1u);
  const auto live_frame = adapter->frames().front();

  ASSERT_TRUE(h.manager->tombstone_panel("dock", "closed").ok());
  h.clock->advance(0.1);
  EXPECT_EQ(h.manager->tick(), 1u) << "the notice must actually be sent";

  ASSERT_EQ(adapter->frames().size(), 2u);
  const auto notice_frame = adapter->frames().back();
  // Same buffer layout — a tombstone must never provoke a sender rebuild.
  EXPECT_EQ(notice_frame.width, live_frame.width);
  EXPECT_EQ(notice_frame.height, live_frame.height);
  EXPECT_EQ(h.factory->create_calls(), 1u);
  EXPECT_EQ(h.manager->diagnostics().panels.front().adapter_rebuilds, 1u);
  // ...and it is a DIFFERENT picture. The dark AR ground alone guarantees this
  // on a host that rasterises no glyphs, which is exactly why the notice is
  // built on a filled surface rather than on text alone.
  EXPECT_NE(notice_frame.rgba, live_frame.rgba);
}

TEST(PanelLifetime, ATombstonedPanelKeepsStreamingAcrossManyTicks)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  ASSERT_TRUE(h.manager->tombstone_panel("dock", "closed").ok());

  for (int i = 0; i < 20; ++i) {
    h.clock->advance(0.1);
    h.manager->tick();
  }
  // Still one sender, never closed, and the panel is still known.
  EXPECT_EQ(h.factory->create_calls(), 1u);
  EXPECT_EQ(h.factory->latest()->close_calls(), 0u);
  EXPECT_TRUE(h.manager->is_tombstoned("dock"));
}

TEST(PanelLifetime, ANoticeTakesNoInput)
{
  Harness h;
  SourceWidget source;
  PanelStreamOptions options = options_no_pacing();
  options.placement.pose.position = QarVector3{0.0f, 0.0f, 0.0f};
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options).ok());
  ASSERT_EQ(h.manager->tick(), 1u);
  // A point on the panel's plane resolves while it is live...
  ASSERT_TRUE(
    h.manager->route_room_point(
      QarVector3{0.0f, 0.0f, 0.0f}, qar_qt_streaming::PanelPointerEvent::Kind::Hover));

  ASSERT_TRUE(h.manager->tombstone_panel("dock", "closed").ok());
  // ...and lands on nothing once it is a notice, rather than being swallowed by
  // a panel that has nothing to click.
  EXPECT_FALSE(
    h.manager->route_room_point(
      QarVector3{0.0f, 0.0f, 0.0f}, qar_qt_streaming::PanelPointerEvent::Kind::Hover));
}

// --- the AR side ------------------------------------------------------------

TEST(PanelLifetime, ClosingThePanelInArStopsCaptureWithoutDestroyingTheSender)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();
  const std::size_t submitted_before = adapter->frames().size();

  EXPECT_TRUE(h.manager->note_ar_panel_closed("dock"));

  for (int i = 0; i < 5; ++i) {
    h.clock->advance(0.1);
    EXPECT_EQ(h.manager->tick(), 0u) << "a panel closed in AR must not be captured";
  }
  EXPECT_EQ(adapter->frames().size(), submitted_before);
  // The rule, asserted: teardown is deferred to session shutdown.
  EXPECT_EQ(adapter->close_calls(), 0u);
  EXPECT_TRUE(h.manager->has_panel("dock"));
  EXPECT_TRUE(h.manager->diagnostics().panels.front().ar_panel_closed);
  EXPECT_EQ(h.manager->diagnostics().ar_closed_panels, 1u);
}

TEST(PanelLifetime, TheArCloseArrivesThroughTheInputSourceAndIsAppliedOnTick)
{
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto clock = std::make_shared<ManualClock>();
  auto source_queue = std::make_shared<qar_ar_core::QueuedPanelInputSource>();
  QtPanelStreamingConfig config;
  config.panel_sender_factory = factory;
  config.clock = clock;
  config.dialog_panels = false;
  config.close_mode = PanelCloseMode::Notice;
  config.input_source = source_queue;
  QtPanelStreamManager manager(config);

  SourceWidget source;
  ASSERT_TRUE(manager.add_panel("dock", source.get(), options_no_pacing()).ok());
  clock->advance(0.1);
  ASSERT_EQ(manager.tick(), 1u);

  // What QarCApiPanelInputSource pushes when qar_gui_panels_subscribe_updates
  // reports the panel's state reaching QAR_GUI_PANEL_STATE_CLOSED.
  source_queue->push_closed_panel("dock");
  clock->advance(0.1);
  EXPECT_EQ(manager.tick(), 0u);
  EXPECT_TRUE(manager.diagnostics().panels.front().ar_panel_closed);
  EXPECT_EQ(factory->latest()->close_calls(), 0u);
}

// --- session shutdown -------------------------------------------------------

TEST(PanelLifetime, SessionShutdownClosesEveryPanelExactlyOnce)
{
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  {
    auto clock = std::make_shared<ManualClock>();
    QtPanelStreamingConfig config;
    config.panel_sender_factory = factory;
    config.clock = clock;
    config.dialog_panels = false;
    config.close_mode = PanelCloseMode::Notice;
    QtPanelStreamManager manager(config);

    SourceWidget live;
    SourceWidget doomed;
    SourceWidget dismissed;
    ASSERT_TRUE(manager.add_panel("live", live.get(), options_no_pacing()).ok());
    ASSERT_TRUE(manager.add_panel("closed", doomed.get(), options_no_pacing()).ok());
    ASSERT_TRUE(manager.add_panel("in-ar", dismissed.get(), options_no_pacing()).ok());
    clock->advance(0.1);
    ASSERT_EQ(manager.tick(), 3u);
    ASSERT_EQ(factory->create_calls(), 3u);

    ASSERT_TRUE(manager.tombstone_panel("closed", "closed in RViz").ok());
    ASSERT_TRUE(manager.note_ar_panel_closed("in-ar"));
    ASSERT_EQ(total_close_calls(*factory), 0u) << "nothing closes a sender mid-session";

    manager.clear();
    EXPECT_EQ(total_close_calls(*factory), 3u);
    // ...and the destructor must not close them a second time.
  }
  EXPECT_EQ(total_close_calls(*factory), 3u);
}

// --- bounding the accumulation ----------------------------------------------

TEST(PanelLifetime, ManyOpenCloseCyclesReuseTheOneSender)
{
  Harness h;
  for (int cycle = 0; cycle < 25; ++cycle) {
    SourceWidget source;
    ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok())
      << "cycle " << cycle;
    h.clock->advance(0.1);
    h.manager->tick();
    ASSERT_TRUE(h.manager->tombstone_panel("dock", "closed").ok()) << "cycle " << cycle;
  }
  // The bound: one key, one sender, however many times the window comes and
  // goes — and not one of them closed.
  EXPECT_EQ(h.factory->create_calls(), 1u);
  EXPECT_EQ(total_close_calls(*h.factory), 0u);
  EXPECT_EQ(h.manager->panel_keys().size(), 1u);
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.tombstoned_panels, 1u);
  EXPECT_EQ(snapshot.tombstones_created, 25u);
  EXPECT_EQ(snapshot.tombstones_revived, 24u);
}

TEST(PanelLifetime, RevivingARetiredSlotKeepsTheSenderAndStreamsTheNewWidget)
{
  Harness h;
  SourceWidget first;
  ASSERT_TRUE(h.manager->add_panel("dock", first.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  ASSERT_TRUE(h.manager->tombstone_panel("dock", "closed").ok());

  SourceWidget second;  // same size, so the sender's layout still fits
  ASSERT_TRUE(h.manager->add_panel("dock", second.get(), options_no_pacing()).ok());
  EXPECT_FALSE(h.manager->is_tombstoned("dock"));
  h.clock->advance(0.1);
  EXPECT_EQ(h.manager->tick(), 1u);
  EXPECT_EQ(h.factory->create_calls(), 1u) << "reviving must not mint a sender";
  EXPECT_EQ(h.factory->latest()->close_calls(), 0u);
}

TEST(PanelLifetime, DistinctWindowsAreBoundedByTheTombstoneCap)
{
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto clock = std::make_shared<ManualClock>();
  QtPanelStreamingConfig config;
  config.panel_sender_factory = factory;
  config.clock = clock;
  config.dialog_panels = false;
  config.close_mode = PanelCloseMode::Notice;
  config.max_tombstoned_panels = 2;
  QtPanelStreamManager manager(config);

  std::vector<std::unique_ptr<SourceWidget>> widgets;
  for (int i = 0; i < 2; ++i) {
    widgets.push_back(std::make_unique<SourceWidget>());
    const std::string key = "dock" + std::to_string(i);
    ASSERT_TRUE(manager.add_panel(key, widgets.back()->get(), options_no_pacing()).ok());
    clock->advance(0.1);
    manager.tick();
    ASSERT_TRUE(manager.tombstone_panel(key, "closed").ok());
  }
  ASSERT_EQ(manager.diagnostics().tombstoned_panels, 2u);

  // At the cap a NEW window is refused rather than an existing sender being
  // freed: freeing one is the mid-session destroy this lifetime exists to avoid.
  SourceWidget extra;
  const auto refused = manager.add_panel("dock-extra", extra.get(), options_no_pacing());
  EXPECT_FALSE(refused.ok());
  EXPECT_EQ(refused.status, Status::InvalidState);
  EXPECT_FALSE(manager.has_panel("dock-extra"));
  EXPECT_GE(manager.diagnostics().tombstone_cap_refusals, 1u);
  EXPECT_EQ(total_close_calls(*factory), 0u);

  // Re-opening a window that already HAS a retired slot still works: it creates
  // nothing, so the cap has nothing to say about it.
  ASSERT_TRUE(manager.add_panel("dock0", widgets.front()->get(), options_no_pacing()).ok());
  EXPECT_FALSE(manager.is_tombstoned("dock0"));
  EXPECT_EQ(factory->create_calls(), 2u);
}

TEST(PanelLifetime, APanelThatNeverGotASenderIsErasedRatherThanTombstoned)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  // No tick, so no sender was ever created. There is nothing to protect, and a
  // notice about a stream that never existed would occupy a slot for nothing.
  ASSERT_EQ(h.factory->create_calls(), 0u);
  ASSERT_TRUE(h.manager->tombstone_panel("dock", "closed").ok());
  EXPECT_FALSE(h.manager->has_panel("dock"));
  EXPECT_EQ(h.manager->diagnostics().tombstoned_panels, 0u);
}

// --- diagnostics ------------------------------------------------------------

TEST(PanelLifetime, DiagnosticsSeparateLiveFromTombstonedPanels)
{
  Harness h;
  SourceWidget live;
  SourceWidget doomed;
  ASSERT_TRUE(h.manager->add_panel("live", live.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("gone", doomed.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 2u);
  ASSERT_TRUE(h.manager->tombstone_panel("gone", "the dialog was dismissed").ok());

  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.panel_count, 2u);
  EXPECT_EQ(snapshot.live_panels, 1u);
  EXPECT_EQ(snapshot.tombstoned_panels, 1u);
  EXPECT_EQ(snapshot.tombstones_created, 1u);
  for (const auto & panel : snapshot.panels) {
    if (panel.key == "gone") {
      EXPECT_TRUE(panel.tombstoned);
      EXPECT_EQ(panel.tombstone_reason, "the dialog was dismissed");
      EXPECT_FALSE(panel.stream_id.empty()) << "the retired stream must still be nameable";
    } else {
      EXPECT_FALSE(panel.tombstoned);
    }
  }
}
