// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// QarOS panels that outlive a stream: the common name of every panel, a size change that swaps
// the video sender on the same QarOS panel (QAR_PANEL_RESIZE=swap) or makes a second panel
// (new-panel), the end of a session that hides and detaches instead of closing, and the rule that
// ties them together: within one session a key never gets a second live sender while another
// sender of the same key holds the same common name, because both would point at ONE QarOS panel.
//
// The recording factory counts such a collision (live_name_collisions), so every test of the rule
// ends with that count at 0. What the real adapter does with a name needs the SDK.

#include <gtest/gtest.h>

#include "qar_ar_core/common_name.hpp"
#include "qar_qt_streaming/panel_resize_mode.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QCoreApplication>
#include <QDialog>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using qar_ar_core::DiagnosticSeverity;
using qar_qt_streaming::PanelCloseMode;
using qar_qt_streaming::PanelOverlayAttribution;
using qar_qt_streaming::PanelResizeMode;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

/// A streamable widget, shown under the offscreen platform plugin so render() draws it.
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
    palette.setColor(QPalette::Window, Qt::blue);
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

/// A 200 x 120 panel whose whole area is one button, so a dialog can be parented to it.
class ButtonPanel
{
public:
  ButtonPanel()
  {
    widget_ = new QWidget();
    auto * layout = new QVBoxLayout(widget_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QPushButton("Go", widget_));
    widget_->resize(200, 120);
    widget_->show();
    widget_->ensurePolished();
    widget_->layout()->activate();
    QCoreApplication::processEvents();
  }

  ~ButtonPanel() { delete widget_; }

  QWidget * widget() const { return widget_; }

private:
  QWidget * widget_ = nullptr;
};

struct DialogGuard
{
  DialogGuard() { PanelOverlayAttribution::instance().reset(); }
  ~DialogGuard() { PanelOverlayAttribution::instance().reset(); }
};

PanelStreamOptions
options_no_pacing()
{
  PanelStreamOptions options;
  options.max_fps = 0;              // every tick captures
  options.keepalive_seconds = 0.0;  // isolate the dirty check
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  options.placement.pose.position = QarVector3{0.0f, 0.0f, 0.0f};
  return options;
}

class RecordingDiagnostics : public qar_ar_core::PanelDiagnosticsSink
{
public:
  void on_message(
    DiagnosticSeverity severity, const std::string & category, const std::string & message,
    const std::string & panel_key) override
  {
    (void)category;
    (void)panel_key;
    entries.push_back({severity, message});
  }

  std::size_t count(const std::string & needle) const
  {
    std::size_t n = 0;
    for (const auto & entry : entries) {
      if (entry.second.find(needle) != std::string::npos) {
        ++n;
      }
    }
    return n;
  }

  const std::pair<DiagnosticSeverity, std::string> * find(const std::string & needle) const
  {
    for (const auto & entry : entries) {
      if (entry.second.find(needle) != std::string::npos) {
        return &entry;
      }
    }
    return nullptr;
  }

  std::vector<std::pair<DiagnosticSeverity, std::string>> entries;
};

/// A manager over the recording factory whose senders can detach (like the real sender) unless
/// told otherwise, with a name scope and a resize mode.
struct Rig
{
  explicit Rig(
    PanelCloseMode mode = PanelCloseMode::Park, PanelResizeMode resize = PanelResizeMode::Swap,
    bool detach = true, bool dialogs = false)
  {
    factory = std::make_shared<MockQarPanelSenderFactory>();
    factory->set_senders_support_detach(detach);
    clock = std::make_shared<ManualClock>();
    QtPanelStreamingConfig config;
    config.panel_sender_factory = factory;
    config.clock = clock;
    config.dialog_panels = dialogs;
    config.diagnostics = &diagnostics;
    config.pump_input_in_tick = false;
    config.close_mode = mode;
    config.resize_mode = resize;
    config.name_scope = scope();
    manager = std::make_unique<QtPanelStreamManager>(config);
  }

