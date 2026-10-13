// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/latest_video_frame.hpp"

#include "qar_ar_core/timing_log.hpp"

#include <utility>

namespace qar_qt_streaming
{

void
LatestVideoFrame::publish(QImage frame)
{
  publish(std::move(frame), VideoFrameTrace{});
}

void
LatestVideoFrame::publish(QImage frame, VideoFrameTrace trace)
{
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mutex_);

  if (has_last_publish_) {
    const double dt = std::chrono::duration<double>(now - last_publish_time_).count();
    if (dt > 0.0) {
      const double instantaneous = 1.0 / dt;
      // Exponential moving average: a single stalled or bursty interval must
      // not make the reported rate meaningless.
      constexpr double kAlpha = 0.2;
      fps_ = (fps_ > 0.0) ? (kAlpha * instantaneous + (1.0 - kAlpha) * fps_) : instantaneous;
    }
  }
  last_publish_time_ = now;
  has_last_publish_ = true;

  latest_ = std::make_shared<const QImage>(std::move(frame));
  if (trace.valid) {
    trace.mailbox_us = qar_ar_core::timing_epoch_us();  // visible to readers from here on
  }
  latest_trace_ = trace;
  ++sequence_;
  // A frame just arrived, which is itself evidence that whatever the status
  // text described ("waiting for...", a previous decode error) has ended.
  status_.clear();
}

std::shared_ptr<const QImage>
LatestVideoFrame::latest() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_;
}

VideoFrameTrace
LatestVideoFrame::latest_trace() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_trace_;
}

std::uint64_t
LatestVideoFrame::sequence() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return sequence_;
}

double
LatestVideoFrame::fps() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return fps_;
}

void
LatestVideoFrame::set_status(std::string text)
{
  std::lock_guard<std::mutex> lock(mutex_);
  status_ = std::move(text);
}

std::string
LatestVideoFrame::status() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

}  // namespace qar_qt_streaming
