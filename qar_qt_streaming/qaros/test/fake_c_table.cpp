// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "fake_c_table.hpp"

#include "qar_qt_streaming/qaros/sdk_config_check.hpp"

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <utility>

// The one expansion of the function tables of the C header in the test program. The library
// itself expands none: the program that links it does, once.
QAR_IMPLEMENT_DYNAMIC_LOADING()

namespace qar_qt_streaming::qaros_backend::test
{

namespace
{

QarResult
ok_result()
{
  QarResult result{};
  result.code = QAR_STATUS_SUCCESS;
  result.error_handle = 0;
  return result;
}

QarResult
error_result(int code)
{
  QarResult result{};
  result.code = static_cast<QarStatusCode>(code);
  result.error_handle = 1;
  return result;
}

struct FakeToken
{
  bool cancelled = false;
  std::string kind;  ///< "gestures" or "updates" once subscribed; empty before.
};

struct FakeSession
{
  int magic = 0x5E55;
};

struct FakeSender
{
  QarStreamId stream{};
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t pitch = 0;
  std::vector<std::uint8_t> buffer;
};

struct PanelHandle
{
  FakePanel panel;
};

struct GestureHandle
{
  FakeGesture gesture;
};

}  // namespace

struct FakeCApi::State
{
  struct Subscription
  {
    FakeToken * token = nullptr;
    QarGuiPanelId panel{};
    qar_gui_panel_update_callback_t update_callback = nullptr;
    qar_gui_panel_gesture_event_callback_t gesture_callback = nullptr;
    void * user = nullptr;
    int running = 0;
    bool active = true;
    std::string kind;
  };

