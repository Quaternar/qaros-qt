// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The pure pointer state machine: raw gesture samples in, semantic
// enter/move/press/drag/release/click/leave/cancel out. No Qt, no geometry —
// samples arrive already resolved to a panel key and a widget pixel.

#include <gtest/gtest.h>

#include "qar_qt_streaming/panel_pointer_state.hpp"

#include <string>
#include <vector>

using qar_qt_streaming::PanelPointerStateMachine;
using qar_qt_streaming::PointerAction;
using qar_qt_streaming::PointerActionKind;
using qar_ar_core::PointerGesture;
using qar_ar_core::PointerPhase;
using qar_qt_streaming::PointerStateConfig;
using qar_qt_streaming::ResolvedPointerSample;

namespace
{

constexpr PointerGesture kHover = PointerGesture::Hover;
constexpr PointerGesture kClick = PointerGesture::Click;

/// A sample resolved onto @p key at (x, y); an empty key means it hit nothing.
ResolvedPointerSample
sample(
  std::uint32_t id, PointerGesture gesture, PointerPhase phase, const std::string & key,
  double x = 10.0, double y = 20.0, bool inside = true, double t = 0.0)
{
  ResolvedPointerSample s;
  s.pointer_id = id;
  s.gesture = gesture;
  s.phase = phase;
  s.timestamp_s = t;
  s.target.panel_key = key;
  s.target.x = x;
  s.target.y = y;
  s.target.inside = inside;
  return s;
}

std::vector<PointerActionKind>
kinds(const std::vector<PointerAction> & actions)
{
  std::vector<PointerActionKind> out;
  for (const auto & a : actions) {
    out.push_back(a.kind);
  }
  return out;
}

/// Feed one sample and return only the actions IT produced.
std::vector<PointerAction>
feed(PanelPointerStateMachine & machine, const ResolvedPointerSample & s)
{
  std::vector<PointerAction> out;
  machine.process(s, out);
  return out;
}

using K = PointerActionKind;

}  // namespace

TEST(PointerState, HoverOverAPanelEntersThenMoves)
{
  PanelPointerStateMachine m;
  auto first = feed(m, sample(1, kHover, PointerPhase::Updated, "A"));
  EXPECT_EQ(kinds(first), (std::vector<K>{K::Enter, K::Move}));
  EXPECT_EQ(first[0].panel_key, "A");
  EXPECT_EQ(first[0].pointer_id, 1u);
  EXPECT_DOUBLE_EQ(first[1].x, 10.0);

  auto second = feed(m, sample(1, kHover, PointerPhase::Updated, "A", 11.0, 21.0));
  EXPECT_EQ(kinds(second), (std::vector<K>{K::Move}));
  EXPECT_EQ(m.hovered_panel(1), "A");
  EXPECT_FALSE(m.is_pressed(1));
}

TEST(PointerState, HoverStartedAndInstantAreMovesToo)
{
  PanelPointerStateMachine m;
  EXPECT_EQ(kinds(feed(m, sample(1, kHover, PointerPhase::Started, "A"))), (std::vector<K>{K::Enter, K::Move}));
  EXPECT_EQ(kinds(feed(m, sample(1, kHover, PointerPhase::Instant, "A"))), (std::vector<K>{K::Move}));
}

TEST(PointerState, HoverCrossingPanelsLeavesTheOldBeforeEnteringTheNew)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kHover, PointerPhase::Updated, "A"));
  auto crossing = feed(m, sample(1, kHover, PointerPhase::Updated, "B", 5.0, 5.0));
  ASSERT_EQ(kinds(crossing), (std::vector<K>{K::Leave, K::Enter, K::Move}));
  EXPECT_EQ(crossing[0].panel_key, "A");
  EXPECT_DOUBLE_EQ(crossing[0].x, 10.0) << "Leave reports the last position ON the old panel";
  EXPECT_EQ(crossing[1].panel_key, "B");
  EXPECT_EQ(m.hovered_panel(1), "B");
}

