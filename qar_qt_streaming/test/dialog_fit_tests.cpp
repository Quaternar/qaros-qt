// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Fitting a dialog to its content before its panel is made: with a dialog size adjuster
// installed, a dialog the sweep finds is called with, once per 100 ms, until it is done, and its
// stream is added once, at the final size. The census adjuster grows a dialog by the overflow of
// its scroll areas and settles only on two equal measures. Real dialogs under the offscreen
// platform; the streams run against the recording sender.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/scroll_census.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QListWidget>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using qar_ar_core::DiagnosticSeverity;
using qar_qt_streaming::PanelCloseMode;
using qar_qt_streaming::PanelOverlayAttribution;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_qt_streaming::ScrollCensus;
using qar_qt_streaming::take_scroll_census;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderAdapter;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

void
settle()
{
  for (int i = 0; i < 5; ++i) {
    QCoreApplication::processEvents();
  }
}

/// Leaves the process-wide attribution state clean around a dialog test.
struct DialogGuard
{
  DialogGuard() { PanelOverlayAttribution::instance().reset(); }
  ~DialogGuard() { PanelOverlayAttribution::instance().reset(); }
};

class Owner
{
public:
  Owner()
  {
    widget_ = new QWidget();
    auto * layout = new QVBoxLayout(widget_);
    layout->addWidget(new QPushButton("Add", widget_));
    widget_->resize(200, 120);
    widget_->show();
    widget_->ensurePolished();
    widget_->layout()->activate();
    settle();
  }

  ~Owner() { delete widget_; }

  QWidget * get() const { return widget_; }

private:
  QWidget * widget_ = nullptr;
};

PanelStreamOptions
options_no_pacing()
{
  PanelStreamOptions options;
  options.max_fps = 0;
  options.keepalive_seconds = 0.0;
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  options.placement.pose.position = QarVector3{0.0f, 0.0f, 0.0f};
  return options;
}

class RecordingDiagnostics : public qar_ar_core::PanelDiagnosticsSink
{
public:
  struct Entry
  {
    DiagnosticSeverity severity;
    std::string message;
    std::string panel_key;
  };

  void on_message(
    DiagnosticSeverity severity, const std::string &, const std::string & message,
    const std::string & panel_key) override
  {
    messages.push_back(Entry{severity, message, panel_key});
  }

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

/// A manager on the recording factory and a manual clock, with dialog panels on.
struct Harness
{
  explicit Harness(
    PanelCloseMode mode = PanelCloseMode::Park, std::uint32_t cap = 16,
    double dialog_height_factor = 1.0)
  {
    factory = std::make_shared<MockQarPanelSenderFactory>();
    clock = std::make_shared<ManualClock>();
    QtPanelStreamingConfig config;
    config.panel_sender_factory = factory;
    config.clock = clock;
    config.dialog_panels = true;
    config.dialog_height_factor = dialog_height_factor;
    config.diagnostics = &diagnostics;
    config.pump_input_in_tick = false;
    config.close_mode = mode;
    config.max_tombstoned_panels = cap;
    manager = std::make_unique<QtPanelStreamManager>(config);
  }

  /// The event loop runs between ticks, as it does in an application: queued layout calls and
  /// scroll bar updates happen there.
  std::uint32_t tick(double dt = 0.1)
  {
    settle();
    clock->advance(dt);
    return manager->tick();
  }

  void tick_until(const std::string & key, int limit = 60, double dt = 0.1)
  {
    for (int i = 0; i < limit && !manager->has_panel(key); ++i) {
      tick(dt);
    }
  }

  qar_ar_core::PanelStreamDiagnostics panel(const std::string & key) const
  {
    for (const auto & entry : manager->diagnostics().panels) {
      if (entry.key == key) {
        return entry;
      }
    }
    return qar_ar_core::PanelStreamDiagnostics{};
  }

  std::shared_ptr<MockQarPanelSenderAdapter> adapter(const std::string & key) const
  {
    for (const auto & candidate : factory->created()) {
      if (candidate->desc().key == key) {
        return candidate;
      }
    }
    return nullptr;
  }

  std::shared_ptr<MockQarPanelSenderFactory> factory;
  std::shared_ptr<ManualClock> clock;
  RecordingDiagnostics diagnostics;
  std::unique_ptr<QtPanelStreamManager> manager;
};

QTreeWidget *
find_tree(QDialog * dialog)
{
  return dialog->findChild<QTreeWidget *>();
}

/// A dialog of a tree of @p rows rows and an OK button, shown at @p size.
QDialog *
show_tree_dialog(QWidget * parent, const QSize & size, const QString & title, int rows)
{
  auto * dialog = new QDialog(parent);
  dialog->setWindowTitle(title);
  auto * layout = new QVBoxLayout(dialog);
  auto * tree = new QTreeWidget(dialog);
  tree->setHeaderHidden(true);
  tree->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
  for (int i = 0; i < rows; ++i) {
    tree->addTopLevelItem(new QTreeWidgetItem(QStringList(QString("entry %1").arg(i))));
  }
  layout->addWidget(tree);
  layout->addWidget(new QPushButton("OK", dialog));
  dialog->resize(size);
  dialog->show();
  dialog->ensurePolished();
  settle();
  tree->doItemsLayout();
  settle();
  return dialog;
}

void
close_dialog(QDialog * dialog)
{
  dialog->hide();
  settle();
  delete dialog;
  settle();
}

/// An adjuster that records its calls and answers from a script of steps; the last step repeats.
struct ScriptedAdjuster
{
  struct Call
  {
    int round;
    double at_seconds;
    QRect room;
    QSize size_before;
  };

  QtPanelStreamManager::DialogSizeAdjuster make()
  {
    return [this](QWidget & dialog, const QRect & room, int round) {
             calls.push_back(Call{round, clock->now_seconds(), room, dialog.size()});
             const std::size_t index = std::min(calls.size() - 1, steps.size() - 1);
             QtPanelStreamManager::DialogFitStep step = steps[index];
             if (step.size.isEmpty()) {
               step.size = dialog.size();
             } else {
               dialog.resize(step.size);
             }
             return step;
           };
  }