  mutable std::mutex mutex;
  std::condition_variable cv;
  std::vector<Call> calls;
  std::map<std::string, int> failures;
  std::map<std::string, std::function<void()>> hooks;
  std::vector<FakePanel> panels;
  std::vector<std::string> requested;
  std::list<Subscription> subscriptions;
  std::set<std::thread::id> callback_threads;
  std::thread::id session_destroy_thread;
  bool sender_ready = true;
  int try_begin_status = 0;
  std::uint32_t frame_pitch = 0;
  std::uint32_t last_width = 0;
  std::uint32_t last_height = 0;
  std::vector<std::uint8_t> last_frame;
  FakeSender * last_sender = nullptr;
  std::atomic<int> live_senders{0};
  std::atomic<int> live_sessions{0};
  std::atomic<int> live_tokens{0};
  std::atomic<int> frames_shown{0};
  std::atomic<int> next_sender{1};
};

namespace
{

FakeCApi::State * g_state = nullptr;

/// Records the call and runs its hook; returns the status a test scripted for it, or 0.
int
enter(const char * name)
{
  std::function<void()> hook;
  int forced = 0;
  {
    std::lock_guard<std::mutex> lock(g_state->mutex);
    g_state->calls.push_back(Call{name, std::this_thread::get_id()});
    const auto failure = g_state->failures.find(name);
    if (failure != g_state->failures.end()) {
      forced = failure->second;
    }
    const auto found = g_state->hooks.find(name);
    if (found != g_state->hooks.end()) {
      hook = found->second;
    }
  }
  if (hook) {
    hook();
  }
  return forced;
}

void
record_only(const std::string & name)
{
  std::lock_guard<std::mutex> lock(g_state->mutex);
  g_state->calls.push_back(Call{name, std::this_thread::get_id()});
}

QarGuiPanelId
panel_id_for(const std::string & name)
{
  QarGuiPanelId id{};
  std::uint32_t hash = 2166136261u;
  for (const char c : name) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 16777619u;
  }
  for (int i = 0; i < QAR_MAX_ID_LENGTH; ++i) {
    id.data[i] = static_cast<std::uint8_t>((hash >> ((i % 4) * 8)) + i);
  }
  return id;
}

FakePanel *
find_panel(const QarGuiPanelId * id)
{
  for (FakePanel & panel : g_state->panels) {
    if (std::memcmp(panel.id.data, id->data, sizeof(id->data)) == 0) {
      return &panel;
    }
  }
  return nullptr;
}

QarResult
copy_text(const std::string & text, char * out, std::size_t size)
{
  if (out == nullptr || size == 0) {
    return error_result(QAR_STATUS_ARGUMENT_NOT_SUPPORTED);
  }
  std::snprintf(out, size, "%s", text.c_str());
  return ok_result();
}

// --- result ---------------------------------------------------------------------------------

bool fake_result_is_error(QarResult result) {return result.code != QAR_STATUS_SUCCESS;}
bool fake_result_is_success(QarResult result) {return result.code == QAR_STATUS_SUCCESS;}
bool fake_result_has_code(QarResult result, QarStatusCode code) {return result.code == code;}

void
fake_result_message(QarResult result, char * buffer, size_t size)
{
  if (buffer != nullptr && size > 0) {
    std::snprintf(buffer, size, "fake message for code %d", static_cast<int>(result.code));
  }
}

void fake_result_log_if_error(QarResult) {}

// --- cancel tokens --------------------------------------------------------------------------

QarResult
fake_cancel_token_create(QarCancelToken ** out)
{
  record_only("cancel_token_create");
  *out = reinterpret_cast<QarCancelToken *>(new FakeToken());
  g_state->live_tokens.fetch_add(1);
  return ok_result();
}

/// Marks the subscriptions of @p token inactive and waits for a callback of theirs that runs.
void
end_subscriptions(FakeToken * token)
{
  std::unique_lock<std::mutex> lock(g_state->mutex);
  for (auto & sub : g_state->subscriptions) {
    if (sub.token == token) {
      sub.active = false;
    }
  }
  g_state->cv.wait(lock, [token] {
      for (const auto & sub : g_state->subscriptions) {
        if (sub.token == token && sub.running > 0) {
          return false;
        }
      }
      return true;
    });
}

QarResult
fake_cancel_token_cancel(QarCancelToken * handle)
{
  record_only("cancel_token_cancel");
  auto * token = reinterpret_cast<FakeToken *>(handle);
  {
    std::lock_guard<std::mutex> lock(g_state->mutex);
    token->cancelled = true;
  }
  end_subscriptions(token);
  return ok_result();
}

void
fake_cancel_token_handle_destroy(QarCancelToken * handle)
{
  auto * token = reinterpret_cast<FakeToken *>(handle);
  std::string kind;
  {
    std::lock_guard<std::mutex> lock(g_state->mutex);
    kind = token->kind;
  }
  // The call that ends a subscription is named after what it ends, so that a test can read the
  // close order off the list of calls.
  enter(kind.empty() ? "cancel_token_destroy" : ("cancel_token_destroy:" + kind).c_str());
  end_subscriptions(token);
  {
    std::lock_guard<std::mutex> lock(g_state->mutex);
    g_state->subscriptions.remove_if(
      [token](const FakeCApi::State::Subscription & sub) {return sub.token == token;});
  }
  delete token;
  g_state->live_tokens.fetch_sub(1);
}

void
fake_session_handle_destroy(QarSession * handle)
{
  {
    std::lock_guard<std::mutex> lock(g_state->mutex);
    g_state->calls.push_back(Call{"session_handle_destroy", std::this_thread::get_id()});
    g_state->session_destroy_thread = std::this_thread::get_id();
  }
  delete reinterpret_cast<FakeSession *>(handle);
  g_state->live_sessions.fetch_sub(1);
}

// --- gui panels -----------------------------------------------------------------------------

QarResult
fake_gui_panels_get_or_create(QarSession *, const QarGuiPanelInit * init, QarGuiPanelId * out)
{
  const int forced = enter("gui_panels_get_or_create");
  if (forced != 0) {
    return error_result(forced);
  }
  std::lock_guard<std::mutex> lock(g_state->mutex);
  const std::string name = init->common_name != nullptr ? init->common_name : "";
  g_state->requested.push_back(name);
  const QarGuiPanelId id = panel_id_for(name);
  if (FakePanel * existing = find_panel(&id)) {
    *out = existing->id;
    return ok_result();
  }
  FakePanel panel;
  panel.common_name = name;
  panel.display_name = init->display_name != nullptr ? init->display_name : "";
  panel.id = id;
  panel.size = init->size;
  panel.pose = init->pose;
  for (const auto * header = static_cast<const QarStructureHeader *>(init->header.next);
    header != nullptr; header = static_cast<const QarStructureHeader *>(header->next))
  {
    if (header->type == QAR_STRUCTURE_TYPE_GUI_PANEL_STREAM_ID_EXT) {
      panel.has_stream = true;
      panel.stream = reinterpret_cast<const QarGuiPanelStreamIdExt *>(header)->stream_id;
    } else if (header->type == QAR_STRUCTURE_TYPE_GUI_PANEL_URL_EXT) {
      panel.is_url = true;
    }
  }
  if (!panel.has_stream) {
    panel.is_url = true;
  }
  g_state->panels.push_back(panel);
  *out = id;
  return ok_result();
}

template<typename Change>
QarResult
change_panel(const char * name, const QarGuiPanelId * id, Change change)
{
  const int forced = enter(name);
  if (forced != 0) {
    return error_result(forced);
  }
  std::lock_guard<std::mutex> lock(g_state->mutex);
  FakePanel * panel = find_panel(id);
  if (panel == nullptr) {
    return error_result(QAR_STATUS_LOGIC_ERROR);
  }
  change(*panel);
  return ok_result();
}

QarResult
fake_gui_panels_update_pose(QarSession *, const QarGuiPanelId * id, const QarPose * pose)
{
  return change_panel(
    "gui_panels_update_pose", id, [pose](FakePanel & panel) {panel.pose = *pose;});
}

QarResult
fake_gui_panels_change_size(QarSession *, const QarGuiPanelId * id, const QarGuiPanelSize * size)
{
  return change_panel(
    "gui_panels_change_size", id, [size](FakePanel & panel) {panel.size = *size;});
}

QarResult
fake_gui_panels_set_state(QarSession *, const QarGuiPanelId * id, QarGuiPanelState state)
{
  return change_panel(
    "gui_panels_set_state", id, [state](FakePanel & panel) {panel.state = state;});
}

QarResult
fake_gui_panels_close_panel(QarSession *, const QarGuiPanelId * id)
{
  return change_panel(
    "gui_panels_close_panel", id,
    [](FakePanel & panel) {panel.state = QAR_GUI_PANEL_STATE_CLOSED;});
}

QarResult
fake_gui_panels_set_stream(QarSession *, const QarGuiPanelId * id, const QarStreamId * stream)
{
  return change_panel(
    "gui_panels_set_stream", id, [stream](FakePanel & panel) {
      panel.is_url = false;
      panel.has_stream = true;
      panel.stream = *stream;
    });
}

QarResult
subscribe(
  const char * name, const char * kind, const QarGuiPanelId * id,
  qar_gui_panel_update_callback_t update_callback,
  qar_gui_panel_gesture_event_callback_t gesture_callback, void * user, QarCancelToken * token)
{
  const int forced = enter(name);
  if (forced != 0) {
    return error_result(forced);
  }
  std::lock_guard<std::mutex> lock(g_state->mutex);
  FakeCApi::State::Subscription sub;
  sub.token = reinterpret_cast<FakeToken *>(token);
  sub.token->kind = kind;
  sub.panel = *id;
  sub.update_callback = update_callback;
  sub.gesture_callback = gesture_callback;
  sub.user = user;
  sub.kind = kind;
  g_state->subscriptions.push_back(sub);
  return ok_result();
}

QarResult
fake_gui_panels_subscribe_all_gesture_events(
  QarSession *, const QarGuiPanelId * id, qar_gui_panel_gesture_event_callback_t callback,
  void * user, QarCancelToken * token)
{
  return subscribe(
    "gui_panels_subscribe_all_gesture_events", "gestures", id, nullptr, callback, user, token);
}

QarResult
fake_gui_panels_subscribe_panel_updates(
  QarSession *, const QarGuiPanelId * id, qar_gui_panel_update_callback_t callback, void * user,
  QarCancelToken * token)
{
  return subscribe(
    "gui_panels_subscribe_panel_updates", "updates", id, callback, nullptr, user, token);
}

QarResult
fake_query_gui_panels_count(QarSession *, size_t * count)
{
  const int forced = enter("query_gui_panels_count");
  if (forced != 0) {
    return error_result(forced);
  }
  std::lock_guard<std::mutex> lock(g_state->mutex);
  *count = g_state->panels.size();
  return ok_result();
}

QarResult
fake_query_gui_panels(QarSession *, QarGuiPanel ** handles, size_t capacity, size_t * written)
{
  const int forced = enter("query_gui_panels");
  if (forced != 0) {
    return error_result(forced);
  }
  std::lock_guard<std::mutex> lock(g_state->mutex);
  size_t n = 0;
  for (; n < g_state->panels.size() && n < capacity; ++n) {
    handles[n] = reinterpret_cast<QarGuiPanel *>(new PanelHandle{g_state->panels[n]});
  }
  *written = n;
  return ok_result();
}

const FakePanel &
as_panel(QarGuiPanel * handle)
{
  return reinterpret_cast<PanelHandle *>(handle)->panel;
}

void
fake_gui_panel_handle_destroy(QarGuiPanel * handle)
{
  delete reinterpret_cast<PanelHandle *>(handle);
}

QarResult
fake_gui_panel_get_id(QarGuiPanel * handle, QarGuiPanelId * out)
{
  *out = as_panel(handle).id;
  return ok_result();
}

QarResult
fake_gui_panel_get_display_name(QarGuiPanel * handle, char * out, size_t size)
{
  return copy_text(as_panel(handle).display_name, out, size);
}

QarResult
fake_gui_panel_get_pose(QarGuiPanel * handle, QarPose * out)
{
  *out = as_panel(handle).pose;
  return ok_result();
}

QarResult
fake_gui_panel_get_size(QarGuiPanel * handle, QarGuiPanelSize * out)
{
  *out = as_panel(handle).size;
  return ok_result();
}

QarResult
fake_gui_panel_get_state(QarGuiPanel * handle, QarGuiPanelState * out)
{
  *out = as_panel(handle).state;
  return ok_result();
}

QarResult
fake_gui_panel_get_content_type(QarGuiPanel * handle, QarGuiPanelContentType * out)
{
  *out = as_panel(handle).is_url ? QAR_GUI_PANEL_CONTENT_TYPE_WEBSITE_URI :
    QAR_GUI_PANEL_CONTENT_TYPE_STREAM_ID;
  return ok_result();
}

QarResult
fake_gui_panel_get_content_stream_id(QarGuiPanel * handle, QarStreamId * out)
{
  *out = as_panel(handle).stream;
  return ok_result();
}

QarResult
fake_gui_panel_get_content_uri(QarGuiPanel *, char * out, size_t size)
{
  return copy_text("about:blank", out, size);
}

QarResult
fake_gui_panel_get_visible_to_peers_count(QarGuiPanel *, size_t * count)
{
  *count = 0;
  return ok_result();
}

QarResult
fake_gui_panel_get_visible_to_peers(QarGuiPanel *, QarPeerId *, size_t, size_t * written)
{
  *written = 0;
  return ok_result();
}

// --- gesture handles ------------------------------------------------------------------------

const FakeGesture &
as_gesture(QarGuiPanelGesture * handle)
{
  return reinterpret_cast<GestureHandle *>(handle)->gesture;
}

QarResult
fake_gesture_lifecycle(QarGuiPanelGesture * handle, QarPeerId * peer, uint64_t * id)
{
  const FakeGesture & gesture = as_gesture(handle);
  if (gesture.unreadable) {
    return error_result(QAR_STATUS_LOGIC_ERROR);
  }
  for (int i = 0; i < QAR_MAX_ID_LENGTH; ++i) {
    peer->data[i] = gesture.peer_tag;
  }
  *id = gesture.gesture_id;
  return ok_result();
}

QarResult
fake_gesture_classification(
  QarGuiPanelGesture * handle, QarGuiPanelGestureKind * kind, QarGesturePhase * phase)
{
  *kind = as_gesture(handle).kind;
  *phase = as_gesture(handle).phase;
  return ok_result();
}

QarResult
fake_gesture_positions(
  QarGuiPanelGesture * handle, QarGuiPanelPoint * position, QarGuiPanelPoint * start)
{
  position->x_pixels = as_gesture(handle).x_pixels;
  position->y_pixels = as_gesture(handle).y_pixels;
  *start = *position;
  return ok_result();
}

QarResult
fake_gesture_panel_id(QarGuiPanelGesture *, QarGuiPanelId * out)
{
  *out = QarGuiPanelId{};
  return ok_result();
}

QarResult
fake_gesture_timestamp(QarGuiPanelGesture *, QarTimePoint * out)
{
  out->unix_ns = 0;
  return ok_result();
}

QarResult
fake_gesture_handedness(QarGuiPanelGesture * handle, QarHandedness * out)
{
  *out = as_gesture(handle).hand;
  return ok_result();
}

QarResult
fake_gesture_tap_count(QarGuiPanelGesture * handle, uint8_t * out)
{
  *out = as_gesture(handle).tap_count;
  return ok_result();
}

// --- video senders --------------------------------------------------------------------------

QarResult
fake_video_sender_create(QarSession *, const QarVideoSenderInit * init, QarVideoSender ** out)
{
  const int forced = enter("video_sender_create");
  if (forced != 0) {
    *out = nullptr;
    return error_result(forced);
  }
  auto * sender = new FakeSender();
  const int n = g_state->next_sender.fetch_add(1);
  for (int i = 0; i < QAR_MAX_ID_LENGTH; ++i) {
    sender->stream.data[i] = static_cast<std::uint8_t>(0xC0 + n + i);
  }
  sender->width = init->layout.textures[0].width;
  sender->height = init->layout.textures[0].height;
  {
    std::lock_guard<std::mutex> lock(g_state->mutex);
    sender->pitch = g_state->frame_pitch != 0 ? g_state->frame_pitch : sender->width * 4u;
    g_state->last_width = sender->width;
    g_state->last_height = sender->height;
    g_state->last_sender = sender;
  }
  sender->buffer.assign(static_cast<std::size_t>(sender->pitch) * sender->height, 0);
  g_state->live_senders.fetch_add(1);
  *out = reinterpret_cast<QarVideoSender *>(sender);
  return ok_result();
}

QarResult
fake_video_sender_stream_id(QarVideoSender * handle, QarStreamId * out)
{
  enter("video_sender_stream_id");
  *out = reinterpret_cast<FakeSender *>(handle)->stream;
  return ok_result();
}

QarResult
fake_video_sender_try_begin_frame(QarVideoSender *, bool * out_ready)
{
  const int forced = enter("video_sender_try_begin_frame");
  std::lock_guard<std::mutex> lock(g_state->mutex);
  *out_ready = false;
  if (forced != 0) {
    return error_result(forced);
  }
  if (g_state->try_begin_status != 0) {
    return error_result(g_state->try_begin_status);
  }
  *out_ready = g_state->sender_ready;
  return ok_result();
}

QarResult
fake_video_sender_frame_cpu(QarVideoSender * handle, QarVideoFrameCpu * out)
{
  const int forced = enter("video_sender_frame_cpu");
  if (forced != 0) {
    return error_result(forced);
  }
  auto * sender = reinterpret_cast<FakeSender *>(handle);
  out->texture_views_count = 0;
  out->textures_count = 1;
  out->textures[0].size.width = sender->width;
  out->textures[0].size.height = sender->height;
  out->textures[0].size.array_layers = 1;
  out->textures[0].pitch = sender->pitch;
  out->textures[0].texture_data = sender->buffer.data();
  out->textures[0].texture_data_size = sender->buffer.size();
  return ok_result();
}

QarResult
fake_video_sender_show_frame(QarVideoSender * handle)
{
  const int forced = enter("video_sender_show_frame");
  if (forced != 0) {
    return error_result(forced);
  }
  auto * sender = reinterpret_cast<FakeSender *>(handle);
  {
    std::lock_guard<std::mutex> lock(g_state->mutex);
    g_state->last_frame = sender->buffer;
  }
  g_state->frames_shown.fetch_add(1);
  return ok_result();
}

void
fake_video_sender_handle_destroy(QarVideoSender * handle)
{
  enter("video_sender_handle_destroy");
  auto * sender = reinterpret_cast<FakeSender *>(handle);
  {
    std::lock_guard<std::mutex> lock(g_state->mutex);
    if (g_state->last_sender == sender) {
      g_state->last_sender = nullptr;
    }
  }
  delete sender;
  g_state->live_senders.fetch_sub(1);
}

/// Callbacks of the library: the call that delivers one, run on a thread of the library.
template<typename Body>
void
deliver_on_library_thread(const Body & body)
{
  std::thread library([&body] {
      {
        std::lock_guard<std::mutex> lock(g_state->mutex);
        g_state->callback_threads.insert(std::this_thread::get_id());
      }
      body();
    });
  library.join();
}

}  // namespace