TEST(PointerState, HoverEndedLeaves)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kHover, PointerPhase::Updated, "A"));
  EXPECT_EQ(kinds(feed(m, sample(1, kHover, PointerPhase::Ended, ""))), (std::vector<K>{K::Leave}));
  EXPECT_TRUE(m.hovered_panel(1).empty());
  // Nothing to leave the second time.
  EXPECT_TRUE(feed(m, sample(1, kHover, PointerPhase::Canceled, "")).empty());
}

TEST(PointerState, HoverOffEveryPanelLeavesAndCountsAMiss)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kHover, PointerPhase::Updated, "A"));
  EXPECT_EQ(kinds(feed(m, sample(1, kHover, PointerPhase::Updated, ""))), (std::vector<K>{K::Leave}));
  EXPECT_EQ(m.diagnostics().samples_missed, 1u);
  EXPECT_EQ(m.diagnostics().samples_dropped, 0u) << "pointing at nothing is not an error";
}

TEST(PointerState, AnInstantClickIsEnterPressReleaseClick)
{
  PanelPointerStateMachine m;
  auto tap = feed(m, sample(1, kClick, PointerPhase::Instant, "A", 30.0, 40.0));
  ASSERT_EQ(kinds(tap), (std::vector<K>{K::Enter, K::Press, K::Release, K::Click}));
  EXPECT_TRUE(tap[2].inside);
  EXPECT_EQ(tap[3].click_count, 1u);
  EXPECT_DOUBLE_EQ(tap[3].x, 30.0);
  EXPECT_FALSE(m.is_pressed(1));
  EXPECT_EQ(m.hovered_panel(1), "A") << "the pointer is still over the panel it tapped";
}

TEST(PointerState, StartedUpdatedEndedInsideIsAClick)
{
  PanelPointerStateMachine m;
  EXPECT_EQ(kinds(feed(m, sample(1, kClick, PointerPhase::Started, "A"))), (std::vector<K>{K::Enter, K::Press}));
  EXPECT_TRUE(m.is_pressed(1));
  EXPECT_EQ(m.grabbed_panel(1), "A");

  auto drag = feed(m, sample(1, kClick, PointerPhase::Updated, "A", 12.0, 22.0));
  ASSERT_EQ(kinds(drag), (std::vector<K>{K::Drag}));
  EXPECT_TRUE(drag[0].inside);

  auto release = feed(m, sample(1, kClick, PointerPhase::Ended, "A", 12.0, 22.0));
  ASSERT_EQ(kinds(release), (std::vector<K>{K::Release, K::Click}));
  EXPECT_TRUE(release[0].inside);
  EXPECT_FALSE(m.is_pressed(1));
  EXPECT_TRUE(m.grabbed_panel(1).empty());
}

TEST(PointerState, DragOffThePanelStaysGrabbedAndReleaseOutsideIsNotAClick)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Started, "A"));

  // The manager projects a grabbed pointer onto its panel's plane even when it
  // is outside the rectangle: same key, inside == false, coordinates past the edge.
  auto drag = feed(m, sample(1, kClick, PointerPhase::Updated, "A", -15.0, 20.0, false));
  ASSERT_EQ(kinds(drag), (std::vector<K>{K::Drag}));
  EXPECT_FALSE(drag[0].inside);
  EXPECT_DOUBLE_EQ(drag[0].x, -15.0);
  EXPECT_EQ(m.grabbed_panel(1), "A") << "Qt's implicit grab: the press owns the pointer";

  auto release = feed(m, sample(1, kClick, PointerPhase::Ended, "A", -15.0, 20.0, false));
  ASSERT_EQ(kinds(release), (std::vector<K>{K::Release, K::Leave}));
  EXPECT_FALSE(release[0].inside);
  EXPECT_TRUE(m.hovered_panel(1).empty());
}

TEST(PointerState, SamplesResolvedElsewhereWhilePressedStillRouteToTheGrab)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Started, "A"));
  // A source that could not place the sample on A (another panel, or nothing).
  auto drag = feed(m, sample(1, kClick, PointerPhase::Updated, "B", 3.0, 3.0));
  ASSERT_EQ(kinds(drag), (std::vector<K>{K::Drag}));
  EXPECT_EQ(drag[0].panel_key, "A");
  EXPECT_FALSE(drag[0].inside);
  EXPECT_DOUBLE_EQ(drag[0].x, 10.0) << "the last known position on A is kept";
}

