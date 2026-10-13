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

  /// @brief Make the next call to the named method fail.
  void fail_next_begin() { fail_begin_ = true; }
  void fail_next_submit() { fail_submit_ = true; }
  /// @brief Make the close_panel step of the next close() fail, as the runtime's
  /// `qar_gui_panels_close_panel` returning an error would.
  void fail_next_close_panel() { fail_close_panel_ = true; }

  bool try_begin_frame(bool & out_ready) override
  {
    ++begin_calls_;
    if (fail_begin_) {
      fail_begin_ = false;
      last_error_ = "mock begin failure";
      return false;
    }
    out_ready = ready_;
    return true;
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
    if (closed_) {
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

private:
  qar_ar_core::PanelDesc desc_;
  std::shared_ptr<std::vector<std::string>> events_;
  qar_ar_core::PanelPlacement placement_{};
  std::vector<RecordedPanelFrame> frames_;
  std::vector<std::string> steps_;
  std::string last_error_;
  std::string stream_id_ = "mock-stream";
  bool ready_ = true;
  bool fail_begin_ = false;
  bool fail_submit_ = false;
  bool fail_close_panel_ = false;
  bool closed_ = false;
  bool last_close_panel_closed_in_ar_ = false;
  std::uint32_t begin_calls_ = 0;
  std::uint32_t submit_calls_ = 0;
  std::uint32_t placement_calls_ = 0;
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
    // Distinct per sender, like the runtime's: a rebuild mints a new stream,
    // and a test that asserts on a retirement line needs to tell them apart.
    adapter->set_stream_id("mock-stream-" + std::to_string(create_calls_));
    if (senders_start_not_ready_) {
      adapter->set_ready(false);
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
