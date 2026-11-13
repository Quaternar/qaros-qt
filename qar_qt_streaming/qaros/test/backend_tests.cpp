// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The qaros backend against a fake of the QarOS function tables: no library is loaded and no Hub
// is started. The test thread plays the Qt thread; the fake records the thread of every call.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../src/backend_detail.hpp"
#include "fake_c_table.hpp"
#include "qar_qt_streaming/qaros/conversions.hpp"
#include "qar_qt_streaming/qaros/qaros_panel_input_source.hpp"
#include "qar_qt_streaming/qaros/qaros_panel_sender.hpp"
#include "qar_qt_streaming/qaros/qaros_panel_sender_factory.hpp"
#include "qar_qt_streaming/qaros/sdk_config_check.hpp"

namespace qb = qar_qt_streaming::qaros_backend;
namespace seam = qar_qt_streaming::seam;
using qar_qt_streaming::qaros_backend::test::FakeCApi;
using qar_qt_streaming::qaros_backend::test::FakeGesture;
using qar_qt_streaming::qaros_backend::test::FakePanel;
using namespace std::chrono_literals;

namespace
{

/// A gate a test closes in a hook of the fake, to hold the worker where it stands.
class Gate
{
public:
  void open()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      open_ = true;
    }
    cv_.notify_all();
  }

  void wait()
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] {return open_;});
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool open_ = false;
};

template<typename Pred>
bool
wait_until(Pred pred, std::chrono::milliseconds limit = 5000ms)
{
  const auto end = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < end) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return pred();
}

class RecordingSink : public seam::PanelDiagnosticsSink
{
public:
  struct Entry
  {
    std::thread::id thread;
    seam::DiagnosticSeverity severity;
    std::string category;
    std::string message;
    std::string key;
  };

  void on_message(
    seam::DiagnosticSeverity severity, const std::string & category, const std::string & message,
    const std::string & panel_key) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.push_back(Entry{std::this_thread::get_id(), severity, category, message, panel_key});
  }

  std::vector<Entry> entries() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
  }

  bool contains(const std::string & text) const
  {
    for (const Entry & entry : entries()) {
      if (entry.message.find(text) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

private:
  mutable std::mutex mutex_;
  std::vector<Entry> entries_;
};

/// The clock the panel-state guard reads.
class ManualClock
{
public:
  std::chrono::steady_clock::time_point now() const
  {
    return std::chrono::steady_clock::time_point(std::chrono::nanoseconds(ns_.load()));
  }

  void advance(std::chrono::milliseconds by)
  {
    ns_.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(by).count());
  }

private:
  std::atomic<std::int64_t> ns_{1000000000LL};
};

seam::PanelDesc
make_desc(const std::string & key = "a")
{
  seam::PanelDesc desc;
  desc.key = key;
  desc.display_name = "Panel " + key;
  desc.common_name = key + "-0123abcd.test.host.qaros-ros";
  desc.buffer_width = 64;
  desc.buffer_height = 32;
  desc.content_width = 60;
  desc.content_height = 30;
  desc.pixel_format = seam::PixelFormat::R8G8B8A8;
  desc.placement.pose.position = {0.0f, 1.5f, -1.0f};
  desc.placement.size = {1.0f, 0.5f, 1.0f};
  return desc;
}

std::string
uuid_text(const std::uint8_t (&bytes)[16])
{
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  for (std::size_t i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out.push_back('-');
    }
    out.push_back(kHex[(bytes[i] >> 4) & 0x0F]);
    out.push_back(kHex[bytes[i] & 0x0F]);
  }
  return out;
}

class BackendTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    session = fake.make_session();
    input = std::make_shared<qb::QarosPanelInputSource>();
    start_factory();
  }

  void TearDown() override
  {
    senders.clear();
    factory.reset();
    input.reset();
    session = qaros::Session();
  }

  void start_factory(std::chrono::milliseconds budget = 8000ms)
  {
    qb::FactoryOptions options;
    options.diagnostics = &sink;
    options.shutdown_budget = budget;
    options.clock = [this] {return clock.now();};
    factory = std::make_unique<qb::QarosPanelSenderFactory>(session, input, options);
  }

  std::shared_ptr<qb::QarosPanelSender> make_sender(const seam::PanelDesc & desc = make_desc())
  {
    std::shared_ptr<seam::IPanelSender> made = factory->create_panel_sender(desc);
    auto sender = std::dynamic_pointer_cast<qb::QarosPanelSender>(made);
    if (sender) {
      senders.push_back(sender);
    }
    return sender;
  }

  std::shared_ptr<qb::QarosPanelSender> ready_sender(const seam::PanelDesc & desc = make_desc())
  {
    auto sender = make_sender(desc);
    EXPECT_TRUE(sender != nullptr);
    if (sender) {
      EXPECT_TRUE(wait_until([&sender] {return sender->ready();})) << "the panel was not made";
    }
    return sender;
  }

  /// Returns when every task that was queued before this call has run: a placement task is queued
  /// behind them and is the last to touch the panel.
  void flush_worker(const std::shared_ptr<qb::QarosPanelSender> & sender)
  {
    const int before = fake.count("gui_panels_update_pose");
    seam::PanelPlacement placement;
    placement.size = {1.0f + 0.01f * static_cast<float>(++flushes), 0.5f, 1.0f};
    ASSERT_TRUE(sender->set_placement(placement));
    ASSERT_TRUE(wait_until([&] {return fake.count("gui_panels_update_pose") > before;}));
  }

  static seam::PanelPixels pixels_of(const std::vector<std::uint8_t> & data, std::uint32_t w,
    std::uint32_t h, std::uint32_t stride)
  {
    seam::PanelPixels pixels;
    pixels.data = data.data();
    pixels.width = w;
    pixels.height = h;
    pixels.stride_bytes = stride;
    return pixels;
  }

  FakeCApi fake;
  RecordingSink sink;
  ManualClock clock;
  qaros::Session session;
  std::shared_ptr<qb::QarosPanelInputSource> input;
  std::unique_ptr<qb::QarosPanelSenderFactory> factory;
  std::vector<std::shared_ptr<qb::QarosPanelSender>> senders;
  int flushes = 0;
};

}  // namespace

// ----- create and the frame cycle -----------------------------------------------------------