  std::shared_ptr<ManualClock> clock;
  std::vector<QtPanelStreamManager::DialogFitStep> steps;
  std::vector<Call> calls;
};

QtPanelStreamManager::DialogFitStep
step(bool settled, const std::string & note, QSize size = QSize())
{
  QtPanelStreamManager::DialogFitStep out;
  out.settled = settled;
  out.note = note;
  out.size = size;
  return out;
}

}  // namespace

// --- the census adjuster ------------------------------------------------------------------

TEST(DialogFit, TheStreamIsAddedOnlyWhenTheFitIsOverAndAtTheFinalSize)
{
  DialogGuard guard;
  Harness h;
  int calls = 0;
  const auto census_adjuster = QtPanelStreamManager::make_census_dialog_adjuster();
  h.manager->set_dialog_size_adjuster(
    [&](QWidget & dialog, const QRect & room, int round) {
      ++calls;
      return census_adjuster(dialog, room, round);
    });
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 25);
  QTreeWidget * tree = find_tree(dialog);
  ASSERT_GT(take_scroll_census(*dialog).stacked_overflow_px, 0) << "the dialog starts too small";
  const int initial_height = dialog->height();

  h.tick();  // the sweep finds the dialog; the first look makes it taller
  EXPECT_EQ(calls, 1);
  EXPECT_GT(dialog->height(), initial_height);
  EXPECT_FALSE(h.manager->has_panel("dialog:Add")) << "not streamed at once";
  EXPECT_EQ(h.factory->create_calls(), 1u) << "only the owner's panel so far";
  EXPECT_EQ(dialog->width(), 240) << "the width is left alone";

  h.tick_until("dialog:Add");
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));

  // Done at a size where nothing scrolls, confirmed by two equal looks.
  const ScrollCensus after = take_scroll_census(*dialog);
  EXPECT_EQ(after.stacked_overflow_px, 0);
  EXPECT_FALSE(after.any_vertical_bar);
  EXPECT_EQ(tree->verticalScrollBar()->maximum(), tree->verticalScrollBar()->minimum());
  EXPECT_GE(calls, 3) << "grow, then two looks at the new size";

  // The QarOS panel was made once, at the final size, and nothing is rebuilt afterwards.
  const auto adapter = h.adapter("dialog:Add");
  ASSERT_NE(adapter, nullptr);
  EXPECT_EQ(adapter->desc().content_width, 240u);
  EXPECT_EQ(adapter->desc().content_height, static_cast<std::uint32_t>(dialog->height()));
  for (int i = 0; i < 8; ++i) {
    h.tick(0.2);
  }
  EXPECT_EQ(h.panel("dialog:Add").adapter_rebuilds, 1u);
  EXPECT_EQ(h.factory->create_calls(), 2u);
  EXPECT_EQ(adapter->close_calls(), 0u);
  EXPECT_EQ(h.diagnostics.count("changed size"), 0u);
  EXPECT_EQ(h.manager->dialog_panel_keys(), std::vector<std::string>{"dialog:Add"});

  const auto * line = h.diagnostics.find("'dialog:Add' fitted to ");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->severity, DiagnosticSeverity::Info);
  EXPECT_EQ(line->panel_key, "dialog:Add");
  EXPECT_EQ(
    line->message,
    "'dialog:Add' fitted to 240x" + std::to_string(dialog->height()) + " px in " +
    std::to_string(calls) + " round(s): no scroll bar");
  EXPECT_EQ(h.diagnostics.count("fitted to"), 1u) << "said once";

  close_dialog(dialog);
}

TEST(DialogFit, ADialogThatAlreadyFitsIsSettledByTheSecondLookAndKeepsItsSize)
{
  DialogGuard guard;
  Harness h;
  std::vector<int> rounds;
  const auto census_adjuster = QtPanelStreamManager::make_census_dialog_adjuster();
  h.manager->set_dialog_size_adjuster(
    [&](QWidget & dialog, const QRect & room, int round) {
      rounds.push_back(round);
      return census_adjuster(dialog, room, round);
    });
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 300), "Add", 3);
  ASSERT_EQ(take_scroll_census(*dialog).stacked_overflow_px, 0);

  h.tick();
  EXPECT_FALSE(h.manager->has_panel("dialog:Add")) << "one look is not enough";
  h.tick(0.1);
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(rounds, (std::vector<int>{0, 1}));
  EXPECT_EQ(dialog->height(), 300) << "nothing to make room for";
  const auto * line = h.diagnostics.find("fitted to");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->message, "'dialog:Add' fitted to 240x300 px in 2 round(s): no scroll bar");

  close_dialog(dialog);
}

TEST(DialogFit, TheSecondEqualLookIsWhatSettlesAFitNotTheFirstZero)
{
  // A tree that gets its rows between the first look and the second, the way a model fills in
  // after the dialog is shown: the first look sees nothing to fix, the next one does.
  DialogGuard guard;
  Harness h;
  h.manager->set_dialog_size_adjuster(QtPanelStreamManager::make_census_dialog_adjuster());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 160), "Add", 2);
  QTreeWidget * tree = find_tree(dialog);
  ASSERT_EQ(take_scroll_census(*dialog).stacked_overflow_px, 0);

  h.tick();  // look 1: nothing to remove
  for (int i = 0; i < 12; ++i) {
    tree->addTopLevelItem(new QTreeWidgetItem(QStringList(QString("late %1").arg(i))));
  }
  tree->doItemsLayout();
  const int before = dialog->height();
  h.tick();  // look 2: the rows are there now
  EXPECT_FALSE(h.manager->has_panel("dialog:Add"));
  EXPECT_GT(dialog->height(), before);
  h.tick_until("dialog:Add");
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(take_scroll_census(*dialog).stacked_overflow_px, 0);
  EXPECT_EQ(h.adapter("dialog:Add")->desc().content_height,
    static_cast<std::uint32_t>(dialog->height()));
  EXPECT_EQ(h.panel("dialog:Add").adapter_rebuilds, 1u);

  close_dialog(dialog);
}

