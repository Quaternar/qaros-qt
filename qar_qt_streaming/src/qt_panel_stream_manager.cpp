// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/qt_panel_stream_manager.hpp"

#include "qar_qt_streaming/qt_widget_capture.hpp"

#include <QSize>
#include <QString>
#include <QWidget>

#include <QPointer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <stdexcept>
#include <utility>

namespace qar_qt_streaming
{
namespace
{

/// The pointer id the route_* entry points (one implicit pointer) act as.
constexpr std::uint32_t kImplicitPointerId = 0;

PanelHit
hit_from_uv(float u, float v)
{
  PanelHit hit;
  hit.valid = true;
  hit.u = u;
  hit.v = v;
  hit.inside = u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
  return hit;
}

/// One decimal, no locale, no iostreams: these strings end up in log lines that
/// are grepped, and `std::to_string(double)` would print six decimals of noise.
std::string
format_seconds(double seconds)
{
  char buf[32] = {0};
  std::snprintf(buf, sizeof(buf) - 1, "%.1fs", seconds);
  return std::string(buf);
}

/// `'key' (stream <uuid>)` — the pair that lets a line here be matched against
/// the runtime's `streamRequest/<uuid>` / `streamCatalog/<uuid>` traffic. The
/// stream half is omitted rather than printed empty when the backend has no
/// stream identity (the test double in a unit test, a sender that failed to bootstrap).
std::string
describe_stream(const qar_ar_core::PanelStreamDiagnostics & d)
{
  std::string out = "'" + d.key + "'";
  if (!d.stream_id.empty()) {
    out += " (stream " + d.stream_id + ")";
  }
  return out;
}

}  // namespace

QtPanelStreamManager::QtPanelStreamManager(QtPanelStreamingConfig config)
: config_(std::move(config)),
  pointer_state_(config_.pointer_state)
{
  if (!config_.panel_sender_factory) {
    throw std::invalid_argument(
      "QtPanelStreamingConfig::panel_sender_factory must not be null");
  }
  clock_ = config_.clock ? config_.clock : std::make_shared<SteadyClock>();
}

QtPanelStreamManager::~QtPanelStreamManager()
{
  clear();
}

void
QtPanelStreamManager::emit_diagnostic(
  qar_ar_core::DiagnosticSeverity severity,
  const std::string & category,
  const std::string & message,
  const std::string & panel_key)
{
  if (severity == qar_ar_core::DiagnosticSeverity::Error) {
    last_error_ = message;
  }
  if (config_.diagnostics != nullptr) {
    config_.diagnostics->on_message(severity, category, message, panel_key);
  }
}

qar_ar_core::Result
QtPanelStreamManager::add_panel(
  const std::string & key, QWidget * widget, const PanelStreamOptions & options)
{
  if (key.empty()) {
    return qar_ar_core::Result::error(qar_ar_core::Status::InvalidArgument, "panel key must not be empty");
  }
  if (widget == nullptr) {
    return qar_ar_core::Result::error(qar_ar_core::Status::InvalidArgument, "widget must not be null");
  }
  const auto existing = streams_.find(key);
  if (existing != streams_.end()) {
    if (!existing->second->tombstoned()) {
      return qar_ar_core::Result::error(qar_ar_core::Status::AlreadyExists, "a panel is already streaming for key " + key);
    }
    // A retired slot, re-adopted. This is the bound on tombstone growth: the
    // sender and the QarOS panel that are already standing behind this key are
    // reused, so an operator who closes and re-opens the same window pays for
    // exactly one sender however many times they do it. The panel's TITLE is
    // fixed at creation and therefore stays whatever it was; the pose is the
    // new caller's.
    if (!existing->second->revive(widget)) {
      return qar_ar_core::Result::error(
        qar_ar_core::Status::InvalidState, "could not revive the retired panel slot for key " + key);
    }
    existing->second->set_pose(options.placement.pose);
    ++tombstones_revived_;
    tombstone_cap_reported_ = false;  // room again; say so if it fills once more
    PanelOverlayAttribution::instance().register_panel(widget);
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Info, "panel",
      "re-used the retired panel slot for this key, so no second video sender was "
      "created (the QarOS panel and its stream are the ones that were already there)",
      key);
    return qar_ar_core::Result::success();
  }

  if (refuse_for_tombstone_cap(key)) {
    return qar_ar_core::Result::error(
      qar_ar_core::Status::InvalidState,
      "the tombstone cap is reached; no new panel stream can be created this session");
  }

  streams_.emplace(
    key,
    std::make_unique<QtPanelStream>(key, widget, options, config_.panel_sender_factory));
  // Known to dialog attribution for as long as it streams, so a dialog parented
  // into THIS widget's tree can never be claimed by a click on another panel.
  // An input-enabled panel's injector registers it too; the call is idempotent,
  // and doing it here covers a display-only panel, which has no injector.
  PanelOverlayAttribution::instance().register_panel(widget);
  emit_diagnostic(qar_ar_core::DiagnosticSeverity::Info, "panel", "added panel stream", key);
  return qar_ar_core::Result::success();
}

qar_ar_core::Result
QtPanelStreamManager::remove_panel(const std::string & key)
{
  // "This widget is finished". What that means is the close mode's to say:
  // destroy the panel by default, a notice under QAR_PANEL_CLOSE_MODE=notice.
  return close_panel(key, std::string());
}

qar_ar_core::Result
QtPanelStreamManager::close_panel(
  const std::string & key, const std::string & reason, const std::string & notice_reason)
{
  if (config_.close_mode == PanelCloseMode::Notice) {
    // The escape hatch: the tombstone, with the notice's own wording.
    return tombstone_panel(key, notice_reason.empty() ? reason : notice_reason);
  }

  const auto it = streams_.find(key);
  if (it == streams_.end()) {
    return qar_ar_core::Result::error(qar_ar_core::Status::NotFound, "no panel stream for key " + key);
  }
  // Already on its way out — a handler run by the pointer release below asked
  // again, say. Closed once is closed.
  if (std::find(closing_keys_.begin(), closing_keys_.end(), key) != closing_keys_.end()) {
    return qar_ar_core::Result::success();
  }

  if (it->second->delivering()) {
    // The close was asked for from INSIDE a pointer delivery into this very
    // panel's widget — the host's Stop button, pressed in AR on the panel it
    // stops, does exactly that. The events are sent synchronously, so the
    // injector, the stream and this map entry are all still on the stack below
    // the handler; destroying them now would hand the handler a freed object
    // to return into. So the close is queued and runs the moment that delivery
    // has returned (run_deferred_closes), and the caller is told it succeeded,
    // because from its point of view it has.
    for (const DeferredClose & pending : deferred_closes_) {
      if (pending.key == key) {
        return qar_ar_core::Result::success();  // already queued
      }
    }
    ++closes_deferred_;
    deferred_closes_.push_back(DeferredClose{key, reason});
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Info, "panel",
      "closing " + describe_stream(it->second->diagnostics()) +
      " was asked for from inside a pointer action still being delivered into its own "
      "widget; the close runs as soon as that action has returned",
      key);
    return qar_ar_core::Result::success();
  }
  return destroy_panel(key, reason);
}