// ----------------------------------------------------------------------------------------------
// FakeCApi
// ----------------------------------------------------------------------------------------------

FakeCApi::FakeCApi()
: state_(std::make_unique<State>())
{
  g_state = state_.get();

  g_qar_result_api.result_is_error = &fake_result_is_error;
  g_qar_result_api.result_is_success = &fake_result_is_success;
  g_qar_result_api.result_has_code = &fake_result_has_code;
  g_qar_result_api.result_message = &fake_result_message;
  g_qar_result_api.result_log_if_error = &fake_result_log_if_error;

  g_qar_cancelation_token_api.cancel_token_create = &fake_cancel_token_create;
  g_qar_cancelation_token_api.cancel_token_cancel = &fake_cancel_token_cancel;
  g_qar_cancelation_token_api.cancel_token_handle_destroy = &fake_cancel_token_handle_destroy;

  g_qar_session_api.session_handle_destroy = &fake_session_handle_destroy;

  g_qar_gui_panels_api.gui_panels_get_or_create = &fake_gui_panels_get_or_create;
  g_qar_gui_panels_api.gui_panels_update_pose = &fake_gui_panels_update_pose;
  g_qar_gui_panels_api.gui_panels_change_size = &fake_gui_panels_change_size;
  g_qar_gui_panels_api.gui_panels_set_state = &fake_gui_panels_set_state;
  g_qar_gui_panels_api.gui_panels_close_panel = &fake_gui_panels_close_panel;
  g_qar_gui_panels_api.gui_panels_set_stream = &fake_gui_panels_set_stream;
  g_qar_gui_panels_api.gui_panels_subscribe_all_gesture_events =
    &fake_gui_panels_subscribe_all_gesture_events;
  g_qar_gui_panels_api.gui_panels_subscribe_panel_updates =
    &fake_gui_panels_subscribe_panel_updates;
  g_qar_gui_panels_api.query_gui_panels_count = &fake_query_gui_panels_count;
  g_qar_gui_panels_api.query_gui_panels = &fake_query_gui_panels;
  g_qar_gui_panels_api.gui_panel_handle_destroy = &fake_gui_panel_handle_destroy;
  g_qar_gui_panels_api.gui_panel_get_id = &fake_gui_panel_get_id;
  g_qar_gui_panels_api.gui_panel_get_display_name = &fake_gui_panel_get_display_name;
  g_qar_gui_panels_api.gui_panel_get_pose = &fake_gui_panel_get_pose;
  g_qar_gui_panels_api.gui_panel_get_size = &fake_gui_panel_get_size;
  g_qar_gui_panels_api.gui_panel_get_state = &fake_gui_panel_get_state;
  g_qar_gui_panels_api.gui_panel_get_content_type = &fake_gui_panel_get_content_type;
  g_qar_gui_panels_api.gui_panel_get_content_stream_id = &fake_gui_panel_get_content_stream_id;
  g_qar_gui_panels_api.gui_panel_get_content_uri = &fake_gui_panel_get_content_uri;
  g_qar_gui_panels_api.gui_panel_get_visible_to_peers_count =
    &fake_gui_panel_get_visible_to_peers_count;
  g_qar_gui_panels_api.gui_panel_get_visible_to_peers = &fake_gui_panel_get_visible_to_peers;
  g_qar_gui_panels_api.gui_panel_gesture_get_lifecycle = &fake_gesture_lifecycle;
  g_qar_gui_panels_api.gui_panel_gesture_get_classification = &fake_gesture_classification;
  g_qar_gui_panels_api.gui_panel_gesture_get_positions = &fake_gesture_positions;
  g_qar_gui_panels_api.gui_panel_gesture_get_panel_id = &fake_gesture_panel_id;
  g_qar_gui_panels_api.gui_panel_gesture_get_timestamp = &fake_gesture_timestamp;
  g_qar_gui_panels_api.gui_panel_gesture_get_handedness = &fake_gesture_handedness;
  g_qar_gui_panels_api.gui_panel_gesture_get_tap_count = &fake_gesture_tap_count;

  g_qar_video_sender_api.video_sender_create = &fake_video_sender_create;
  g_qar_video_sender_api.video_sender_stream_id = &fake_video_sender_stream_id;
  g_qar_video_sender_api.video_sender_try_begin_frame = &fake_video_sender_try_begin_frame;
  g_qar_video_sender_api.video_sender_frame_cpu = &fake_video_sender_frame_cpu;
  g_qar_video_sender_api.video_sender_show_frame = &fake_video_sender_show_frame;
  g_qar_video_sender_api.video_sender_handle_destroy = &fake_video_sender_handle_destroy;
}

