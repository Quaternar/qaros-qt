// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/qaros/qaros_panel_sender.hpp"

#include "backend_detail.hpp"
#include "qar_qt_streaming/qaros/conversions.hpp"
#include "qar_qt_streaming/qaros/sdk_config_check.hpp"
#include "qar_qt_streaming/seam/panel_attach.hpp"
#include "qar_qt_streaming/seam/panel_state_guard.hpp"

#include <qar_streaming_cpp/Session.hpp>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace qar_qt_streaming::qaros_backend
{

namespace
{

constexpr const char * kCategory = "qar-panel-adapter";

/// Local wall-clock time as `HH:MM:SS.mmm`, the shape of the host's own log lines, so that a close
/// can be lined up against them.
std::string
local_time_hms_ms()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &seconds);
#else
  localtime_r(&seconds, &local);
#endif
  const auto millis =
    std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  char buf[16] = {0};
  std::snprintf(
    buf, sizeof(buf) - 1, "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec,
    static_cast<int>(millis));
  return std::string(buf);
}

/// Canonical 8-4-4-4-12 UUID text of one of the runtime's 16-byte ids, the form in which the
/// runtime names the same id in its own log.
template<typename Tag>
std::string
id_text(const qaros::Id<Tag> & id)
{
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(36);
  for (std::size_t i = 0; i < id.Bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out.push_back('-');
    }
    out.push_back(kHex[(id.Bytes[i] >> 4) & 0x0F]);
    out.push_back(kHex[id.Bytes[i] & 0x0F]);
  }
  return out;
}

bool
same_panel_size(const qaros::GuiPanelSize & a, const qaros::GuiPanelSize & b)
{
  return std::fabs(a.WidthMeters - b.WidthMeters) < 1e-4f &&
         std::fabs(a.HeightMeters - b.HeightMeters) < 1e-4f &&
         std::fabs(a.ContentScale - b.ContentScale) < 1e-4f;
}

/// The single-texture, single-view CPU layout of one panel: one plain 2D image and one
/// full-extent colour view with no eye, deliberately unlike a stereo render layout.
qaros::VideoFrameLayout
make_panel_layout(const seam::PanelDesc & desc)
{
  qaros::VideoFrameLayout layout;
  qaros::TextureSize texture;
  texture.Format = to_sdk(desc.pixel_format);
  texture.Width = desc.buffer_width;
  texture.Height = desc.buffer_height;
  texture.ArrayLayers = 1;
  layout.Textures.push_back(texture);

  qaros::VideoFrameView view;
  view.StartX = 0;
  view.StartY = 0;
  view.EndX = desc.buffer_width;  // the end values are exclusive
  view.EndY = desc.buffer_height;
  view.TextureIndex = 0;
  view.ArrayLayerIndex = 0;
  view.TextureFormat = to_sdk(desc.pixel_format);
  view.Type = qaros::VideoFrameViewType::Color;
  view.Eye = qaros::VideoFrameViewEye::None;
  layout.Views.push_back(view);
  return layout;
}

/// Reads the panel @p id out of the session's panel list: its state, its kind of content, its
/// stream and its size. A panel that is not listed, or a list that cannot be read, is "not
/// found", which is the decision "created" with nothing to carry out.
seam::PanelProbe
probe_panel(
  const qaros::GuiPanelControl & panels, const qaros::GuiPanelId & id,
  const qaros::StreamId & ours, const qaros::GuiPanelSize & wanted)
{
  seam::PanelProbe probe;
  const qaros::Result<std::vector<qaros::GuiPanel>> listed = panels.GetAll();
  if (!listed.Ok()) {
    return probe;
  }
  for (const qaros::GuiPanel & panel : listed.Value()) {
    if (panel.Id != id) {
      continue;
    }
    probe.found = true;
    probe.state = to_seam(panel.State);
    probe.content_mismatch = panel.ContentType == qaros::GuiPanelContentType::WebsiteUri;
    probe.content_differs = true;
    if (!probe.content_mismatch && panel.ContentStreamId.has_value()) {
      probe.content_differs = *panel.ContentStreamId != ours;
    }
    probe.size_differs = !same_panel_size(panel.Size, wanted);
    break;
  }
  return probe;
}

/// The attach line of the seam without its bracketed tag: the tag is the category of the message.
std::string
attach_line_text(const seam::PanelAttachLogFields & fields)
{
  std::string line = seam::panel_attach_log_line(fields);
  const std::string tag = "[qar-panel-adapter] ";
  if (line.compare(0, tag.size(), tag) == 0) {
    line.erase(0, tag.size());
  }
  return line;
}

}  // namespace

// What the tasks of the worker and the Qt thread share. The tasks hold it by shared_ptr, so it
// outlives a sender that was released while its tasks are queued. The video sender and the panel
// are made and destroyed on the worker; the Qt thread uses the sender for the frame calls only
// between the moment `frames_open` is set (the panel exists and shows the stream) or `phase`
// reads Ready, and the moment it closes the sender. After that moment the worker touches the
// video sender in the teardown only, which a close queues behind the create task.
struct QarosPanelSender::Core : std::enable_shared_from_this<Core>
{
  enum class Phase { kCreating, kReady, kFailed, kClosed };