TEST_F(BackendTest, ASenderReportsCreatingUntilThePanelExists)
{
  Gate gate;
  fake.set_call_hook("video_sender_create", [&gate] {gate.wait();});
  auto sender = make_sender();
  ASSERT_TRUE(sender != nullptr) << "the factory returns at once";
  EXPECT_TRUE(sender->pending());
  EXPECT_FALSE(sender->ready());

  bool ready = true;
  EXPECT_TRUE(sender->try_begin_frame(ready));
  EXPECT_FALSE(ready);
  EXPECT_EQ(sender->not_ready_cause(), seam::PanelNotReadyCause::Creating);
  EXPECT_EQ(fake.count("video_sender_try_begin_frame"), 0) << "no frame call before the sender";

  gate.open();
  ASSERT_TRUE(wait_until([&sender] {return sender->ready();}));
  EXPECT_FALSE(sender->pending());
  EXPECT_TRUE(sender->try_begin_frame(ready));
  EXPECT_TRUE(ready);
  EXPECT_NE(sender->not_ready_cause(), seam::PanelNotReadyCause::Creating);
  EXPECT_EQ(sender->common_name(), make_desc().common_name);
  EXPECT_FALSE(sender->reattached());
  EXPECT_FALSE(sender->stream_id_text().empty());
}

TEST_F(BackendTest, ThePanelIsMadeWithTheStreamOfTheSenderTheLayoutAndThePlacement)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const FakePanel panel = fake.panel(make_desc().common_name);
  EXPECT_TRUE(panel.has_stream);
  EXPECT_FALSE(panel.is_url);
  EXPECT_EQ(uuid_text(panel.stream.data), sender->stream_id_text());
  EXPECT_EQ(panel.display_name, "Panel a");
  EXPECT_FLOAT_EQ(panel.size.width_meters, 1.0f);
  EXPECT_FLOAT_EQ(panel.size.height_meters, 0.5f);
  EXPECT_FLOAT_EQ(panel.pose.position.y, 1.5f);
  EXPECT_EQ(fake.layout_width(), 64u);
  EXPECT_EQ(fake.layout_height(), 32u);
  EXPECT_EQ(fake.count("gui_panels_subscribe_all_gesture_events"), 1);
  EXPECT_EQ(fake.count("gui_panels_subscribe_panel_updates"), 1);
  EXPECT_TRUE(input->has_panel("a"));
}

TEST_F(BackendTest, ARowIsCopiedWithThePitchOfTheSdkAndTheStrideOfTheCaller)
{
  fake.set_frame_pitch(64 * 4 + 16);  // the sender's pitch is not width * 4
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const std::uint32_t stride = 64 * 4 + 8;  // and neither is the caller's
  std::vector<std::uint8_t> source(static_cast<std::size_t>(stride) * 32, 0xEE);
  for (std::uint32_t y = 0; y < 32; ++y) {
    for (std::uint32_t x = 0; x < 64 * 4; ++x) {
      source[static_cast<std::size_t>(y) * stride + x] = static_cast<std::uint8_t>(y * 7 + x);
    }
  }
  bool ready = false;
  ASSERT_TRUE(sender->try_begin_frame(ready));
  ASSERT_TRUE(ready);
  ASSERT_TRUE(sender->submit_frame(pixels_of(source, 64, 32, stride))) << sender->last_error();

  const std::vector<std::uint8_t> frame = fake.last_frame();
  ASSERT_EQ(frame.size(), static_cast<std::size_t>(64 * 4 + 16) * 32);
  for (std::uint32_t y = 0; y < 32; ++y) {
    const std::size_t row = static_cast<std::size_t>(y) * (64 * 4 + 16);
    for (std::uint32_t x = 0; x < 64 * 4; ++x) {
      ASSERT_EQ(frame[row + x], static_cast<std::uint8_t>(y * 7 + x)) << "y " << y << " x " << x;
    }
    for (std::uint32_t x = 64 * 4; x < 64 * 4 + 16; ++x) {
      ASSERT_EQ(frame[row + x], 0) << "the padding of the destination stays untouched";
    }
  }
}

TEST_F(BackendTest, EveryFrameShowsOnceAndATickWithoutADestinationCallsNothingMore)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const std::vector<std::uint8_t> source(64 * 4 * 32, 0x11);
  for (int i = 0; i < 3; ++i) {
    bool ready = false;
    ASSERT_TRUE(sender->try_begin_frame(ready));
    ASSERT_TRUE(ready);
    ASSERT_TRUE(sender->submit_frame(pixels_of(source, 64, 32, 64 * 4)));
  }
  EXPECT_EQ(fake.count("video_sender_try_begin_frame"), 3);
  EXPECT_EQ(fake.count("video_sender_frame_cpu"), 3);
  EXPECT_EQ(fake.count("video_sender_show_frame"), 3);
  EXPECT_EQ(fake.frames_shown(), 3);

  fake.set_sender_ready(false);
  bool ready = true;
  EXPECT_TRUE(sender->try_begin_frame(ready));
  EXPECT_FALSE(ready);
  EXPECT_EQ(fake.count("video_sender_frame_cpu"), 3);
  EXPECT_EQ(fake.count("video_sender_show_frame"), 3);
  EXPECT_FALSE(sender->submit_frame(pixels_of(source, 64, 32, 64 * 4)))
    << "a frame needs a reserved cycle";
}

TEST_F(BackendTest, TheThreeNotReadyStatusesAreNotFailuresAndKeepTheirCauses)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  struct Row
  {
    int code;
    seam::PanelNotReadyCause cause;
  };
  const Row rows[] = {
    {604, seam::PanelNotReadyCause::NoActiveTransfers},
    {603, seam::PanelNotReadyCause::Reconnecting},
    {602, seam::PanelNotReadyCause::Paused}};
  for (const Row & row : rows) {
    fake.set_try_begin_status(row.code);
    bool ready = true;
    EXPECT_TRUE(sender->try_begin_frame(ready)) << row.code;
    EXPECT_FALSE(ready) << row.code;
    EXPECT_EQ(sender->not_ready_cause(), row.cause) << row.code;
  }
  fake.set_try_begin_status(5);
  bool ready = true;
  EXPECT_FALSE(sender->try_begin_frame(ready));
  EXPECT_NE(sender->last_error().find("TryBeginFrame"), std::string::npos);
}

TEST_F(BackendTest, AFrameWithoutPixelsOrAReservationIsRefused)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  seam::PanelPixels none;
  EXPECT_FALSE(sender->submit_frame(none));
  bool ready = false;
  ASSERT_TRUE(sender->try_begin_frame(ready));
  ASSERT_TRUE(ready);
  EXPECT_FALSE(sender->submit_frame(none));
  EXPECT_EQ(sender->last_error(), "submit_frame got a null pixel buffer");
  EXPECT_EQ(fake.count("video_sender_show_frame"), 0);
}

// ----- refusals and failures ----------------------------------------------------------------

