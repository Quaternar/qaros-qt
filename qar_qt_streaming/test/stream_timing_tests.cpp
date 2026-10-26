// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The opt-in per-frame stage timing (timing_log.hpp): the CSV sink and writer thread, the
// trace a decoded frame carries through LatestVideoFrame, and the row QtPanelStream writes
// for every capture tick of a video panel. The analyser (analyze_video_timing.py in
// qar_rviz_plugin) reads exactly these columns, so the tests pin the formats.
//
// Include order note: qt_panel_stream_manager.hpp declares a method literally named `emit`,
// which only parses before any real Qt header has defined Qt's `emit` macro (see the note in
// qt_panel_stream.hpp), so the project headers come first and plain Qt headers after.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"
#include "qar_ar_core/timing_log.hpp"
#include "qar_qt_streaming/video_panel_widget.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QWidget>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using qar_ar_core::kTimingBlank;
using qar_qt_streaming::LatestVideoFrame;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStream;
using qar_ar_core::TimingLog;
using qar_ar_core::TimingSink;
using qar_qt_streaming::VideoFrameTrace;
using qar_qt_streaming::VideoPanelWidget;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

namespace fs = std::filesystem;

/// A fresh scratch folder, removed with the object.
class TempDir
{
public:
  TempDir()
  {
    std::random_device rd;
    std::ostringstream name;
    name << "qar_timing_test_" << std::hex << rd() << rd();
    path_ = fs::temp_directory_path() / name.str();
    fs::create_directories(path_);
  }
  ~TempDir()
  {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  TempDir(const TempDir &) = delete;
  TempDir & operator=(const TempDir &) = delete;

  const fs::path & path() const { return path_; }
  std::string str() const { return path_.string(); }

private:
  fs::path path_;
};

std::vector<std::string> read_lines(const fs::path & file)
{
  std::vector<std::string> lines;
  std::ifstream in(file, std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

std::vector<std::string> split_csv(const std::string & line)
{
  std::vector<std::string> cells;
  std::string cell;
  std::istringstream in(line);
  while (std::getline(in, cell, ',')) {
    cells.push_back(cell);
  }
  if (!line.empty() && line.back() == ',') {
    cells.emplace_back();  // a trailing empty field
  }
  return cells;
}

/// One row of a CSV as header-name -> text.
using Row = std::map<std::string, std::string>;

std::vector<Row> read_rows(const fs::path & file)
{
  const std::vector<std::string> lines = read_lines(file);
  std::vector<Row> rows;
  if (lines.empty()) {
    return rows;
  }
  const std::vector<std::string> header = split_csv(lines[0]);
  for (std::size_t i = 1; i < lines.size(); ++i) {
    const std::vector<std::string> cells = split_csv(lines[i]);
    Row row;
    for (std::size_t c = 0; c < header.size(); ++c) {
      row[header[c]] = (c < cells.size()) ? cells[c] : std::string();
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

std::int64_t as_int(const Row & row, const std::string & column)
{
  const auto it = row.find(column);
  EXPECT_NE(it, row.end()) << "no column " << column;
  return (it == row.end() || it->second.empty()) ? kTimingBlank : std::stoll(it->second);
}

/// Makes TimingLog::instance() return @p log for the scope.
class ScopedTimingLog
{
public:
  explicit ScopedTimingLog(TimingLog & log)
  : previous_(TimingLog::override_instance_for_testing(&log))
  {
  }
  ~ScopedTimingLog() { TimingLog::override_instance_for_testing(previous_); }
  ScopedTimingLog(const ScopedTimingLog &) = delete;
  ScopedTimingLog & operator=(const ScopedTimingLog &) = delete;

private:
  TimingLog * previous_;
};

QImage solid_frame(const QColor & colour)
{
  QImage img(64, 36, QImage::Format_RGBA8888);
  img.fill(colour);
  return img;
}

VideoFrameTrace make_trace(std::uint64_t seq)
{
  VideoFrameTrace t;
  t.valid = true;
  t.seq = seq;
  t.header_stamp_us = 1000000 + static_cast<std::int64_t>(seq);
  t.arrival_us = 2000000 + static_cast<std::int64_t>(seq);
  t.decode_start_us = 2000100 + static_cast<std::int64_t>(seq);
  t.decode_done_us = 2000500 + static_cast<std::int64_t>(seq);
  return t;
}

PanelStreamOptions video_options()
{
  PanelStreamOptions options;
  options.display_name = "Video";
  options.max_fps = 30;             // frame-driven capture, paced at 30 fps
  options.keepalive_seconds = 0.0;  // isolate the dirty check from the keepalive
  return options;
}

}  // namespace

// ---------------------------------------------------------------------------
// LatestVideoFrame carries the frame's trace
// ---------------------------------------------------------------------------

TEST(FrameTrace, PublishWithATraceStampsMailboxTimeInsideAndKeepsItWithTheFrame)
{
  LatestVideoFrame mailbox;
  EXPECT_FALSE(mailbox.latest_trace().valid);

  const std::int64_t before = qar_ar_core::timing_epoch_us();
  mailbox.publish(solid_frame(Qt::red), make_trace(7));
  const std::int64_t after = qar_ar_core::timing_epoch_us();

  const VideoFrameTrace trace = mailbox.latest_trace();
  ASSERT_TRUE(trace.valid);
  EXPECT_EQ(trace.seq, 7u);
  EXPECT_EQ(trace.header_stamp_us, make_trace(7).header_stamp_us);
  EXPECT_EQ(trace.decode_done_us, make_trace(7).decode_done_us);
  EXPECT_GE(trace.mailbox_us, before);
  EXPECT_LE(trace.mailbox_us, after);
  EXPECT_EQ(mailbox.sequence(), 1u);
}

TEST(FrameTrace, APlainPublishReplacesTheTraceWithNone)
{
  LatestVideoFrame mailbox;
  mailbox.publish(solid_frame(Qt::red), make_trace(1));
  ASSERT_TRUE(mailbox.latest_trace().valid);
  mailbox.publish(solid_frame(Qt::blue));
  EXPECT_FALSE(mailbox.latest_trace().valid) << "the trace must describe the LATEST frame only";
}

// ---------------------------------------------------------------------------
// QtPanelStream: one row per capture tick of a video panel
// ---------------------------------------------------------------------------

TEST(StreamTiming, ASubmittedVideoFrameWritesOneRowWithItsWholeLife)
{
  TempDir dir;
  TimingLog log(dir.str(), std::chrono::milliseconds(10000));
  ScopedTimingLog scope(log);

  auto source = std::make_shared<LatestVideoFrame>();
  source->publish(solid_frame(Qt::red), make_trace(42));
  VideoPanelWidget widget(source, QStringLiteral("cam"));
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("video:/cam/image_raw", &widget, video_options(), factory);

  ASSERT_TRUE(stream.tick(1.0));
  log.flush_all();

  const fs::path file = dir.path() / "video_cam_image_raw.csv";
  ASSERT_TRUE(fs::exists(file)) << "the AR video panel's key, sanitised, names the file";
  const std::vector<Row> rows = read_rows(file);
  ASSERT_EQ(rows.size(), 1u);
  const Row & row = rows[0];
  EXPECT_EQ(row.at("outcome"), "submitted");
  // The frame's own trace, as the decode worker stored it...
  EXPECT_EQ(as_int(row, "seq"), 42);
  EXPECT_EQ(as_int(row, "header_stamp_us"), make_trace(42).header_stamp_us);
  EXPECT_EQ(as_int(row, "arrival_us"), make_trace(42).arrival_us);
  EXPECT_EQ(as_int(row, "decode_done_us"), make_trace(42).decode_done_us);
  const std::int64_t mailbox = as_int(row, "mailbox_us");
  EXPECT_GT(mailbox, 0);
  EXPECT_EQ(as_int(row, "decode_seq"), 1);
  // ...then the stages this tick added, in order.
  const std::int64_t tick = as_int(row, "tick_us");
  const std::int64_t capture = as_int(row, "capture_us");
  const std::int64_t ready = as_int(row, "ready_us");
  const std::int64_t submit = as_int(row, "submit_us");
  EXPECT_GE(tick, mailbox) << "the frame was visible before the tick that picked it up";
  EXPECT_GE(capture, tick);
  EXPECT_GE(ready, capture);
  EXPECT_GE(submit, ready);
  EXPECT_EQ(as_int(row, "not_ready_count"), kTimingBlank);
  EXPECT_EQ(as_int(row, "not_ready_start_us"), kTimingBlank);
}

TEST(StreamTiming, NotReadyTicksFoldIntoOneRunThatTheNextReadyTickCloses)
{
  TempDir dir;
  TimingLog log(dir.str(), std::chrono::milliseconds(10000));
  ScopedTimingLog scope(log);

  auto source = std::make_shared<LatestVideoFrame>();
  source->publish(solid_frame(Qt::red), make_trace(1));
  VideoPanelWidget widget(source, QStringLiteral("cam"));
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("video:/cam/image_raw", &widget, video_options(), factory);

  ASSERT_TRUE(stream.tick(1.0));                       // submitted, sender created
  factory->latest()->set_ready(false);                 // QarOS has no destination now
  source->publish(solid_frame(Qt::green), make_trace(2));
  EXPECT_FALSE(stream.tick(1.1));
  source->publish(solid_frame(Qt::blue), make_trace(3));
  EXPECT_FALSE(stream.tick(1.2));
  factory->latest()->set_ready(true);
  source->publish(solid_frame(Qt::yellow), make_trace(4));
  EXPECT_TRUE(stream.tick(1.3));
  log.flush_all();

  const std::vector<Row> rows = read_rows(dir.path() / "video_cam_image_raw.csv");
  ASSERT_EQ(rows.size(), 4u);
  EXPECT_EQ(rows[0].at("outcome"), "submitted");
  EXPECT_EQ(rows[1].at("outcome"), "not_ready");
  EXPECT_EQ(rows[2].at("outcome"), "not_ready");
  EXPECT_EQ(rows[3].at("outcome"), "submitted");

  EXPECT_EQ(as_int(rows[1], "not_ready_count"), 1);
  EXPECT_EQ(as_int(rows[2], "not_ready_count"), 2) << "the run grows tick by tick";
  const std::int64_t run_start = as_int(rows[1], "not_ready_start_us");
  EXPECT_EQ(as_int(rows[2], "not_ready_start_us"), run_start);
  EXPECT_EQ(as_int(rows[1], "ready_us"), kTimingBlank);
  EXPECT_EQ(as_int(rows[1], "submit_us"), kTimingBlank);

  // The tick that found QarOS ready closes the run: it carries start, count, and an end that
  // is its own ready_us.
  EXPECT_EQ(as_int(rows[3], "not_ready_count"), 2);
  EXPECT_EQ(as_int(rows[3], "not_ready_start_us"), run_start);
  EXPECT_EQ(as_int(rows[3], "not_ready_end_us"), as_int(rows[3], "ready_us"));
  EXPECT_GE(as_int(rows[3], "not_ready_end_us"), run_start);
  EXPECT_EQ(as_int(rows[3], "seq"), 4);

  // ...and the run is over: the next submitted frame carries none.
  source->publish(solid_frame(Qt::cyan), make_trace(5));
  EXPECT_TRUE(stream.tick(1.4));
  log.flush_all();
  const std::vector<Row> later = read_rows(dir.path() / "video_cam_image_raw.csv");
  ASSERT_EQ(later.size(), 5u);
  EXPECT_EQ(as_int(later[4], "not_ready_count"), kTimingBlank);
}

TEST(StreamTiming, AnUnchangedFrameIsRecordedAsSuchAndAFramelessTraceLeavesBlanks)
{
  TempDir dir;
  TimingLog log(dir.str(), std::chrono::milliseconds(10000));
  ScopedTimingLog scope(log);

  auto source = std::make_shared<LatestVideoFrame>();
  source->publish(solid_frame(Qt::red));   // no trace: timing was off when it was decoded
  VideoPanelWidget widget(source, QStringLiteral("cam"));
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("video:/cam/image_raw", &widget, video_options(), factory);

  ASSERT_TRUE(stream.tick(1.0));
  source->publish(solid_frame(Qt::red));   // a new frame, byte-identical pixels
  EXPECT_FALSE(stream.tick(1.1));
  log.flush_all();

  const std::vector<Row> rows = read_rows(dir.path() / "video_cam_image_raw.csv");
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].at("outcome"), "submitted");
  EXPECT_EQ(as_int(rows[0], "seq"), kTimingBlank) << "no trace: blank, not zero";
  EXPECT_EQ(as_int(rows[0], "arrival_us"), kTimingBlank);
  EXPECT_NE(as_int(rows[0], "tick_us"), kTimingBlank) << "the GUI stages are still recorded";
  EXPECT_EQ(rows[1].at("outcome"), "unchanged");
  EXPECT_EQ(as_int(rows[1], "ready_us"), kTimingBlank);
}

TEST(StreamTiming, APanelThatIsNotVideoDrivenWritesNoVideoFile)
{
  TempDir dir;
  TimingLog log(dir.str(), std::chrono::milliseconds(10000));
  ScopedTimingLog scope(log);

  QWidget widget;
  widget.resize(120, 80);
  widget.show();
  widget.ensurePolished();
  QCoreApplication::processEvents();
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("control-panel", &widget, video_options(), factory);

  EXPECT_TRUE(stream.tick(1.0));
  log.flush_all();
  EXPECT_FALSE(fs::exists(dir.path() / "video_control-panel.csv"));
  std::size_t files = 0;
  for (const auto & entry : fs::directory_iterator(dir.path())) {
    (void)entry;
    ++files;
  }
  EXPECT_EQ(files, 0u);
}

TEST(StreamTiming, WithTimingOffTheStreamBehavesAsBeforeAndWritesNothing)
{
  TempDir dir;
  TimingLog log("");   // disabled
  ScopedTimingLog scope(log);

  auto source = std::make_shared<LatestVideoFrame>();
  source->publish(solid_frame(Qt::red), make_trace(1));
  VideoPanelWidget widget(source, QStringLiteral("cam"));
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("video:/cam/image_raw", &widget, video_options(), factory);

  EXPECT_TRUE(stream.tick(1.0));
  source->publish(solid_frame(Qt::blue), make_trace(2));
  EXPECT_TRUE(stream.tick(1.1));
  EXPECT_EQ(stream.diagnostics().frames_submitted, 2u);
  std::size_t files = 0;
  for (const auto & entry : fs::directory_iterator(dir.path())) {
    (void)entry;
    ++files;
  }
  EXPECT_EQ(files, 0u);
}
