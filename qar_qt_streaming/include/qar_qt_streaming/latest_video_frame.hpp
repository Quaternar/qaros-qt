// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__LATEST_VIDEO_FRAME_HPP_
#define QAR_QT_STREAMING__LATEST_VIDEO_FRAME_HPP_

/// @file latest_video_frame.hpp
/// @brief The single-slot mailbox between a decode worker and the GUI thread.
///
/// ## The threading model this exists for
///
/// The message callback of the frame source stores only the newest message and
/// decodes nothing; a WORKER thread decodes it with video_frame.hpp's decoders
/// and calls @ref LatestVideoFrame::publish; the
/// GUI-thread capture tick (`VideoPanelWidget::paintEvent`, or
/// `QtWidgetCapture`'s region substitution) only ever calls
/// @ref LatestVideoFrame::latest. Decoding a 1080p frame is not free, and
/// qt_widget_capture.hpp's whole design (docs/ARCHITECTURE.md#thread-model)
/// depends on the GUI thread never blocking — so decode must happen off it,
/// and this class is the one thing both threads touch.
///
/// `QImage` itself is implicitly shared (copy-on-write) but is NOT thread-safe
/// across a mutation on one thread while another reads it — so what crosses
/// here is a `std::shared_ptr<const QImage>`: the worker builds a new QImage,
/// wraps it read-only and publishes the pointer; a reader takes a copy of the
/// `shared_ptr` (cheap, atomic refcount) and then only ever reads through it,
/// so nothing is ever mutated while shared.

#include <QImage>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace qar_qt_streaming
{

/// @brief Where one frame was, and when, on its way from the source's message callback to
/// the mailbox.
///
/// Only filled while per-frame timing is on (QAR_VIDEO_TIMING_DIR, timing_log.hpp); a
/// default-constructed trace (`valid == false`) means "not recorded". Every timestamp is
/// `timing_epoch_us()` - microseconds since the Unix epoch on the system clock - except
/// `header_stamp_us`, which is the message's own `header.stamp` converted to the same unit
/// (the publisher stamps it from the same clock, so `arrival_us - header_stamp_us` is the
/// publisher-to-subscriber latency).
struct VideoFrameTrace
{
  bool valid = false;
  /// 1-based count of messages the source's callback had received when this one came
  /// (decode failures and messages dropped before decoding consume numbers too).
  std::uint64_t seq = 0;
  std::int64_t header_stamp_us = 0;
  /// Entry of the source's message callback.
  std::int64_t arrival_us = 0;
  std::int64_t decode_start_us = 0;
  std::int64_t decode_done_us = 0;
  /// The frame became visible to readers: stamped by @ref LatestVideoFrame::publish inside
  /// its critical section.
  std::int64_t mailbox_us = 0;
};

/// @brief Thread-safe single-slot "latest frame" mailbox.
///
/// Every accessor takes the same mutex; there is one slot, so a publish
/// simply replaces whatever was there. Cheap enough at video-frame rates
/// (tens of Hz) that a lock-free structure would be solving a problem that
/// does not exist here.
class LatestVideoFrame
{
public:
  LatestVideoFrame() = default;

  LatestVideoFrame(const LatestVideoFrame &) = delete;
  LatestVideoFrame & operator=(const LatestVideoFrame &) = delete;

  /// @brief Publish a newly decoded frame. Safe from any thread; typically the
  /// decode worker.
  ///
  /// Increments @ref sequence and updates the @ref fps estimate from the wall
  /// time between this call and the previous one (an exponential moving
  /// average, so one stalled tick does not make the reported rate
  /// meaningless). Clears @ref status: a frame arriving is itself evidence
  /// that whatever the status text described has ended.
  void publish(QImage frame);

  /// @brief As @ref publish, also recording @p trace as the trace of the new latest frame.
  /// @p trace.mailbox_us is stamped here, inside the critical section, at the moment the
  /// frame becomes visible; a trace that is not `valid` is stored as "none".
  void publish(QImage frame, VideoFrameTrace trace);

  /// @brief The trace stored with the latest frame (`valid == false` when none was given).
  VideoFrameTrace latest_trace() const;

  /// @brief The most recently published frame, or null if @ref publish has
  /// never been called. Safe from any thread.
  std::shared_ptr<const QImage> latest() const;

  /// @brief How many frames @ref publish has accepted. Monotonically
  /// increasing; never wraps back to a smaller value.
  std::uint64_t sequence() const;

  /// @brief The current smoothed publish rate, in frames per second. 0 before
  /// a second frame arrives (one sample alone has no interval to measure).
  double fps() const;

  /// @brief Set the human-readable status text a reader shows in place of a
  /// frame — e.g. "Waiting for video on /cam/image_raw…" or a decode error.
  /// Empty means healthy (a frame is expected imminently or already showing).
  void set_status(std::string text);
  std::string status() const;

private:
  mutable std::mutex mutex_;
  std::shared_ptr<const QImage> latest_;
  VideoFrameTrace latest_trace_{};
  std::uint64_t sequence_ = 0;
  std::string status_;

  std::chrono::steady_clock::time_point last_publish_time_{};
  bool has_last_publish_ = false;
  double fps_ = 0.0;
};

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__LATEST_VIDEO_FRAME_HPP_