TEST_F(BackendTest, ADescriptionThatCannotBeServedIsRefusedWithoutACall)
{
  seam::PanelDesc desc = make_desc();
  desc.buffer_width = 0;
  EXPECT_EQ(factory->create_panel_sender(desc), nullptr);
  EXPECT_EQ(factory->last_error(), "panel buffer size must be non-zero");
  desc = make_desc();
  desc.common_name.clear();
  EXPECT_EQ(factory->create_panel_sender(desc), nullptr);
  EXPECT_NE(factory->last_error().find("no common name"), std::string::npos);
  EXPECT_EQ(fake.count("video_sender_create"), 0);

  qb::QarosPanelSenderFactory without(qaros::Session(), nullptr);
  EXPECT_EQ(without.create_panel_sender(make_desc()), nullptr);
  EXPECT_EQ(without.last_error(), "no QarOS session");
}

TEST_F(BackendTest, AFailedCreationEndsTheSenderAndIsReportedOnce)
{
  fake.set_failure("video_sender_create", 7);
  auto sender = make_sender();
  ASSERT_TRUE(sender != nullptr);
  std::vector<seam::PanelOpResult> results;
  ASSERT_TRUE(wait_until([&] {
        sender->drain_results(results);
        return !results.empty();
      }));
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].op, seam::PanelOpResult::Op::Create);
  EXPECT_FALSE(results[0].ok);
  EXPECT_NE(results[0].error.find("VideoSenderControl::Create failed"), std::string::npos);
  EXPECT_FALSE(sender->pending());
  bool ready = true;
  EXPECT_FALSE(sender->try_begin_frame(ready));
  EXPECT_NE(sender->last_error().find("code=7"), std::string::npos);
  sender->close();
  senders.clear();
  factory.reset();
  EXPECT_EQ(fake.live_senders(), 0);
}

TEST_F(BackendTest, AFailedPanelCallReleasesTheSenderItHad)
{
  fake.set_failure("gui_panels_get_or_create", 9);
  auto sender = make_sender();
  ASSERT_TRUE(sender != nullptr);
  std::vector<seam::PanelOpResult> results;
  ASSERT_TRUE(wait_until([&] {
        sender->drain_results(results);
        return !results.empty();
      }));
  EXPECT_FALSE(results[0].ok);
  sender->close();
  senders.clear();
  factory.reset();
  EXPECT_EQ(fake.live_senders(), 0);
  EXPECT_EQ(fake.count("gui_panels_close_panel"), 0) << "no panel was made";
  EXPECT_EQ(fake.count("video_sender_handle_destroy"), 1);
}

// ----- an earlier session's panel -----------------------------------------------------------

TEST_F(BackendTest, APanelOfAnEarlierSessionIsFoundAgainAndGetsTheNewStream)
{
  FakePanel old;
  old.common_name = make_desc().common_name;
  old.state = QAR_GUI_PANEL_STATE_HIDDEN;
  old.has_stream = true;
  for (int i = 0; i < QAR_MAX_ID_LENGTH; ++i) {
    old.stream.data[i] = static_cast<std::uint8_t>(i + 1);
  }
  old.size = {2.0f, 2.0f, 1.0f};
  fake.add_existing_panel(old);

  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  EXPECT_TRUE(sender->reattached());
  const FakePanel panel = fake.panel(make_desc().common_name);
  EXPECT_EQ(panel.state, QAR_GUI_PANEL_STATE_VISIBLE) << "a hidden panel is shown again";
  EXPECT_EQ(uuid_text(panel.stream.data), sender->stream_id_text()) << "the stream is swapped";
  EXPECT_FLOAT_EQ(panel.size.width_meters, 1.0f) << "the size is fitted";
  EXPECT_EQ(fake.count("gui_panels_set_stream"), 1);
  EXPECT_EQ(fake.count("gui_panels_change_size"), 1);
  EXPECT_EQ(fake.count("gui_panels_update_pose"), 1);
  EXPECT_EQ(fake.count("gui_panels_set_state"), 1);
  EXPECT_EQ(fake.requested_names().size(), 1u);
  std::vector<seam::PanelOpResult> results;
  sender->drain_results(results);
  ASSERT_FALSE(results.empty());
  EXPECT_TRUE(results[0].reattached);
}

TEST_F(BackendTest, AClosedNameIsTriedAgainWithTheSuffixR1)
{
  FakePanel closed;
  closed.common_name = make_desc().common_name;
  closed.state = QAR_GUI_PANEL_STATE_CLOSED;
  closed.has_stream = true;
  fake.add_existing_panel(closed);

  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const std::string base = make_desc().common_name;
  const std::vector<std::string> expected = {base, base + "-r1"};
  EXPECT_EQ(fake.requested_names(), expected);
  EXPECT_EQ(sender->common_name(), base + "-r1");
  EXPECT_FALSE(sender->reattached());
  EXPECT_TRUE(fake.has_panel(base + "-r1"));
  EXPECT_EQ(fake.panel(base).state, QAR_GUI_PANEL_STATE_CLOSED) << "the closed panel is left alone";
}

TEST_F(BackendTest, WhenEveryNameIsClosedThePanelIsRefused)
{
  const std::string base = make_desc().common_name;
  for (int i = 0; i <= 8; ++i) {
    FakePanel closed;
    closed.common_name = i == 0 ? base : base + "-r" + std::to_string(i);
    closed.state = QAR_GUI_PANEL_STATE_CLOSED;
    closed.has_stream = true;
    fake.add_existing_panel(closed);
  }
  auto sender = make_sender();
  ASSERT_TRUE(sender != nullptr);
  std::vector<seam::PanelOpResult> results;
  ASSERT_TRUE(wait_until([&] {
        sender->drain_results(results);
        return !results.empty();
      }));
  EXPECT_FALSE(results[0].ok);
  EXPECT_NE(results[0].error.find("no free name is left"), std::string::npos);
  EXPECT_EQ(fake.requested_names().size(), 9u);
}

// ----- placement and state ------------------------------------------------------------------

TEST_F(BackendTest, PlacementsAskedWhileTheWorkerIsBusyCoalesceToTheLastOne)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  Gate gate;
  fake.set_call_hook("gui_panels_set_state", [&gate] {gate.wait();});
  ASSERT_TRUE(sender->set_state(seam::PanelState::Hidden));
  ASSERT_TRUE(wait_until([this] {return fake.count("gui_panels_set_state") == 1;}));

  for (int i = 1; i <= 5; ++i) {
    seam::PanelPlacement placement;
    placement.pose.position = {static_cast<float>(i), 0.0f, 0.0f};
    placement.size = {1.0f + static_cast<float>(i), 0.5f, 1.0f};
    ASSERT_TRUE(sender->set_placement(placement));
  }
  gate.open();
  senders.clear();
  sender->close();
  sender.reset();
  factory.reset();

  EXPECT_EQ(fake.count("gui_panels_update_pose"), 1) << "one task carried the latest wish";
  EXPECT_EQ(fake.count("gui_panels_change_size"), 1);
  const FakePanel panel = fake.panel(make_desc().common_name);
  EXPECT_FLOAT_EQ(panel.pose.position.x, 5.0f);
  EXPECT_FLOAT_EQ(panel.size.width_meters, 6.0f);
}

