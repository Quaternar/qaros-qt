// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Two helpers the factory, the senders and the input source share: the queue of diagnostic
// messages that the Qt thread hands to the sink, and the executor that runs the tasks which can
// wait on the runtime.

#ifndef QAR_QT_STREAMING__QAROS__BACKEND_DETAIL_HPP_
#define QAR_QT_STREAMING__QAROS__BACKEND_DETAIL_HPP_

#include "qar_qt_streaming/seam/panel_types.hpp"

#include <qar_streaming_cpp/Session.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace qar_qt_streaming::qaros_backend::detail
{

/// "<what> failed: code=<n> msg=<message>", the form of the error text a panel keeps.
inline std::string
failure_text(const char * what, const qaros::Status & status)
{
  return std::string(what) + " failed: code=" +
         std::to_string(static_cast<long long>(status.Code())) + " msg=" + status.Message();
}

/// Messages from any thread, delivered to the sink by flush() on the Qt thread. The sink is called
/// there and nowhere else, as its contract says.
class DiagnosticsHub
{
public:
  explicit DiagnosticsHub(seam::PanelDiagnosticsSink * sink)
  : sink_(sink) {}

  /// Any thread. Dropped when there is no sink; the queue is bounded.
  void post(
    seam::DiagnosticSeverity severity, const std::string & category, const std::string & message,
    const std::string & panel_key)
  {
    if (sink_ == nullptr) {
      return;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= kMaxQueued) {
      ++dropped_;
      return;
    }
    queue_.push_back(Message{severity, category, message, panel_key});
  }

  /// The Qt thread only.
  void flush()
  {
    if (sink_ == nullptr) {
      return;
    }
    std::deque<Message> taken;
    std::uint64_t dropped = 0;
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      taken.swap(queue_);
      dropped = dropped_;
      dropped_ = 0;
    }
    for (const Message & message : taken) {
      sink_->on_message(message.severity, message.category, message.text, message.panel_key);
    }
    if (dropped != 0) {
      sink_->on_message(
        seam::DiagnosticSeverity::Warning, "qar-panel-adapter",
        std::to_string(static_cast<unsigned long long>(dropped)) +
        " messages were dropped because the sink was not drained", std::string());
    }
  }

private:
  struct Message
  {
    seam::DiagnosticSeverity severity;
    std::string category;
    std::string text;
    std::string panel_key;
  };

  static constexpr std::size_t kMaxQueued = 512;

  seam::PanelDiagnosticsSink * sink_;
  std::mutex mutex_;
  std::deque<Message> queue_;
  std::uint64_t dropped_ = 0;
};

/// Runs tasks one after the other, in the order they were posted, off the Qt thread, each with the
/// session. Either on a worker thread of its own, or on the application's executor.
class SessionExecutor
{
public:
  using Task = std::function<void(qaros::Session &)>;
  using Post = std::function<bool(const char *, std::function<void()>)>;

  /// @param post The application's executor, or empty for a worker thread of the executor's own.
  SessionExecutor(qaros::Session session, Post post, std::chrono::milliseconds budget);
  ~SessionExecutor();

  SessionExecutor(const SessionExecutor &) = delete;
  SessionExecutor & operator=(const SessionExecutor &) = delete;

  /// Any thread; returns at once. False when the executor takes no more tasks.
  bool post(const std::string & name, Task task);

  bool accepting() const;

  /// Takes no more tasks, lets the worker run the ones queued and joins it within the budget.
  /// Idempotent. A worker still busy after the budget is abandoned.
  void shutdown();

  bool abandoned() const;

private:
  struct Item
  {
    std::string name;
    Task task;
  };

  /// What the worker thread and the posted tasks share; it outlives an executor that was
  /// abandoned.
  struct State
  {
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable finished;
    std::deque<Item> queue;
    bool stopping = false;
    bool done = false;
    qaros::Session session;
  };

  static void run(const std::shared_ptr<State> & state);

  std::shared_ptr<State> state_;
  Post post_;
  std::chrono::milliseconds budget_;
  std::thread worker_;
  std::atomic<bool> abandoned_{false};
  std::atomic<bool> shut_down_{false};
};

}  // namespace qar_qt_streaming::qaros_backend::detail

#endif  // QAR_QT_STREAMING__QAROS__BACKEND_DETAIL_HPP_