TEST(PointerState, HoverEndedWhilePressedDoesNotLeave)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Started, "A"));
  EXPECT_TRUE(feed(m, sample(1, kHover, PointerPhase::Ended, "")).empty());
  EXPECT_TRUE(m.is_pressed(1));
  EXPECT_EQ(m.hovered_panel(1), "A");
  EXPECT_EQ(kinds(feed(m, sample(1, kClick, PointerPhase::Ended, "A"))), (std::vector<K>{K::Release, K::Click}));
}

TEST(PointerState, ACanceledPressIsCancelNeverClick)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Started, "A"));
  auto cancel = feed(m, sample(1, kClick, PointerPhase::Canceled, "A"));
  EXPECT_EQ(kinds(cancel), (std::vector<K>{K::Cancel}));
  EXPECT_FALSE(m.is_pressed(1));
  EXPECT_EQ(m.hovered_panel(1), "A");
}

TEST(PointerState, ACancelOutsideThePanelAlsoLeaves)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Started, "A"));
  EXPECT_EQ(kinds(feed(m, sample(1, kClick, PointerPhase::Canceled, "", 0.0, 0.0, false))),
    (std::vector<K>{K::Cancel, K::Leave}));
  EXPECT_TRUE(m.hovered_panel(1).empty());
}

TEST(PointerState, AReleaseWithoutAPressIsDropped)
{
  PanelPointerStateMachine m;
  EXPECT_TRUE(feed(m, sample(1, kClick, PointerPhase::Ended, "A")).empty());
  EXPECT_EQ(m.diagnostics().samples_dropped, 1u);
  EXPECT_NE(m.diagnostics().last_drop_reason.find("release without a press"), std::string::npos);
  EXPECT_TRUE(feed(m, sample(1, kClick, PointerPhase::Canceled, "A")).empty());
  EXPECT_EQ(m.diagnostics().samples_dropped, 2u);
}

TEST(PointerState, APressThatBeginsOffPanelOwnsNothingUntilItEnds)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kHover, PointerPhase::Updated, "A"));
  // Pressed in the void: leave the hovered panel, own no panel.
  EXPECT_EQ(kinds(feed(m, sample(1, kClick, PointerPhase::Started, ""))), (std::vector<K>{K::Leave}));
  EXPECT_TRUE(m.is_pressed(1));
  EXPECT_TRUE(m.grabbed_panel(1).empty());

  // Dragging onto a panel gives it nothing — Qt never delivers a drag that
  // began elsewhere.
  EXPECT_TRUE(feed(m, sample(1, kClick, PointerPhase::Updated, "A")).empty());
  EXPECT_EQ(m.diagnostics().samples_dropped, 1u);

  // The release frees the pointer, which is now over A: hover resumes.
  EXPECT_EQ(kinds(feed(m, sample(1, kClick, PointerPhase::Ended, "A"))), (std::vector<K>{K::Enter, K::Move}));
  EXPECT_FALSE(m.is_pressed(1));
}

TEST(PointerState, ADuplicateStartedCancelsThePendingPress)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Started, "A"));
  auto again = feed(m, sample(1, kClick, PointerPhase::Started, "A", 11.0, 21.0));
  EXPECT_EQ(kinds(again), (std::vector<K>{K::Cancel, K::Press}));
  EXPECT_TRUE(m.is_pressed(1));
}

TEST(PointerState, AnInstantWhilePressedAbandonsThePendingPressThenTaps)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Started, "A"));
  auto tap = feed(m, sample(1, kClick, PointerPhase::Instant, "A"));
  EXPECT_EQ(kinds(tap), (std::vector<K>{K::Cancel, K::Press, K::Release, K::Click}));
  EXPECT_FALSE(m.is_pressed(1));
}