TEST_F(BackendTest, TheStateOfThePanelFollowsTheCallerAndTheOutcomeComesBack)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  ASSERT_TRUE(sender->supports_state());
  ASSERT_TRUE(sender->set_state(seam::PanelState::Hidden));
  std::vector<seam::PanelOpResult> results;
  ASSERT_TRUE(wait_until([&] {
        sender->drain_results(results);
        for (const auto & r : results) {
          if (r.op == seam::PanelOpResult::Op::State) {
            return true;
          }
        }
        return false;
      }));
  EXPECT_EQ(fake.panel(make_desc().common_name).state, QAR_GUI_PANEL_STATE_HIDDEN);

  fake.set_failure("gui_panels_set_state", 11);
  ASSERT_TRUE(sender->set_state(seam::PanelState::Visible));
  results.clear();
  ASSERT_TRUE(wait_until([&] {
        sender->drain_results(results);
        for (const auto & r : results) {
          if (r.op == seam::PanelOpResult::Op::State) {
            return true;
          }
        }
        return false;
      }));
  const auto failed = std::find_if(
    results.begin(), results.end(),
    [](const seam::PanelOpResult & r) {return r.op == seam::PanelOpResult::Op::State;});
  ASSERT_TRUE(failed != results.end());
  EXPECT_FALSE(failed->ok);
  EXPECT_EQ(failed->state, seam::PanelState::Visible);
  EXPECT_NE(failed->error.find("code=11"), std::string::npos);
}

TEST_F(BackendTest, TheStateGuardAsksAgainAfterAStaleHiddenAndGivesUpAfterThreeAsks)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const std::string name = make_desc().common_name;

  // A runtime that reports HIDDEN right after the panel was shown is asked once.
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_HIDDEN));
  ASSERT_TRUE(wait_until([this] {return fake.count("gui_panels_set_state") == 1;}));
  flush_worker(sender);
  EXPECT_EQ(fake.panel(name).state, QAR_GUI_PANEL_STATE_VISIBLE);

  // A contrary record inside the hold of the ask is the echo of the old state: not answered.
  clock.advance(500ms);
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_HIDDEN));
  flush_worker(sender);
  EXPECT_EQ(fake.count("gui_panels_set_state"), 1);

  // After the hold it is answered again.
  clock.advance(600ms);
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_HIDDEN));
  ASSERT_TRUE(wait_until([this] {return fake.count("gui_panels_set_state") == 2;}));
  flush_worker(sender);

  // A record inside the hold that nothing follows is asked about at the next tick.
  clock.advance(500ms);
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_HIDDEN));
  flush_worker(sender);
  EXPECT_EQ(fake.count("gui_panels_set_state"), 2);
  clock.advance(600ms);
  std::vector<seam::PanelOpResult> results;
  sender->drain_results(results);
  ASSERT_TRUE(wait_until([this] {return fake.count("gui_panels_set_state") == 3;}));
  flush_worker(sender);

  // Three asks have gone unconfirmed: the guard gives up and says so, once.
  clock.advance(1100ms);
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_HIDDEN));
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_HIDDEN));
  flush_worker(sender);
  sender->drain_results(results);
  EXPECT_EQ(fake.count("gui_panels_set_state"), 3);
  int gave_up = 0;
  for (const auto & entry : sink.entries()) {
    if (entry.message.find("gave up after 3 asks") != std::string::npos) {
      ++gave_up;
      EXPECT_EQ(entry.severity, seam::DiagnosticSeverity::Warning);
    }
  }
  EXPECT_EQ(gave_up, 1);
}

TEST_F(BackendTest, TheStateTheOperatorSetsInArIsNotContested)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const std::string name = make_desc().common_name;
  // The operator minimizes the panel, and hides it a long time after the change.
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_MINIMIZED));
  clock.advance(6000ms);
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_HIDDEN));
  flush_worker(sender);
  EXPECT_EQ(fake.count("gui_panels_set_state"), 0);
}

// ----- close, detach, the operator's close --------------------------------------------------

TEST_F(BackendTest, ACloseEndsTheGesturesThenTheUpdatesThenClosesThePanelThenReleasesTheSender)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const seam::PanelCloseReport report = sender->close();
  EXPECT_TRUE(report.input_unsubscribed);
  EXPECT_EQ(report.panel, seam::PanelCloseOutcome::Queued);
  senders.clear();
  sender.reset();
  factory.reset();

  const std::vector<std::string> expected = {
    "cancel_token_destroy:gestures", "cancel_token_destroy:updates", "gui_panels_close_panel",
    "video_sender_handle_destroy"};
  EXPECT_EQ(
    fake.names_in({"cancel_token_destroy:gestures", "cancel_token_destroy:updates",
      "gui_panels_close_panel", "video_sender_handle_destroy"}), expected);
  EXPECT_EQ(fake.panel(make_desc().common_name).state, QAR_GUI_PANEL_STATE_CLOSED);
  EXPECT_EQ(fake.live_senders(), 0);
  EXPECT_EQ(fake.live_tokens(), 0);
  EXPECT_FALSE(input->has_panel("a"));
}

TEST_F(BackendTest, ASecondCloseDoesNothing)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  sender->close();
  const seam::PanelCloseReport again = sender->close();
  EXPECT_FALSE(again.input_unsubscribed);
  EXPECT_EQ(again.panel, seam::PanelCloseOutcome::NoPanel);
  senders.clear();
  sender.reset();
  factory.reset();
  EXPECT_EQ(fake.count("gui_panels_close_panel"), 1);
  EXPECT_EQ(fake.count("video_sender_handle_destroy"), 1);
}

TEST_F(BackendTest, APanelTheOperatorClosedInArIsNotClosedAgain)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const std::string name = make_desc().common_name;
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_CLOSED));
  std::vector<std::string> closed;
  input->drain_closed_panels(closed);
  ASSERT_EQ(closed.size(), 1u);

  sender->close(/*panel_closed_in_ar=*/true);
  senders.clear();
  sender.reset();
  factory.reset();
  EXPECT_EQ(fake.count("gui_panels_close_panel"), 0);
  EXPECT_EQ(fake.count("video_sender_handle_destroy"), 1);
  EXPECT_EQ(fake.count("cancel_token_destroy:gestures"), 1);
  EXPECT_EQ(fake.count("cancel_token_destroy:updates"), 1);
}

