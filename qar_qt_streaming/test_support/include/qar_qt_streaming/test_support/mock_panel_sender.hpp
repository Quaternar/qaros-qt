// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__TEST_SUPPORT__MOCK_PANEL_SENDER_HPP_
#define QAR_QT_STREAMING__TEST_SUPPORT__MOCK_PANEL_SENDER_HPP_

/// @file mock_panel_sender.hpp
/// @brief Recording stand-ins for the QarOS panel seam.
///
/// Header-only so tests (and a standalone Qt sample) can drive the
/// whole capture path with no QarOS runtime. Mirrors qar_ogre_streaming's
/// test_support::MockQarRenderSenderAdapter.

#include "qar_qt_streaming/clock.hpp"
#include "qar_ar_core/qar_panel_sender.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace qar_qt_streaming
{
namespace test_support
{

/// @brief One frame the adapter accepted, with its pixels copied out.
struct RecordedPanelFrame
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;  ///< Tightly packed, stride removed.
};

/// @brief Records everything the library asks of a panel sender.
class MockQarPanelSenderAdapter : public qar_ar_core::IQarPanelSenderAdapter
{
public:
  /// @param events Optional log SHARED across every adapter one factory mints, so
  ///        a test can assert the order of closes and creates across senders
  ///        ("the old sender was closed before the new one was created").
  explicit MockQarPanelSenderAdapter(
    qar_ar_core::PanelDesc desc, std::shared_ptr<std::vector<std::string>> events = nullptr)
  : desc_(std::move(desc)),
    events_(std::move(events))
  {
  }

  /// @brief What `try_begin_frame` reports. Flip it to simulate a receiver that
  /// has not reserved a destination yet.
  void set_ready(bool ready) { ready_ = ready; }

  /// @brief The cause `not_ready_cause()` names while the adapter is not ready, as the real
  /// adapter's 604 (no active transfers) and 603 (reconnecting) answers do.
  void set_not_ready_cause(qar_ar_core::PanelNotReadyCause cause) { not_ready_cause_ = cause; }

  /// @brief Whether the adapter is still being made, like the real one while its panel and video
  /// sender are made on the session's thread: pending() is true and try_begin_frame() reports
  /// "not ready" with PanelNotReadyCause::Creating.
  void set_pending(bool pending) { pending_ = pending; }

  /// @brief Queues the outcome of a call, which the next drain_results() hands to the stream. The
  /// calls themselves are accepted at once, as the real adapter accepts them.
  void queue_result(qar_ar_core::PanelOpResult result) { results_.push_back(std::move(result)); }
  /// @brief How often drain_results() was called.
  std::uint32_t drain_calls() const { return drain_calls_; }

  /// @brief Make the next call to the named method fail.
  void fail_next_begin() { fail_begin_ = true; }
  void fail_next_submit() { fail_submit_ = true; }
  /// @brief Make the close_panel step of the next close() fail, as the runtime's
  /// `qar_gui_panels_close_panel` returning an error would.
  void fail_next_close_panel() { fail_close_panel_ = true; }
  /// @brief Make the next @p times calls of set_state() fail, as the runtime's
  /// `qar_gui_panels_set_state` returning an error would.
  void fail_next_set_state(std::uint32_t times = 1) { fail_set_state_ = times; }
  /// @brief Whether supports_state() says the backend can hide and show a panel. True, like the
  /// real adapter; false models one that cannot, and the stream then never calls set_state().
  void set_supports_state(bool supported) { supports_state_ = supported; }
  /// @brief Whether supports_detach() says the backend leaves the QarOS panel in place on
  /// detach(). False like a sender that closes, so a test that does not ask for it sees the
  /// close/keep behaviour the library had before.
  void set_supports_detach(bool supported) { supports_detach_ = supported; }
  /// @brief What reattached() reports: the factory found a panel of this name and took it over.
  void set_reattached(bool reattached) { reattached_ = reattached; }

  bool try_begin_frame(bool & out_ready) override
  {
    ++begin_calls_;
    if (fail_begin_) {
      fail_begin_ = false;
      last_error_ = "mock begin failure";
      return false;
    }
    out_ready = ready_ && !pending_;
    return true;
  }

  qar_ar_core::PanelNotReadyCause not_ready_cause() const override
  {
    return pending_ ? qar_ar_core::PanelNotReadyCause::Creating : not_ready_cause_;
  }

  bool pending() const override { return pending_; }

  void drain_results(std::vector<qar_ar_core::PanelOpResult> & out) override
  {
    ++drain_calls_;
    for (qar_ar_core::PanelOpResult & result : results_) {
      out.push_back(std::move(result));
    }
    results_.clear();
  }

  bool submit_frame(const qar_ar_core::PanelPixels & pixels) override
  {
    ++submit_calls_;
    if (fail_submit_) {
      fail_submit_ = false;
      last_error_ = "mock submit failure";
      return false;
    }
    RecordedPanelFrame frame;
    frame.width = pixels.width;
    frame.height = pixels.height;
    frame.rgba.resize(static_cast<std::size_t>(pixels.width) * pixels.height * 4u);
    for (std::uint32_t y = 0; y < pixels.height; ++y) {
      const std::uint8_t * row = pixels.data + static_cast<std::size_t>(y) * pixels.stride_bytes;
      std::copy(row, row + pixels.width * 4u, frame.rgba.begin() + static_cast<std::size_t>(y) * pixels.width * 4u);
    }
    frames_.push_back(std::move(frame));
    last_error_.clear();
    return true;
  }

  bool set_placement(const qar_ar_core::PanelPlacement & placement) override
  {
    placement_ = placement;
    ++placement_calls_;
    return true;
  }

  /// Records every state it is asked for, refused ones included.
  bool set_state(QarGuiPanelState state) override
  {
    ++state_calls_;
    states_.push_back(state);
    if (!supports_state_) {
      last_error_ = "this recording adapter has no panel state";
      return false;
    }
    if (fail_set_state_ > 0) {
      --fail_set_state_;
      last_error_ = "injected set_state failure";
      return false;
    }
    last_state_ = state;
    last_error_.clear();
    return true;
  }

  bool supports_state() const override { return supports_state_; }

  /// Models the real adapter's three steps in the real adapter's order, and
  /// records them, so a manager-level test can say "the panel was closed, the
  /// sender destroyed, each exactly once" without a QarOS runtime. The ORDER of
  /// the real thing has no automated test: it needs the real SDK and is checked
  /// by running the adapter against it.
  using qar_ar_core::IQarPanelSenderAdapter::close;
  qar_ar_core::PanelCloseReport close(bool panel_closed_in_ar) override
  {
    ++close_calls_;
    last_close_panel_closed_in_ar_ = panel_closed_in_ar;
    qar_ar_core::PanelCloseReport report;
    if (closed_ || detached_) {
      return report;  // idempotent, like the real one
    }
    closed_ = true;

    steps_.push_back("unsubscribe_input");
    report.input_unsubscribed = true;

    if (panel_closed_in_ar) {
      steps_.push_back("close_panel_skipped");
      ++close_panel_skipped_;
      report.panel = qar_ar_core::PanelCloseOutcome::AlreadyClosedInAr;
    } else {
      steps_.push_back("close_panel");
      ++close_panel_calls_;
      if (fail_close_panel_) {
        fail_close_panel_ = false;
        report.panel = qar_ar_core::PanelCloseOutcome::Failed;
        report.panel_message = "mock close_panel failure";
        last_error_ = report.panel_message;
      } else {
        report.panel = qar_ar_core::PanelCloseOutcome::Closed;
      }
    }

    steps_.push_back("destroy_sender");
    ++sender_destroy_calls_;
    report.sender_destroyed = true;

    if (events_) {
      events_->push_back("close " + desc_.key);
    }
    return report;
  }

  /// Records the call, and the event "detach <key>" in the factory's shared log. The panel is
  /// left as it is: nothing is hidden or closed. Idempotent; the adapter is finished after it.
  void detach() override
  {
    ++detach_calls_;
    if (detached_ || closed_) {
      return;
    }
    detached_ = true;
    steps_.push_back("detach");
    if (events_) {
      events_->push_back("detach " + desc_.key);
    }
  }

  bool supports_detach() const override { return supports_detach_; }

  const std::string & last_error() const override { return last_error_; }

  std::string stream_id_text() const override { return stream_id_; }

  /// @brief Stand in for the UUID the real runtime mints per sender. The
  /// factory gives every adapter a distinct one, so a test can model a QarOS
  /// that asks for a named stream nobody serves.
  void set_stream_id(std::string id) { stream_id_ = std::move(id); }

  const qar_ar_core::PanelDesc & desc() const { return desc_; }
  const qar_ar_core::PanelPlacement & placement() const { return placement_; }
  const std::vector<RecordedPanelFrame> & frames() const { return frames_; }
  std::uint32_t begin_calls() const { return begin_calls_; }
  std::uint32_t submit_calls() const { return submit_calls_; }
  std::uint32_t placement_calls() const { return placement_calls_; }
  /// Every call to set_state(), refused ones included, and the states asked for in order.
  std::uint32_t state_calls() const { return state_calls_; }
  const std::vector<QarGuiPanelState> & states() const { return states_; }
  /// The state of the panel as this adapter holds it: the last one set_state() accepted,
  /// VISIBLE before any.
  QarGuiPanelState last_state() const { return last_state_; }
  /// Every call to close(), idempotent repeats included.
  std::uint32_t close_calls() const { return close_calls_; }
  /// What close() actually did, once: the steps it ran, in order, and how often
  /// the two C-API calls it models were made. `close_panel_calls` is the one that
  /// must stay 0 for a panel the operator had already closed in AR.
  const std::vector<std::string> & close_steps() const { return steps_; }
  std::uint32_t close_panel_calls() const { return close_panel_calls_; }
  std::uint32_t close_panel_skipped() const { return close_panel_skipped_; }
  std::uint32_t sender_destroy_calls() const { return sender_destroy_calls_; }
  /// The flag the most recent close() was given.
  bool last_close_panel_closed_in_ar() const { return last_close_panel_closed_in_ar_; }
  /// Every call to detach(), idempotent repeats included.
  std::uint32_t detach_calls() const { return detach_calls_; }
  bool detached() const { return detached_; }
  /// True while the adapter holds its QarOS panel: neither closed nor detached.
  bool live() const { return !closed_ && !detached_; }
  /// What the real adapter reports after its probe: the panel of this name existed already.
  bool reattached() const { return reattached_; }
  /// The common name the library gave the panel (PanelDesc::common_name).
  const std::string & common_name() const { return desc_.common_name; }

private:
  qar_ar_core::PanelDesc desc_;
  std::shared_ptr<std::vector<std::string>> events_;
  qar_ar_core::PanelPlacement placement_{};
  std::vector<RecordedPanelFrame> frames_;
  std::vector<std::string> steps_;
  std::string last_error_;
  std::string stream_id_ = "mock-stream";
  qar_ar_core::PanelNotReadyCause not_ready_cause_ = qar_ar_core::PanelNotReadyCause::NoDestination;
  bool ready_ = true;
  bool pending_ = false;
  std::vector<qar_ar_core::PanelOpResult> results_;
  std::uint32_t drain_calls_ = 0;
  bool fail_begin_ = false;
  bool fail_submit_ = false;
  bool fail_close_panel_ = false;
  std::uint32_t fail_set_state_ = 0;
  bool supports_state_ = true;
  bool supports_detach_ = false;
  bool reattached_ = false;
  bool detached_ = false;
  std::uint32_t detach_calls_ = 0;
  bool closed_ = false;
  bool last_close_panel_closed_in_ar_ = false;
  QarGuiPanelState last_state_ = QAR_GUI_PANEL_STATE_VISIBLE;
  std::vector<QarGuiPanelState> states_;
  std::uint32_t begin_calls_ = 0;
  std::uint32_t submit_calls_ = 0;
  std::uint32_t placement_calls_ = 0;
  std::uint32_t state_calls_ = 0;
  std::uint32_t close_calls_ = 0;
  std::uint32_t close_panel_calls_ = 0;
  std::uint32_t close_panel_skipped_ = 0;
  std::uint32_t sender_destroy_calls_ = 0;
};

/// @brief Hands out MockQarPanelSenderAdapters and keeps them alive for asserts.
class MockQarPanelSenderFactory : public qar_ar_core::IQarPanelSenderFactory
{
public:
  /// @brief Hand out senders that report "no destination reserved" forever —
  /// the shape of a panel QarOS has been promised a stream for and is never
  /// served (the runtime reports it as MEDIA_STREAMING_NO_ACTIVE_TRANSFERS).
  void set_senders_start_not_ready(bool not_ready) { senders_start_not_ready_ = not_ready; }

  /// @brief Hand out senders whose supports_detach() is @p detach, like the real sender (true).
  /// A name that an adapter of this factory holds live (not closed, not detached) when another
  /// one is made for it counts as a collision (see @ref live_name_collisions); a name that a
  /// finished adapter held makes the new adapter report reattached(), as the real probe does.
  void set_senders_support_detach(bool detach) { senders_detach_ = detach; }

  /// @brief Hand out senders that are still being made, like the real one while its panel and
  /// video sender are made on the session's thread: pending() is true and the stream id is empty
  /// until the test lets them finish (MockQarPanelSenderAdapter::set_pending, set_stream_id and a
  /// queued Create result).
  void set_senders_start_pending(bool pending) { senders_start_pending_ = pending; }

  /// @brief Make every subsequent create_panel_sender() return null.
  void set_fails(bool fails, std::string reason = "mock factory refuses")
  {
    fails_ = fails;
    failure_reason_ = std::move(reason);
  }

  std::shared_ptr<qar_ar_core::IQarPanelSenderAdapter> create_panel_sender(const qar_ar_core::PanelDesc & desc) override
  {
    ++create_calls_;
    if (fails_) {
      last_error_ = failure_reason_;
      return nullptr;
    }
    auto adapter = std::make_shared<MockQarPanelSenderAdapter>(desc, events_);
    adapter->set_supports_detach(senders_detach_);
    if (!desc.common_name.empty()) {
      for (const auto & earlier : created_) {
        if (earlier->common_name() != desc.common_name) {
          continue;
        }
        if (earlier->live()) {
          ++live_name_collisions_;  // two senders on ONE QarOS panel
        } else {
          adapter->set_reattached(true);
        }
      }
    }
    // Distinct per sender, like the runtime's: a rebuild mints a new stream,
    // and a test that asserts on a retirement line needs to tell them apart.
    adapter->set_stream_id("mock-stream-" + std::to_string(create_calls_));
    if (senders_start_not_ready_) {
      adapter->set_ready(false);
    }
    if (senders_start_pending_) {
      adapter->set_pending(true);
      adapter->set_stream_id(std::string());
    }
    created_.push_back(adapter);
    events_->push_back("create " + desc.key);
    last_error_.clear();
    return adapter;
  }

  const std::string & last_error() const override { return last_error_; }

  /// @brief Every create and every completed close across every sender this
  /// factory minted, in the order they happened: "create <key>" / "close <key>".
  const std::vector<std::string> & events() const { return *events_; }

  std::uint32_t create_calls() const { return create_calls_; }
  /// @brief How often an adapter was made for a common name that another adapter of this factory
  /// still held (neither closed nor detached): both would point at one QarOS panel. Must stay 0.
  std::uint32_t live_name_collisions() const { return live_name_collisions_; }
  const std::vector<std::shared_ptr<MockQarPanelSenderAdapter>> & created() const
  {
    return created_;
  }
  /// @brief The most recently created adapter, or null if none.
  std::shared_ptr<MockQarPanelSenderAdapter> latest() const
  {
    return created_.empty() ? nullptr : created_.back();
  }

private:
  std::vector<std::shared_ptr<MockQarPanelSenderAdapter>> created_;
  std::shared_ptr<std::vector<std::string>> events_ =
    std::make_shared<std::vector<std::string>>();
  std::string last_error_;
  std::string failure_reason_ = "mock factory refuses";
  bool fails_ = false;
  bool senders_start_not_ready_ = false;
  bool senders_detach_ = false;
  bool senders_start_pending_ = false;
  std::uint32_t live_name_collisions_ = 0;
  std::uint32_t create_calls_ = 0;
};

/// @brief Records resolved AR interactions.
class RecordingPanelInputSink : public IQarPanelInputSink
{
public:
  void on_panel_pointer(const PanelPointerEvent & event) override { events_.push_back(event); }
  const std::vector<PanelPointerEvent> & events() const { return events_; }

private:
  std::vector<PanelPointerEvent> events_;
};

/// @brief IClock a test advances by hand.
class ManualClock : public IClock
{
public:
  double now_seconds() const override { return now_; }
  void advance(double seconds) { now_ += seconds; }
  void set(double seconds) { now_ = seconds; }

private:
  double now_ = 0.0;
};

}  // namespace test_support
}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__TEST_SUPPORT__MOCK_PANEL_SENDER_HPP_