TEST(DialogFit, ADialogThatCannotFitTheScreenIsClampedAndSaysSo)
{
  DialogGuard guard;
  Harness h;
  h.manager->set_dialog_size_adjuster(QtPanelStreamManager::make_census_dialog_adjuster());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 600);
  const QRect room = QGuiApplication::primaryScreen()->availableGeometry();

  h.tick_until("dialog:Add");
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(dialog->height(), room.height() - 40) << "the available height less 40 px";
  EXPECT_EQ(dialog->width(), 240);
  const auto * line = h.diagnostics.find("fitted to");
  ASSERT_NE(line, nullptr);
  EXPECT_NE(
    line->message.find("clamped to the screen (" + std::to_string(room.height()) + " px)"),
    std::string::npos)
    << line->message;
  EXPECT_EQ(h.panel("dialog:Add").adapter_rebuilds, 1u);
  EXPECT_EQ(
    h.adapter("dialog:Add")->desc().content_height, static_cast<std::uint32_t>(dialog->height()));

  close_dialog(dialog);
}

TEST(DialogFit, GrowingThatDoesNotShrinkTheOverflowEndsTheFitWithTheBarAndItsReason)
{
  // A tree in a dialog of a fixed height: resizing it does nothing, and the fit must not go on
  // making rounds of it.
  DialogGuard guard;
  Harness h;
  h.manager->set_dialog_size_adjuster(QtPanelStreamManager::make_census_dialog_adjuster());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 40);
  dialog->setFixedHeight(140);
  settle();
  find_tree(dialog)->doItemsLayout();
  settle();

  h.tick_until("dialog:Add");
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(dialog->height(), 140);
  const int overflow = take_scroll_census(*dialog).stacked_overflow_px;
  ASSERT_GT(overflow, 0);
  const auto * line = h.diagnostics.find("fitted to");
  ASSERT_NE(line, nullptr);
  EXPECT_NE(
    line->message.find(
      ": a scroll bar of " + std::to_string(overflow) +
      " px is left (growing the dialog did not remove it)"),
    std::string::npos)
    << line->message;
  EXPECT_NE(line->message.find(" in 2 round(s)"), std::string::npos) << line->message;

  close_dialog(dialog);
}

TEST(DialogFit, AnAreaThatScrollsByDesignNeverMakesTheDialogGrowButIsNamed)
{
  DialogGuard guard;
  Harness h;
  h.manager->set_dialog_size_adjuster(QtPanelStreamManager::make_census_dialog_adjuster());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  auto * dialog = new QDialog(owner.get());
  dialog->setWindowTitle("Add");
  auto * layout = new QVBoxLayout(dialog);
  auto * description = new QTextBrowser(dialog);
  description->setObjectName("description");
  description->setPlainText(QString("some text\n").repeated(150));
  layout->addWidget(description);
  dialog->resize(240, 160);
  dialog->show();
  settle();
  ASSERT_TRUE(take_scroll_census(*dialog).any_vertical_bar_by_design);

  h.tick_until("dialog:Add");
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(dialog->height(), 160) << "a description that is longer than its box is by design";
  const auto * line = h.diagnostics.find("fitted to");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(
    line->message.rfind("'dialog:Add' fitted to 240x160 px in 2 round(s): no scroll bar", 0), 0u)
    << line->message;
  EXPECT_NE(line->message.find("QTextBrowser 'description' scrolls by design"), std::string::npos)
    << line->message;

  close_dialog(dialog);
}

TEST(DialogFit, TheTwoLooksThatSettleAFitAreAtTheSameSize)
{
  DialogGuard guard;
  Owner owner;
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 300), "Add", 3);
  const QRect room(0, 0, 800, 600);
  const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster();
  ASSERT_EQ(take_scroll_census(*dialog).stacked_overflow_px, 0);

  EXPECT_FALSE(adjuster(*dialog, room, 0).settled);
  dialog->resize(240, 330);  // nothing overflows at either size, but it is not the same size
  settle();
  ASSERT_EQ(take_scroll_census(*dialog).stacked_overflow_px, 0);
  EXPECT_FALSE(adjuster(*dialog, room, 1).settled) << "the earlier look was at another size";
  EXPECT_TRUE(adjuster(*dialog, room, 2).settled) << "this look repeats the one before it";
  close_dialog(dialog);
}

TEST(DialogFit, AZeroAfterAnOverflowIsNotYetTheSecondEqualLook)
{
  DialogGuard guard;
  Owner owner;
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 40);
  QTreeWidget * tree = find_tree(dialog);
  dialog->setFixedHeight(140);  // growing it does nothing, so the size stays what it was
  settle();
  tree->doItemsLayout();
  settle();
  const QRect room(0, 0, 800, 600);
  const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster();
  ASSERT_GT(take_scroll_census(*dialog).stacked_overflow_px, 0);

  EXPECT_FALSE(adjuster(*dialog, room, 0).settled);
  ASSERT_EQ(dialog->height(), 140);
  tree->clear();  // the rows are gone: nothing overflows any more, at the very same size
  tree->doItemsLayout();
  settle();
  ASSERT_EQ(take_scroll_census(*dialog).stacked_overflow_px, 0);
  EXPECT_FALSE(adjuster(*dialog, room, 1).settled) << "a zero that follows an overflow is one look";
  EXPECT_TRUE(adjuster(*dialog, room, 2).settled);
  close_dialog(dialog);
}