qar_ar_core::Result
QtPanelStreamManager::destroy_panel(const std::string & requested_key, const std::string & reason)
{
  // A copy, because the caller's reference may be to storage this very call
  // erases (a key read straight out of a map node).
  const std::string key = requested_key;
  auto it = streams_.find(key);
  if (it == streams_.end()) {
    return qar_ar_core::Result::error(qar_ar_core::Status::NotFound, "no panel stream for key " + key);
  }

  // Marked closing for the whole teardown: releasing a pressed pointer below
  // runs widget code, and a widget that answers by asking for this same close
  // again must find it already under way rather than start a second one.
  struct Closing
  {
    Closing(std::vector<std::string> & keys, const std::string & key) : keys_(keys)
    {
      keys_.push_back(key);
    }
    ~Closing() { keys_.pop_back(); }
    Closing(const Closing &) = delete;
    Closing & operator=(const Closing &) = delete;
    std::vector<std::string> & keys_;
  } closing(closing_keys_, key);

  // Learn what the AR side has already told us BEFORE deciding whether the panel
  // still needs closing: a panel the operator closed a moment ago is closed, and
  // the adapter must not close it again. (The same drain runs every tick; this one
  // only narrows the window between the operator's close and ours.)
  pump_closed_panels();

  // Release any pointer pressed on this panel while the stream can still deliver,
  // so no widget is left holding a press — the same first step a tombstone takes.
  // Depth-guarded like handle_sample: a panel whose widget dies while a modal
  // dialog's nested loop is running gets retired from inside that loop, with an
  // outer dispatch_actions() still walking the shared scratch.
  ++input_depth_;
  {
    std::vector<PointerAction> nested;
    std::vector<PointerAction> & actions = input_depth_ > 1 ? nested : action_scratch_;
    actions.clear();
    pointer_state_.forget_panel(key, actions);
    dispatch_actions(actions, PanelHit{});
  }
  --input_depth_;

  // Whatever those events ran may have changed the map; look again.
  it = streams_.find(key);
  if (it == streams_.end()) {
    return qar_ar_core::Result::success();
  }

  // The source widget stops being a panel the moment it stops being streamed — a
  // dialog parented into this tree must not be attributed to it. Before the close,
  // while widget() is still the real window.
  PanelOverlayAttribution::instance().unregister_panel(it->second->widget());

  const qar_ar_core::PanelStreamDiagnostics before = it->second->diagnostics();

  // THE sequence, in the adapter: input subscriptions, then the QarOS panel
  // (skipped if the operator closed it), then the video sender. The stream passes
  // it what the manager knows of the AR side.
  const qar_ar_core::PanelCloseReport report = it->second->close();

  // Anything the runtime reported between the drain above and the adapter
  // cancelling its subscriptions is applied now, to the stream that earned it —
  // not left in the queue to be read, a tick later, as news about a NEW stream
  // that has since taken this key.
  pump_closed_panels();
  it = streams_.find(key);
  if (it != streams_.end()) {
    streams_.erase(it);
  }
  dialog_panels_.erase(
    std::remove_if(
      dialog_panels_.begin(), dialog_panels_.end(),
      [&key](const DialogPanel & entry) { return entry.key == key; }),
    dialog_panels_.end());

  ++panels_closed_;
  if (report.panel == qar_ar_core::PanelCloseOutcome::AlreadyClosedInAr) {
    ++close_panel_skipped_;
  } else if (report.panel == qar_ar_core::PanelCloseOutcome::Failed) {
    ++close_panel_failures_;
  }
  report_closed(before, report, reason);
  return qar_ar_core::Result::success();
}

void
QtPanelStreamManager::run_deferred_closes()
{
  if (deferred_closes_.empty()) {
    return;
  }
  // Taken, not iterated in place: closing runs widget code, which can queue more.
  std::vector<DeferredClose> pending;
  pending.swap(deferred_closes_);
  for (const DeferredClose & close : pending) {
    const auto it = streams_.find(close.key);
    if (it == streams_.end()) {
      continue;  // gone some other way meanwhile; nothing left to close
    }
    if (it->second->delivering()) {
      // An outer frame is STILL delivering into it (a nested event loop is
      // running underneath this call). Not yet.
      deferred_closes_.push_back(close);
      continue;
    }
    destroy_panel(close.key, close.reason);
  }
}

void
QtPanelStreamManager::report_closed(
  const qar_ar_core::PanelStreamDiagnostics & before, const qar_ar_core::PanelCloseReport & report,
  const std::string & reason)
{
  // What each step did, in the order it ran — the thing a crash dump has to be
  // lined up against. The adapter prints the same three steps with a wall-clock
  // time of its own; this is the host-side record of the outcome.
  const std::string teardown =
    std::string("input ") +
    (report.input_unsubscribed ? "subscriptions cancelled" : "had no subscription") +
    ", close_panel " + to_string(report.panel) + ", video sender " +
    (report.sender_destroyed ? "destroyed" : "none to destroy");
  const std::string why = reason.empty() ? std::string("none given") : reason;
  const std::string tail =
    " Teardown: " + teardown + ". The stream entry is gone (no closed-window notice); "
    "opening the window again is a fresh add on a new sender.";

  if (before.frames_submitted == 0 && !before.stream_id.empty()) {
    // The bad shape report_retired warns about, and still worth a Warning now
    // that the stream is gone rather than kept: we announced a stream to QarOS, it
    // went looking for it, and the window closed without one frame ever landing.
    // Phrased the same way so one grep finds both.
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Warning, "panel",
      "closed " + describe_stream(before) + " having served it NO frames after " +
      format_seconds(before.unserved_seconds) + " (reason: " + why +
      "). QarOS was told this stream exists and never got a frame on it." + tail,
      before.key);
  } else {
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Info, "panel",
      "closed " + describe_stream(before) + " after " +
      std::to_string(static_cast<unsigned long long>(before.frames_submitted)) +
      " frame(s) (reason: " + why + ")." + tail,
      before.key);
  }
  if (report.panel == qar_ar_core::PanelCloseOutcome::Failed) {
    // Reported, never hidden and never turned back into a notice: the caller wants
    // the panel gone, and a failure to remove it from QarOS is something to know
    // about, not something to paper over by keeping a stream alive.
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Warning, "panel",
      "the AR panel for " + describe_stream(before) + " could not be closed: " +
      report.panel_message + ". The video sender was destroyed and the stream entry "
      "dropped regardless, so nothing of it remains on this side, but QarOS may still "
      "be showing the panel.",
      before.key);
  }
}

