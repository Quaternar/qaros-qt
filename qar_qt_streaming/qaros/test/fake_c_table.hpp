// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// A stand-in for the QarOS library, for the tests of the qaros backend. The C header resolves
// every call through the function tables g_qar_*_api (dynamic loading); this fake fills the
// entries the backend and the SDK wrapper can reach with functions of its own, so no test loads
// the real library and none can start a Hub.
//
// What it imitates: a subscription's callbacks run on a thread of the library, never on the
// caller's; ending a subscription (destroying its cancel token) waits for a callback that runs; the
// calls of the video sender hand out a CPU frame whose pitch the test chooses. Every call is
// recorded with its name and the thread it was made on.

#ifndef QAR_QT_STREAMING__QAROS__TEST__FAKE_C_TABLE_HPP_
#define QAR_QT_STREAMING__QAROS__TEST__FAKE_C_TABLE_HPP_

#include <qar_streaming_cpp/Session.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace qar_qt_streaming::qaros_backend::test
{

/// One call into the fake library.
struct Call
{
  std::string name;
  std::thread::id thread;
};

/// A panel of the fake session, as the tests see it.
struct FakePanel
{
  std::string common_name;
  std::string display_name;
  QarGuiPanelId id{};
  QarGuiPanelState state = QAR_GUI_PANEL_STATE_VISIBLE;
  bool is_url = false;
  bool has_stream = false;
  QarStreamId stream{};
  QarGuiPanelSize size{};
  QarPose pose{};
};

/// One gesture to deliver to a subscription.
struct FakeGesture
{
  std::uint8_t peer_tag = 1;
  std::uint64_t gesture_id = 1;
  QarGuiPanelGestureKind kind = QAR_GUI_PANEL_GESTURE_KIND_TAP;
  QarGesturePhase phase = QAR_GESTURE_PHASE_INSTANT;
  std::int32_t x_pixels = 0;
  std::int32_t y_pixels = 0;
  QarHandedness hand = QAR_HANDEDNESS_RIGHT;
  std::uint8_t tap_count = 1;
  /// A getter of the gesture handle answers with an error.
  bool unreadable = false;
};

class FakeCApi
{
public:
  FakeCApi();
  ~FakeCApi();
  FakeCApi(const FakeCApi &) = delete;
  FakeCApi & operator=(const FakeCApi &) = delete;

  /// A session over the fake: the handle is adopted by a qaros::Session, whose last release
  /// calls session_handle_destroy of the fake.
  qaros::Session make_session();

  // --- scripting -----------------------------------------------------------------------------
  /// Every later call named @p name (as calls() records it) fails with the status @p code. A code
  /// of 0 clears it.
  void set_failure(const std::string & name, int code);
  /// Runs on the calling thread of a call named @p name, before the call does its work. A test
  /// blocks the worker here.
  void set_call_hook(const std::string & name, std::function<void()> hook);
  /// Whether video_sender_try_begin_frame reserves a cycle (default: yes).
  void set_sender_ready(bool ready);
  /// The status video_sender_try_begin_frame answers with (default 0, success).
  void set_try_begin_status(int code);
  /// The pitch of the CPU frame in bytes; 0 (default) is width * 4.
  void set_frame_pitch(std::uint32_t pitch);
  /// A panel of an earlier session, listed and found by the common name.
  void add_existing_panel(const FakePanel & panel);

  // --- events --------------------------------------------------------------------------------
  /// Delivers a panel update to the updates subscription of the panel @p common_name, on a thread
  /// of the library, and returns when the callback has returned. False when there is none.
  bool emit_panel_update(const std::string & common_name, QarGuiPanelState state);
  /// Delivers a gesture the same way to the gesture subscription of the panel.
  bool emit_gesture(const std::string & common_name, const FakeGesture & gesture);

  // --- observations --------------------------------------------------------------------------
  std::vector<Call> calls() const;
  std::vector<std::string> call_names() const;
  int count(const std::string & name) const;
  /// The names of the calls that match @p wanted, in order.
  std::vector<std::string> names_in(const std::set<std::string> & wanted) const;
  /// The threads the subscription callbacks ran on.
  std::set<std::thread::id> callback_threads() const;
  /// The thread session_handle_destroy ran on (default id if it did not run).
  std::thread::id session_destroy_thread() const;
  FakePanel panel(const std::string & common_name) const;
  bool has_panel(const std::string & common_name) const;
  /// The common names get_or_create was asked for, in order.
  std::vector<std::string> requested_names() const;
  int frames_shown() const;
  int live_senders() const;
  int live_sessions() const;
  int live_tokens() const;
  /// The bytes of the destination of the last frame (rows of the pitch).
  std::vector<std::uint8_t> last_frame() const;
  std::uint32_t layout_width() const;
  std::uint32_t layout_height() const;

  struct State;

private:
  std::unique_ptr<State> state_;
};

}  // namespace qar_qt_streaming::qaros_backend::test

#endif  // QAR_QT_STREAMING__QAROS__TEST__FAKE_C_TABLE_HPP_
