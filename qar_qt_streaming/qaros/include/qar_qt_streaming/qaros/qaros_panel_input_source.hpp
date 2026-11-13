// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QAROS__QAROS_PANEL_INPUT_SOURCE_HPP_
#define QAR_QT_STREAMING__QAROS__QAROS_PANEL_INPUT_SOURCE_HPP_

/// @file qaros_panel_input_source.hpp
/// @brief The pointer input of the panels over the QarOS C++ SDK: one gesture subscription per
/// panel, a locked queue the SDK's threads fill, and the pull-based drain the Qt side calls.
///
/// ## Threads
///
/// The callbacks of the SDK run on a library thread. They read the gesture off the handle they
/// were given, hand it to the @ref seam::PanelGestureMapper under one lock and push the sample into
/// a queue; they do no I/O, call no SDK function and touch no Qt object. `drain` and
/// `drain_closed_panels` run on the Qt thread. `subscribe_panel` and `unsubscribe_panel` call the
/// SDK and may wait, so they belong to the thread that owns the session (the factory's worker); a
/// callback never ends a subscription.

#include "qar_qt_streaming/qaros/sdk_config_check.hpp"
#include "qar_qt_streaming/seam/panel_gesture_mapper.hpp"
#include "qar_qt_streaming/seam/panel_types.hpp"
#include "qar_qt_streaming/seam/pointer_input.hpp"

#include <qar_streaming_cpp/GuiPanels.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace qar_qt_streaming::qaros_backend
{

struct InputSourceOptions
{
  /// How long a started or updated gesture may be silent before it is cancelled.
  double lifecycle_timeout_s = seam::PanelGestureMapper::kDefaultLifecycleTimeoutS;
  /// Monotonic seconds. Unset: the steady clock. A test replaces it.
  std::function<double()> clock;
};

class QarosPanelInputSource final : public seam::IPanelInputSource
{
public:
  explicit QarosPanelInputSource(InputSourceOptions options = {});
  ~QarosPanelInputSource() override;

  QarosPanelInputSource(const QarosPanelInputSource &) = delete;
  QarosPanelInputSource & operator=(const QarosPanelInputSource &) = delete;

  /// @brief Where the source reports what it sees (the first events, every 50th). Called on the
  /// Qt thread only, from `drain`. Unset: nothing is reported. The factory sets it from its
  /// options.
  void set_diagnostics(seam::PanelDiagnosticsSink * sink);

  /// @brief Called by `drain` on the Qt thread, so that the messages of the worker (a close that
  /// ran after its sender was dropped, say) reach the sink while any panel is streaming. The
  /// factory sets it.
  void set_message_flush(std::function<void()> flush);

  /// @brief Subscribes to every gesture of the panel @p id and reports them against @p panel_key.
  /// A subscription the key already has is ended first.
  /// @param content_width,content_height The extent of the content the gestures' pixels are in.
  /// @param error The reason, when false is returned.
  /// May wait: call it on the thread that owns the session.
  bool subscribe_panel(
    const qaros::GuiPanelControl & panels, const std::string & panel_key,
    const qaros::GuiPanelId & id, std::uint32_t content_width, std::uint32_t content_height,
    std::string & error);

  /// @brief Ends the subscription of @p panel_key, which waits for a running callback, and lets
  /// go of every gesture of the panel that was still held down. May wait; never call it inside a
  /// callback.
  void unsubscribe_panel(const std::string & panel_key);

  /// @brief unsubscribe_panel for every panel.
  void unsubscribe_all();

  /// @brief Whether a subscription for @p panel_key exists. Any thread.
  bool has_panel(const std::string & panel_key) const;

  /// @brief The operator closed the panel @p panel_key in AR; reported once per subscription.
  /// Any thread (the sender's update callback calls it).
  void report_closed(const std::string & panel_key);

  void drain(std::vector<seam::PanelPointerSample> & out) override;
  void drain_closed_panels(std::vector<std::string> & out) override;

  std::size_t subscription_count() const;
  seam::GestureStats stats() const;
  void set_lifecycle_timeout(double seconds);

private:
  struct TraceRecord
  {
    std::uint64_t index = 0;
    std::string panel_key;
    seam::PanelGestureKind kind = seam::PanelGestureKind::Hover;
    seam::PanelGesturePhase phase = seam::PanelGesturePhase::Instant;
    std::int32_t x_pixels = 0;
    std::int32_t y_pixels = 0;
    std::uint64_t gesture_id = 0;
    seam::PointerHand hand = seam::PointerHand::Unknown;
    std::uint8_t tap_count = 0;
    seam::GestureOutcome outcome = seam::GestureOutcome::Sampled;
    float u = 0.0f;
    float v = 0.0f;
  };

  void on_gesture(const seam::GestureSlot & slot, const qaros::Result<qaros::GuiPanelGesture> & g);
  double now_s() const;

  const std::function<double()> clock_;
  std::function<void()> flush_messages_;  // the Qt thread only
  seam::QueuedPanelInputSource queue_;

  mutable std::mutex mutex_;  ///< Guards the members below.
  /// Written on the Qt thread under the lock; the callbacks read it under the lock.
  seam::PanelDiagnosticsSink * sink_ = nullptr;
  seam::PanelGestureMapper mapper_;
  std::map<std::string, qaros::Subscription> panels_;
  std::set<std::string> closed_;
  std::vector<TraceRecord> pending_trace_;
};

}  // namespace qar_qt_streaming::qaros_backend

#endif  // QAR_QT_STREAMING__QAROS__QAROS_PANEL_INPUT_SOURCE_HPP_