qar_ar_core::Result
QtPanelStreamManager::tombstone_panel(const std::string & key, const std::string & reason)
{
  const auto it = streams_.find(key);
  if (it == streams_.end()) {
    return qar_ar_core::Result::error(qar_ar_core::Status::NotFound, "no panel stream for key " + key);
  }
  if (it->second->tombstoned()) {
    return qar_ar_core::Result::success();  // already a notice; the first reason stands
  }

  // Cancel/leave through the state machine FIRST, while the stream can still
  // deliver, so the pointer state and the widgets agree that nothing is held.
  // Depth-guarded like handle_sample: a panel whose widget dies while a modal
  // dialog's nested loop is running gets retired from inside that loop, with an
  // outer dispatch_actions() still walking the shared scratch.
  ++input_depth_;
  std::vector<PointerAction> nested;
  std::vector<PointerAction> & actions = input_depth_ > 1 ? nested : action_scratch_;
  actions.clear();
  pointer_state_.forget_panel(key, actions);
  dispatch_actions(actions, PanelHit{});
  --input_depth_;

  // The source widget stops being a panel the moment it stops being streamed —
  // a dialog parented into a tree that is now a notice must not be attributed
  // to it. Done BEFORE the swap, while widget() is still the real window.
  PanelOverlayAttribution::instance().unregister_panel(it->second->widget());

  if (!it->second->has_adapter()) {
    // Nothing was ever created for this panel, so there is no sender to protect
    // and a notice would be a permanently occupied slot showing a message about
    // a stream that never existed. Erase it outright — the usual retirement
    // line still names it, so the sequence is still traceable.
    const qar_ar_core::PanelStreamDiagnostics retired = it->second->diagnostics();
    it->second->close();
    streams_.erase(it);
    report_retired(retired);
    return qar_ar_core::Result::success();
  }

  PanelTombstoneText text;
  // The window's name, which is what lets an operator with four rectangles in
  // the room tell which one died: the display name when the panel has one (a
  // dialog's title), otherwise the key.
  const qar_ar_core::PanelStreamDiagnostics & before = it->second->diagnostics();
  text.panel_name = before.display_name.empty() ? key : before.display_name;
  text.reason = reason;
  it->second->tombstone(text);
  ++tombstones_created_;

  const qar_ar_core::PanelStreamDiagnostics & after = it->second->diagnostics();
  if (after.frames_submitted == 0 && !after.stream_id.empty()) {
    // The same bad shape report_retired warns about, and it does not stop being
    // worth a Warning because the stream now lives on: we announced a stream to
    // QarOS, it went looking for it, and the window closed without one frame
    // ever landing on it. Phrased identically so one grep still finds both.
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Warning, "panel",
      "the source window for " + describe_stream(after) +
      " has closed having served it NO frames after " +
      format_seconds(after.unserved_seconds) +
      ". QarOS was told this stream exists and never got a frame on it. The stream "
      "stays up showing the panel-closed notice; its video sender is deliberately "
      "NOT destroyed, because close mode 'notice' is for a QarOS runtime that does "
      "not survive destroying a sender mid-session.",
      key);
  } else {
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Info, "panel",
      "the source window for " + describe_stream(after) + " has closed after " +
      std::to_string(static_cast<unsigned long long>(after.frames_submitted)) +
      " frame(s); the stream stays up and now shows the panel-closed notice. The "
      "video sender is deliberately NOT destroyed — close mode 'notice' is for a "
      "QarOS runtime that does not survive destroying a sender mid-session. The "
      "operator dismisses the panel in AR.",
      key);
  }
  if (after.tombstone_clipped) {
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Warning, "panel",
      "the panel is too narrow to show its own name in the closed-source notice; "
      "the operator will see it cut off",
      key);
  }
  // Saying it here rather than only at the refusal keeps the number in front of
  // the operator while there is still room to do something about it.
  const std::uint32_t tombstones = tombstoned_count();
  if (config_.max_tombstoned_panels > 0 && tombstones >= config_.max_tombstoned_panels) {
    refuse_for_tombstone_cap(key);
  }
  return qar_ar_core::Result::success();
}

bool
QtPanelStreamManager::note_ar_panel_closed(const std::string & key)
{
  const auto it = streams_.find(key);
  if (it == streams_.end()) {
    return false;
  }
  if (it->second->capture_suspended()) {
    return true;
  }
  // Pointers pressed on a panel that is gone from AR have to be let go of on
  // this side too, exactly as a tombstone does.
  ++input_depth_;
  std::vector<PointerAction> nested;
  std::vector<PointerAction> & actions = input_depth_ > 1 ? nested : action_scratch_;
  actions.clear();
  pointer_state_.forget_panel(key, actions);
  dispatch_actions(actions, PanelHit{});
  --input_depth_;

  it->second->suspend_capture();
  if (config_.close_mode == PanelCloseMode::Notice) {
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Info, "panel",
      "the operator closed this panel in AR; capture has stopped. The video sender "
      "stays alive until session shutdown, because close mode 'notice' is for a "
      "QarOS runtime that does not survive destroying a sender mid-session.",
      key);
  } else {
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Info, "panel",
      "the operator closed this panel in AR; capture has stopped. The video sender is "
      "destroyed when its Qt window closes (or at session shutdown), and "
      "qar_gui_panels_close_panel will not be called for it again.",
      key);
  }
  return true;
}

void
QtPanelStreamManager::pump_closed_panels()
{
  if (!config_.input_source) {
    return;
  }
  // The member scratch is TAKEN for the loop, not walked in place: applying a
  // close can run widget code (the pointer release), and a close the widget asks
  // for in answer drains again from inside this one. The nested call finds the
  // member empty and uses a vector of its own, so it never clears what this
  // frame is iterating.
  std::vector<std::string> keys;
  keys.swap(closed_panel_scratch_);
  keys.clear();
  config_.input_source->drain_closed_panels(keys);
  for (const std::string & key : keys) {
    note_ar_panel_closed(key);
  }
  keys.clear();
  closed_panel_scratch_.swap(keys);  // hand the capacity back for the next tick
}