TEST(DialogFit, TheAdjusterGivesUpAtItsRoundBoundAndSaysSo)
{
  DialogGuard guard;
  Owner owner;
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 24);
  QTreeWidget * tree = find_tree(dialog);
  const QRect room(0, 0, 800, 600);

  // A bound of one round: the first look is the last, and it only reports.
  {
    const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster(8, 1);
    const int overflow = take_scroll_census(*dialog).stacked_overflow_px;
    ASSERT_GT(overflow, 0);
    const auto result = adjuster(*dialog, room, 0);
    EXPECT_TRUE(result.settled);
    EXPECT_EQ(dialog->height(), 140) << "a fit that has no round left changes nothing";
    EXPECT_EQ(
      result.note,
      "a scroll bar of " + std::to_string(overflow) + " px is left (the fit gave up after 1 "
      "round(s))");
  }

  // A bound of two: the first look grows the dialog, rows come in, the second look gives up.
  {
    const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster(8, 2);
    const int first_overflow = take_scroll_census(*dialog).stacked_overflow_px;
    const auto first = adjuster(*dialog, room, 0);
    EXPECT_FALSE(first.settled);
    EXPECT_EQ(dialog->height(), 140 + first_overflow + 8);
    EXPECT_EQ(first.size, dialog->size());
    settle();
    tree->doItemsLayout();
    settle();
    ASSERT_EQ(take_scroll_census(*dialog).stacked_overflow_px, 0);
    for (int i = 0; i < 6; ++i) {
      tree->addTopLevelItem(new QTreeWidgetItem(QStringList(QString("more %1").arg(i))));
    }
    tree->doItemsLayout();
    settle();
    const int second_overflow = take_scroll_census(*dialog).stacked_overflow_px;
    ASSERT_GT(second_overflow, 0);
    ASSERT_LT(second_overflow, first_overflow);
    const auto second = adjuster(*dialog, room, 1);
    EXPECT_TRUE(second.settled);
    EXPECT_EQ(
      second.note,
      "a scroll bar of " + std::to_string(second_overflow) + " px is left (the fit gave up "
      "after 2 round(s))");
  }
  close_dialog(dialog);
}

// --- what a zero is worth ------------------------------------------------------------------

TEST(DialogFit, AZeroAtTheLastRoundThatNoLookConfirmedIsNotNoScrollBar)
{
  // The dialog was made taller by the round before: the last look finds nothing, but it is the
  // first look at this size, and nothing says that a queued layout call is not about to bring a
  // bar.
  DialogGuard guard;
  Owner owner;
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 24);
  const QRect room(0, 0, 800, 600);
  const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster(8, 2);
  ASSERT_GT(take_scroll_census(*dialog).stacked_overflow_px, 0);

  EXPECT_FALSE(adjuster(*dialog, room, 0).settled);
  ASSERT_GT(dialog->height(), 140) << "the first look made it taller";
  settle();
  find_tree(dialog)->doItemsLayout();
  settle();
  ASSERT_EQ(take_scroll_census(*dialog).stacked_overflow_px, 0);

  const auto last = adjuster(*dialog, room, 1);
  EXPECT_TRUE(last.settled) << "the round bound is reached";
  EXPECT_EQ(last.note, "not confirmed (the round limit)");
  EXPECT_NE(last.note.rfind("no scroll bar", 0), 0u)
    << "the scorecard reads a note that starts so as a confirmed fit";
  close_dialog(dialog);
}

TEST(DialogFit, AZeroAtTheLastRoundThatTheLookBeforeConfirmedIsNoScrollBar)
{
  DialogGuard guard;
  Owner owner;
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 300), "Add", 3);
  const QRect room(0, 0, 800, 600);
  const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster(8, 2);
  ASSERT_EQ(take_scroll_census(*dialog).stacked_overflow_px, 0);

  EXPECT_FALSE(adjuster(*dialog, room, 0).settled);
  const auto last = adjuster(*dialog, room, 1);
  EXPECT_TRUE(last.settled);
  EXPECT_EQ(last.note, "no scroll bar") << "two equal looks, the second one at the last round";
  close_dialog(dialog);
}

TEST(DialogFit, TheLineOfAFitThatNoLookConfirmedSaysNotConfirmed)
{
  DialogGuard guard;
  Harness h;
  h.manager->set_dialog_size_adjuster(QtPanelStreamManager::make_census_dialog_adjuster(8, 2));
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 25);
  h.tick_until("dialog:Add");
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));

  const auto * line = h.diagnostics.find("fitted to");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->severity, DiagnosticSeverity::Info);
  EXPECT_EQ(
    line->message,
    "'dialog:Add' fitted to 240x" + std::to_string(dialog->height()) +
    " px in 2 round(s): not confirmed (the round limit)");
  close_dialog(dialog);
}

namespace
{

/// A list whose first row reports no height: the rows its bar counts cannot become pixels.
class UnmeasurableList : public QListWidget
{
public:
  int sizeHintForRow(int) const override {return 0;}
};

/// A dialog that holds @p widget below nothing else, shown at @p size.
QDialog *
show_dialog_with(QWidget * parent, QWidget * widget, const QSize & size)
{
  auto * dialog = new QDialog(parent);
  dialog->setWindowTitle("Add");
  auto * layout = new QVBoxLayout(dialog);
  layout->addWidget(widget);
  dialog->resize(size);
  dialog->show();
  dialog->ensurePolished();
  settle();
  return dialog;
}

}  // namespace

TEST(DialogFit, AnAreaThatCannotBeMeasuredIsNeverNoScrollBar)
{
  DialogGuard guard;
  Owner owner;
  auto * list = new UnmeasurableList;
  for (int i = 0; i < 60; ++i) {
    list->addItem(QString("item %1").arg(i));
  }
  list->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
  QDialog * dialog = show_dialog_with(owner.get(), list, QSize(240, 200));
  list->doItemsLayout();
  settle();
  ASSERT_GT(list->verticalScrollBar()->maximum(), 0);
  const ScrollCensus census = take_scroll_census(*dialog);
  ASSERT_GT(census.unmeasured, 0);
  ASSERT_EQ(census.stacked_overflow_px, 0) << "its overflow cannot be told";

  const QRect room(0, 0, 800, 600);
  const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster(8, 4);
  for (int round = 0; round < 3; ++round) {
    EXPECT_FALSE(adjuster(*dialog, room, round).settled)
      << "two equal looks are no confirmation while an area is unmeasured (round " << round << ")";
  }
  const auto last = adjuster(*dialog, room, 3);
  EXPECT_TRUE(last.settled);
  EXPECT_EQ(last.note, "not confirmed (an area could not be measured)");
  EXPECT_EQ(dialog->height(), 200) << "there is nothing to make it taller by";
  close_dialog(dialog);
}