  Core(Context ctx, const seam::PanelDesc & d)
  : context(std::move(ctx)), desc(d), common_name(d.common_name) {}

  ~Core()
  {
    // The teardown task has ended the subscription and released the sender. This is for a sender
    // whose teardown could not be posted because the executor was gone.
    updates.End();
    sender.reset();
  }

  Clock::time_point now() const { return context.clock ? context.clock() : Clock::now(); }

  void message(seam::DiagnosticSeverity severity, const std::string & text) const
  {
    if (context.hub) {
      context.hub->post(severity, kCategory, text, desc.key);
    }
  }

  void info(const std::string & text) const { message(seam::DiagnosticSeverity::Info, text); }

  /// Records the failure of the creation; the phase becomes Failed.
  void fail(const std::string & error_text)
  {
    seam::PanelOpResult result;
    result.op = seam::PanelOpResult::Op::Create;
    result.ok = false;
    result.error = error_text;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      error = error_text;
      results.push_back(result);
    }
    Phase expected = Phase::kCreating;
    phase.compare_exchange_strong(expected, Phase::kFailed);
  }

  /// Carries out what the guard decided: asks again, or reports that it gave up. Any thread; it
  /// only posts.
  void act_on(const seam::PanelStateGuard::Decision & decision)
  {
    using Kind = seam::PanelStateGuard::Decision::Kind;
    if (decision.kind == Kind::kNone) {
      return;
    }
    if (decision.kind == Kind::kGaveUp) {
      message(
        seam::DiagnosticSeverity::Warning,
        "QarOS keeps reporting " + seam::panel_state_name(decision.seen) + " while " +
        seam::panel_state_name(decision.wanted) + " is wanted - gave up after " +
        std::to_string(seam::PanelStateGuard::kMaxAsks) + " asks");
      return;
    }
    const std::weak_ptr<Core> weak = weak_from_this();
    const bool posted = context.executor->post(
      "panel-state-ask:" + desc.key, [weak, decision](qaros::Session & session) {
        if (const std::shared_ptr<Core> self = weak.lock()) {
          self->run_ask(session, decision);
        }
      });
    if (!posted) {
      const std::lock_guard<std::mutex> lock(mutex);
      guard.ask_done();
    }
  }

  /// The updates of the panel (library thread): the operator's close, and the state the guard
  /// keeps against a runtime that reports the opposite of what was set. Only reads the update and
  /// posts.
  void on_panel_update(const qaros::Result<qaros::GuiPanel> & update)
  {
    if (!update.Ok()) {
      return;
    }
    const qaros::GuiPanelState state = update.Value().State;
    if (state == qaros::GuiPanelState::Closed) {
      // Every other state is a panel that is still there: minimized and hidden are the operator
      // putting it away, not finishing with it.
      if (context.input) {
        context.input->report_closed(desc.key);
      }
      return;
    }
    seam::PanelStateGuard::Decision decision;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      if (retired) {
        return;
      }
      decision = guard.on_record(to_seam(state), now());
    }
    act_on(decision);
  }

  void run_ask(qaros::Session & session, const seam::PanelStateGuard::Decision & decision)
  {
    qaros::GuiPanelId id;
    bool go = false;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      id = panel_id;
      go = !retired && guard.has_wanted() && guard.wanted() == decision.wanted;
    }
    if (go) {
      message(
        seam::DiagnosticSeverity::Info,
        "QarOS reported " + seam::panel_state_name(decision.seen) + " while " +
        seam::panel_state_name(decision.wanted) + " is wanted - asked again (" +
        std::to_string(decision.number) + "/" + std::to_string(seam::PanelStateGuard::kMaxAsks) +
        ")");
      const qaros::Result<void> r = session.GuiPanels().SetState(id, to_sdk(decision.wanted));
      if (!r.Ok()) {
        message(seam::DiagnosticSeverity::Error, detail::failure_text("SetState", r.GetStatus()));
      }
    }
    const std::lock_guard<std::mutex> lock(mutex);
    guard.ask_done();
  }

  // The create chain: video sender -> stream id -> panel -> probe, and the reattach steps when the
  // panel is one of an earlier session. Runs on the worker. The failing step is recorded in
  // `error` and the phase becomes Failed. The frames open right after the first GetOrCreate, so
  // the probe and the reattach do not hold them.
  void run_create(qaros::Session & session)
  {
    // A sender closed or detached before its turn never made a panel: nothing is created, so the
    // hide and the detach queued behind this task find nothing to change and cost no call. One
    // that is closed while this task runs leaves what it made for those tasks.
    if (phase.load(std::memory_order_acquire) == Phase::kClosed) {
      return;
    }
    const seam::PanelDesc wanted_desc = [this] {
        const std::lock_guard<std::mutex> lock(mutex);
        return desc;
      }();
    const qaros::Pose wanted_pose = to_sdk(wanted_desc.placement.pose);
    const qaros::GuiPanelSize wanted_size = to_sdk(wanted_desc.placement.size);

    qaros::VideoSenderBuilder builder(make_panel_layout(wanted_desc));
    builder.WithCpu();
    qaros::Result<qaros::VideoSender> made = session.VideoSenders().Create(builder);
    if (!made.Ok()) {
      fail(detail::failure_text("VideoSenderControl::Create", made.GetStatus()));
      return;
    }
    auto owned = std::make_unique<qaros::VideoSender>(std::move(made).Value());
    const qaros::Result<qaros::StreamId> stream = owned->GetStreamId();
    {
      // Kept even when the stream id cannot be read: the teardown releases it.
      const std::lock_guard<std::mutex> lock(mutex);
      sender = std::move(owned);
    }
    if (!stream.Ok()) {
      fail(detail::failure_text("VideoSender::GetStreamId", stream.GetStatus()));
      return;
    }
    const qaros::StreamId stream_id = stream.Value();
    {
      // Kept for the life of the sender, the close included: everything the runtime says about the
      // content of the panel is keyed by this id and by nothing else.
      const std::lock_guard<std::mutex> lock(mutex);
      stream_id_text = id_text(stream_id);
    }

    // The panel of this common name: made, or found again when an earlier session left it behind.
    // A name that is taken by a panel that cannot be reused is tried again with a suffix appended
    // to the base name: "-v2" for a website panel, "-r1" .. "-r8" for closed panels.
    const qaros::GuiPanelControl panels = session.GuiPanels();
    std::string name = wanted_desc.common_name;
    qaros::GuiPanelId id;
    seam::PanelAttachDecision decision;
    seam::PanelProbe probe;
    for (unsigned retries = 0;; ++retries) {
      qaros::GuiPanelBuilder panel_builder(name);
      if (!wanted_desc.display_name.empty()) {
        panel_builder.WithDisplayName(wanted_desc.display_name);
      }
      // The panel's content IS the stream; without it the runtime would make a website panel.
      panel_builder.WithPose(wanted_pose).WithSize(wanted_size).WithStream(stream_id);
      const qaros::Result<qaros::GuiPanelId> created = panels.GetOrCreate(panel_builder);
      if (!created.Ok()) {
        fail(detail::failure_text("GuiPanelControl::GetOrCreate", created.GetStatus()));
        return;
      }
      id = created.Value();
      // The panel exists, and the receiver may attach to the stream at once. Frames sent before
      // the probe ends are harmless: a stream nobody shows has no transfer and the sender answers
      // "no active transfers". The sender stays the same through a retry with a suffix and through
      // the reattach (which names the stream, not the sender).
      frames_open.store(true, std::memory_order_release);

      probe = probe_panel(panels, id, stream_id, wanted_size);
      decision = seam::decide_panel_attach(probe, retries);
      if (decision.kind != seam::PanelAttachKind::RetryWithSuffix) {
        break;
      }
      info(
        "panel '" + wanted_desc.key + "' name=" + name + " is taken by " +
        (decision.reason == "closed" ? "a closed" : "a web") + " panel, trying " +
        wanted_desc.common_name + decision.retry_suffix);
      name = wanted_desc.common_name + decision.retry_suffix;
    }
    {
      const std::lock_guard<std::mutex> lock(mutex);
      common_name = name;
      panel_id = id;
    }
    if (decision.kind == seam::PanelAttachKind::Refused) {
      const std::string refusal = decision.reason == "closed" ?
        "closed panels of the names " + wanted_desc.common_name + " to " + name +
        " exist; no free name is left" :
        "a web panel of the name " + name + " exists";
      info("panel '" + wanted_desc.key + "' refused: " + refusal);
      fail(refusal);
      return;
    }

    const bool was_reattached = decision.kind == seam::PanelAttachKind::Reattached;
    if (was_reattached) {
      // The panel of an earlier session shows the stream of a sender that is gone. Our stream
      // goes on it, then size and pose, then it is shown again.
      qaros::Result<void> r = panels.SetStream(id, stream_id);
      if (!r.Ok()) {
        fail(detail::failure_text("GuiPanelControl::SetStream", r.GetStatus()));
        return;
      }
      if (decision.change_size) {
        r = panels.ChangeSize(id, wanted_size);
        if (!r.Ok()) {
          fail(detail::failure_text("GuiPanelControl::ChangeSize", r.GetStatus()));
          return;
        }
      }
      r = panels.UpdatePose(id, wanted_pose);
      if (!r.Ok()) {
        fail(detail::failure_text("GuiPanelControl::UpdatePose", r.GetStatus()));
        return;
      }
      if (decision.show) {
        r = panels.SetState(id, qaros::GuiPanelState::Visible);
        if (!r.Ok()) {
          fail(detail::failure_text("GuiPanelControl::SetState", r.GetStatus()));
          return;
        }
      }
    }
    // Marked only now: a step of the reattach that failed leaves the panel of the earlier
    // session alone, because the consumer drops a failed sender and its teardown closes the panel
    // it owns.
    {
      const std::lock_guard<std::mutex> lock(mutex);
      has_panel = true;
      reattached = was_reattached;
    }

    // How the panel came to be, in the one format the log checks read.
    seam::PanelAttachLogFields fields;
    fields.time = local_time_hms_ms();
    fields.key = wanted_desc.key;
    fields.reattached = was_reattached;
    fields.common_name = name;
    fields.panel_id = id_text(id);
    if (probe.found) {
      fields.state_was = seam::panel_state_name(probe.state);
    }
    fields.stream_swapped = was_reattached && decision.swap_content;
    fields.size_changed = was_reattached && decision.change_size;
    info(attach_line_text(fields));
    info(
      "panel '" + wanted_desc.key + "' " + (was_reattached ? "reattached" : "created") +
      ": id=" + id_text(id) + " stream=" + id_text(stream_id) + " buffer=" +
      std::to_string(wanted_desc.buffer_width) + "x" + std::to_string(wanted_desc.buffer_height) +
      " px");

    // The panel exists, so it can be pointed at. Subscribing here makes the lifetime of the
    // subscription exactly the panel's: the teardown is the only place it ends. A failure is not
    // fatal: a panel that streams but takes no input is better than no panel.
    if (context.input) {
      const std::uint32_t content_w =
        wanted_desc.content_width != 0 ? wanted_desc.content_width : wanted_desc.buffer_width;
      const std::uint32_t content_h =
        wanted_desc.content_height != 0 ? wanted_desc.content_height : wanted_desc.buffer_height;
      std::string input_error;
      if (!context.input->subscribe_panel(
          panels, wanted_desc.key, id, content_w, content_h, input_error))
      {
        message(
          seam::DiagnosticSeverity::Warning,
          "panel '" + wanted_desc.key + "' streams but takes no AR input: " + input_error);
      } else {
        info(
          "panel '" + wanted_desc.key + "' subscribed for AR gestures: id='" + id_text(id) +
          "' content=" + std::to_string(content_w) + "x" + std::to_string(content_h) + " px");
      }
    }

    // The panel shows the stream, so VISIBLE is the state to keep. A runtime that reports the
    // panel hidden right after it was shown is asked again. A watch that cannot start is reported
    // and the panel works without it. The callback holds this object by a raw pointer: the
    // subscription is ended before the object goes (the destructor, the teardown).
    {
      Core * self = this;
      qaros::Result<qaros::Subscription> watch = panels.SubscribeUpdates(
        id, [self](qaros::Result<qaros::GuiPanel> update) { self->on_panel_update(update); });
      if (!watch.Ok()) {
        message(
          seam::DiagnosticSeverity::Warning,
          "panel '" + wanted_desc.key + "' does not watch its state: " +
          detail::failure_text("SubscribeUpdates", watch.GetStatus()));
      } else {
        updates = std::move(watch).Value();
      }
    }
    {
      const std::lock_guard<std::mutex> lock(mutex);
      guard.created(now());
    }

    seam::PanelOpResult done;
    done.op = seam::PanelOpResult::Op::Create;
    done.ok = true;
    done.reattached = was_reattached;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      error.clear();
      results.push_back(done);
    }
    // Published last: the Qt thread reads the sender after it saw Ready. A close that came in
    // meanwhile keeps the phase Closed, and its task cleans up behind this one.
    Phase expected = Phase::kCreating;
    phase.compare_exchange_strong(
      expected, Phase::kReady, std::memory_order_release, std::memory_order_relaxed);
  }

  void run_place(qaros::Session & session)
  {
    seam::PanelPlacement placement;
    qaros::GuiPanelId id;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      place_queued = false;
      if (!has_wanted) {
        return;
      }
      has_wanted = false;
      placement = wanted;
      id = panel_id;
      if (!has_panel || phase.load() == Phase::kFailed) {
        return;
      }
    }
    seam::PanelOpResult result;
    result.op = seam::PanelOpResult::Op::Placement;
    const qaros::GuiPanelControl panels = session.GuiPanels();
    const qaros::Result<void> posed = panels.UpdatePose(id, to_sdk(placement.pose));
    if (!posed.Ok()) {
      result.ok = false;
      result.error = detail::failure_text("GuiPanelControl::UpdatePose", posed.GetStatus());
    } else {
      const qaros::Result<void> sized = panels.ChangeSize(id, to_sdk(placement.size));
      if (!sized.Ok()) {
        result.ok = false;
        result.error = detail::failure_text("GuiPanelControl::ChangeSize", sized.GetStatus());
      }
    }
    const std::lock_guard<std::mutex> lock(mutex);
    if (result.ok) {
      desc.placement = placement;
    }
    results.push_back(std::move(result));
  }

  void run_state(qaros::Session & session, seam::PanelState state)
  {
    // Whatever this task finds to do, the guard learns that the request has been served.
    struct Served
    {
      Core & core;
      ~Served()
      {
        const std::lock_guard<std::mutex> lock(core.mutex);
        core.guard.request_done(core.now());
      }
    } served{*this};
    qaros::GuiPanelId id;
    std::string key;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      if (!has_panel || phase.load() == Phase::kFailed) {
        return;  // the panel was never made, or its creation failed: nothing to change
      }
      id = panel_id;
      key = desc.key;
    }
    seam::PanelOpResult result;
    result.op = seam::PanelOpResult::Op::State;
    result.state = state;
    const qaros::Result<void> r = session.GuiPanels().SetState(id, to_sdk(state));
    if (!r.Ok()) {
      result.ok = false;
      result.error = detail::failure_text("GuiPanelControl::SetState", r.GetStatus());
    } else {
      // The input source reads an update as an AR-side close for the state CLOSED only, so the
      // HIDDEN or VISIBLE set here is not taken for one.
      info("panel '" + key + "' state -> " + seam::panel_state_name(state));
    }
    const std::lock_guard<std::mutex> lock(mutex);
    results.push_back(std::move(result));
  }

  // The one teardown, in the one safe order, on the worker. The Qt thread has stopped using the
  // video sender before it queued this task.
  void run_teardown(qaros::Session & session, bool panel_closed_in_ar, bool keep_panel)
  {
    std::unique_ptr<qaros::VideoSender> owned;
    qaros::GuiPanelId id;
    std::string stream_text;
    bool had_panel = false;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      owned = std::move(sender);
      id = panel_id;
      stream_text = stream_id_text;
      had_panel = has_panel;
      has_panel = false;
      retired = true;
    }
    const std::string key = desc.key;
    const auto started = std::chrono::steady_clock::now();
    if (had_panel || owned) {
      info(
        "panel '" + key + "' " + (keep_panel ? "detaching" : "closing") + ": id=" + id_text(id) +
        " stream=" + stream_text +
        (keep_panel ? " (the GUI panel is kept)"
                    : std::string(" (operator already closed it in AR: ") +
          (panel_closed_in_ar ? "yes" : "no") + ")"));
    }

    // Step 1: the input. The gesture subscription ends first, and does not return until no
    // callback for the panel runs, so that nothing can arrive naming a panel that is being
    // destroyed. Then the panel-updates subscription: it is what would take the CLOSED state the
    // runtime republishes for our own close for "the operator closed this panel in AR".
    if (context.input && !key.empty()) {
      const bool had_input = context.input->has_panel(key);
      context.input->unsubscribe_panel(key);
      if (had_input) {
        info("panel '" + key + "' step 1/3: AR gesture subscription ended");
      }
    }
    const bool had_watch = static_cast<bool>(updates.Token());
    updates.End();
    if (had_watch) {
      info("panel '" + key + "' step 1/3: panel-update subscription ended");
    }

    // Step 2: the panel, then (step 3) the stream it shows: releasing the sender first would
    // leave a live panel that refers to a retired stream.
    seam::PanelCloseOutcome outcome = seam::PanelCloseOutcome::NoPanel;
    if (had_panel && keep_panel) {
      // Detach: the panel stays as it is (the caller hid it) and the next session finds it again
      // under its common name.
      info("panel '" + key + "' step 2/3: GUI panel kept for the next session");
    } else if (had_panel) {
      if (panel_closed_in_ar) {
        // The operator closed it: the runtime has removed the panel, and a second close of the
        // same id is a call with no documented behaviour.
        outcome = seam::PanelCloseOutcome::AlreadyClosedInAr;
        info(
          "panel '" + key + "' step 2/3: GuiPanelControl::Close SKIPPED -- the operator already "
          "closed this panel in AR");
      } else {
        const qaros::Result<void> closed = session.GuiPanels().Close(id);
        if (!closed.Ok()) {
          const std::string failure =
            detail::failure_text("GuiPanelControl::Close", closed.GetStatus());
          outcome = seam::PanelCloseOutcome::Failed;
          message(
            seam::DiagnosticSeverity::Error,
            "panel '" + key + "' step 2/3: " + failure +
            " -- the video sender is released regardless; the AR panel may still be on screen");
        } else {
          outcome = seam::PanelCloseOutcome::Closed;
          info("panel '" + key + "' step 2/3: GuiPanelControl::Close ran, ok");
        }
      }
    }

    // Step 3: the video sender, unconditionally.
    bool released = false;
    if (owned) {
      info("panel '" + key + "' step 3/3: releasing the video sender");
      owned.reset();
      released = true;
    }

    bool print_retired = false;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      print_retired = !stream_text.empty() && !retired_logged;
      retired_logged = true;
    }
    if (print_retired) {
      const long long took_ms = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count());
      const char * gui_panel = keep_panel ? "kept" : "none was open";
      switch (outcome) {
        case seam::PanelCloseOutcome::NoPanel:
        case seam::PanelCloseOutcome::Queued:
          break;
        case seam::PanelCloseOutcome::Closed:
          gui_panel = "closed";
          break;
        case seam::PanelCloseOutcome::AlreadyClosedInAr:
          gui_panel = "close skipped (the operator had already closed it in AR)";
          break;
        case seam::PanelCloseOutcome::Failed:
          gui_panel = "close FAILED (see step 2/3 above)";
          break;
      }
      info(
        "panel '" + key + "' retired: id=" + id_text(id) + " stream=" + stream_text +
        " took=" + std::to_string(took_ms) + "ms -- GUI panel " + gui_panel + ", video sender " +
        (released ? "released" : "none to release"));
    }
  }

  Context context;

  std::atomic<Phase> phase{Phase::kCreating};
  /// Set by the create task as soon as the panel exists and shows the stream of the sender: from
  /// then on the Qt thread may send frames, although the attach probe, a reattach and the input
  /// subscription are still to run. The receiver shows the panel from this moment, so a frame
  /// that waits for the end of the create task is a black panel.
  std::atomic<bool> frames_open{false};

  /// Guards everything below, except `updates`.
  mutable std::mutex mutex;
  seam::PanelDesc desc;
  std::unique_ptr<qaros::VideoSender> sender;
  qaros::GuiPanelId panel_id;
  std::string stream_id_text;
  std::string common_name;
  std::string error;
  bool has_panel = false;
  bool reattached = false;
  /// A placement task is queued; the newest placement asked for is in `wanted`.
  bool place_queued = false;
  bool has_wanted = false;
  seam::PanelPlacement wanted;
  std::vector<seam::PanelOpResult> results;
  bool retired = false;
  bool retired_logged = false;
  /// Keeps the panel in the state the sender asked for (the seam's panel-state guard).
  seam::PanelStateGuard guard;

  /// The subscription to the updates of the panel; touched by the worker's tasks only. It is the
  /// last member, so it is the first to go.
  qaros::Subscription updates;
};