std::uint32_t
QtPanelStreamManager::tombstoned_count() const
{
  std::uint32_t count = 0;
  for (const auto & entry : streams_) {
    if (entry.second->tombstoned()) {
      ++count;
    }
  }
  return count;
}

bool
QtPanelStreamManager::refuse_for_tombstone_cap(const std::string & key)
{
  if (config_.max_tombstoned_panels == 0) {
    return false;
  }
  if (tombstoned_count() < config_.max_tombstoned_panels) {
    return false;
  }
  ++tombstone_cap_refusals_;
  if (!tombstone_cap_reported_) {
    tombstone_cap_reported_ = true;
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Warning, "panel",
      "this session is holding " + std::to_string(static_cast<unsigned long long>(
        config_.max_tombstoned_panels)) +
      " panels whose source windows have closed. Their video senders are not "
      "released before session shutdown (close mode 'notice' is for a QarOS "
      "runtime that does not survive destroying one mid-session), so no further "
      "panel stream will be created. "
      "Re-opening a window that already has a retired panel re-uses it; anything "
      "else needs a restart, or a larger max_tombstoned_panels.",
      key);
  }
  return true;
}

void
QtPanelStreamManager::report_retired(const qar_ar_core::PanelStreamDiagnostics & retired)
{
  if (retired.frames_submitted == 0 && !retired.stream_id.empty()) {
    // The bad shape, and worth a Warning rather than the usual Info: we
    // announced a stream to QarOS, it went looking for it, and we retired it
    // without ever putting a frame on it. From the headset's side that is
    // indistinguishable from a producer that hung.
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Warning, "panel",
      "removed panel stream " + describe_stream(retired) +
      " having served it NO frames after " + format_seconds(retired.unserved_seconds) +
      ". QarOS was told this stream exists and never got a frame on it.",
      retired.key);
    return;
  }
  emit_diagnostic(
    qar_ar_core::DiagnosticSeverity::Info, "panel",
    "removed panel stream " + describe_stream(retired) + " after " +
    std::to_string(static_cast<unsigned long long>(retired.frames_submitted)) + " frame(s)",
    retired.key);
}

void
QtPanelStreamManager::clear()
{
  // What the operator closed in AR is applied FIRST, so the panels they already
  // closed are closed here without a second qar_gui_panels_close_panel.
  pump_closed_panels();
  // Everything still held is about to be closed, so a queued close has nothing
  // left to wait for.
  deferred_closes_.clear();

  ++input_depth_;
  std::vector<PointerAction> nested;
  std::vector<PointerAction> & actions = input_depth_ > 1 ? nested : action_scratch_;
  actions.clear();
  pointer_state_.reset(actions);
  dispatch_actions(actions, PanelHit{});
  --input_depth_;

  // THE session-shutdown path. In Notice mode it is the only place a video
  // sender is destroyed (every other teardown tombstones instead — see the
  // lifetime table on QtPanelStreamManager); here the whole peer is going away,
  // so there is no other stream left for a reconnecting receiver to starve. Each
  // panel is closed exactly once: close() is idempotent, and the map is emptied
  // immediately afterwards so nothing can reach a closed stream again.
  for (auto & entry : streams_) {
    // Unregister BEFORE close(), while widget() is whatever this stream is
    // actually showing — a tombstoned stream shows a notice it owns, and that
    // notice dies with the stream a few lines below.
    PanelOverlayAttribution::instance().unregister_panel(entry.second->widget());
    entry.second->close();
    // Same accounting as a tombstone: a session that ends is still the last
    // word on what each panel served, and a panel that served nothing is worth
    // the same warning here as anywhere else.
    report_retired(entry.second->diagnostics());
  }
  streams_.clear();
  dialog_panels_.clear();
}

bool
QtPanelStreamManager::is_tombstoned(const std::string & key) const
{
  const auto it = streams_.find(key);
  return it != streams_.end() && it->second->tombstoned();
}

bool
QtPanelStreamManager::key_is_live(const std::string & key) const
{
  const auto it = streams_.find(key);
  return it != streams_.end() && !it->second->tombstoned();
}

std::map<std::string, std::unique_ptr<QtPanelStream>>::const_iterator
QtPanelStreamManager::find_live(const std::string & key) const
{
  const auto it = streams_.find(key);
  if (it != streams_.end() && (it->second->tombstoned() || it->second->capture_suspended())) {
    return streams_.end();
  }
  return it;
}

std::vector<std::string>
QtPanelStreamManager::tombstoned_panel_keys() const
{
  std::vector<std::string> keys;
  for (const auto & entry : streams_) {
    if (entry.second->tombstoned()) {
      keys.push_back(entry.first);
    }
  }
  return keys;
}

bool
QtPanelStreamManager::has_panel(const std::string & key) const
{
  return streams_.find(key) != streams_.end();
}

std::vector<std::string>
QtPanelStreamManager::panel_keys() const
{
  std::vector<std::string> keys;
  keys.reserve(streams_.size());
  for (const auto & entry : streams_) {
    keys.push_back(entry.first);
  }
  return keys;
}

std::vector<std::string>
QtPanelStreamManager::dialog_panel_keys() const
{
  std::vector<std::string> keys;
  keys.reserve(dialog_panels_.size());
  for (const DialogPanel & entry : dialog_panels_) {
    keys.push_back(entry.key);
  }
  return keys;
}

qar_ar_core::Result
QtPanelStreamManager::set_panel_pose(const std::string & key, const QarPose & pose)
{
  const auto it = streams_.find(key);
  if (it == streams_.end()) {
    return qar_ar_core::Result::error(qar_ar_core::Status::NotFound, "no panel stream for key " + key);
  }
  it->second->set_pose(pose);
  return qar_ar_core::Result::success();
}

qar_ar_core::Result
QtPanelStreamManager::set_panel_region_source(
  const std::string & key, QWidget * region, std::shared_ptr<LatestVideoFrame> source)
{
  const auto it = streams_.find(key);
  if (it == streams_.end()) {
    return qar_ar_core::Result::error(qar_ar_core::Status::NotFound, "no panel stream for key " + key);
  }
  it->second->set_region_source(region, std::move(source));
  return qar_ar_core::Result::success();
}

qar_ar_core::Result
QtPanelStreamManager::clear_panel_region_sources(const std::string & key)
{
  const auto it = streams_.find(key);
  if (it == streams_.end()) {
    return qar_ar_core::Result::error(qar_ar_core::Status::NotFound, "no panel stream for key " + key);
  }
  it->second->clear_region_sources();
  return qar_ar_core::Result::success();
}