FakeCApi::~FakeCApi()
{
  g_qar_result_api = QarResultLoadedApi{};
  g_qar_cancelation_token_api = QarCancelationTokenLoadedApi{};
  g_qar_session_api = QarSessionLoadedApi{};
  g_qar_gui_panels_api = QarGuiPanelsLoadedApi{};
  g_qar_video_sender_api = QarVideoSenderLoadedApi{};
  g_state = nullptr;
}

qaros::Session
FakeCApi::make_session()
{
  state_->live_sessions.fetch_add(1);
  qaros::SessionPtr owned(reinterpret_cast<QarSession *>(new FakeSession()));
  return qaros::Session(std::move(owned));
}

void
FakeCApi::set_failure(const std::string & name, int code)
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (code == 0) {
    state_->failures.erase(name);
  } else {
    state_->failures[name] = code;
  }
}

void
FakeCApi::set_call_hook(const std::string & name, std::function<void()> hook)
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (hook) {
    state_->hooks[name] = std::move(hook);
  } else {
    state_->hooks.erase(name);
  }
}

void
FakeCApi::set_sender_ready(bool ready)
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  state_->sender_ready = ready;
}

void
FakeCApi::set_try_begin_status(int code)
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  state_->try_begin_status = code;
}

void
FakeCApi::set_frame_pitch(std::uint32_t pitch)
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  state_->frame_pitch = pitch;
}