// ===========================================================================
//  QarosPanelSender
// ===========================================================================

QarosPanelSender::QarosPanelSender(Context context, const seam::PanelDesc & desc)
: context_(context),
  desc_(desc),
  core_(std::make_shared<Core>(std::move(context), desc))
{
  // What can be checked without the runtime is refused at once.
  if (!context_.executor) {
    refuse("no QarOS session (the factory has none)");
    return;
  }
  if (desc_.buffer_width == 0 || desc_.buffer_height == 0) {
    refuse("panel buffer size must be non-zero");
    return;
  }
  if (desc_.common_name.empty()) {
    refuse("no common name for panel '" + desc_.key + "'");
    return;
  }
  const std::shared_ptr<Core> core = core_;
  const bool posted = context_.executor->post(
    "panel-create:" + desc_.key, [core](qaros::Session & session) { core->run_create(session); });
  if (!posted) {
    refuse("QarOS session is not accepting calls");
  }
}

QarosPanelSender::~QarosPanelSender()
{
  close();
}

void
QarosPanelSender::refuse(const std::string & reason)
{
  refused_ = true;
  last_error_ = reason;
  const std::lock_guard<std::mutex> lock(core_->mutex);
  core_->error = reason;
  core_->phase.store(Core::Phase::kFailed, std::memory_order_release);
}