TEST(PointerState, AnInstantClickOffPanelIsAMissAndLeaves)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kHover, PointerPhase::Updated, "A"));
  EXPECT_EQ(kinds(feed(m, sample(1, kClick, PointerPhase::Instant, ""))), (std::vector<K>{K::Leave}));
  EXPECT_EQ(m.diagnostics().samples_missed, 1u);
}

TEST(PointerState, ClickUpdatedWithoutAPressIsJustAHoverMove)
{
  PanelPointerStateMachine m;
  EXPECT_EQ(kinds(feed(m, sample(1, kClick, PointerPhase::Updated, "A"))), (std::vector<K>{K::Enter, K::Move}));
  EXPECT_FALSE(m.is_pressed(1));
}

TEST(PointerState, TwoQuickTapsInPlaceAreADoubleClickAndTheThirdIsSingleAgain)
{
  PanelPointerStateMachine m;  // Qt defaults: 400 ms, 5 px
  auto first = feed(m, sample(1, kClick, PointerPhase::Instant, "A", 10.0, 10.0, true, 0.0));
  EXPECT_EQ(first[1].click_count, 1u);

  auto second = feed(m, sample(1, kClick, PointerPhase::Instant, "A", 12.0, 9.0, true, 0.2));
  ASSERT_EQ(kinds(second), (std::vector<K>{K::Press, K::Release, K::Click}));
  EXPECT_EQ(second[0].click_count, 2u);
  EXPECT_EQ(second[2].click_count, 2u);

  auto third = feed(m, sample(1, kClick, PointerPhase::Instant, "A", 12.0, 9.0, true, 0.3));
  EXPECT_EQ(third[0].click_count, 1u) << "Qt alternates single/double";
}

TEST(PointerState, ASlowOrDistantSecondTapIsSingle)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Instant, "A", 10.0, 10.0, true, 0.0));
  auto slow = feed(m, sample(1, kClick, PointerPhase::Instant, "A", 10.0, 10.0, true, 1.0));
  EXPECT_EQ(slow[0].click_count, 1u);

  feed(m, sample(1, kClick, PointerPhase::Instant, "A", 10.0, 10.0, true, 2.0));
  auto distant = feed(m, sample(1, kClick, PointerPhase::Instant, "A", 20.0, 10.0, true, 2.1));
  EXPECT_EQ(distant[0].click_count, 1u);

  // On a different panel it cannot be the same click either.
  feed(m, sample(1, kClick, PointerPhase::Instant, "A", 10.0, 10.0, true, 3.0));
  auto elsewhere = feed(m, sample(1, kClick, PointerPhase::Instant, "B", 10.0, 10.0, true, 3.1));
  EXPECT_EQ(elsewhere[1].click_count, 1u);
}

TEST(PointerState, DoubleClickTunablesAreHonoured)
{
  PointerStateConfig config;
  config.double_click_interval_s = 0.05;
  PanelPointerStateMachine m(config);
  feed(m, sample(1, kClick, PointerPhase::Instant, "A", 10.0, 10.0, true, 0.0));
  auto second = feed(m, sample(1, kClick, PointerPhase::Instant, "A", 10.0, 10.0, true, 0.2));
  EXPECT_EQ(second[0].click_count, 1u);
}

TEST(PointerState, PressReleaseSequencesDoubleClickToo)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Started, "A", 10.0, 10.0, true, 0.0));
  feed(m, sample(1, kClick, PointerPhase::Ended, "A", 10.0, 10.0, true, 0.05));
  auto press = feed(m, sample(1, kClick, PointerPhase::Started, "A", 10.0, 10.0, true, 0.2));
  ASSERT_EQ(kinds(press), (std::vector<K>{K::Press}));
  EXPECT_EQ(press[0].click_count, 2u);
  auto release = feed(m, sample(1, kClick, PointerPhase::Ended, "A", 10.0, 10.0, true, 0.25));
  EXPECT_EQ(release[1].click_count, 2u);
}