void
FakeCApi::add_existing_panel(const FakePanel & panel)
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  FakePanel stored = panel;
  stored.id = panel_id_for(panel.common_name);
  state_->panels.push_back(stored);
}

bool
FakeCApi::emit_panel_update(const std::string & common_name, QarGuiPanelState state)
{
  State::Subscription sub;
  PanelHandle handle;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const QarGuiPanelId id = panel_id_for(common_name);
    FakePanel * panel = find_panel(&id);
    if (panel == nullptr) {
      return false;
    }
    panel->state = state;
    handle.panel = *panel;
    bool found = false;
    for (auto & candidate : state_->subscriptions) {
      if (candidate.kind == "updates" && candidate.active &&
        std::memcmp(candidate.panel.data, id.data, sizeof(id.data)) == 0)
      {
        sub = candidate;
        ++candidate.running;
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  deliver_on_library_thread([&sub, &handle] {
      sub.update_callback(reinterpret_cast<QarGuiPanel *>(&handle), sub.user);
    });
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    for (auto & candidate : state_->subscriptions) {
      if (candidate.token == sub.token) {
        --candidate.running;
      }
    }
  }
  state_->cv.notify_all();
  return true;
}

bool
FakeCApi::emit_gesture(const std::string & common_name, const FakeGesture & gesture)
{
  State::Subscription sub;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const QarGuiPanelId id = panel_id_for(common_name);
    bool found = false;
    for (auto & candidate : state_->subscriptions) {
      if (candidate.kind == "gestures" && candidate.active &&
        std::memcmp(candidate.panel.data, id.data, sizeof(id.data)) == 0)
      {
        sub = candidate;
        ++candidate.running;
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  GestureHandle handle{gesture};
  deliver_on_library_thread([&sub, &handle] {
      sub.gesture_callback(reinterpret_cast<QarGuiPanelGesture *>(&handle), sub.user);
    });
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    for (auto & candidate : state_->subscriptions) {
      if (candidate.token == sub.token) {
        --candidate.running;
      }
    }
  }
  state_->cv.notify_all();
  return true;
}

std::vector<Call>
FakeCApi::calls() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->calls;
}

std::vector<std::string>
FakeCApi::call_names() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  std::vector<std::string> names;
  for (const Call & call : state_->calls) {
    names.push_back(call.name);
  }
  return names;
}