bool
QarosPanelSender::ready() const
{
  return core_->phase.load(std::memory_order_acquire) == Core::Phase::kReady;
}

bool
QarosPanelSender::pending() const
{
  return core_->phase.load(std::memory_order_acquire) == Core::Phase::kCreating &&
         !core_->frames_open.load(std::memory_order_acquire);
}

std::string
QarosPanelSender::common_name() const
{
  const std::lock_guard<std::mutex> lock(core_->mutex);
  return core_->common_name;
}

bool
QarosPanelSender::reattached() const
{
  const std::lock_guard<std::mutex> lock(core_->mutex);
  return core_->reattached;
}

std::string
QarosPanelSender::stream_id_text() const
{
  const std::lock_guard<std::mutex> lock(core_->mutex);
  return core_->stream_id_text;
}

void
QarosPanelSender::drain_results(std::vector<seam::PanelOpResult> & out)
{
  // The manager calls this on every tick, which is when a panel that the runtime hid again inside
  // the hold of an ask, and then left alone, is asked about once more.
  seam::PanelStateGuard::Decision decision;
  {
    const std::lock_guard<std::mutex> lock(core_->mutex);
    if (!core_->retired) {
      decision = core_->guard.poll(core_->now());
    }
  }
  core_->act_on(decision);

  std::vector<seam::PanelOpResult> taken;
  {
    const std::lock_guard<std::mutex> lock(core_->mutex);
    taken.swap(core_->results);
  }
  for (seam::PanelOpResult & result : taken) {
    if (!result.ok && !result.error.empty()) {
      last_error_ = result.error;
    }
    out.push_back(std::move(result));
  }
  // The messages of the worker and of the SDK's threads reach the sink here, on the Qt thread.
  if (context_.hub) {
    context_.hub->flush();
  }
}

