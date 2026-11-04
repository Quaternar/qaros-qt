// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The gesture mapper: the table of kinds, phases and hands, the pointer ids, the pixel to (u, v)
// conversion with its clamping, and the lifecycle bookkeeping with its liveness sweep.

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "qar_qt_streaming/seam/panel_gesture_mapper.hpp"

using qar_qt_streaming::seam::GestureOutcome;
using qar_qt_streaming::seam::GestureSlot;
using qar_qt_streaming::seam::PanelGestureEvent;
using qar_qt_streaming::seam::PanelGestureKind;
using qar_qt_streaming::seam::PanelGestureMapper;
using qar_qt_streaming::seam::PanelGesturePhase;
using qar_qt_streaming::seam::PanelPointerSample;
using qar_qt_streaming::seam::PeerId;
using qar_qt_streaming::seam::PointerGesture;
using qar_qt_streaming::seam::PointerHand;
using qar_qt_streaming::seam::PointerPhase;

namespace
{

PeerId peer_of(std::uint8_t tag)
{
  PeerId peer;
  for (auto & byte : peer.data) {
    byte = tag;
  }
  return peer;
}

GestureSlot slot_of(const std::string & key, std::uint32_t w = 200, std::uint32_t h = 100)
{
  GestureSlot slot;
  slot.panel_key = key;
  slot.content_width = w;
  slot.content_height = h;
  return slot;
}

PanelGestureEvent event_of(
  PanelGestureKind kind, PanelGesturePhase phase, std::int32_t x = 10, std::int32_t y = 20,
  std::uint8_t peer_tag = 1, PointerHand hand = PointerHand::Right, std::uint64_t id = 7)
{
  PanelGestureEvent event;
  event.peer = peer_of(peer_tag);
  event.gesture_id = id;
  event.kind = kind;
  event.phase = phase;
  event.x_pixels = x;
  event.y_pixels = y;
  event.hand = hand;
  return event;
}

}  // namespace

// ----- classification ------------------------------------------------------------------------

struct KindRow
{
  PanelGestureKind kind;
  PointerGesture gesture;
};

class MapperKinds : public ::testing::TestWithParam<KindRow> {};

