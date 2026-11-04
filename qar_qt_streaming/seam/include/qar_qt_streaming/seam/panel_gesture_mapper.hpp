// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__SEAM__PANEL_GESTURE_MAPPER_HPP_
#define QAR_QT_STREAMING__SEAM__PANEL_GESTURE_MAPPER_HPP_

/// @file panel_gesture_mapper.hpp
/// @brief The rules that turn one gesture of a QarOS GUI panel into a pointer sample: the
/// classification of the gesture, the pixel to (u, v) conversion, the pointer id of a (peer, hand)
/// pair and the bookkeeping of the gestures that have a lifecycle. Pure: no QarOS call, no clock of
/// its own, no I/O.
///
/// A backend reads the gesture off the runtime's handle, fills a @ref PanelGestureEvent and hands
/// it to @ref PanelGestureMapper::offer together with the content size of the subscription and its
/// own monotonic time. The samples it gets back go to the queue of the input source.

#include "qar_qt_streaming/seam/pointer_input.hpp"
#include "qar_qt_streaming/seam/values.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace qar_qt_streaming::seam
{

/// @brief The kind of a panel gesture. The values equal those of the runtime's
/// `QarGuiPanelGestureKind`.
enum class PanelGestureKind : std::uint8_t
{
  Hover = 0,
  Tap = 1,
  LongPress = 2,
  Drag = 3,
  /// A kind this build does not know (a newer runtime).
  Unknown = 255
};

/// @brief The phase of a panel gesture. The values equal those of the runtime's
/// `QarGesturePhase`.
enum class PanelGesturePhase : std::uint8_t
{
  Started = 0,
  Updated = 1,
  Ended = 2,
  Instant = 3,
  Canceled = 4,
  /// A phase this build does not know (a newer runtime).
  Unknown = 255
};

/// @brief One gesture as the backend read it off the runtime's handle.
struct PanelGestureEvent
{
  /// False when the handle could not be read: the event is invalid and is counted,
  /// nothing more.
  bool readable = true;
  /// The peer that made the gesture.
  PeerId peer{};
  /// Unique within one source peer, shared by the kinds of that peer.
  std::uint64_t gesture_id = 0;
  PanelGestureKind kind = PanelGestureKind::Hover;
  PanelGesturePhase phase = PanelGesturePhase::Instant;
  /// Content-space pixels: origin top-left, x to the right, y downward.
  std::int32_t x_pixels = 0;
  std::int32_t y_pixels = 0;
  /// The hand, when the runtime reported one.
  PointerHand hand = PointerHand::Unknown;
  /// The recogniser's own tap count; 0 when it reports none.
  std::uint8_t tap_count = 0;
};

/// @brief What the mapper did with one event.
enum class GestureOutcome
{
  /// A sample was produced.
  Sampled,
  /// The event was not readable.
  Invalid,
  /// The kind or the phase is outside the enums this build knows.
  UnknownKind,
  /// The subscription has no content size, so no (u, v) can be computed.
  NoContentSize
};

/// @brief The panel and the content size a gesture was subscribed for.
struct GestureSlot
{
  std::string panel_key;
  /// The extent of the content the event's pixels are in; zero in either member is "unknown".
  std::uint32_t content_width = 0;
  std::uint32_t content_height = 0;
};

/// @brief Counters of the events the mapper has seen.
struct GestureStats
{
  std::uint64_t events_received = 0;
  std::uint64_t events_invalid = 0;
  /// Events whose pixel lay outside the content and was moved to its edge.
  std::uint64_t events_clamped = 0;
  /// Gestures whose lifecycle went silent and were cancelled by @ref
  /// PanelGestureMapper::sweep_lifecycles.
  std::uint64_t lifecycle_timeouts = 0;
  /// ... of which hovers.
  std::uint64_t hover_timeouts = 0;
  std::uint64_t events_unknown_kind = 0;
  std::uint64_t events_multi_tap = 0;
  std::uint64_t events_drag = 0;
  std::uint64_t events_long_press = 0;
};

/// @brief Maps panel gestures to pointer samples. Not thread safe; the host serialises the calls.
class PanelGestureMapper
{
public:
  /// Three heartbeats of 250 ms: a started or updated gesture that is silent for this long is
  /// cancelled.
  static constexpr double kDefaultLifecycleTimeoutS = 0.75;

  explicit PanelGestureMapper(double lifecycle_timeout_s = kDefaultLifecycleTimeoutS)
  : lifecycle_timeout_s_(lifecycle_timeout_s) {}

  /// @brief Classifies @p event and, when it is usable, fills @p sample.
  ///
  /// TAP and DRAG become PointerGesture::Click, HOVER becomes Hover and LONG_PRESS becomes
  /// LongPress; the phase is copied one to one. A gesture with a lifecycle (any phase but
  /// Instant) is remembered until it ends or is cancelled, and the liveness sweep cancels one that
  /// goes silent.
  /// @param now_s Seconds on one monotonic clock; the sample carries it as its timestamp.
  GestureOutcome offer(
    const PanelGestureEvent & event, const GestureSlot & slot, double now_s,
    PanelPointerSample & sample);

  /// @brief Appends one Canceled sample for every remembered gesture that has been silent for
  /// the lifecycle timeout, and forgets it. Nothing happens when the timeout is zero or less.
  void sweep_lifecycles(double now_s, std::vector<PanelPointerSample> & out);

  /// @brief Appends one Canceled sample for every remembered gesture of @p panel_key (of every
  /// panel when @p panel_key is empty), and forgets it. For a panel that is about to lose its
  /// subscription: a widget left mid-press never gets its release otherwise.
  void cancel_lifecycles_for_panel(
    const std::string & panel_key, double now_s, std::vector<PanelPointerSample> & out);

  /// @brief Number of gestures with a lifecycle that are remembered.
  std::size_t live_count() const { return live_.size(); }

  const GestureStats & stats() const { return stats_; }

  void set_lifecycle_timeout(double seconds) { lifecycle_timeout_s_ = seconds; }
  double lifecycle_timeout() const { return lifecycle_timeout_s_; }

  /// @brief The pointer id of a (peer, hand) pair: an FNV-1a hash of the peer id bytes and the
  /// hand, kept clear of 0 (the manager's implicit room pointer) and 1 (a single shared panel
  /// pointer). Two hands of one peer, and the hands of two peers, are different pointers.
  static std::uint32_t pointer_id_for(const PeerId & peer, PointerHand hand);

  /// @brief Content pixel to (u, v), both at the CENTRE of the pixel, so that `floor(u * width)`
  /// gives the same pixel back. A pixel outside the content is moved to its edge (negative
  /// ones too) and @p out_clamped is set.
  /// @return false when @p width or @p height is zero.
  static bool uv_from_content_pixels(
    std::int32_t x_pixels, std::int32_t y_pixels, std::uint32_t width, std::uint32_t height,
    float & out_u, float & out_v, bool & out_clamped);

  /// @brief The pointer gesture a panel gesture kind stands for. False for Unknown.
  static bool pointer_gesture_from_kind(PanelGestureKind kind, PointerGesture & out);

  /// @brief The pointer phase a panel gesture phase stands for. False for Unknown.
  static bool pointer_phase_from_phase(PanelGesturePhase phase, PointerPhase & out);

private:
  struct LifecycleKey
  {
    std::string peer;  ///< The bytes of the peer id.
    std::uint64_t gesture_id = 0;
    int kind = 0;

    bool operator<(const LifecycleKey & other) const
    {
      if (peer != other.peer) {
        return peer < other.peer;
      }
      if (gesture_id != other.gesture_id) {
        return gesture_id < other.gesture_id;
      }
      return kind < other.kind;
    }
  };

  struct LiveLifecycle
  {
    std::uint32_t pointer_id = 0;
    std::string panel_key;
    PointerGesture gesture = PointerGesture::Hover;
    PointerHand hand = PointerHand::Unknown;
    float u = 0.0f;
    float v = 0.0f;
    double last_seen_s = 0.0;
  };

  static PanelPointerSample cancel_sample(const LiveLifecycle & live, double now_s);

  GestureStats stats_{};
  std::map<LifecycleKey, LiveLifecycle> live_;
  double lifecycle_timeout_s_ = kDefaultLifecycleTimeoutS;
};

}  // namespace qar_qt_streaming::seam

#endif  // QAR_QT_STREAMING__SEAM__PANEL_GESTURE_MAPPER_HPP_