bool
QarosPanelSender::try_begin_frame(bool & out_ready)
{
  out_ready = false;
  not_ready_cause_ = seam::PanelNotReadyCause::NoDestination;
  const Core::Phase phase = core_->phase.load(std::memory_order_acquire);
  if (phase == Core::Phase::kCreating && !core_->frames_open.load(std::memory_order_acquire)) {
    not_ready_cause_ = seam::PanelNotReadyCause::Creating;
    return true;
  }
  if ((phase != Core::Phase::kReady && phase != Core::Phase::kCreating) || closed_) {
    if (phase == Core::Phase::kFailed) {
      const std::lock_guard<std::mutex> lock(core_->mutex);
      last_error_ = core_->error;
    }
    if (last_error_.empty()) {
      last_error_ = "panel sender is not ready";
    }
    return false;
  }
  if (sender_ == nullptr) {
    const std::lock_guard<std::mutex> lock(core_->mutex);
    sender_ = core_->sender.get();
  }
  if (sender_ == nullptr) {
    if (last_error_.empty()) {
      last_error_ = "panel sender is not ready";
    }
    return false;
  }

  const qaros::Result<bool> r = sender_->TryBeginFrame();
  if (!r.Ok()) {
    // NOT a failure, a not-ready state, and the difference matters: the caller marks the panel
    // broken on a failure and simply polls again on a not-ready. A panel sender exists before
    // anything in the headset has attached to its stream, and that is exactly what
    // MediaStreamingNoActiveTransfers says: there is no transfer to reserve a destination on.
    // StreamIsReconnecting has the same shape (transient, retried by polling), and
    // StreamIsPaused is the receiver pausing the stream. All three are reported as success with
    // `out_ready == false`, and the caller counts them apart.
    const qaros::Status & status = r.GetStatus();
    const bool no_transfers = status.HasCode(qaros::StatusCode::MediaStreamingNoActiveTransfers);
    const bool reconnecting = status.HasCode(qaros::StatusCode::MediaStreamingStreamIsReconnecting);
    const bool paused = status.HasCode(qaros::StatusCode::MediaStreamingStreamIsPaused);
    if (no_transfers || reconnecting || paused) {
      not_ready_cause_ = paused ? seam::PanelNotReadyCause::Paused :
        reconnecting ? seam::PanelNotReadyCause::Reconnecting :
        seam::PanelNotReadyCause::NoActiveTransfers;
      frame_reserved_ = false;
      out_ready = false;
      return true;
    }
    last_error_ = detail::failure_text("VideoSender::TryBeginFrame", status);
    return false;
  }
  frame_reserved_ = r.Value();
  out_ready = r.Value();
  return true;
}