TEST_P(MapperKinds, EveryKindMapsToItsPointerGestureInEveryPhaseAndHand)
{
  const PanelGesturePhase phases[] = {
    PanelGesturePhase::Started, PanelGesturePhase::Updated, PanelGesturePhase::Ended,
    PanelGesturePhase::Instant, PanelGesturePhase::Canceled};
  const PointerPhase expected[] = {
    PointerPhase::Started, PointerPhase::Updated, PointerPhase::Ended, PointerPhase::Instant,
    PointerPhase::Canceled};
  const PointerHand hands[] = {PointerHand::Unknown, PointerHand::Left, PointerHand::Right};
  for (std::size_t p = 0; p < 5; ++p) {
    for (const PointerHand hand : hands) {
      PanelGestureMapper mapper;
      PanelPointerSample sample;
      ASSERT_EQ(
        mapper.offer(
          event_of(GetParam().kind, phases[p], 50, 25, 3, hand), slot_of("a"), 1.5, sample),
        GestureOutcome::Sampled);
      EXPECT_EQ(sample.gesture, GetParam().gesture);
      EXPECT_EQ(sample.phase, expected[p]);
      EXPECT_EQ(sample.hand, hand);
      EXPECT_EQ(sample.panel_key, "a");
      EXPECT_TRUE(sample.has_panel_uv);
      EXPECT_DOUBLE_EQ(sample.timestamp_s, 1.5);
      EXPECT_EQ(sample.pointer_id, PanelGestureMapper::pointer_id_for(peer_of(3), hand));
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
  Table, MapperKinds,
  ::testing::Values(
    KindRow{PanelGestureKind::Hover, PointerGesture::Hover},
    KindRow{PanelGestureKind::Tap, PointerGesture::Click},
    KindRow{PanelGestureKind::Drag, PointerGesture::Click},
    KindRow{PanelGestureKind::LongPress, PointerGesture::LongPress}));

TEST(PanelGestureMapper, AnUnknownKindOrPhaseIsCountedApartAndProducesNothing)
{
  PanelGestureMapper mapper;
  PanelPointerSample sample;
  EXPECT_EQ(
    mapper.offer(
      event_of(PanelGestureKind::Unknown, PanelGesturePhase::Instant), slot_of("a"), 0.0, sample),
    GestureOutcome::UnknownKind);
  EXPECT_EQ(
    mapper.offer(
      event_of(PanelGestureKind::Tap, PanelGesturePhase::Unknown), slot_of("a"), 0.0, sample),
    GestureOutcome::UnknownKind);
  EXPECT_EQ(mapper.stats().events_received, 2u);
  EXPECT_EQ(mapper.stats().events_unknown_kind, 2u);
  EXPECT_EQ(mapper.stats().events_invalid, 0u);
  EXPECT_EQ(mapper.live_count(), 0u);
}

TEST(PanelGestureMapper, AnUnreadableEventIsInvalidAndCostsNoLeave)
{
  PanelGestureMapper mapper;
  PanelPointerSample sample;
  PanelGestureEvent event = event_of(PanelGestureKind::Hover, PanelGesturePhase::Started);
  event.readable = false;
  EXPECT_EQ(mapper.offer(event, slot_of("a"), 0.0, sample), GestureOutcome::Invalid);
  EXPECT_EQ(mapper.stats().events_invalid, 1u);
  EXPECT_EQ(mapper.live_count(), 0u);
}

TEST(PanelGestureMapper, TheKindCountersFollowTheKindsAndTheRecognisersTapCount)
{
  PanelGestureMapper mapper;
  PanelPointerSample sample;
  mapper.offer(
    event_of(PanelGestureKind::Drag, PanelGesturePhase::Started), slot_of("a"), 0, sample);
  mapper.offer(
    event_of(PanelGestureKind::LongPress, PanelGesturePhase::Instant), slot_of("a"), 0, sample);
  PanelGestureEvent tap = event_of(PanelGestureKind::Tap, PanelGesturePhase::Instant);
  tap.tap_count = 1;
  mapper.offer(tap, slot_of("a"), 0, sample);
  EXPECT_EQ(sample.tap_count, 1u);
  tap.tap_count = 2;
  mapper.offer(tap, slot_of("a"), 0, sample);
  EXPECT_EQ(sample.tap_count, 2u);
  EXPECT_EQ(mapper.stats().events_drag, 1u);
  EXPECT_EQ(mapper.stats().events_long_press, 1u);
  EXPECT_EQ(mapper.stats().events_multi_tap, 1u);
  EXPECT_EQ(mapper.stats().events_received, 4u);
}

// ----- pointer ids ---------------------------------------------------------------------------

TEST(PanelGestureMapper, ThePointerIdIsStablePerPeerAndHandAndDistinctAcrossThem)
{
  const std::uint32_t right_1 = PanelGestureMapper::pointer_id_for(peer_of(1), PointerHand::Right);
  EXPECT_EQ(right_1, PanelGestureMapper::pointer_id_for(peer_of(1), PointerHand::Right));
  std::set<std::uint32_t> ids;
  for (std::uint8_t peer = 1; peer <= 4; ++peer) {
    for (const PointerHand hand : {PointerHand::Unknown, PointerHand::Left, PointerHand::Right}) {
      const std::uint32_t id = PanelGestureMapper::pointer_id_for(peer_of(peer), hand);
      EXPECT_GE(id, 16u) << "clear of 0 and 1";
      ids.insert(id);
    }
  }
  EXPECT_EQ(ids.size(), 12u);
}

TEST(PanelGestureMapper, ThePointerIdIsTheFnvHashOfThePeerBytesAndTheHand)
{
  // Values computed outside this code base with the same recipe: 32-bit FNV-1a over the 16 bytes
  // of the peer id, then over the hand (0 unknown, 1 left, 2 right), reduced modulo 0xFFFFF000
  // and moved up by 16.
  EXPECT_EQ(PanelGestureMapper::pointer_id_for(peer_of(0), PointerHand::Unknown), 4117914351u);
  EXPECT_EQ(PanelGestureMapper::pointer_id_for(peer_of(0xAA), PointerHand::Left), 2197036572u);
  EXPECT_EQ(PanelGestureMapper::pointer_id_for(peer_of(0xAA), PointerHand::Right), 2247369429u);
  PeerId counting;
  for (std::size_t i = 0; i < sizeof(counting.data); ++i) {
    counting.data[i] = static_cast<std::uint8_t>(i);
  }
  EXPECT_EQ(PanelGestureMapper::pointer_id_for(counting, PointerHand::Unknown), 2146047775u);
}

// ----- (u, v) --------------------------------------------------------------------------------

TEST(PanelGestureMapper, AnInnerPixelMapsToItsCentre)
{
  float u = 0, v = 0;
  bool clamped = true;
  ASSERT_TRUE(PanelGestureMapper::uv_from_content_pixels(0, 0, 200, 100, u, v, clamped));
  EXPECT_FLOAT_EQ(u, 0.5f / 200.0f);
  EXPECT_FLOAT_EQ(v, 0.5f / 100.0f);
  EXPECT_FALSE(clamped);
  ASSERT_TRUE(PanelGestureMapper::uv_from_content_pixels(199, 99, 200, 100, u, v, clamped));
  EXPECT_FLOAT_EQ(u, 199.5f / 200.0f);
  EXPECT_FLOAT_EQ(v, 99.5f / 100.0f);
  EXPECT_FALSE(clamped);
}

TEST(PanelGestureMapper, TheCentreOfAPixelMapsBackToThePixelAtAnySize)
{
  for (const std::uint32_t width : {1u, 7u, 200u, 1921u}) {
    for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); x += (width > 50 ? 13 : 1)) {
      float u = 0, v = 0;
      bool clamped = false;
      ASSERT_TRUE(PanelGestureMapper::uv_from_content_pixels(x, 0, width, 1, u, v, clamped));
      EXPECT_EQ(static_cast<std::int32_t>(u * static_cast<float>(width)), x)
        << "width " << width << " x " << x;
    }
  }
}