std::uint32_t
QtPanelStreamManager::tick()
{
  // First of all: a close that had to wait for a pointer delivery to unwind. By
  // the time a tick runs, the delivery that asked for it has normally returned (a
  // tick that runs INSIDE a nested loop leaves it queued while its stream is
  // still mid-delivery), and nothing below should capture a stream that is
  // already closing.
  run_deferred_closes();

  // Input before capture: a press changes what the widget looks like, and the
  // frame captured a few lines below should already show it. A host whose tick
  // is a QTimer opts out and pumps from a posted event instead — see
  // QtPanelStreamingConfig::pump_input_in_tick, and the freeze it prevents.
  if (config_.pump_input_in_tick) {
    pump_input();
  }

  // The other thing the runtime tells us about a panel: the operator closed it
  // in AR. Drained here unconditionally — including in a host that pumps input
  // from a posted event — because applying one touches no widget and therefore
  // cannot enter a nested event loop, which is the only reason pump_input is
  // ever kept out of tick(). Before the sweep and before any capture, so a
  // panel closed in AR is not captured again on this tick.
  pump_closed_panels();

  // Dialog panels BEFORE any capture, for two reasons: a dialog destroyed since
  // the last tick must be retired before its stream paints through a dangling
  // widget, and a dialog opened by the input pumped just above should reach the
  // headset on this tick rather than the next one.
  sweep_dialog_panels();

  // One clock read for the whole batch, so every panel paces against the same
  // instant (and a fake clock makes the pacing exactly reproducible).
  const double now = clock_->now_seconds();
  ++ticks_;

  std::uint32_t submitted = 0;
  for (auto & entry : streams_) {
    const std::string previous_error = entry.second->diagnostics().last_error;
    const std::uint64_t previous_rebuilds = entry.second->diagnostics().adapter_rebuilds;
    const std::uint32_t previous_popups = entry.second->owned_popup_count();
    const bool ok = entry.second->tick(now);
    // A popup opening or closing over this panel: one line per edge, never per frame (the
    // count only changes on a captured tick, and a string is built only on an edge).
    const std::uint32_t popups = entry.second->owned_popup_count();
    if (previous_popups == 0 && popups > 0) {
      emit_diagnostic(
        qar_ar_core::DiagnosticSeverity::Info, "panel",
        "popup open over this panel: " + entry.second->popup_summary() +
        (entry.second->offscreen_popup_count() > 0 ?
        "; kept off the desktop, drawn only into the AR panel" :
        "; a desktop window, drawn into the AR panel too"),
        entry.first);
    } else if (previous_popups > 0 && popups == 0) {
      emit_diagnostic(qar_ar_core::DiagnosticSeverity::Info, "panel", "popup closed", entry.first);
    }
    // A changed adapter_rebuilds count means a panel sender was just built (at
    // creation, or after a settled resize): log its full geometry exactly
    // once, the same "diff the diagnostics" idiom the error case below uses.
    if (entry.second->diagnostics().adapter_rebuilds != previous_rebuilds) {
      emit_diagnostic(
        qar_ar_core::DiagnosticSeverity::Info, "panel", entry.second->diagnostics().last_geometry_summary,
        entry.first);
    }
    if (ok) {
      ++submitted;
      continue;
    }
    const std::string & error = entry.second->diagnostics().last_error;
    // Only surface a CHANGED error, otherwise a persistently unlaid-out widget
    // would spam the sink at the tick rate.
    if (!error.empty() && error != previous_error) {
      emit_diagnostic(qar_ar_core::DiagnosticSeverity::Warning, "panel", error, entry.first);
    }
  }

  // AFTER the batch, so a panel that submitted on this very tick is not
  // reported for the run it just ended.
  for (auto & entry : streams_) {
    report_unserved(*entry.second);
  }
  return submitted;
}

void
QtPanelStreamManager::report_unserved(QtPanelStream & stream)
{
  if (config_.unserved_warn_seconds <= 0.0) {
    return;
  }
  const qar_ar_core::PanelStreamDiagnostics & d = stream.diagnostics();
  // A panel the operator closed in AR is not capturing any more, so it will
  // never submit and there is nobody left waiting on it. Nagging about it every
  // heartbeat for the rest of the session would be noise.
  if (d.ar_panel_closed) {
    return;
  }
  if (!d.active || d.frames_submitted != 0 || d.unserved_reported ||
    d.unserved_seconds < config_.unserved_warn_seconds)
  {
    return;
  }
  stream.mark_unserved_reported();
  emit_diagnostic(
    qar_ar_core::DiagnosticSeverity::Warning, "panel",
    "QarOS has a panel from us it is not being served: " +
    describe_stream(d) + " has had a sender for " +
    format_seconds(d.unserved_seconds) + " and has submitted no frame (" +
    std::to_string(static_cast<unsigned long long>(d.not_ready_streak)) +
    " consecutive not-ready). The headset re-asks for an unanswered stream on "
    "its own timer, so this repeats until a frame lands or the panel is retired.",
    d.key);
}

std::uint32_t
QtPanelStreamManager::pump_input()
{
  if (!config_.input_source) {
    return 0;
  }
  // Re-entrancy, not paranoia: delivering a click below can enter QDialog::exec,
  // and the posted pump that feeds the operator's next AR click into that dialog
  // runs while this frame is still on the stack. Borrow the shared scratch only
  // at the top level; deeper frames pay one small allocation to stay disjoint.
  ++input_depth_;
  std::vector<qar_ar_core::PanelPointerSample> nested;
  std::vector<qar_ar_core::PanelPointerSample> & batch = input_depth_ > 1 ? nested : sample_scratch_;
  batch.clear();
  config_.input_source->drain(batch);
  // Indexed, not range-for: handle_sample() re-enters the event loop for a modal
  // dialog, and an inner pump can append to this very vector if it is the shared
  // scratch. Indexing keeps the walk valid where an iterator would dangle.
  for (std::size_t i = 0; i < batch.size(); ++i) {
    const qar_ar_core::PanelPointerSample sample = batch[i];
    handle_sample(sample);
  }
  const auto processed = static_cast<std::uint32_t>(batch.size());
  --input_depth_;
  if (input_depth_ == 0) {
    // The outermost frame has unwound, so no delivery of ours is on the stack any
    // more: a close a handler asked for from inside one can run now.
    run_deferred_closes();
  }
  return processed;
}