bool
QarosPanelSender::submit_frame(const seam::PanelPixels & pixels)
{
  if (!frame_reserved_) {
    last_error_ = "submit_frame without a reserved cycle";
    return false;
  }
  // Whatever happens below, the reservation is consumed: leaving it set would make the next tick
  // believe that a cycle is still open.
  frame_reserved_ = false;

  if (pixels.data == nullptr) {
    last_error_ = "submit_frame got a null pixel buffer";
    return false;
  }

  const qaros::Result<qaros::VideoFrameCpu> acquired = sender_->GetCpuFrame();
  if (!acquired.Ok()) {
    last_error_ = detail::failure_text("VideoSender::GetCpuFrame", acquired.GetStatus());
    return false;
  }
  if (acquired.Value().Textures.empty() || acquired.Value().Textures[0].Data == nullptr) {
    last_error_ = "VideoSender::GetCpuFrame returned no writable texture";
    return false;
  }

  const qaros::VideoTextureCpu & dst = acquired.Value().Textures[0];
  // The sender owns the destination and its layout is fixed at creation, so a mismatch means that
  // our geometry and the sender have drifted apart; copying anyway would write past the end.
  if (dst.Size.Width != desc_.buffer_width || dst.Size.Height != desc_.buffer_height) {
    last_error_ = "panel frame size mismatch: sender has " + std::to_string(dst.Size.Width) +
      "x" + std::to_string(dst.Size.Height) + ", we captured " +
      std::to_string(desc_.buffer_width) + "x" + std::to_string(desc_.buffer_height);
    return false;
  }

  const std::uint32_t row_bytes = desc_.buffer_width * 4u;
  const std::uint32_t dst_pitch = dst.Pitch != 0 ? dst.Pitch : row_bytes;
  if (dst_pitch < row_bytes || pixels.stride_bytes < row_bytes) {
    last_error_ = "panel frame pitch is smaller than a row of pixels";
    return false;
  }
  const std::size_t needed =
    static_cast<std::size_t>(dst_pitch) * (desc_.buffer_height - 1) + row_bytes;
  if (dst.DataSize < needed) {
    last_error_ = "panel destination buffer is too small for the captured frame";
    return false;
  }

  // Row by row: the scanline stride of Qt and the pitch of the runtime are chosen independently,
  // so one flat copy would shear the image whenever they differ.
  for (std::uint32_t y = 0; y < desc_.buffer_height; ++y) {
    std::memcpy(
      dst.Data + static_cast<std::size_t>(y) * dst_pitch,
      pixels.data + static_cast<std::size_t>(y) * pixels.stride_bytes, row_bytes);
  }

  const qaros::Result<void> shown = sender_->ShowFrame();
  if (!shown.Ok()) {
    last_error_ = detail::failure_text("VideoSender::ShowFrame", shown.GetStatus());
    return false;
  }
  ++frames_shown_;
  if (frames_shown_ == 1) {
    core_->info("panel '" + desc_.key + "' first frame shown");
  }
  last_error_.clear();
  return true;
}