TEST_F(BackendTest, ADetachKeepsThePanelHiddenAndReleasesTheRest)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  ASSERT_TRUE(sender->supports_detach());
  ASSERT_TRUE(sender->set_state(seam::PanelState::Hidden));
  sender->detach();
  senders.clear();
  sender.reset();
  factory.reset();

  EXPECT_EQ(fake.panel(make_desc().common_name).state, QAR_GUI_PANEL_STATE_HIDDEN);
  EXPECT_EQ(fake.count("gui_panels_close_panel"), 0);
  EXPECT_EQ(fake.count("video_sender_handle_destroy"), 1);
  EXPECT_EQ(fake.count("cancel_token_destroy:gestures"), 1);
  EXPECT_EQ(fake.count("cancel_token_destroy:updates"), 1);
  EXPECT_EQ(fake.live_senders(), 0);
}

TEST_F(BackendTest, ASenderClosedBeforeItsTurnMakesNothing)
{
  Gate gate;
  fake.set_call_hook("gui_panels_get_or_create", [&gate] {gate.wait();});
  auto first = make_sender(make_desc("a"));
  ASSERT_TRUE(wait_until([this] {return fake.count("gui_panels_get_or_create") == 1;}));
  auto second = make_sender(make_desc("b"));
  second->close();  // queued behind the creation of the first
  gate.open();
  ASSERT_TRUE(wait_until([&first] {return first->ready();}));
  senders.clear();
  first->close();
  first.reset();
  second.reset();
  factory.reset();
  EXPECT_EQ(fake.count("video_sender_create"), 1) << "the closed sender made nothing";
  EXPECT_FALSE(fake.has_panel(make_desc("b").common_name));
}

TEST_F(BackendTest, ADetachBeforeTheCreationRunsLeavesNothingBehind)
{
  Gate gate;
  fake.set_call_hook("video_sender_create", [&gate] {gate.wait();});
  auto sender = make_sender();
  ASSERT_TRUE(wait_until([this] {return fake.count("video_sender_create") == 1;}));
  sender->detach();  // while the creation is running
  gate.open();
  senders.clear();
  sender.reset();
  factory.reset();
  EXPECT_EQ(fake.live_senders(), 0);
  EXPECT_EQ(fake.live_tokens(), 0);
}

// ----- input ---------------------------------------------------------------------------------

TEST_F(BackendTest, AGestureBecomesASampleAgainstThePanelKeyAndTheContentSize)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  FakeGesture gesture;
  gesture.peer_tag = 3;
  gesture.kind = QAR_GUI_PANEL_GESTURE_KIND_TAP;
  gesture.phase = QAR_GESTURE_PHASE_INSTANT;
  gesture.x_pixels = 30;
  gesture.y_pixels = 15;
  gesture.hand = QAR_HANDEDNESS_LEFT;
  gesture.tap_count = 2;
  ASSERT_TRUE(fake.emit_gesture(make_desc().common_name, gesture));

  std::vector<seam::PanelPointerSample> samples;
  input->drain(samples);
  ASSERT_EQ(samples.size(), 1u);
  const seam::PanelPointerSample & sample = samples[0];
  EXPECT_EQ(sample.panel_key, "a");
  EXPECT_TRUE(sample.has_panel_uv);
  EXPECT_FLOAT_EQ(sample.u, 30.5f / 60.0f) << "the content size, not the buffer size";
  EXPECT_FLOAT_EQ(sample.v, 15.5f / 30.0f);
  EXPECT_EQ(sample.gesture, seam::PointerGesture::Click);
  EXPECT_EQ(sample.phase, seam::PointerPhase::Instant);
  EXPECT_EQ(sample.hand, seam::PointerHand::Left);
  EXPECT_EQ(sample.tap_count, 2u);
  seam::PeerId peer;
  for (auto & byte : peer.data) {
    byte = 3;
  }
  EXPECT_EQ(
    sample.pointer_id, seam::PanelGestureMapper::pointer_id_for(peer, seam::PointerHand::Left));
  EXPECT_EQ(input->stats().events_received, 1u);
  input->drain(samples);
  EXPECT_EQ(samples.size(), 1u) << "a second drain appends nothing";
}

TEST_F(BackendTest, AnUnreadableGestureIsCountedAndProducesNothing)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  FakeGesture gesture;
  gesture.unreadable = true;
  ASSERT_TRUE(fake.emit_gesture(make_desc().common_name, gesture));
  std::vector<seam::PanelPointerSample> samples;
  input->drain(samples);
  EXPECT_TRUE(samples.empty());
  EXPECT_EQ(input->stats().events_invalid, 1u);
}

TEST_F(BackendTest, AGestureStillHeldDownWhenThePanelCloses)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  FakeGesture gesture;
  gesture.kind = QAR_GUI_PANEL_GESTURE_KIND_DRAG;
  gesture.phase = QAR_GESTURE_PHASE_STARTED;
  gesture.x_pixels = 10;
  gesture.y_pixels = 12;
  ASSERT_TRUE(fake.emit_gesture(make_desc().common_name, gesture));
  gesture.phase = QAR_GESTURE_PHASE_UPDATED;
  gesture.x_pixels = 20;
  ASSERT_TRUE(fake.emit_gesture(make_desc().common_name, gesture));

  sender->close();
  senders.clear();
  sender.reset();
  factory.reset();

  std::vector<seam::PanelPointerSample> samples;
  input->drain(samples);
  ASSERT_EQ(samples.size(), 3u);
  EXPECT_EQ(samples[0].phase, seam::PointerPhase::Started);
  EXPECT_EQ(samples[1].phase, seam::PointerPhase::Updated);
  EXPECT_EQ(samples[2].phase, seam::PointerPhase::Canceled) << "the widget gets its release";
  EXPECT_EQ(samples[2].pointer_id, samples[0].pointer_id);
  EXPECT_FLOAT_EQ(samples[2].u, 20.5f / 60.0f) << "at the last place it was seen";
}

TEST_F(BackendTest, ASilentGestureIsCancelledByTheLivenessSweep)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  input->set_lifecycle_timeout(0.05);
  FakeGesture gesture;
  gesture.kind = QAR_GUI_PANEL_GESTURE_KIND_HOVER;
  gesture.phase = QAR_GESTURE_PHASE_STARTED;
  ASSERT_TRUE(fake.emit_gesture(make_desc().common_name, gesture));
  std::vector<seam::PanelPointerSample> samples;
  input->drain(samples);
  ASSERT_EQ(samples.size(), 1u);
  std::this_thread::sleep_for(120ms);
  input->drain(samples);
  ASSERT_EQ(samples.size(), 2u);
  EXPECT_EQ(samples[1].phase, seam::PointerPhase::Canceled);
  EXPECT_EQ(input->stats().hover_timeouts, 1u);
}