std::uint32_t
QtPanelStreamManager::dismiss_owned_dialogs(const std::string & key)
{
  const auto it = streams_.find(key);
  if (it == streams_.end()) {
    return 0;
  }
  QWidget * root = it->second->widget();
  if (root == nullptr) {
    return 0;
  }
  PanelOverlayOptions options;
  options.popups = false;
  options.tooltips = false;
  options.dialogs = true;
  // Read-only, like the input path: the escape hatch closes dialogs this panel
  // is actually showing, and never adopts one by being asked to tidy up.
  options.decide_attribution = false;
  std::uint32_t closed = 0;
  // Innermost first: closing a parent dialog can destroy its children, and
  // walking the snapshot forwards would then touch freed windows.
  const std::vector<QWidget *> dialogs = collect_panel_overlays(root, options);
  for (auto d = dialogs.rbegin(); d != dialogs.rend(); ++d) {
    QPointer<QWidget> guard(*d);
    if (guard.isNull()) {
      continue;
    }
    guard->close();
    ++closed;
  }
  if (closed > 0) {
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Info, "panel", "closed " + std::to_string(closed) +
      " dialog(s) owned by this panel", key);
  }
  return closed;
}

// --- dialog panels -----------------------------------------------------------

void
QtPanelStreamManager::sweep_dialog_panels()
{
  if (!config_.dialog_panels) {
    return;
  }
  // Retire to a fixed point before promoting: retiring one dialog panel orphans
  // every dialog panel IT owns, and those must go in the same sweep or a nested
  // dialog would keep a QarOS panel alive with nothing behind it.
  while (retire_one_stale_dialog_panel()) {
  }
  promote_owned_dialogs();
}

bool
QtPanelStreamManager::retire_one_stale_dialog_panel()
{
  for (std::size_t i = 0; i < dialog_panels_.size(); ++i) {
    const DialogPanel & entry = dialog_panels_[i];
    // Destroyed, or merely hidden: both mean the operator is done with it.
    // `close()` on a QDialog hides it, and `exec()` returning hides it, so
    // visibility - not destruction - is what ends a dialog panel's life.
    const bool gone = entry.dialog.isNull() || !entry.dialog->isVisible();
    // key_is_live, not find(): an owner that has been TOMBSTONED is not
    // streaming a window, so its dialogs are orphans exactly as if it had been
    // erased. Testing mere existence here would leave a dialog panel ticking
    // against a notice for the rest of the session.
    const bool orphaned = !key_is_live(entry.owner_key);
    if (!gone && !orphaned) {
      continue;
    }
    const std::string key = entry.key;
    // Erase the bookkeeping FIRST: close_panel() below delivers cancel/leave
    // actions into real widgets, which is arbitrary code, and it must never find
    // this entry half-removed.
    dialog_panels_.erase(dialog_panels_.begin() + static_cast<std::ptrdiff_t>(i));
    // Through the close mode, like every other Qt-side close. In the default
    // Destroy mode the dialog's AR panel is closed and its sender destroyed, and
    // opening the same dialog again is a fresh add: a new panel on a new sender.
    // Under QAR_PANEL_CLOSE_MODE=notice this is the tombstone — the sender is
    // kept (on a runtime that does not survive destroying one mid-session, the
    // mixer's receiver for the dead stream enters `reconnecting` and the 3D
    // scene freezes), the panel shows the notice, and the key is reusable so
    // re-opening the dialog revives this very stream instead of minting another
    // sender (see unique_key).
    close_panel(
      key,
      gone ? "the dialog was dismissed"
           : "the panel that opened it stopped streaming",
      gone ? "This dialog has been dismissed, so its picture is no longer live."
           : "The panel that opened this dialog has stopped streaming, so its picture "
             "is no longer live.");
    return true;
  }
  return false;
}

void
QtPanelStreamManager::promote_owned_dialogs()
{
  // Snapshot the keys: add_dialog_panel() below mutates streams_. A dialog panel
  // created this tick is therefore considered from the NEXT tick on, so a dialog
  // opened from a dialog is promoted one level per tick - in the order the
  // operator opened them, which is the order they stack on the desktop.
  std::vector<std::string> owner_keys;
  owner_keys.reserve(streams_.size());
  for (const auto & entry : streams_) {
    owner_keys.push_back(entry.first);
  }

  PanelOverlayOptions options;
  options.popups = false;
  options.tooltips = false;
  options.dialogs = true;
  // The one place that DECIDES an attribution. Promoting the dialog is what
  // makes it visible in AR, and the rule is that a dialog becomes clickable in
  // AR exactly when it becomes visible in AR - so the input path only ever reads
  // decisions this sweep has already taken.
  options.decide_attribution = true;

  for (const std::string & owner_key : owner_keys) {
    const auto owner = streams_.find(owner_key);
    if (owner == streams_.end()) {
      continue;
    }
    // A notice owns no dialogs: its widget is a placeholder we built, and the
    // tree that opened dialogs is gone.
    if (owner->second->tombstoned() || owner->second->capture_suspended()) {
      continue;
    }
    QWidget * const root = owner->second->widget();
    if (root == nullptr) {
      continue;
    }
    for (QWidget * dialog : collect_panel_overlays(root, options)) {
      if (dialog == nullptr || widget_is_streamed(dialog)) {
        continue;  // already has a panel - this one, or the host's own
      }
      add_dialog_panel(owner_key, dialog);
    }
  }
}