  static qar_ar_core::NameScope scope()
  {
    return qar_ar_core::make_name_scope("qt-panel-example", "", "TESTPC");
  }

  std::uint32_t tick(double dt = 0.1)
  {
    clock->advance(dt);
    return manager->tick();
  }

  /// A size change of @p source, ticked until the hysteresis has adopted it.
  void resize_and_settle(SourceWidget & source, int width, int height)
  {
    source.get()->resize(width, height);
    QCoreApplication::processEvents();
    for (int i = 0; i < 6; ++i) {
      tick(0.2);
    }
  }

  std::shared_ptr<MockQarPanelSenderFactory> factory;
  std::shared_ptr<ManualClock> clock;
  RecordingDiagnostics diagnostics;
  std::unique_ptr<QtPanelStreamManager> manager;
};

// --- the resize switch --------------------------------------------------------------

TEST(PanelResizeMode, UnsetAndEmptyGiveTheDefaultSwapWithoutAWarning)
{
  for (const char * value : {static_cast<const char *>(nullptr), "", "  \t "}) {
    const auto choice = qar_qt_streaming::parse_panel_resize_mode(value);
    EXPECT_EQ(choice.mode, PanelResizeMode::Swap);
    EXPECT_FALSE(choice.explicitly_chosen);
    EXPECT_TRUE(choice.warning.empty());
  }
}

TEST(PanelResizeMode, BothModesParseWhateverTheCaseAndSpellingOfTheHyphen)
{
  for (const char * value : {"swap", " SWAP ", "Swap"}) {
    const auto choice = qar_qt_streaming::parse_panel_resize_mode(value);
    EXPECT_EQ(choice.mode, PanelResizeMode::Swap) << value;
    EXPECT_TRUE(choice.explicitly_chosen) << value;
    EXPECT_TRUE(choice.warning.empty()) << value;
  }
  for (const char * value : {"new-panel", "NEW_PANEL", " newpanel "}) {
    const auto choice = qar_qt_streaming::parse_panel_resize_mode(value);
    EXPECT_EQ(choice.mode, PanelResizeMode::NewPanel) << value;
    EXPECT_TRUE(choice.explicitly_chosen) << value;
    EXPECT_TRUE(choice.warning.empty()) << value;
  }
  EXPECT_STREQ(qar_qt_streaming::to_string(PanelResizeMode::Swap), "swap");
  EXPECT_STREQ(qar_qt_streaming::to_string(PanelResizeMode::NewPanel), "new-panel");
}

TEST(PanelResizeMode, AnUnknownWordKeepsTheDefaultAndSaysSo)
{
  const auto choice = qar_qt_streaming::parse_panel_resize_mode("swop");
  EXPECT_EQ(choice.mode, PanelResizeMode::Swap);
  EXPECT_FALSE(choice.explicitly_chosen);
  EXPECT_NE(choice.warning.find("QAR_PANEL_RESIZE='swop'"), std::string::npos) << choice.warning;
  EXPECT_NE(choice.warning.find("'swap' or 'new-panel'"), std::string::npos) << choice.warning;
}

// --- the common name ----------------------------------------------------------------