TEST_F(BackendTest, TheOperatorsCloseIsReportedOncePerSubscriptionAndNotForOtherStates)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const std::string name = make_desc().common_name;
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_MINIMIZED));
  std::vector<std::string> closed;
  input->drain_closed_panels(closed);
  EXPECT_TRUE(closed.empty());
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_CLOSED));
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_CLOSED));
  input->drain_closed_panels(closed);
  ASSERT_EQ(closed.size(), 1u);
  EXPECT_EQ(closed[0], "a");
  input->drain_closed_panels(closed);
  EXPECT_EQ(closed.size(), 1u);
}

TEST_F(BackendTest, ASubscriptionForTheSameKeyReplacesTheOldOne)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const std::string name = make_desc().common_name;
  ASSERT_TRUE(fake.emit_panel_update(name, QAR_GUI_PANEL_STATE_CLOSED));
  std::vector<std::string> closed;
  input->drain_closed_panels(closed);
  ASSERT_EQ(closed.size(), 1u);

  // A second sender for the same key (the stream of a resized widget) subscribes the key again.
  sender->detach();
  auto next = ready_sender();
  ASSERT_TRUE(next != nullptr);
  EXPECT_EQ(input->subscription_count(), 1u);
  // The earlier panel is closed now, so the new sender works on a panel of another name.
  ASSERT_TRUE(fake.emit_panel_update(next->common_name(), QAR_GUI_PANEL_STATE_CLOSED));
  input->drain_closed_panels(closed);
  EXPECT_EQ(closed.size(), 2u) << "the history of the key starts afresh";
}

TEST_F(BackendTest, APanelWithoutAnInputSourceStreamsAndTakesNoInput)
{
  qb::FactoryOptions options;
  options.diagnostics = &sink;
  qb::QarosPanelSenderFactory plain(session, nullptr, options);
  auto made = plain.create_panel_sender(make_desc("p"));
  auto sender = std::dynamic_pointer_cast<qb::QarosPanelSender>(made);
  ASSERT_TRUE(sender != nullptr);
  ASSERT_TRUE(wait_until([&sender] {return sender->ready();}));
  EXPECT_EQ(fake.count("gui_panels_subscribe_all_gesture_events"), 0);
  EXPECT_EQ(fake.count("gui_panels_subscribe_panel_updates"), 1);
  const seam::PanelCloseReport report = sender->close();
  EXPECT_FALSE(report.input_unsubscribed);
}

// ----- threads -------------------------------------------------------------------------------

namespace
{

bool
starts_with(const std::string & text, const char * prefix)
{
  return text.rfind(prefix, 0) == 0;
}

}  // namespace

TEST_F(BackendTest, EverySdkCallButTheThreeFrameCallsRunsOffTheQtThread)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const std::vector<std::uint8_t> source(64 * 4 * 32, 0x22);
  bool ready = false;
  ASSERT_TRUE(sender->try_begin_frame(ready));
  ASSERT_TRUE(ready);
  ASSERT_TRUE(sender->submit_frame(pixels_of(source, 64, 32, 64 * 4)));
  seam::PanelPlacement placement;
  placement.size = {2.0f, 1.0f, 1.0f};
  ASSERT_TRUE(sender->set_placement(placement));
  ASSERT_TRUE(sender->set_state(seam::PanelState::Hidden));
  FakeGesture gesture;
  ASSERT_TRUE(fake.emit_gesture(make_desc().common_name, gesture));
  ASSERT_TRUE(fake.emit_panel_update(make_desc().common_name, QAR_GUI_PANEL_STATE_HIDDEN));
  sender->close();
  senders.clear();
  sender.reset();
  factory.reset();

  const std::thread::id qt_thread = std::this_thread::get_id();
  const std::set<std::string> frame_calls = {
    "video_sender_try_begin_frame", "video_sender_frame_cpu", "video_sender_show_frame"};
  int on_qt_thread = 0;
  for (const auto & call : fake.calls()) {
    if (frame_calls.count(call.name) != 0) {
      EXPECT_EQ(call.thread, qt_thread) << call.name;
      ++on_qt_thread;
    } else if (call.name != "session_handle_destroy") {
      EXPECT_NE(call.thread, qt_thread) << call.name << " ran on the Qt thread";
    }
  }
  EXPECT_EQ(on_qt_thread, 3);
  EXPECT_FALSE(fake.callback_threads().empty());
  EXPECT_EQ(fake.callback_threads().count(qt_thread), 0u);
}

TEST_F(BackendTest, TheSdkCallsTheBackendMakesAreTheDocumentedOnes)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  const std::vector<std::uint8_t> source(64 * 4 * 32, 0x22);
  bool ready = false;
  ASSERT_TRUE(sender->try_begin_frame(ready));
  ASSERT_TRUE(sender->submit_frame(pixels_of(source, 64, 32, 64 * 4)));
  seam::PanelPlacement placement;
  ASSERT_TRUE(sender->set_placement(placement));
  ASSERT_TRUE(sender->set_state(seam::PanelState::Hidden));
  sender->close();
  senders.clear();
  sender.reset();
  factory.reset();

  const std::set<std::string> documented = {
    "cancel_token_create", "cancel_token_destroy:gestures", "cancel_token_destroy:updates",
    "video_sender_create", "video_sender_stream_id", "video_sender_try_begin_frame",
    "video_sender_frame_cpu", "video_sender_show_frame", "video_sender_handle_destroy",
    "gui_panels_get_or_create", "query_gui_panels_count", "query_gui_panels",
    "gui_panels_subscribe_all_gesture_events", "gui_panels_subscribe_panel_updates",
    "gui_panels_update_pose", "gui_panels_change_size", "gui_panels_set_state",
    "gui_panels_close_panel", "gui_panels_set_stream", "session_handle_destroy"};
  std::set<std::string> seen;
  for (const auto & name : fake.call_names()) {
    seen.insert(name);
    EXPECT_EQ(documented.count(name), 1u) << "undocumented call " << name;
  }
  EXPECT_GE(seen.size(), 15u);
  for (const std::string & name : seen) {
    EXPECT_FALSE(starts_with(name, "gui_panels_navigate")) << name;
  }
}

TEST_F(BackendTest, TheSessionIsNeverReleasedOnACallbackThread)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  FakeGesture gesture;
  ASSERT_TRUE(fake.emit_gesture(make_desc().common_name, gesture));
  ASSERT_TRUE(fake.emit_panel_update(make_desc().common_name, QAR_GUI_PANEL_STATE_HIDDEN));
  sender->close();
  senders.clear();
  sender.reset();
  factory.reset();
  input.reset();
  EXPECT_EQ(fake.live_sessions(), 1) << "the application still holds its copy";
  session = qaros::Session();
  EXPECT_EQ(fake.live_sessions(), 0);
  EXPECT_FALSE(fake.callback_threads().empty());
  EXPECT_EQ(fake.callback_threads().count(fake.session_destroy_thread()), 0u);
  EXPECT_EQ(fake.session_destroy_thread(), std::this_thread::get_id());
}