void
QtPanelStreamManager::add_dialog_panel(const std::string & owner_key, QWidget * dialog)
{
  const auto owner = streams_.find(owner_key);
  if (owner == streams_.end() || dialog == nullptr) {
    return;
  }

  // Sized from the DIALOG. A visible dialog is already laid out, so this normally
  // changes nothing at all - the operator's real window is not ours to rearrange.
  // The hints only stand in for a dialog shown but not yet given a size, where
  // the alternative is a panel of zero pixels.
  QSize natural = dialog->size();
  if (natural.isEmpty()) {
    natural = dialog->sizeHint();
  }
  if (natural.isEmpty()) {
    natural = dialog->minimumSizeHint();
  }
  if (!natural.isEmpty() && dialog->size() != natural) {
    dialog->resize(natural);
  }

  // Readability, pacing and input policy come from the owner, so a dialog reads
  // at the same scale and takes AR input the same way as the panel that opened
  // it. Only the identity and the pose are the dialog's own.
  PanelStreamOptions options = owner->second->options();

  QString title = dialog->windowTitle();
  if (title.isEmpty()) {
    title = dialog->objectName();
  }
  if (title.isEmpty()) {
    title = QStringLiteral("Dialog");
  }
  const std::string name = title.toStdString();
  options.display_name = name;

  // Beside the owner, never on top of it: no call asks QarOS where the operator is
  // looking (the viewer's pose comes only with a render sender), so the best available
  // placement is the owner's pose stepped sideways, as the host does for several panels.
  std::size_t sibling_index = 0;
  for (const DialogPanel & entry : dialog_panels_) {
    if (entry.owner_key == owner_key) {
      ++sibling_index;
    }
  }
  options.placement = owner->second->placement();
  options.placement.pose.position.x += static_cast<float>(
    config_.dialog_panel_offset_m * static_cast<double>(sibling_index + 1));

  const std::string key = unique_key("dialog:" + name);
  const qar_ar_core::Result added = add_panel(key, dialog, options);
  if (!added.ok()) {
    emit_diagnostic(
      qar_ar_core::DiagnosticSeverity::Warning, "panel",
      "could not give the dialog a panel of its own: " + added.message, key);
    return;
  }

  DialogPanel entry;
  entry.key = key;
  entry.owner_key = owner_key;
  entry.dialog = dialog;
  dialog_panels_.push_back(std::move(entry));
  emit_diagnostic(
    qar_ar_core::DiagnosticSeverity::Info, "panel",
    "dialog opened from '" + owner_key + "' is streaming as a panel of its own", key);
}

bool
QtPanelStreamManager::widget_is_streamed(const QWidget * widget) const
{
  for (const auto & entry : streams_) {
    // A tombstoned stream's widget is the notice we own, never a dialog, so it
    // can never match — but say so explicitly: a dialog re-shown after its
    // panel was tombstoned MUST come out "not streamed" here, so the sweep
    // promotes it again and add_panel revives the retired slot.
    if (entry.second->tombstoned()) {
      continue;
    }
    if (entry.second->widget() == widget) {
      return true;
    }
  }
  return false;
}

std::string
QtPanelStreamManager::unique_key(const std::string & base) const
{
  std::string key = base;
  // Two dialogs can genuinely share a title (two "Add" boxes from two docks), and
  // a key is an identity the pointer path round-trips - so collisions get a
  // suffix rather than a refusal.
  //
  // key_is_live, not find(): a TOMBSTONED key is free, so the same dialog opened
  // again lands back on its own retired slot and add_panel revives it. Testing
  // mere existence here would mint "dialog:Add #2", "#3", "#4"... - one
  // permanent video sender per open, which under a lifetime that never destroys
  // senders is an unbounded leak rather than untidy naming.
  for (int n = 2; key_is_live(key); ++n) {
    key = base + " #" + std::to_string(n);
  }
  return key;
}