TEST(PanelGestureMapper, APixelOutsideTheContentLandsOnTheNearestEdgeAndIsCounted)
{
  PanelGestureMapper mapper;
  PanelPointerSample sample;
  ASSERT_EQ(
    mapper.offer(
      event_of(PanelGestureKind::Tap, PanelGesturePhase::Instant, -5, 500), slot_of("a"), 0.0,
      sample),
    GestureOutcome::Sampled);
  EXPECT_FLOAT_EQ(sample.u, 0.5f / 200.0f);
  EXPECT_FLOAT_EQ(sample.v, 99.5f / 100.0f);
  EXPECT_EQ(mapper.stats().events_clamped, 1u);
  ASSERT_EQ(
    mapper.offer(
      event_of(PanelGestureKind::Tap, PanelGesturePhase::Instant, 200, 0), slot_of("a"), 0.0,
      sample),
    GestureOutcome::Sampled);
  EXPECT_FLOAT_EQ(sample.u, 199.5f / 200.0f);
  EXPECT_EQ(mapper.stats().events_clamped, 2u);
}

TEST(PanelGestureMapper, WithoutAContentSizeNothingIsPlacedButTheKindIsStillCounted)
{
  PanelGestureMapper mapper;
  PanelPointerSample sample;
  float u = 0, v = 0;
  bool clamped = false;
  EXPECT_FALSE(PanelGestureMapper::uv_from_content_pixels(1, 1, 0, 10, u, v, clamped));
  EXPECT_FALSE(PanelGestureMapper::uv_from_content_pixels(1, 1, 10, 0, u, v, clamped));
  EXPECT_EQ(
    mapper.offer(
      event_of(PanelGestureKind::Drag, PanelGesturePhase::Started), slot_of("a", 0, 0), 0.0,
      sample),
    GestureOutcome::NoContentSize);
  EXPECT_EQ(mapper.stats().events_drag, 1u);
  EXPECT_EQ(mapper.live_count(), 0u);
}

// ----- lifecycles ----------------------------------------------------------------------------

TEST(PanelGestureMapper, ADragIsRememberedFromStartToEndAndATapNever)
{
  PanelGestureMapper mapper;
  PanelPointerSample sample;
  mapper.offer(
    event_of(PanelGestureKind::Drag, PanelGesturePhase::Started), slot_of("a"), 0, sample);
  EXPECT_EQ(mapper.live_count(), 1u);
  mapper.offer(
    event_of(PanelGestureKind::Drag, PanelGesturePhase::Updated), slot_of("a"), 0.1, sample);
  EXPECT_EQ(mapper.live_count(), 1u) << "the same lifecycle";
  mapper.offer(
    event_of(PanelGestureKind::Tap, PanelGesturePhase::Instant), slot_of("a"), 0.1, sample);
  EXPECT_EQ(mapper.live_count(), 1u);
  mapper.offer(
    event_of(PanelGestureKind::Drag, PanelGesturePhase::Ended), slot_of("a"), 0.2, sample);
  EXPECT_EQ(mapper.live_count(), 0u);
}