// --- a horizontal bar ----------------------------------------------------------------------

namespace
{

/// A scroll area with a content of @p width by @p height pixels.
QScrollArea *
make_scroll_area(int width, int height)
{
  auto * area = new QScrollArea;
  auto * content = new QWidget;
  content->setFixedSize(width, height);
  area->setWidget(content);
  // Room for the content and a horizontal bar: the area has no vertical overflow of its own.
  area->setMinimumHeight(height + 70);
  return area;
}

}  // namespace

TEST(DialogFit, AHorizontalBarAloneDoesNotMakeTheDialogTallerAndEndsTheFitWithItsReason)
{
  DialogGuard guard;
  Harness h;
  h.manager->set_dialog_size_adjuster(QtPanelStreamManager::make_census_dialog_adjuster());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();
  QDialog * dialog = show_dialog_with(owner.get(), make_scroll_area(900, 40), QSize(240, 300));
  const ScrollCensus census = take_scroll_census(*dialog);
  ASSERT_GT(census.horizontal_only_px, 0) << "wider than its viewport, and low";
  ASSERT_EQ(census.stacked_overflow_px, 0);

  h.tick_until("dialog:Add");
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(dialog->height(), 300) << "a taller dialog keeps the bar: it is left as it is";
  const auto * line = h.diagnostics.find("fitted to");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(
    line->message,
    "'dialog:Add' fitted to 240x300 px in 2 round(s): a scroll bar of " +
    std::to_string(census.horizontal_only_px) +
    " px is left (a horizontal bar: a taller dialog does not remove it)");
  EXPECT_EQ(h.panel("dialog:Add").adapter_rebuilds, 1u);
  close_dialog(dialog);
}

TEST(DialogFit, AHorizontalBarAloneIsSaidAfterTwoAgreeingLooks)
{
  DialogGuard guard;
  Owner owner;
  QDialog * dialog = show_dialog_with(owner.get(), make_scroll_area(900, 40), QSize(240, 300));
  const QRect room(0, 0, 800, 600);
  const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster();
  const int bar = take_scroll_census(*dialog).horizontal_only_px;
  ASSERT_GT(bar, 0);

  const auto first = adjuster(*dialog, room, 0);
  EXPECT_FALSE(first.settled) << "one look is no confirmation";
  EXPECT_EQ(dialog->height(), 300);
  const auto second = adjuster(*dialog, room, 1);
  EXPECT_TRUE(second.settled);
  EXPECT_EQ(
    second.note,
    "a scroll bar of " + std::to_string(bar) +
    " px is left (a horizontal bar: a taller dialog does not remove it)");
  close_dialog(dialog);
}

TEST(DialogFit, OnlyTheAreasWithAVerticalOverflowMakeTheDialogGrowAndTheirBarsCountWithThem)
{
  // A wide, low area next to a tall tree: the dialog grows for the tree, and for nothing the wide
  // area adds; afterwards the bar of the wide area is what is left.
  DialogGuard guard;
  Owner owner;
  auto * both = new QWidget;
  auto * both_layout = new QVBoxLayout(both);
  QTreeWidget * tree = new QTreeWidget;
  tree->setHeaderHidden(true);
  tree->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
  for (int i = 0; i < 24; ++i) {
    tree->addTopLevelItem(new QTreeWidgetItem(QStringList(QString("entry %1").arg(i))));
  }
  both_layout->addWidget(tree);
  both_layout->addWidget(make_scroll_area(900, 20));
  QDialog * dialog = show_dialog_with(owner.get(), both, QSize(240, 300));
  tree->doItemsLayout();
  settle();
  const ScrollCensus census = take_scroll_census(*dialog);
  ASSERT_GT(census.stacked_overflow_px, 0);
  ASSERT_GT(census.horizontal_only_px, 0);

  const QRect room(0, 0, 800, 800);
  const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster(8, 6);
  const int height_before = dialog->height();
  EXPECT_FALSE(adjuster(*dialog, room, 0).settled);
  EXPECT_EQ(dialog->height(), height_before + census.stacked_overflow_px + 8)
    << "the overflow of the tree and the slack, and no bar of the wide area";
  close_dialog(dialog);
}

// --- a dialog that is made taller keeps to the screen ---------------------------------------

TEST(DialogFit, ADialogOpenedLowIsMovedUpWhenItIsMadeTaller)
{
  DialogGuard guard;
  Owner owner;
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 25);
  const QRect room(0, 0, 800, 600);
  dialog->move(100, 450);
  settle();
  const int top_before = dialog->frameGeometry().top();
  ASSERT_EQ(top_before, 450);
  const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster();

  EXPECT_FALSE(adjuster(*dialog, room, 0).settled);
  EXPECT_GT(dialog->height(), 140) << "it grew";
  EXPECT_LE(dialog->frameGeometry().bottom(), room.bottom()) << "its buttons are on the screen";
  EXPECT_LT(dialog->frameGeometry().top(), top_before) << "it was moved up";
  EXPECT_GE(dialog->frameGeometry().top(), room.top());
  EXPECT_EQ(dialog->frameGeometry().left(), 100) << "and only up";
  close_dialog(dialog);
}

TEST(DialogFit, ADialogThatStillFitsTheScreenAfterItGrewIsNotMoved)
{
  DialogGuard guard;
  Owner owner;
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 25);
  const QRect room(0, 0, 800, 600);
  dialog->move(100, 20);
  settle();
  const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster();

  EXPECT_FALSE(adjuster(*dialog, room, 0).settled);
  EXPECT_GT(dialog->height(), 140);
  ASSERT_LE(dialog->frameGeometry().bottom(), room.bottom());
  EXPECT_EQ(dialog->frameGeometry().top(), 20);
  EXPECT_EQ(dialog->frameGeometry().left(), 100);
  close_dialog(dialog);
}