TEST(PanelCommonName, EveryPanelCarriesTheNameOfItsKeyInTheScopeOfTheManager)
{
  Rig rig;
  SourceWidget source;
  ASSERT_TRUE(rig.manager->add_panel("Control Panel", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  ASSERT_EQ(rig.factory->create_calls(), 1u);
  const std::string expected =
    qar_ar_core::make_panel_common_name("Control Panel", Rig::scope());
  EXPECT_EQ(rig.factory->latest()->common_name(), expected);
  EXPECT_NE(expected.find("qt-panel-example.testpc.qaros-ros"), std::string::npos) << expected;
  EXPECT_EQ(rig.factory->latest()->desc().key, "Control Panel");
}

TEST(PanelCommonName, AnEmptyScopeStillGivesAUsableNameForTheMocks)
{
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto clock = std::make_shared<ManualClock>();
  QtPanelStreamingConfig config;
  config.panel_sender_factory = factory;
  config.clock = clock;
  QtPanelStreamManager manager(config);
  SourceWidget source;
  ASSERT_TRUE(manager.add_panel("p", source.get(), options_no_pacing()).ok());
  clock->advance(0.1);
  ASSERT_EQ(manager.tick(), 1u);
  EXPECT_EQ(factory->latest()->common_name(), qar_ar_core::make_panel_common_name("p", {}));
  EXPECT_FALSE(factory->latest()->common_name().empty());
}

// --- a size change: the stream swap -------------------------------------------------

TEST(PanelSwap, ASizeChangeDetachesTheOldSenderFirstAndTheNewOneJoinsTheSamePanel)
{
  Rig rig;
  SourceWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  const auto first = rig.factory->created()[0];
  EXPECT_FALSE(first->reattached());

  rig.resize_and_settle(source, 400, 240);
  ASSERT_EQ(rig.factory->create_calls(), 2u);
  const auto second = rig.factory->created()[1];

  // Detach, then create: the old sender is gone before the panel is found again.
  EXPECT_EQ(
    rig.factory->events(), (std::vector<std::string>{"create p", "detach p", "create p"}));
  EXPECT_EQ(first->detach_calls(), 1u);
  EXPECT_EQ(first->close_calls(), 0u);
  EXPECT_EQ(first->state_calls(), 0u) << "the panel is neither hidden nor closed";
  EXPECT_EQ(second->common_name(), first->common_name()) << "the same QarOS panel";
  EXPECT_TRUE(second->reattached()) << "the factory finds the panel by its name";
  EXPECT_EQ(second->desc().buffer_width, 400u);
  EXPECT_EQ(rig.factory->live_name_collisions(), 0u);
  EXPECT_EQ(rig.manager->replaced_panel_count(), 0u) << "nothing is kept next to the new sender";
  EXPECT_EQ(rig.manager->diagnostics().panels_closed, 0u);
  EXPECT_GT(second->submit_calls(), 0u) << "the stream goes on";

  const auto * line = rig.diagnostics.find("changed size");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->first, DiagnosticSeverity::Info);
  EXPECT_EQ(
    line->second,
    "'p' changed size: its QarOS panel stays and gets a new video sender (no new panel, no close)");
}

TEST(PanelSwap, EverySizeChangeKeepsOneLiveSenderOnOnePanel)
{
  Rig rig;
  SourceWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  rig.resize_and_settle(source, 400, 240);
  rig.resize_and_settle(source, 300, 200);
  rig.resize_and_settle(source, 200, 120);  // back to the first size: still a swap, no revival
  ASSERT_EQ(rig.factory->create_calls(), 4u);
  std::size_t live = 0;
  for (const auto & adapter : rig.factory->created()) {
    live += adapter->live() ? 1u : 0u;
    EXPECT_EQ(adapter->close_calls(), 0u);
    EXPECT_EQ(adapter->common_name(), rig.factory->created()[0]->common_name());
  }
  EXPECT_EQ(live, 1u);
  EXPECT_TRUE(rig.factory->latest()->live());
  EXPECT_EQ(rig.factory->live_name_collisions(), 0u);
}

TEST(PanelSwap, APanelHiddenInArIsSwappedWhenItIsShownAndNotBefore)
{
  Rig rig;
  SourceWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  ASSERT_TRUE(rig.manager->set_panel_ar_visible("p", false).ok());
  rig.tick();
  ASSERT_TRUE(rig.manager->is_ar_hidden("p"));

  // A hidden stream captures nothing and keeps its sender at the size it has.
  rig.resize_and_settle(source, 400, 240);
  EXPECT_EQ(rig.factory->create_calls(), 1u);
  EXPECT_EQ(rig.factory->created()[0]->detach_calls(), 0u);

  ASSERT_TRUE(rig.manager->set_panel_ar_visible("p", true).ok());
  rig.resize_and_settle(source, 400, 240);
  ASSERT_EQ(rig.factory->create_calls(), 2u);
  EXPECT_EQ(rig.factory->created()[0]->detach_calls(), 1u);
  EXPECT_EQ(rig.factory->created()[1]->desc().buffer_width, 400u);
  EXPECT_FALSE(rig.manager->is_ar_hidden("p"));
  EXPECT_EQ(rig.factory->live_name_collisions(), 0u);
}

TEST(PanelSwap, ASenderThatCannotDetachKeepsTheOldPathAndItsKeptPanel)
{
  Rig rig(PanelCloseMode::Park, PanelResizeMode::Swap, /*detach=*/false);
  SourceWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  rig.resize_and_settle(source, 400, 240);
  ASSERT_EQ(rig.factory->create_calls(), 2u);
  EXPECT_EQ(rig.manager->replaced_panel_count(), 1u) << "kept hidden, as before";
  EXPECT_EQ(rig.factory->created()[0]->detach_calls(), 0u);
  EXPECT_EQ(rig.factory->created()[0]->last_state(), QAR_GUI_PANEL_STATE_HIDDEN);
}

TEST(PanelSwap, InDestroyModeTheOldPanelIsClosedAsThatModeAlwaysDid)
{
  Rig rig(PanelCloseMode::Destroy);
  SourceWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  rig.resize_and_settle(source, 400, 240);
  ASSERT_EQ(rig.factory->create_calls(), 2u);
  EXPECT_EQ(rig.factory->created()[0]->close_calls(), 1u);
  EXPECT_EQ(rig.factory->created()[0]->detach_calls(), 0u);
  EXPECT_EQ(
    rig.factory->events(), (std::vector<std::string>{"create p", "close p", "create p"}));
}

// --- a size change: the new-panel fallback ------------------------------------------

TEST(PanelNewPanel, TheCommonNameCarriesTheBufferSizeAndAReturnToAKeptSizeShowsThatPanel)
{
  Rig rig(PanelCloseMode::Park, PanelResizeMode::NewPanel);
  SourceWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  const auto first = rig.factory->created()[0];
  const std::string base = qar_ar_core::make_panel_common_name("p", Rig::scope());
  EXPECT_EQ(
    first->common_name(),
    base + "@" + std::to_string(first->desc().buffer_width) + "x" +
    std::to_string(first->desc().buffer_height));

  rig.resize_and_settle(source, 400, 240);
  ASSERT_EQ(rig.factory->create_calls(), 2u);
  const auto second = rig.factory->created()[1];
  EXPECT_NE(second->common_name(), first->common_name()) << "a second QarOS panel";
  EXPECT_EQ(first->last_state(), QAR_GUI_PANEL_STATE_HIDDEN);
  EXPECT_EQ(first->detach_calls(), 0u) << "kept, not detached";
  EXPECT_EQ(rig.manager->replaced_panel_count(), 1u);

  // Back to the first size: the kept panel of that name is shown again, no third sender is made
  // for it (it would share the panel that the kept sender still serves).
  rig.resize_and_settle(source, 200, 120);
  EXPECT_EQ(rig.factory->create_calls(), 2u);
  EXPECT_EQ(rig.factory->live_name_collisions(), 0u);
  EXPECT_EQ(first->last_state(), QAR_GUI_PANEL_STATE_VISIBLE);
  EXPECT_EQ(second->last_state(), QAR_GUI_PANEL_STATE_HIDDEN);
  EXPECT_EQ(rig.manager->replaced_panel_count(), 1u) << "the 400 x 240 panel is the kept one now";
  EXPECT_GT(first->submit_calls(), 1u) << "the revived sender serves the stream again";
}

TEST(PanelNewPanel, ALongBaseNameIsCutSoTheNameWithItsSizeStaysWithinTheLimit)
{
  Rig rig(PanelCloseMode::Park, PanelResizeMode::NewPanel);
  SourceWidget source(200, 120);
  const std::string key(200, 'k');
  ASSERT_TRUE(rig.manager->add_panel(key, source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  const std::string name = rig.factory->latest()->common_name();
  EXPECT_LE(name.size(), qar_ar_core::kCommonNameMax);
  EXPECT_NE(name.find('@'), std::string::npos);
}

// --- the rule: no second live sender on a parked panel's name --------------------------

TEST(PanelNameRule, AddingAParkedKeyAgainRevivesTheParkedSenderAndMakesNoSecondOne)
{
  Rig rig;
  SourceWidget first_window(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", first_window.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  for (int round = 0; round < 3; ++round) {
    ASSERT_TRUE(rig.manager->close_panel("p", "window closed").ok());
    rig.tick();
    ASSERT_TRUE(rig.manager->is_parked("p")) << round;
    SourceWidget window(200, 120);
    ASSERT_TRUE(rig.manager->add_panel("p", window.get(), options_no_pacing()).ok()) << round;
    rig.tick();
    EXPECT_FALSE(rig.manager->is_parked("p")) << round;
    ASSERT_TRUE(rig.manager->close_panel("p", "again").ok());
    rig.tick();
  }
  EXPECT_EQ(rig.factory->create_calls(), 1u) << "one sender for the key, whatever the rounds";
  EXPECT_EQ(rig.factory->live_name_collisions(), 0u);
  EXPECT_EQ(rig.factory->created()[0]->close_calls(), 0u);
  EXPECT_EQ(rig.factory->created()[0]->detach_calls(), 0u);
}

TEST(PanelNameRule, OpeningADismissedDialogAgainRevivesItsParkedSenderOnItsOwnName)
{
  DialogGuard guard;
  Rig rig(PanelCloseMode::Park, PanelResizeMode::Swap, /*detach=*/true, /*dialogs=*/true);
  ButtonPanel panel;
  ASSERT_TRUE(rig.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  rig.tick();
  const std::uint32_t after_owner = rig.factory->create_calls();
  for (int round = 0; round < 3; ++round) {
    auto * dialog = new QDialog(panel.widget());
    dialog->setWindowTitle("Add");
    dialog->resize(QSize(240, 100));
    dialog->show();
    dialog->ensurePolished();
    QCoreApplication::processEvents();
    rig.tick();
    dialog->hide();
    QCoreApplication::processEvents();
    rig.tick();
    EXPECT_TRUE(rig.manager->is_parked("dialog:Add")) << round;
    delete dialog;
    QCoreApplication::processEvents();
  }
  EXPECT_EQ(rig.factory->create_calls(), after_owner + 1u) << "one sender for the dialog key";
  EXPECT_EQ(rig.factory->live_name_collisions(), 0u);
  for (const auto & adapter : rig.factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 0u);
    EXPECT_EQ(adapter->detach_calls(), 0u);
  }
}

// --- the end of the session ---------------------------------------------------------

TEST(PanelSessionEnd, EveryPanelIsHiddenThenDetachedAndNeverClosed)
{
  Rig rig;
  SourceWidget a(200, 120);
  SourceWidget b(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("a", a.get(), options_no_pacing()).ok());
  ASSERT_TRUE(rig.manager->add_panel("b", b.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 2u);
  // "b" is parked: hidden already, it is detached without a second hide.
  ASSERT_TRUE(rig.manager->close_panel("b", "closed").ok());
  rig.tick();

  rig.manager->clear();
  const auto first = rig.factory->created()[0];
  const auto second = rig.factory->created()[1];
  ASSERT_EQ(first->desc().key, "a");
  EXPECT_EQ(first->states(), (std::vector<QarGuiPanelState>{QAR_GUI_PANEL_STATE_HIDDEN}));
  EXPECT_EQ(first->detach_calls(), 1u);
  EXPECT_EQ(first->close_calls(), 0u);
  EXPECT_EQ(second->states(), (std::vector<QarGuiPanelState>{QAR_GUI_PANEL_STATE_HIDDEN}))
    << "hidden once, when it was parked";
  EXPECT_EQ(second->detach_calls(), 1u);
  EXPECT_EQ(second->close_calls(), 0u);
  EXPECT_EQ(rig.manager->panel_keys().size(), 0u);
  EXPECT_NE(rig.diagnostics.find("ended the session for 'a'"), nullptr);
  EXPECT_EQ(rig.diagnostics.count("could not hide"), 0u);
}

TEST(PanelSessionEnd, TheKeptPanelsOfASizeChangeAreDetachedToo)
{
  Rig rig(PanelCloseMode::Park, PanelResizeMode::NewPanel);
  SourceWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  rig.resize_and_settle(source, 400, 240);
  ASSERT_EQ(rig.manager->replaced_panel_count(), 1u);

  rig.manager->clear();
  for (const auto & adapter : rig.factory->created()) {
    EXPECT_EQ(adapter->detach_calls(), 1u);
    EXPECT_EQ(adapter->close_calls(), 0u);
    EXPECT_EQ(adapter->last_state(), QAR_GUI_PANEL_STATE_HIDDEN);
  }
}

TEST(PanelSessionEnd, AHideThatQarOsRefusesIsReportedAndTheSenderIsDetachedAllTheSame)
{
  Rig rig;
  SourceWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  rig.factory->latest()->fail_next_set_state();
  rig.manager->clear();
  EXPECT_EQ(rig.factory->created()[0]->detach_calls(), 1u);
  EXPECT_EQ(rig.factory->created()[0]->close_calls(), 0u);
  const auto * warning = rig.diagnostics.find("could not hide 1 QarOS panel(s) of 'p'");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->first, DiagnosticSeverity::Warning);
}

TEST(PanelSessionEnd, InDestroyModeThePanelsAreClosedAsBefore)
{
  Rig rig(PanelCloseMode::Destroy);
  SourceWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  rig.manager->clear();
  EXPECT_EQ(rig.factory->created()[0]->close_calls(), 1u);
  EXPECT_EQ(rig.factory->created()[0]->detach_calls(), 0u);
  EXPECT_EQ(rig.factory->created()[0]->state_calls(), 0u);
}

TEST(PanelSessionEnd, ASenderThatCannotDetachIsClosedOnceAtTheEnd)
{
  Rig rig(PanelCloseMode::Park, PanelResizeMode::Swap, /*detach=*/false);
  SourceWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  rig.manager->clear();
  EXPECT_EQ(rig.factory->created()[0]->close_calls(), 1u);
  EXPECT_EQ(rig.factory->created()[0]->detach_calls(), 0u);
}

// --- a paused stream is not a fault -------------------------------------------------

TEST(PanelPaused, APausedStreamCountsAsNotReadyButNeverAsUnserved)
{
  Rig rig;
  rig.factory->set_senders_start_not_ready(true);  // nothing is ever submitted
  PanelStreamOptions options = options_no_pacing();
  options.keepalive_seconds = 1.0;  // an unserved panel attempts a submit on every tick
  SourceWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options).ok());
  rig.tick();
  const auto adapter = rig.factory->latest();
  adapter->set_not_ready_cause(qar_ar_core::PanelNotReadyCause::Paused);
  for (int i = 0; i < 20; ++i) {
    rig.tick(1.0);  // 20 s on the manual clock: far past the 5 s unserved threshold
  }
  const auto snapshot = rig.manager->diagnostics();
  qar_ar_core::PanelStreamDiagnostics d;
  for (const auto & entry : snapshot.panels) {
    if (entry.key == "p") {
      d = entry;
    }
  }
  EXPECT_GE(d.not_ready_streak, 20u) << "the ticks are in the total";
  // Only the first tick, which ran before the cause was set, is in the split of faults.
  EXPECT_EQ(qar_ar_core::not_ready_split_text(d), "1/0/0") << "the split names faults only";
  EXPECT_EQ(rig.diagnostics.count("is not being served"), 0u);

  // The same silence for a fault cause is reported.
  adapter->set_not_ready_cause(qar_ar_core::PanelNotReadyCause::NoActiveTransfers);
  for (int i = 0; i < 20; ++i) {
    rig.tick(1.0);
  }
  EXPECT_EQ(rig.diagnostics.count("is not being served"), 1u);
}

}  // namespace