TEST_F(BackendTest, WhenTheApplicationLetsGoFirstTheLastReferenceGoesOnTheWorker)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  FakeGesture gesture;
  ASSERT_TRUE(fake.emit_gesture(make_desc().common_name, gesture));
  session = qaros::Session();  // the application's copy goes first
  EXPECT_EQ(fake.live_sessions(), 1);
  sender->close();
  senders.clear();
  sender.reset();
  factory.reset();  // runs the close, ends the worker, which releases the last copy
  EXPECT_EQ(fake.live_sessions(), 0);
  EXPECT_NE(fake.session_destroy_thread(), std::this_thread::get_id());
  EXPECT_EQ(fake.callback_threads().count(fake.session_destroy_thread()), 0u);
}

// ----- messages ------------------------------------------------------------------------------

TEST_F(BackendTest, MessagesReachTheSinkOnTheQtThreadAndOnlyThere)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  FakeGesture gesture;
  ASSERT_TRUE(fake.emit_gesture(make_desc().common_name, gesture));
  EXPECT_TRUE(sink.entries().empty()) << "the worker and the library's threads call no sink";

  std::vector<seam::PanelOpResult> results;
  sender->drain_results(results);
  std::vector<seam::PanelPointerSample> samples;
  input->drain(samples);
  const auto entries = sink.entries();
  ASSERT_FALSE(entries.empty());
  for (const auto & entry : entries) {
    EXPECT_EQ(entry.thread, std::this_thread::get_id());
  }
  bool saw_created = false;
  bool saw_trace = false;
  for (const auto & entry : entries) {
    saw_created = saw_created || entry.message.find("panel 'a' created") != std::string::npos;
    saw_trace = saw_trace || (entry.category == "qar-panel-input" &&
      entry.message.find("tap/instant") != std::string::npos);
  }
  EXPECT_TRUE(saw_created);
  EXPECT_TRUE(saw_trace);
  EXPECT_TRUE(sink.contains("created name="));
}

TEST_F(BackendTest, TheMessagesOfAClosedPanelReachTheSinkThroughTheInputDrainAndTheFactoryEnd)
{
  auto sender = ready_sender();
  ASSERT_TRUE(sender != nullptr);
  sender->close();
  senders.clear();
  sender.reset();  // no sender is left to drain its messages
  ASSERT_TRUE(wait_until([this] {return fake.live_senders() == 0;}));
  ASSERT_TRUE(wait_until([this] {return fake.count("video_sender_handle_destroy") == 1;}));
  ASSERT_TRUE(wait_until([&] {
        std::vector<seam::PanelPointerSample> samples;
        input->drain(samples);
        return sink.contains("retired:");
      })) << "the input source's drain delivers the messages of the worker";
  for (const auto & entry : sink.entries()) {
    EXPECT_EQ(entry.thread, std::this_thread::get_id());
  }

  auto second = ready_sender(make_desc("b"));
  ASSERT_TRUE(second != nullptr);
  second->close();
  senders.clear();
  second.reset();
  factory.reset();  // joins the worker; the messages of the last close are delivered here
  int retired = 0;
  for (const auto & entry : sink.entries()) {
    if (entry.message.find("retired:") != std::string::npos) {
      ++retired;
    }
  }
  EXPECT_EQ(retired, 2);
}

// ----- shutdown ------------------------------------------------------------------------------

TEST(SessionExecutor, RunsTasksInOrderAndEverythingQueuedBeforeAShutdown)
{
  FakeCApi fake;
  std::vector<int> order;
  std::mutex mutex;
  std::thread::id worker_thread;
  {
    qb::detail::SessionExecutor executor(fake.make_session(), {}, 5000ms);
    for (int i = 0; i < 20; ++i) {
      ASSERT_TRUE(executor.post("task", [&, i](qaros::Session & session) {
          EXPECT_TRUE(static_cast<bool>(session));
          std::lock_guard<std::mutex> lock(mutex);
          order.push_back(i);
          worker_thread = std::this_thread::get_id();
        }));
    }
    executor.shutdown();
    EXPECT_FALSE(executor.abandoned());
    EXPECT_FALSE(executor.post("late", [](qaros::Session &) {})) << "no task after a shutdown";
    EXPECT_FALSE(executor.accepting());
  }
  ASSERT_EQ(order.size(), 20u);
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(order[static_cast<std::size_t>(i)], i);
  }
  EXPECT_NE(worker_thread, std::this_thread::get_id());
  EXPECT_EQ(fake.live_sessions(), 0);
  EXPECT_NE(fake.session_destroy_thread(), std::this_thread::get_id())
    << "the worker's copy goes on the worker";
}

TEST(SessionExecutor, AWorkerThatStaysBusyPastTheBudgetIsAbandonedNotJoined)
{
  FakeCApi fake;
  Gate gate;
  std::atomic<bool> started{false};
  std::atomic<bool> finished{false};
  qb::detail::SessionExecutor executor(fake.make_session(), {}, 150ms);
  ASSERT_TRUE(executor.post("block", [&](qaros::Session &) {
      started = true;
      gate.wait();
      finished = true;
    }));
  ASSERT_TRUE(wait_until([&started] {return started.load();}));
  const auto t0 = std::chrono::steady_clock::now();
  executor.shutdown();
  const auto took = std::chrono::steady_clock::now() - t0;
  EXPECT_GE(took, 100ms);
  EXPECT_LT(took, 3000ms) << "the wait is bounded by the budget";
  EXPECT_TRUE(executor.abandoned());
  EXPECT_FALSE(finished.load());
  gate.open();
  ASSERT_TRUE(wait_until([&fake] {return fake.live_sessions() == 0;}))
    << "the abandoned worker ends by itself";
  EXPECT_TRUE(finished.load());
}