TEST(DialogFit, TheMoveUpFollowsTheRoomItIsGiven)
{
  // A room that does not start at the origin: the dialog ends with its lower edge on the room's.
  DialogGuard guard;
  Owner owner;
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 40);
  const QRect room(0, 200, 800, 400);
  dialog->move(100, 380);
  settle();
  const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster();

  EXPECT_FALSE(adjuster(*dialog, room, 0).settled);
  EXPECT_EQ(dialog->height(), room.height() - 40) << "as tall as the room allows";
  EXPECT_LE(dialog->frameGeometry().bottom(), room.bottom());
  EXPECT_GE(dialog->frameGeometry().top(), room.top());
  EXPECT_EQ(dialog->frameGeometry().top(), room.bottom() + 1 - dialog->frameGeometry().height());
  close_dialog(dialog);
}

TEST(DialogFit, WithoutARoomADialogIsMadeTallerAndLeftWhereItIs)
{
  DialogGuard guard;
  Owner owner;
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 25);
  dialog->move(100, 450);
  settle();
  const auto adjuster = QtPanelStreamManager::make_census_dialog_adjuster();

  EXPECT_FALSE(adjuster(*dialog, QRect(), 0).settled);
  EXPECT_GT(dialog->height(), 140);
  EXPECT_EQ(dialog->frameGeometry().top(), 450);
  close_dialog(dialog);
}

TEST(DialogFit, ADialogOpenedLowEndsTheFitOnTheScreen)
{
  DialogGuard guard;
  Harness h;
  h.manager->set_dialog_size_adjuster(QtPanelStreamManager::make_census_dialog_adjuster());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();
  const QRect room = QGuiApplication::primaryScreen()->availableGeometry();
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 25);
  dialog->move(100, room.bottom() - 150);
  settle();

  h.tick_until("dialog:Add");
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_GT(dialog->height(), 140);
  EXPECT_LE(dialog->frameGeometry().bottom(), room.bottom());
  EXPECT_GE(dialog->frameGeometry().top(), room.top());
  close_dialog(dialog);
}

// --- the manager's side --------------------------------------------------------------------

TEST(DialogFit, TheAdjusterIsCalledWithTheDialogItsRoomAndTheRoundOncePer100Ms)
{
  DialogGuard guard;
  Harness h;
  ScriptedAdjuster script;
  script.clock = h.clock;
  script.steps = {step(false, ""), step(false, ""), step(true, "scripted")};
  h.manager->set_dialog_size_adjuster(script.make());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 3);
  // Ticks 20 ms apart: the adjuster is not asked on every one.
  for (int i = 0; i < 40 && !h.manager->has_panel("dialog:Add"); ++i) {
    h.tick(0.02);
  }
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));
  ASSERT_EQ(script.calls.size(), 3u);
  EXPECT_EQ(script.calls[0].round, 0);
  EXPECT_EQ(script.calls[1].round, 1);
  EXPECT_EQ(script.calls[2].round, 2);
  EXPECT_GE(script.calls[1].at_seconds - script.calls[0].at_seconds, 0.1 - 1e-9);
  EXPECT_GE(script.calls[2].at_seconds - script.calls[1].at_seconds, 0.1 - 1e-9);
  EXPECT_LT(script.calls[1].at_seconds - script.calls[0].at_seconds, 0.1 + 0.02 + 1e-9)
    << "and not later than the first tick after the 100 ms";
  const QRect room = QGuiApplication::primaryScreen()->availableGeometry();
  for (const auto & call : script.calls) {
    EXPECT_EQ(call.room, room) << "the available geometry of the screen the dialog is on";
    EXPECT_EQ(call.size_before, QSize(240, 140));
  }
  EXPECT_EQ(h.panel("dialog:Add").adapter_rebuilds, 1u);
  const auto * line = h.diagnostics.find("fitted to");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->message, "'dialog:Add' fitted to 240x140 px in 3 round(s): scripted");

  close_dialog(dialog);
}

TEST(DialogFit, AnAdjusterThatSettlesAtOnceGivesThePanelOnTheTickThatFoundTheDialog)
{
  DialogGuard guard;
  Harness h;
  ScriptedAdjuster script;
  script.clock = h.clock;
  script.steps = {step(true, "right away")};
  h.manager->set_dialog_size_adjuster(script.make());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 3);
  h.tick();
  EXPECT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(script.calls.size(), 1u);
  ASSERT_NE(h.adapter("dialog:Add"), nullptr) << "ticked in the same batch";
  EXPECT_EQ(h.panel("dialog:Add").frames_submitted, 1u);

  close_dialog(dialog);
}

TEST(DialogFit, AnAdjusterThatNeverSettlesIsStreamedAtTheBoundOfTheManager)
{
  DialogGuard guard;
  Harness h;
  ScriptedAdjuster script;
  script.clock = h.clock;
  script.steps = {step(false, "never")};
  h.manager->set_dialog_size_adjuster(script.make());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 3);
  h.tick_until("dialog:Add", 100);
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));
  const auto max_rounds = static_cast<std::size_t>(QtPanelStreamManager::kDialogFitMaxRounds);
  EXPECT_EQ(script.calls.size(), max_rounds);
  const auto * line = h.diagnostics.find("fitted to");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(
    line->message,
    "'dialog:Add' fitted to 240x140 px in " +
    std::to_string(QtPanelStreamManager::kDialogFitMaxRounds) + " round(s): the fit did not "
    "settle within " + std::to_string(QtPanelStreamManager::kDialogFitMaxRounds) + " rounds");
  for (int i = 0; i < 10; ++i) {
    h.tick();
  }
  EXPECT_EQ(script.calls.size(), max_rounds) << "no more calls once the stream exists";

  close_dialog(dialog);
}