bool
QarosPanelSender::set_placement(const seam::PanelPlacement & placement)
{
  const Core::Phase phase = core_->phase.load(std::memory_order_acquire);
  if (closed_ || (phase != Core::Phase::kCreating && phase != Core::Phase::kReady)) {
    last_error_ = "no live panel to place";
    return false;
  }
  bool queue = false;
  {
    // The newest placement wins: one task is queued at most and applies what was asked last.
    const std::lock_guard<std::mutex> lock(core_->mutex);
    core_->wanted = placement;
    core_->has_wanted = true;
    queue = !core_->place_queued;
    core_->place_queued = true;
  }
  if (queue) {
    const std::shared_ptr<Core> core = core_;
    const bool posted = context_.executor->post(
      "panel-place:" + desc_.key, [core](qaros::Session & session) { core->run_place(session); });
    if (!posted) {
      const std::lock_guard<std::mutex> lock(core_->mutex);
      core_->place_queued = false;
      core_->has_wanted = false;
      last_error_ = "QarOS session is not accepting calls";
      return false;
    }
  }
  last_error_.clear();
  return true;
}

bool
QarosPanelSender::set_state(seam::PanelState state)
{
  const Core::Phase phase = core_->phase.load(std::memory_order_acquire);
  if (closed_ || (phase != Core::Phase::kCreating && phase != Core::Phase::kReady)) {
    last_error_ = "no live panel to change";
    return false;
  }
  const std::shared_ptr<Core> core = core_;
  {
    const std::lock_guard<std::mutex> lock(core->mutex);
    core->guard.request(state);
  }
  const bool posted = context_.executor->post(
    "panel-state:" + desc_.key,
    [core, state](qaros::Session & session) { core->run_state(session, state); });
  if (!posted) {
    const std::lock_guard<std::mutex> lock(core->mutex);
    core->guard.request_dropped();
    last_error_ = "QarOS session is not accepting calls";
    return false;
  }
  last_error_.clear();
  return true;
}