TEST(PointerState, PointersAreIndependent)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Started, "A"));
  auto hover = feed(m, sample(2, kHover, PointerPhase::Updated, "A", 50.0, 50.0));
  ASSERT_EQ(kinds(hover), (std::vector<K>{K::Enter, K::Move}));
  EXPECT_EQ(hover[0].pointer_id, 2u);
  EXPECT_TRUE(m.is_pressed(1));
  EXPECT_FALSE(m.is_pressed(2));

  auto tap = feed(m, sample(2, kClick, PointerPhase::Instant, "B"));
  EXPECT_EQ(kinds(tap), (std::vector<K>{K::Leave, K::Enter, K::Press, K::Release, K::Click}));
  EXPECT_EQ(m.grabbed_panel(1), "A") << "pointer 2's tap on B does not disturb pointer 1";
  EXPECT_EQ(m.pointer_count(), 2u);
}

TEST(PointerState, ForgettingAPanelCancelsPressesAndLeavesHovers)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Started, "A"));
  feed(m, sample(2, kHover, PointerPhase::Updated, "A"));
  feed(m, sample(3, kHover, PointerPhase::Updated, "B"));

  std::vector<PointerAction> out;
  m.forget_panel("A", out);
  ASSERT_EQ(kinds(out), (std::vector<K>{K::Cancel, K::Leave, K::Leave}));
  EXPECT_EQ(out[0].pointer_id, 1u);
  EXPECT_EQ(out[1].pointer_id, 1u);
  EXPECT_EQ(out[2].pointer_id, 2u);
  EXPECT_FALSE(m.is_pressed(1));
  EXPECT_TRUE(m.hovered_panel(2).empty());
  EXPECT_EQ(m.hovered_panel(3), "B") << "an unrelated panel is untouched";
}

TEST(PointerState, ForgettingAPanelClearsHalfADoubleClick)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Instant, "A", 10.0, 10.0, true, 0.0));
  std::vector<PointerAction> out;
  m.forget_panel("A", out);
  auto tap = feed(m, sample(1, kClick, PointerPhase::Instant, "A", 10.0, 10.0, true, 0.1));
  EXPECT_EQ(tap[1].click_count, 1u);
}

TEST(PointerState, ResetCancelsEverythingAndForgetsThePointers)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kClick, PointerPhase::Started, "A"));
  feed(m, sample(2, kHover, PointerPhase::Updated, "B"));

  std::vector<PointerAction> out;
  m.reset(out);
  EXPECT_EQ(kinds(out), (std::vector<K>{K::Cancel, K::Leave, K::Leave}));
  EXPECT_EQ(m.pointer_count(), 0u);
  EXPECT_FALSE(m.is_pressed(1));
}

TEST(PointerState, DiagnosticsCountSamplesAndActions)
{
  PanelPointerStateMachine m;
  feed(m, sample(1, kHover, PointerPhase::Updated, "A"));   // Enter + Move
  feed(m, sample(1, kClick, PointerPhase::Instant, "A"));  // Press + Release + Click
  feed(m, sample(1, kClick, PointerPhase::Ended, "A"));    // dropped
  EXPECT_EQ(m.diagnostics().samples, 3u);
  EXPECT_EQ(m.diagnostics().actions, 5u);
  EXPECT_EQ(m.diagnostics().samples_dropped, 1u);
}

TEST(PointerState, ActionsCarryTheSampleTimestamp)
{
  PanelPointerStateMachine m;
  auto tap = feed(m, sample(1, kClick, PointerPhase::Instant, "A", 1.0, 1.0, true, 42.5));
  for (const auto & a : tap) {
    EXPECT_DOUBLE_EQ(a.timestamp_s, 42.5);
  }
}

TEST(PointerActionKind, HasNames)
{
  EXPECT_STREQ(qar_qt_streaming::to_string(K::Press), "Press");
  EXPECT_STREQ(qar_qt_streaming::to_string(K::Cancel), "Cancel");
  EXPECT_STREQ(qar_ar_core::to_string(PointerPhase::Instant), "Instant");
  EXPECT_STREQ(qar_ar_core::to_string(PointerGesture::Hover), "Hover");
}