TEST(DialogFit, ADialogHiddenWhileItIsFittedNeverGetsAPanel)
{
  DialogGuard guard;
  Harness h(PanelCloseMode::Destroy);
  ScriptedAdjuster script;
  script.clock = h.clock;
  script.steps = {step(false, "")};
  h.manager->set_dialog_size_adjuster(script.make());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 3);
  h.tick();
  h.tick();
  ASSERT_GE(script.calls.size(), 1u);
  dialog->hide();
  settle();
  for (int i = 0; i < 6; ++i) {
    h.tick();
  }
  EXPECT_FALSE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(h.factory->create_calls(), 1u) << "no QarOS panel was made for it";
  EXPECT_TRUE(h.manager->dialog_panel_keys().empty());
  EXPECT_EQ(h.diagnostics.count("fitted to"), 0u);
  EXPECT_EQ(h.diagnostics.count("got no panel of its own"), 1u);
  const std::size_t calls = script.calls.size();
  for (int i = 0; i < 6; ++i) {
    h.tick();
  }
  EXPECT_EQ(script.calls.size(), calls) << "not called for a hidden dialog";

  // Shown again, it is a new dialog for the fit: the round counts from 0.
  script.steps = {step(true, "second time")};
  dialog->show();
  settle();
  h.tick();
  EXPECT_EQ(script.calls.back().round, 0);
  EXPECT_TRUE(h.manager->has_panel("dialog:Add"));

  close_dialog(dialog);
}

TEST(DialogFit, ADialogDestroyedWhileItIsFittedLeavesNothingBehind)
{
  DialogGuard guard;
  Harness h;
  ScriptedAdjuster script;
  script.clock = h.clock;
  script.steps = {step(false, "")};
  h.manager->set_dialog_size_adjuster(script.make());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 3);
  h.tick();
  ASSERT_EQ(script.calls.size(), 1u);
  delete dialog;
  settle();
  for (int i = 0; i < 8; ++i) {
    h.tick();
  }
  EXPECT_EQ(script.calls.size(), 1u);
  EXPECT_EQ(h.factory->create_calls(), 1u);
  EXPECT_EQ(h.manager->panel_keys(), std::vector<std::string>{"p"});
}

TEST(DialogFit, ADialogWhoseOwnerStoppedStreamingIsNotFittedFurther)
{
  DialogGuard guard;
  Harness h;
  ScriptedAdjuster script;
  script.clock = h.clock;
  script.steps = {step(false, "")};
  h.manager->set_dialog_size_adjuster(script.make());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 3);
  h.tick();
  ASSERT_EQ(script.calls.size(), 1u);
  ASSERT_TRUE(h.manager->close_panel("p", "closed").ok());
  const std::size_t calls = script.calls.size();
  for (int i = 0; i < 8; ++i) {
    h.tick();
  }
  EXPECT_EQ(script.calls.size(), calls);
  EXPECT_FALSE(h.manager->has_panel("dialog:Add"));

  close_dialog(dialog);
}

TEST(DialogFit, TwoDialogsOfOneTitleFittedTogetherGetTwoKeys)
{
  DialogGuard guard;
  Harness h;
  ScriptedAdjuster script;
  script.clock = h.clock;
  script.steps = {step(true, "scripted")};
  h.manager->set_dialog_size_adjuster(script.make());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * first = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 3);
  QDialog * second = show_tree_dialog(owner.get(), QSize(260, 150), "Add", 3);
  h.tick();
  EXPECT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_TRUE(h.manager->has_panel("dialog:Add #2"));
  EXPECT_EQ(h.manager->dialog_panel_keys().size(), 2u);

  close_dialog(second);
  close_dialog(first);
}

TEST(DialogFit, TheCapDecidesFirstAndARefusedDialogIsNeverFitted)
{
  DialogGuard guard;
  Harness h(PanelCloseMode::Park, /*cap=*/1);
  ScriptedAdjuster script;
  script.clock = h.clock;
  script.steps = {step(true, "scripted")};
  h.manager->set_dialog_size_adjuster(script.make());
  Owner owner;
  Owner other;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("other", other.get(), options_no_pacing()).ok());
  h.tick();
  ASSERT_TRUE(h.manager->close_panel("other", "closed").ok());
  h.tick();
  ASSERT_TRUE(h.manager->is_parked("other"));

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 40);
  for (int i = 0; i < 12; ++i) {
    h.tick();
  }
  EXPECT_TRUE(script.calls.empty()) << "a window the cap refuses is left as it is";
  EXPECT_EQ(dialog->height(), 140);
  EXPECT_FALSE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(
    h.diagnostics.count("could not give the dialog a panel of its own: the tombstone cap"), 1u);
  EXPECT_EQ(h.manager->diagnostics().tombstone_cap_refusals, 1u);

  close_dialog(dialog);
}

TEST(DialogFit, TheCapIsCheckedAgainWhenTheFitIsOver)
{
  DialogGuard guard;
  Harness h(PanelCloseMode::Park, /*cap=*/1);
  ScriptedAdjuster script;
  script.clock = h.clock;
  script.steps = {step(false, ""), step(true, "scripted")};
  h.manager->set_dialog_size_adjuster(script.make());
  Owner owner;
  Owner other;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("other", other.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 3);
  h.tick();  // the cap is not reached yet: the first look is made
  ASSERT_EQ(script.calls.size(), 1u);
  ASSERT_TRUE(h.manager->close_panel("other", "closed").ok());  // and now it is
  h.tick();
  ASSERT_TRUE(h.manager->is_parked("other"));
  for (int i = 0; i < 4; ++i) {
    h.tick();
  }
  ASSERT_EQ(script.calls.size(), 2u) << "the fit went on and ended";
  EXPECT_FALSE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(
    h.diagnostics.count("could not give the dialog a panel of its own: the tombstone cap"), 1u);
  EXPECT_EQ(h.diagnostics.count("fitted to"), 0u);
  EXPECT_EQ(h.manager->diagnostics().tombstone_cap_refusals, 1u);
  for (int i = 0; i < 6; ++i) {
    h.tick();
  }
  EXPECT_EQ(script.calls.size(), 2u) << "a refused dialog is not fitted again while it is shown";
  EXPECT_EQ(h.diagnostics.count("could not give the dialog a panel of its own"), 1u);

  close_dialog(dialog);
}