std::vector<PanelHitCandidate>
QtPanelStreamManager::hit_candidates() const
{
  std::vector<PanelHitCandidate> candidates;
  candidates.reserve(streams_.size());
  for (const auto & entry : streams_) {
    // A notice takes no input: it has nothing to click and no injector. Leaving
    // it in the pick set would let a dead panel swallow a contact aimed at a
    // live one behind it, which is worse than the click missing.
    if (entry.second->tombstoned() || entry.second->capture_suspended()) {
      continue;
    }
    PanelHitCandidate candidate;
    candidate.key = entry.first;
    candidate.placement = entry.second->placement();
    candidate.logical_width = entry.second->logical_width();
    candidate.logical_height = entry.second->logical_height();
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

bool
QtPanelStreamManager::resolve_sample(
  const qar_ar_core::PanelPointerSample & sample, ResolvedPointerSample & out, PanelHit & out_hit) const
{
  out = ResolvedPointerSample{};
  out.pointer_id = sample.pointer_id;
  out.gesture = sample.gesture;
  out.phase = sample.phase;
  out.timestamp_s = sample.timestamp_s;
  out.hand = sample.hand;
  out.tap_count = sample.tap_count;
  out_hit = PanelHit{};

  const std::string grab = pointer_state_.grabbed_panel(sample.pointer_id);
  if (!grab.empty()) {
    // Grab semantics: whatever the pointer is over now, its samples belong to
    // the panel it pressed, projected onto that panel's plane — outside the
    // rectangle if need be. Only that panel's geometry is consulted.
    const auto it = find_live(grab);
    if (it == streams_.end()) {
      return false;
    }
    const QtPanelStream & stream = *it->second;
    PanelHit hit;
    if (sample.has_room_point) {
      hit = hit_test_panel_point(stream.placement(), sample.room_point);
    } else if (sample.has_room_ray) {
      hit = intersect_panel_ray(stream.placement(), sample.ray_origin, sample.ray_direction);
    } else if (sample.has_panel_uv) {
      if (sample.panel_key == grab) {
        hit = hit_from_uv(sample.u, sample.v);
      } else {
        // Resolved to some OTHER panel by the source: go back through room
        // space so the grabbed panel sees it in its own frame.
        const auto other = find_live(sample.panel_key);
        if (other != streams_.end()) {
          const QarVector3 point =
            panel_uv_to_room_point(other->second->placement(), sample.u, sample.v);
          hit = hit_test_panel_point(stream.placement(), point);
        }
      }
    }
    double x = 0.0;
    double y = 0.0;
    if (!panel_hit_to_widget_point(hit, stream.logical_width(), stream.logical_height(), x, y)) {
      return false;  // unplaceable: the state machine keeps the last position
    }
    out.target.panel_key = grab;
    out.target.x = x;
    out.target.y = y;
    out.target.inside = hit.inside;
    out_hit = hit;
    return true;
  }

  PanelPick pick;
  if (sample.has_room_point) {
    pick = pick_panel_for_point(hit_candidates(), sample.room_point);
  } else if (sample.has_room_ray) {
    pick = pick_panel_for_ray(hit_candidates(), sample.ray_origin, sample.ray_direction);
  } else if (sample.has_panel_uv) {
    const auto it = find_live(sample.panel_key);
    if (it != streams_.end()) {
      const PanelHit hit = hit_from_uv(sample.u, sample.v);
      if (panel_uv_to_widget_px(
          hit, it->second->logical_width(), it->second->logical_height(), pick.widget_x,
          pick.widget_y))
      {
        pick.hit = true;
        pick.key = sample.panel_key;
        pick.where = hit;
      }
    }
  }
  if (!pick.hit) {
    return false;
  }

  const auto it = find_live(pick.key);
  double x = 0.0;
  double y = 0.0;
  if (it == streams_.end() ||
    !panel_hit_to_widget_point(
      pick.where, it->second->logical_width(), it->second->logical_height(), x, y))
  {
    return false;
  }
  out.target.panel_key = pick.key;
  out.target.x = x;
  out.target.y = y;
  out.target.inside = true;
  out_hit = pick.where;
  return true;
}

void
QtPanelStreamManager::dispatch_actions(
  const std::vector<PointerAction> & actions, const PanelHit & hit)
{
  for (const PointerAction & action : actions) {
    const auto it = streams_.find(action.panel_key);
    if (it != streams_.end()) {
      it->second->deliver_input(action);
    }

    if (config_.input_sink == nullptr) {
      continue;
    }
    PanelPointerEvent event;
    if (action.kind == PointerActionKind::Move) {
      event.kind = PanelPointerEvent::Kind::Hover;
    } else if (action.kind == PointerActionKind::Click) {
      event.kind = PanelPointerEvent::Kind::Click;
    } else {
      continue;  // the sink is coarse by design; see PanelPointerEvent
    }
    event.panel_key = action.panel_key;
    event.pointer_id = action.pointer_id;
    event.hit = hit;
    // Same floor + clamp as panel_uv_to_widget_px, so the two agree.
    const auto to_px = [](double value, std::uint32_t extent) {
      const auto px = static_cast<std::int32_t>(std::floor(value));
      const std::int32_t last = extent == 0 ? 0 : static_cast<std::int32_t>(extent) - 1;
      return std::clamp(px, 0, last);
    };
    const std::uint32_t width = it != streams_.end() ? it->second->logical_width() : 0;
    const std::uint32_t height = it != streams_.end() ? it->second->logical_height() : 0;
    event.widget_x = to_px(action.x, width);
    event.widget_y = to_px(action.y, height);
    config_.input_sink->on_panel_pointer(event);
  }
}

bool
QtPanelStreamManager::handle_sample(const qar_ar_core::PanelPointerSample & sample)
{
  ResolvedPointerSample resolved;
  PanelHit hit;
  const bool on_panel = resolve_sample(sample, resolved, hit);

  // Count the ARRIVAL against a panel before the state machine, the injector or
  // anything else can discard it. The source's own key wins when it has one
  // (the per-panel path resolved the contact itself, and that is the panel the
  // runtime addressed); otherwise the hit test's answer is the only key there
  // is. A sample that lands on no panel at all is counted nowhere — that is
  // what the manager-wide input_samples/input_samples_missed pair is for.
  const std::string & arrived_on =
    sample.has_panel_uv ? sample.panel_key : resolved.target.panel_key;
  if (!arrived_on.empty()) {
    const auto arrived = streams_.find(arrived_on);
    if (arrived != streams_.end()) {
      arrived->second->note_input_sample();
    }
  }

  // Same re-entrancy rule as pump_input: dispatch_actions() below is what can
  // enter a nested event loop, so the actions it is walking must not be the
  // buffer an inner frame is about to refill.
  ++input_depth_;
  std::vector<PointerAction> nested;
  std::vector<PointerAction> & actions = input_depth_ > 1 ? nested : action_scratch_;
  actions.clear();
  pointer_state_.process(resolved, actions);
  dispatch_actions(actions, hit);
  --input_depth_;
  if (input_depth_ == 0) {
    run_deferred_closes();  // see pump_input
  }
  return on_panel;
}

bool
QtPanelStreamManager::route_room_point(
  const QarVector3 & point_room, PanelPointerEvent::Kind kind)
{
  qar_ar_core::PanelPointerSample sample;
  sample.pointer_id = kImplicitPointerId;
  sample.gesture = kind == PanelPointerEvent::Kind::Click ? qar_ar_core::PointerGesture::Click : qar_ar_core::PointerGesture::Hover;
  sample.phase = kind == PanelPointerEvent::Kind::Click ? qar_ar_core::PointerPhase::Instant : qar_ar_core::PointerPhase::Updated;
  sample.timestamp_s = clock_->now_seconds();
  sample.has_room_point = true;
  sample.room_point = point_room;
  return handle_sample(sample);
}

bool
QtPanelStreamManager::route_room_ray(
  const QarVector3 & origin, const QarVector3 & direction, PanelPointerEvent::Kind kind)
{
  qar_ar_core::PanelPointerSample sample;
  sample.pointer_id = kImplicitPointerId;
  sample.gesture = kind == PanelPointerEvent::Kind::Click ? qar_ar_core::PointerGesture::Click : qar_ar_core::PointerGesture::Hover;
  sample.phase = kind == PanelPointerEvent::Kind::Click ? qar_ar_core::PointerPhase::Instant : qar_ar_core::PointerPhase::Updated;
  sample.timestamp_s = clock_->now_seconds();
  sample.has_room_ray = true;
  sample.ray_origin = origin;
  sample.ray_direction = direction;
  return handle_sample(sample);
}

qar_ar_core::PanelDiagnosticsSnapshot
QtPanelStreamManager::diagnostics() const
{
  qar_ar_core::PanelDiagnosticsSnapshot snapshot;
  snapshot.panel_count = static_cast<std::uint32_t>(streams_.size());
  snapshot.ticks = ticks_;
  snapshot.last_error = last_error_;
  const PointerStateDiagnostics & input = pointer_state_.diagnostics();
  snapshot.input_samples = input.samples;
  snapshot.input_samples_missed = input.samples_missed;
  snapshot.input_samples_dropped = input.samples_dropped;
  snapshot.last_input_drop_reason = input.last_drop_reason;
  snapshot.tombstones_created = tombstones_created_;
  snapshot.tombstones_revived = tombstones_revived_;
  snapshot.tombstone_cap_refusals = tombstone_cap_refusals_;
  snapshot.panels_closed = panels_closed_;
  snapshot.close_panel_skipped = close_panel_skipped_;
  snapshot.close_panel_failures = close_panel_failures_;
  snapshot.closes_deferred = closes_deferred_;
  snapshot.panels.reserve(streams_.size());
  for (const auto & entry : streams_) {
    const qar_ar_core::PanelStreamDiagnostics & d = entry.second->diagnostics();
    snapshot.panels.push_back(d);
    snapshot.streaming_active = snapshot.streaming_active || d.active;
    if (d.tombstoned) {
      ++snapshot.tombstoned_panels;
    } else {
      ++snapshot.live_panels;
    }
    if (d.ar_panel_closed) {
      ++snapshot.ar_closed_panels;
    }
  }
  return snapshot;
}

void
QtPanelStreamManager::reset_submit_gap_windows()
{
  for (auto & entry : streams_) {
    entry.second->reset_submit_gap_window();
  }
}

}  // namespace qar_qt_streaming