TEST(PanelGestureMapper, TheSameGestureIdOfTwoKindsOrTwoPeersAreTwoLifecycles)
{
  PanelGestureMapper mapper;
  PanelPointerSample sample;
  mapper.offer(
    event_of(PanelGestureKind::Hover, PanelGesturePhase::Started, 1, 1, 1), slot_of("a"), 0,
    sample);
  mapper.offer(
    event_of(PanelGestureKind::Drag, PanelGesturePhase::Started, 1, 1, 1), slot_of("a"), 0, sample);
  mapper.offer(
    event_of(PanelGestureKind::Drag, PanelGesturePhase::Started, 1, 1, 2), slot_of("a"), 0, sample);
  EXPECT_EQ(mapper.live_count(), 3u);
  mapper.offer(
    event_of(PanelGestureKind::Hover, PanelGesturePhase::Canceled, 1, 1, 1), slot_of("a"), 0,
    sample);
  EXPECT_EQ(mapper.live_count(), 2u);
}

TEST(PanelGestureMapper, ALostEndIsCancelledAfterTheTimeoutAtTheLastPlace)
{
  PanelGestureMapper mapper(0.75);
  PanelPointerSample sample;
  mapper.offer(
    event_of(PanelGestureKind::Drag, PanelGesturePhase::Started, 100, 50, 4, PointerHand::Left),
    slot_of("a"), 10.0, sample);
  mapper.offer(
    event_of(PanelGestureKind::Drag, PanelGesturePhase::Updated, 120, 60, 4, PointerHand::Left),
    slot_of("a"), 10.5, sample);

  std::vector<PanelPointerSample> out;
  mapper.sweep_lifecycles(11.2, out);
  EXPECT_TRUE(out.empty()) << "0.7 s of silence is not enough";
  mapper.sweep_lifecycles(11.3, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].phase, PointerPhase::Canceled);
  EXPECT_EQ(out[0].gesture, PointerGesture::Click);
  EXPECT_EQ(out[0].hand, PointerHand::Left);
  EXPECT_EQ(out[0].panel_key, "a");
  EXPECT_EQ(out[0].pointer_id, PanelGestureMapper::pointer_id_for(peer_of(4), PointerHand::Left));
  EXPECT_TRUE(out[0].has_panel_uv);
  EXPECT_FLOAT_EQ(out[0].u, 120.5f / 200.0f);
  EXPECT_FLOAT_EQ(out[0].v, 60.5f / 100.0f);
  EXPECT_EQ(mapper.stats().lifecycle_timeouts, 1u);
  EXPECT_EQ(mapper.stats().hover_timeouts, 0u);
  EXPECT_EQ(mapper.live_count(), 0u);
  out.clear();
  mapper.sweep_lifecycles(99.0, out);
  EXPECT_TRUE(out.empty()) << "a cancelled gesture is forgotten";
}

TEST(PanelGestureMapper, AHoverThatGoesSilentIsCountedAsAHoverTimeout)
{
  PanelGestureMapper mapper;
  PanelPointerSample sample;
  mapper.offer(
    event_of(PanelGestureKind::Hover, PanelGesturePhase::Started), slot_of("a"), 0.0, sample);
  std::vector<PanelPointerSample> out;
  mapper.sweep_lifecycles(1.0, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].gesture, PointerGesture::Hover);
  EXPECT_EQ(mapper.stats().hover_timeouts, 1u);
}

TEST(PanelGestureMapper, ATimeoutOfZeroTurnsTheSweepOff)
{
  PanelGestureMapper mapper(0.0);
  PanelPointerSample sample;
  mapper.offer(
    event_of(PanelGestureKind::Drag, PanelGesturePhase::Started), slot_of("a"), 0.0, sample);
  std::vector<PanelPointerSample> out;
  mapper.sweep_lifecycles(1000.0, out);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(mapper.live_count(), 1u);
}

TEST(PanelGestureMapper, CancellingAPanelLetsGoOfItsGesturesOnly)
{
  PanelGestureMapper mapper;
  PanelPointerSample sample;
  mapper.offer(
    event_of(PanelGestureKind::Drag, PanelGesturePhase::Started, 1, 1, 1), slot_of("a"), 0, sample);
  mapper.offer(
    event_of(PanelGestureKind::Drag, PanelGesturePhase::Started, 1, 1, 2), slot_of("b"), 0, sample);
  std::vector<PanelPointerSample> out;
  mapper.cancel_lifecycles_for_panel("a", 3.0, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].panel_key, "a");
  EXPECT_EQ(out[0].phase, PointerPhase::Canceled);
  EXPECT_DOUBLE_EQ(out[0].timestamp_s, 3.0);
  EXPECT_EQ(mapper.live_count(), 1u);
  EXPECT_EQ(mapper.stats().lifecycle_timeouts, 0u) << "not a timeout";
  mapper.cancel_lifecycles_for_panel("", 4.0, out);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[1].panel_key, "b");
  EXPECT_EQ(mapper.live_count(), 0u);
}