TEST(DialogFit, TheFixedFactorIsNotAppliedWhileAnAdjusterIsInstalled)
{
  DialogGuard guard;
  Harness h(PanelCloseMode::Park, 16, /*dialog_height_factor=*/1.05);
  ScriptedAdjuster script;
  script.clock = h.clock;
  script.steps = {step(true, "scripted")};
  h.manager->set_dialog_size_adjuster(script.make());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 100), "Add", 3);
  const int base = dialog->height();  // the layout may allow no less than it needs
  h.tick();
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(dialog->height(), base) << "the adjuster is in charge";
  EXPECT_EQ(h.adapter("dialog:Add")->desc().content_height, static_cast<std::uint32_t>(base));

  close_dialog(dialog);
}

TEST(DialogFit, WithoutAnAdjusterTheFactorStillMakesTheDialogTallerAtOnce)
{
  DialogGuard guard;
  Harness h(PanelCloseMode::Park, 16, /*dialog_height_factor=*/1.05);
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 100), "Add", 3);
  const int base = dialog->height();  // the layout may allow no less than it needs
  const int taller = static_cast<int>(std::lround(base * 1.05));
  ASSERT_GT(taller, base);
  h.tick();
  ASSERT_TRUE(h.manager->has_panel("dialog:Add")) << "streamed on the tick that found it";
  EXPECT_EQ(dialog->height(), taller);
  EXPECT_EQ(h.adapter("dialog:Add")->desc().content_height, static_cast<std::uint32_t>(taller));
  EXPECT_EQ(h.diagnostics.count("fitted to"), 0u);

  close_dialog(dialog);
}

TEST(DialogFit, TakingTheAdjusterAwayLetsTheDialogsBeingFittedHaveTheirPanelsAtTheirSize)
{
  DialogGuard guard;
  Harness h;
  ScriptedAdjuster script;
  script.clock = h.clock;
  script.steps = {step(false, "")};
  h.manager->set_dialog_size_adjuster(script.make());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 3);
  h.tick();
  ASSERT_FALSE(h.manager->has_panel("dialog:Add"));
  h.manager->set_dialog_size_adjuster(QtPanelStreamManager::DialogSizeAdjuster());
  h.tick();
  EXPECT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(h.adapter("dialog:Add")->desc().content_height, 140u);

  close_dialog(dialog);
}

TEST(DialogFit, ADialogShownAgainIsFittedAgainAndRevivesItsSlotWithoutARebuild)
{
  DialogGuard guard;
  Harness h;
  h.manager->set_dialog_size_adjuster(QtPanelStreamManager::make_census_dialog_adjuster());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();
  QDialog * dialog = show_tree_dialog(owner.get(), QSize(240, 140), "Add", 25);
  h.tick_until("dialog:Add");
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));
  const int fitted_height = dialog->height();
  for (int i = 0; i < 4; ++i) {
    h.tick();
  }

  dialog->hide();
  settle();
  h.tick();
  ASSERT_TRUE(h.manager->is_parked("dialog:Add"));
  dialog->show();
  settle();
  find_tree(dialog)->doItemsLayout();
  for (int i = 0; i < 12 && h.manager->is_parked("dialog:Add"); ++i) {
    h.tick();
  }
  EXPECT_FALSE(h.manager->is_parked("dialog:Add"));
  EXPECT_EQ(dialog->height(), fitted_height) << "it still fits: the same size comes out";
  for (int i = 0; i < 8; ++i) {
    h.tick(0.2);
  }
  EXPECT_EQ(h.factory->create_calls(), 2u);
  EXPECT_EQ(h.panel("dialog:Add").adapter_rebuilds, 1u);
  EXPECT_EQ(h.diagnostics.count("fitted to"), 2u) << "once for each time it was shown";

  close_dialog(dialog);
}

TEST(DialogFit, TheAdjusterKeepsItsMemoryPerDialog)
{
  DialogGuard guard;
  Harness h;
  h.manager->set_dialog_size_adjuster(QtPanelStreamManager::make_census_dialog_adjuster());
  Owner owner;
  ASSERT_TRUE(h.manager->add_panel("p", owner.get(), options_no_pacing()).ok());
  h.tick();
  QDialog * cramped = show_tree_dialog(owner.get(), QSize(240, 140), "One", 25);
  QDialog * fits = show_tree_dialog(owner.get(), QSize(240, 400), "Two", 2);
  h.tick_until("dialog:One");
  h.tick_until("dialog:Two");
  ASSERT_TRUE(h.manager->has_panel("dialog:One"));
  ASSERT_TRUE(h.manager->has_panel("dialog:Two"));
  EXPECT_GT(cramped->height(), 140);
  EXPECT_EQ(fits->height(), 400);
  EXPECT_EQ(take_scroll_census(*cramped).stacked_overflow_px, 0);

  close_dialog(fits);
  close_dialog(cramped);
}

TEST(DialogFit, TheFreeFunctionAndTheStaticMakeTheSameKindOfAdjuster)
{
  QDialog dialog;
  auto * layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QPushButton("OK", &dialog));
  dialog.resize(200, 100);
  dialog.show();
  settle();
  const QRect room(0, 0, 800, 600);
  const auto a = qar_qt_streaming::make_census_dialog_adjuster();
  const auto b = QtPanelStreamManager::make_census_dialog_adjuster(8, 6);
  EXPECT_FALSE(a(dialog, room, 0).settled) << "one look is never enough";
  EXPECT_FALSE(b(dialog, room, 0).settled);
  const auto second = a(dialog, room, 1);
  EXPECT_TRUE(second.settled);
  EXPECT_EQ(second.note, "no scroll bar");
  EXPECT_EQ(second.size, QSize(200, 100));
}