TEST(SessionExecutor, TheApplicationsOwnExecutorRunsTheTasksAndIsNeverJoined)
{
  FakeCApi fake;
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::function<void()>> queue;
  bool stop = false;
  std::thread::id app_thread;
  std::thread app([&] {
      app_thread = std::this_thread::get_id();
      std::unique_lock<std::mutex> lock(mutex);
      for (;;) {
        cv.wait(lock, [&] {return stop || !queue.empty();});
        if (queue.empty()) {
          return;
        }
        auto task = std::move(queue.front());
        queue.pop_front();
        lock.unlock();
        task();
        lock.lock();
      }
    });
  std::vector<std::string> names;
  std::atomic<int> ran{0};
  {
    qb::detail::SessionExecutor executor(
      fake.make_session(),
      [&](const char * name, std::function<void()> task) {
        std::lock_guard<std::mutex> lock(mutex);
        names.push_back(name);
        queue.push_back(std::move(task));
        cv.notify_one();
        return true;
      },
      5000ms);
    for (int i = 0; i < 3; ++i) {
      EXPECT_TRUE(executor.post("job-" + std::to_string(i), [&](qaros::Session & session) {
          EXPECT_TRUE(static_cast<bool>(session));
          EXPECT_EQ(std::this_thread::get_id(), app_thread);
          ++ran;
        }));
    }
    ASSERT_TRUE(wait_until([&ran] {return ran.load() == 3;}));
    executor.shutdown();
    EXPECT_FALSE(executor.post("late", [](qaros::Session &) {}));
    EXPECT_FALSE(executor.abandoned());
  }
  {
    std::lock_guard<std::mutex> lock(mutex);
    stop = true;
  }
  cv.notify_all();
  app.join();
  const std::vector<std::string> expected = {"job-0", "job-1", "job-2"};
  EXPECT_EQ(names, expected);
}

TEST_F(BackendTest, TheFactoryDoesNotWaitLongerThanItsBudgetForABusyWorker)
{
  factory.reset();
  start_factory(150ms);
  Gate gate;
  fake.set_call_hook("video_sender_create", [&gate] {gate.wait();});
  auto sender = make_sender();
  ASSERT_TRUE(sender != nullptr);
  ASSERT_TRUE(wait_until([this] {return fake.count("video_sender_create") == 1;}));
  sender->close();
  senders.clear();
  sender.reset();

  const auto t0 = std::chrono::steady_clock::now();
  factory.reset();
  const auto took = std::chrono::steady_clock::now() - t0;
  EXPECT_GE(took, 100ms);
  EXPECT_LT(took, 3000ms);

  gate.open();
  session = qaros::Session();
  input.reset();
  ASSERT_TRUE(wait_until([this] {return fake.live_sessions() == 0 && fake.live_senders() == 0;}))
    << "the abandoned worker made the sender, ran the queued close and ended";
  EXPECT_EQ(fake.count("video_sender_handle_destroy"), 1);
}

TEST_F(BackendTest, AFactoryOnTheApplicationsExecutorPostsEveryTaskToIt)
{
  factory.reset();
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::function<void()>> queue;
  bool stop = false;
  std::thread::id app_thread;
  std::vector<std::string> names;
  std::thread app([&] {
      app_thread = std::this_thread::get_id();
      std::unique_lock<std::mutex> lock(mutex);
      for (;;) {
        cv.wait(lock, [&] {return stop || !queue.empty();});
        if (queue.empty()) {
          return;
        }
        auto task = std::move(queue.front());
        queue.pop_front();
        lock.unlock();
        task();
        lock.lock();
      }
    });
  {
    qb::FactoryOptions options;
    options.diagnostics = &sink;
    options.clock = [this] {return clock.now();};
    options.post = [&](const char * name, std::function<void()> task) {
        std::lock_guard<std::mutex> lock(mutex);
        names.push_back(name);
        queue.push_back(std::move(task));
        cv.notify_one();
        return true;
      };
    qb::QarosPanelSenderFactory own(session, input, options);
    auto sender = std::dynamic_pointer_cast<qb::QarosPanelSender>(
      own.create_panel_sender(make_desc()));
    ASSERT_TRUE(sender != nullptr);
    ASSERT_TRUE(wait_until([&sender] {return sender->ready();}));
    sender->close();
    sender.reset();
    ASSERT_TRUE(wait_until([this] {return fake.live_senders() == 0;}));
  }
  {
    std::lock_guard<std::mutex> lock(mutex);
    stop = true;
  }
  cv.notify_all();
  app.join();
  ASSERT_GE(names.size(), 2u);
  EXPECT_EQ(names.front(), "panel-create:a");
  EXPECT_EQ(names.back(), "panel-close:a");
  for (const auto & call : fake.calls()) {
    if (call.name != "session_handle_destroy" && !starts_with(call.name, "video_sender_try")) {
      EXPECT_EQ(call.thread, app_thread) << call.name;
    }
  }
}

// ----- conversions ---------------------------------------------------------------------------

TEST(Conversions, ValuesSurviveARoundTripBetweenTheSeamAndTheSdk)
{
  seam::Pose pose;
  pose.orientation = {0.1f, 0.2f, 0.3f, 0.9f};
  pose.position = {1.0f, 2.0f, 3.0f};
  const seam::Pose back = qb::to_seam(qb::to_sdk(pose));
  EXPECT_FLOAT_EQ(back.orientation.x, 0.1f);
  EXPECT_FLOAT_EQ(back.orientation.w, 0.9f);
  EXPECT_FLOAT_EQ(back.position.z, 3.0f);
  const qaros::Pose sdk = qb::to_sdk(pose);
  EXPECT_FLOAT_EQ(sdk.Orientation.Y, 0.2f);
  EXPECT_FLOAT_EQ(sdk.Position.X, 1.0f);

  const seam::PanelSize size{1.5f, 0.75f, 2.0f};
  const qaros::GuiPanelSize sdk_size = qb::to_sdk(size);
  EXPECT_FLOAT_EQ(sdk_size.WidthMeters, 1.5f);
  EXPECT_FLOAT_EQ(sdk_size.HeightMeters, 0.75f);
  EXPECT_FLOAT_EQ(sdk_size.ContentScale, 2.0f);
  EXPECT_FLOAT_EQ(qb::to_seam(sdk_size).content_scale, 2.0f);

  for (const seam::PanelState state :
    {seam::PanelState::Visible, seam::PanelState::Minimized, seam::PanelState::Hidden,
      seam::PanelState::Closed})
  {
    EXPECT_EQ(qb::to_seam(qb::to_sdk(state)), state);
  }
  EXPECT_EQ(qb::to_sdk(seam::PixelFormat::R8G8B8A8), qaros::PixelFormat::R8G8B8A8);
  EXPECT_EQ(qb::to_sdk(seam::PixelFormat::B8G8R8A8), qaros::PixelFormat::B8G8R8A8);

  qaros::PeerId peer;
  for (std::size_t i = 0; i < peer.Bytes.size(); ++i) {
    peer.Bytes[i] = static_cast<std::uint8_t>(i + 1);
  }
  const seam::PeerId converted = qb::to_seam(peer);
  for (std::size_t i = 0; i < seam::kPeerIdLength; ++i) {
    EXPECT_EQ(converted.data[i], i + 1);
  }
}