int
FakeCApi::count(const std::string & name) const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  int n = 0;
  for (const Call & call : state_->calls) {
    if (call.name == name) {
      ++n;
    }
  }
  return n;
}

std::vector<std::string>
FakeCApi::names_in(const std::set<std::string> & wanted) const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  std::vector<std::string> names;
  for (const Call & call : state_->calls) {
    if (wanted.count(call.name) != 0) {
      names.push_back(call.name);
    }
  }
  return names;
}

std::set<std::thread::id>
FakeCApi::callback_threads() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->callback_threads;
}

std::thread::id
FakeCApi::session_destroy_thread() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->session_destroy_thread;
}

FakePanel
FakeCApi::panel(const std::string & common_name) const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  for (const FakePanel & panel : state_->panels) {
    if (panel.common_name == common_name) {
      return panel;
    }
  }
  return FakePanel();
}

bool
FakeCApi::has_panel(const std::string & common_name) const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  for (const FakePanel & panel : state_->panels) {
    if (panel.common_name == common_name) {
      return true;
    }
  }
  return false;
}

std::vector<std::string>
FakeCApi::requested_names() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->requested;
}

int FakeCApi::frames_shown() const {return state_->frames_shown.load();}
int FakeCApi::live_senders() const {return state_->live_senders.load();}
int FakeCApi::live_sessions() const {return state_->live_sessions.load();}
int FakeCApi::live_tokens() const {return state_->live_tokens.load();}

std::vector<std::uint8_t>
FakeCApi::last_frame() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->last_frame;
}

std::uint32_t
FakeCApi::layout_width() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->last_width;
}

std::uint32_t
FakeCApi::layout_height() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->last_height;
}

}  // namespace qar_qt_streaming::qaros_backend::test