seam::PanelCloseReport
QarosPanelSender::close(bool panel_closed_in_ar)
{
  return teardown(panel_closed_in_ar, /*keep_panel=*/false);
}

void
QarosPanelSender::detach()
{
  teardown(/*panel_closed_in_ar=*/false, /*keep_panel=*/true);
}

seam::PanelCloseReport
QarosPanelSender::teardown(bool panel_closed_in_ar, bool keep_panel)
{
  seam::PanelCloseReport report;
  // The second close, the destructor after the stream's own, has nothing left to do, and must not
  // do the first step again: a late unsubscribe is keyed by the panel key and would end the
  // subscription of a newer sender that has taken the same key (a dialog opened again).
  if (closed_) {
    return report;
  }
  closed_ = true;
  frame_reserved_ = false;
  sender_ = nullptr;
  // From here on no frame call is made on the video sender, so the worker may release it.
  core_->phase.store(Core::Phase::kClosed, std::memory_order_release);
  {
    // The panel-state guard asks nothing more for a panel that is being retired.
    const std::lock_guard<std::mutex> lock(core_->mutex);
    core_->retired = true;
  }

  if (context_.input && !desc_.key.empty()) {
    report.input_unsubscribed = context_.input->has_panel(desc_.key);
  }
  if (!context_.executor) {
    return report;
  }
  const std::shared_ptr<Core> core = core_;
  const bool posted = context_.executor->post(
    (keep_panel ? "panel-detach:" : "panel-close:") + desc_.key,
    [core, panel_closed_in_ar, keep_panel](qaros::Session & session) {
      core->run_teardown(session, panel_closed_in_ar, keep_panel);
    });
  if (posted) {
    report.panel = seam::PanelCloseOutcome::Queued;
  }
  return report;
}

}  // namespace qar_qt_streaming::qaros_backend
