// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QT_PANEL_STREAM_MANAGER_HPP_
#define QAR_QT_STREAMING__QT_PANEL_STREAM_MANAGER_HPP_

#include "qar_qt_streaming/clock.hpp"
#include "qar_qt_streaming/panel_close_mode.hpp"
#include "qar_qt_streaming/panel_hit_test.hpp"
#include "qar_ar_core/panel_input_source.hpp"
#include "qar_qt_streaming/panel_pointer_state.hpp"
#include "qar_ar_core/panel_types.hpp"
#include "qar_ar_core/qar_panel_sender.hpp"
// qt_panel_stream.hpp pulls in latest_video_frame.hpp (and real Qt headers)
// transitively, AFTER panel_pointer_state.hpp above — see the ordering note
// in qt_panel_stream.hpp for why that order matters (`emit` as a method name).
#include "qar_qt_streaming/qt_panel_stream.hpp"

#include <QPointer>

#include <map>
#include <memory>
#include <string>
#include <vector>

class QWidget;

namespace qar_qt_streaming
{

/// @brief One resolved AR interaction, addressed to a specific panel pixel.
///
/// The coarse OUTPUT of the interaction seam: a hover landed here, a click
/// landed there. Hosts that only want to know "which panel, which pixel" listen
/// to this; the finer press/drag/release stream lives in
/// panel_pointer_state.hpp and is what drives the Qt injector.
struct PanelPointerEvent
{
  enum class Kind
  {
    Hover,  ///< A free pointer moved over the panel.
    Click   ///< A press and release completed inside the panel.
  };

  std::string panel_key;
  Kind kind = Kind::Hover;
  std::uint32_t pointer_id = 0;
  PanelHit hit{};
  /// Widget-local LOGICAL pixel — what a synthetic Qt event would need.
  std::int32_t widget_x = 0;
  std::int32_t widget_y = 0;
};

/// @brief Receives resolved panel interactions.
///
/// Optional and informational: the manager turns every action into real Qt
/// events itself (see qt_panel_input_injector.hpp) whether or not a sink is
/// attached. A host attaches one to log, to visualize, or to drive something
/// that is not a widget.
class IQarPanelInputSink
{
public:
  virtual ~IQarPanelInputSink() = default;
  virtual void on_panel_pointer(const PanelPointerEvent & event) = 0;
};

/// @brief Construction inputs for the manager.
struct QtPanelStreamingConfig
{
  /// The only seam to QarOS. Required.
  std::shared_ptr<qar_ar_core::IQarPanelSenderFactory> panel_sender_factory;

  /// Optional; null => diagnostics are accumulated but not logged.
  qar_ar_core::PanelDiagnosticsSink * diagnostics = nullptr;

  /// Optional; null => a default SteadyClock. Tests inject a fake clock.
  std::shared_ptr<IClock> clock;

  /// Optional; null => resolved interactions are not reported (they are
  /// still injected into the widgets).
  IQarPanelInputSink * input_sink = nullptr;

  /// Optional; drained on every tick() (and on pump_input()). Null => the
  /// host feeds samples through handle_sample() / route_room_point() itself.
  std::shared_ptr<qar_ar_core::IQarPanelInputSource> input_source;

  /// Should tick() drain @ref input_source itself? (default yes)
  ///
  /// **Set this false in any host whose tick runs from a QTimer.** Delivering a
  /// pointer action runs arbitrary widget code — and a button that opens a modal
  /// dialog spins a nested `QEventLoop` inside `QDialog::exec()` that does not return
  /// until the operator dismisses it. If that happens inside the host's
  /// QTimer handler, Qt's dispatcher refuses to re-deliver THAT timer while its
  /// handler is still on the stack (`WinTimerInfo::inTimerEvent` on Windows, the
  /// same guard elsewhere), so the tick — and with it every capture, every
  /// submit and the 3D stream sharing the tick — stops dead for as long as the
  /// dialog is open. This flag exists to close that hole; see
  /// docs/ARCHITECTURE.md#popups-and-dialogs.
  ///
  /// With it false the host calls @ref pump_input from a POSTED event instead
  /// (`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`). A nested loop
  /// entered from there leaves the timer free to keep firing, so the AR stream
  /// keeps running while the dialog is up, and the next posted pump — which a
  /// nested loop delivers like any other — carries the operator's AR clicks into
  /// the dialog.
  bool pump_input_in_tick = true;

  /// @brief Give a dialog opened from a streamed panel a GUI panel of ITS OWN
  /// (default true). See @ref QtPanelStreamManager for the sweep that does it.
  ///
  /// False leaves a dialog invisible in AR and answerable only on the desktop.
  /// There is no third option: compositing the dialog into the panel that opened
  /// it does not work, because a dialog squeezed into a dock's tall, narrow
  /// rectangle shows the operator its top half and none of its buttons.
  bool dialog_panels = true;

  /// @brief Room-space X step between a panel and each dialog panel it owns,
  /// in metres.
  ///
  /// No call asks QarOS where the operator is looking: the viewer's pose comes only
  /// with a render sender's frame information, which a panel-only program has not got.
  /// A dialog panel is therefore placed at its owner's pose, stepped sideways so the
  /// two do not overlap, by the same rule and 0.5 m the host uses for several panels.
  double dialog_panel_offset_m = 0.5;

  /// @brief Seconds a panel may hold a live sender without ever submitting a
  /// frame before the manager says so out loud. 0 disables the report.
  ///
  /// The state itself is legitimate and transient — a sender exists before
  /// anything in the headset has attached to its stream, which the runtime
  /// reports as MEDIA_STREAMING_NO_ACTIVE_TRANSFERS (604) and which we
  /// deliberately treat as not-ready rather than an error. What is NOT
  /// legitimate is for it to last: QarOS re-queries `streamRequest/<stream id>`
  /// for a stream it has been promised and never receives, on its own timer,
  /// for as long as the session lives. Every second past this threshold is a
  /// second of that loop, and without this report the only trace of it is in
  /// the runtime's own log under a UUID nothing on our side prints.
  ///
  /// Five seconds is far longer than a normal attach (well under a second) and
  /// far shorter than a person's patience with a blank rectangle.
  double unserved_warn_seconds = 5.0;

  /// @brief What closing a streamed window does to its AR panel. See
  /// @ref PanelCloseMode and @ref QtPanelStreamManager::close_panel.
  ///
  /// **Default Destroy**: the AR panel is closed, its video sender destroyed, the
  /// stream forgotten. `Notice` is the tombstone mode, the escape hatch for a
  /// runtime on which destroying a sender mid-session is not safe: switching to
  /// it is an environment variable (`QAR_PANEL_CLOSE_MODE=notice`, parsed by
  /// @ref parse_panel_close_mode where the host builds this config), not a
  /// rebuild. Every close the Qt side initiates goes through this — the dialog
  /// sweep, the host's own sweep for a destroyed widget, the Stop button, a
  /// closed dock — so the mode is the single switch.
  PanelCloseMode close_mode = PanelCloseMode::Destroy;

  /// @brief How many tombstoned panels this manager may hold at once. Only
  /// meaningful in @ref PanelCloseMode::Notice: in the default Destroy mode
  /// nothing is ever tombstoned, so nothing accumulates.
  ///
  /// A tombstoned panel (see @ref QtPanelStreamManager, "Panel lifetime")
  /// keeps its video sender and its QarOS GUI panel for the rest of the
  /// session by design: Notice mode exists for a runtime that does not survive
  /// destroying a sender mid-session. That makes tombstones a resource
  /// that only ever grows, so it is bounded in two ways:
  ///
  ///  1. **Slot reuse first.** Re-adding a tombstoned KEY revives that stream
  ///     onto the new widget rather than minting a sender — so an operator
  ///     opening and closing the same dock, or the same dialog, fifty times
  ///     costs exactly one sender. That is what bounds the case that actually
  ///     happens.
  ///  2. **This cap for the rest.** Once this many DISTINCT windows have been
  ///     tombstoned, no new panel stream is created at all — not a
  ///     host-requested one and not a promoted dialog — and a Warning says so
  ///     naming the cap. Refusing to create is the only lever available:
  ///     closing an existing tombstone's sender to make room would destroy a
  ///     sender mid-session, which is what this mode exists to avoid. The
  ///     session has to end (or this number be raised) to reclaim them.
  ///
  /// 16 is chosen to be far more distinct windows than an operator streams in
  /// one session, and far fewer than a number of senders that would trouble
  /// the runtime. 0 disables the cap entirely.
  std::uint32_t max_tombstoned_panels = 16;

  /// Double-click detection tunables; defaults match Qt's desktop defaults.
  PointerStateConfig pointer_state{};
};

/// @brief Top-level coordinator: N QWidgets streamed as N QarOS GUI panels.
///
/// Mirrors qar_ogre_streaming's OgreQarStreamingManager in role, but is far
/// simpler because there is nothing to schedule across threads: every method
/// here must be called on the Qt GUI thread, which is also the only thread
/// allowed to paint a widget or to receive input for one. See
/// qt_widget_capture.hpp and qt_panel_input_injector.hpp.
///
/// ## Dialog panels
///
/// Panels the HOST adds are not the only panels. Every @ref tick begins with a
/// sweep that gives each dialog owned by a streamed panel - owned structurally,
/// or by causation through @c PanelOverlayAttribution - a GUI panel of its own:
///
///  * **sized from the dialog**, not from its owner. That is the whole point:
///    a dock is tall and narrow and the dialog it opens (an "Add" box, say) is
///    not, and compositing the dialog into the dock that opened it would give the
///    operator its top half with the buttons off the edge;
///  * **placed beside its owner**, one
///    @ref QtPanelStreamingConfig::dialog_panel_offset_m step in room X per
///    dialog that owner already has, so the two do not overlap;
///  * **retired** as soon as the dialog hides, closes or is destroyed - or its
///    owner stops streaming. Retiring runs @ref close_panel, so in the default
///    Destroy mode the video sender, the QarOS panel and its click/hover
///    subscription all go with it (and in Notice mode the panel becomes the
///    closed-window notice). That matters more than tidiness: the input source's
///    callback-slot arena is process-lifetime and never reused, so one panel
///    leaked per dialog opened would eventually exhaust it;
///  * **clickable through its own panel.** The dialog's stream has its own
///    injector, for which the dialog IS the panel, so AR input reaches it by the
///    ordinary path. Meanwhile an application-modal dialog still blocks input to
///    its OWNER panel, exactly as Qt blocks the window behind it.
///
/// The sweep is also the only place an attribution DECISION is taken, because
/// promoting the dialog is what makes it visible in AR; see
/// @ref PanelOverlayOptions::decide_attribution.
///
/// ## Panel lifetime — chosen by QtPanelStreamingConfig::close_mode
///
/// **Destroy (the default; the current QarOS runtime survives destroying a
/// stream mid-session).** A streamed window that closes has
/// its AR panel closed and its video sender destroyed, and the stream is
/// forgotten — no notice, no slot kept. @ref close_panel does it, in this order:
///
///  1. cancel the panel's pointer state and its AR input/update subscriptions
///     first, so our own close is not mistaken for an AR-side close and no
///     callback reaches a retiring slot;
///  2. `qar_gui_panels_close_panel` — SKIPPED if the operator already closed the
///     panel from the AR side (never called twice for one panel), and reported,
///     not hidden, if it fails;
///  3. `qar_video_sender_handle_destroy` — the panel was the sender's consumer,
///     so the consumer goes first;
///  4. drop the stream entry.
///
/// Steps 1-3 are the adapter's (IQarPanelSenderAdapter::close); the manager owns
/// the rest and logs every close: key, reason, what each step did, frames served.
/// Re-opening the same window later is an ordinary fresh add.
///
/// **Notice (`QAR_PANEL_CLOSE_MODE=notice`, the escape hatch).** For a runtime on
/// which destroying a sender mid-session is not safe, the rule is:
///
/// > **A video sender is created once and destroyed only at session shutdown.**
///
/// On such a runtime destroying a sender leaves any attached receiver
/// `reconnecting`, and one reconnecting receiver raises 603 out of the QarOS
/// mixer's frame callback, which aborts the callback and starves every OTHER
/// stream that mixer serves — the stereo 3D scene included. So the stream keeps
/// running and, instead of real GUI data, streams a whole-panel message saying
/// the GUI window has been closed on the streamer side and the operator may now
/// close the GUI panel in the AR (see qar_ogre_adapter
/// docs/QAROS_INTEGRATION_NOTES.md, "Teardown and destroy order").
///
/// In Notice mode every path that closes a window leads to one of two states,
/// and neither closes a sender:
///
/// | Event | What happens | Sender |
/// |---|---|---|
/// | Source widget destroyed, dock closed, dialog dismissed, @ref close_panel / @ref remove_panel, dialog sweep retiring a stale dialog | @ref tombstone_panel — the stream keeps running and shows the closed-source notice (panel_tombstone.hpp), naming the window | **kept** |
/// | Operator closes the panel in AR (`qar_gui_panels_subscribe_updates` reports state closed) | @ref note_ar_panel_closed — capture stops, nothing more is submitted | **kept** |
/// | Re-adding a tombstoned key | the slot is revived onto the new widget | **re-used** |
/// | Session shutdown (@ref clear, destructor) | every panel is closed exactly once | **destroyed** |
///
/// The accumulation Notice trades for is bounded by slot reuse plus
/// @ref QtPanelStreamingConfig::max_tombstoned_panels, and is visible through
/// PanelDiagnosticsSnapshot's tombstone counters. In Destroy mode the AR-side
/// close (the third column of the second row) still only suspends capture: the
/// sender is destroyed when the Qt window closes, or at shutdown, whichever is
/// first — and the AR close is then remembered so `close_panel` is not called a
/// second time.
class QtPanelStreamManager
{
public:
  /// @throws std::invalid_argument if @c panel_sender_factory is null.
  explicit QtPanelStreamManager(QtPanelStreamingConfig config);
  ~QtPanelStreamManager();

  QtPanelStreamManager(const QtPanelStreamManager &) = delete;
  QtPanelStreamManager & operator=(const QtPanelStreamManager &) = delete;

  /// @brief Start streaming @p widget as a GUI panel identified by @p key.
  ///
  /// The QarOS panel is not created here — it is created on the first @ref tick
  /// that finds the widget laid out, so this call makes no QarOS call; that tick creates
  /// the sender and the panel, and a close destroys them, synchronously on the GUI thread.
  ///
  /// If @p key names a TOMBSTONED stream, this revives that slot onto @p widget
  /// instead of minting a second sender: same sender, same QarOS panel, same
  /// display name (a panel's title is fixed at creation), new widget and new
  /// pose. That is the primary bound on tombstone growth — see
  /// @ref QtPanelStreamingConfig::max_tombstoned_panels.
  ///
  /// Refused with @c Status::InvalidState when the tombstone cap is already
  /// reached and this would be a NEW stream; reviving is always allowed,
  /// because it creates nothing.
  qar_ar_core::Result add_panel(const std::string & key, QWidget * widget, const PanelStreamOptions & options);

  /// @brief The source window for @p key is gone (or its streaming was stopped):
  /// close its AR panel the way QtPanelStreamingConfig::close_mode says.
  ///
  /// **The one door every Qt-side close goes through** — the dialog sweep, the
  /// host's sweep for a destroyed widget, its Stop button, a closed dock.
  ///
  ///  * @ref PanelCloseMode::Destroy (default): pointers pressed on the panel are
  ///    cancelled, then the adapter is closed — input subscriptions, then
  ///    `qar_gui_panels_close_panel` (skipped if the operator already closed the
  ///    panel in AR; its failure is logged as a Warning and does not stop what
  ///    follows), then the video sender — and the stream entry is dropped. The
  ///    key is free again: adding it later is a normal fresh add. One summary line
  ///    per close goes to the diagnostics sink (key, reason, what each step did,
  ///    frames served).
  ///  * @ref PanelCloseMode::Notice: exactly @ref tombstone_panel.
  ///
  /// **Safe to call from inside the panel's own click handler** — which is where
  /// a Stop button pressed in AR on the very panel it stops calls it from. A
  /// stream with a pointer action still being delivered into its widget is not
  /// destroyed under that delivery; the close is queued and runs as soon as the
  /// delivery has returned (on the way out of the outermost @ref pump_input /
  /// @ref handle_sample, or at the top of the next @ref tick). The call still
  /// returns success.
  ///
  /// @param reason One SHORT phrase for the log line and the status line, e.g.
  ///        "the window was closed". Empty means "no reason given".
  /// @param notice_reason What the closed-source notice says in Notice mode
  ///        (a full sentence is fine there). Empty reuses @p reason.
  /// @return NotFound if @p key is not streaming.
  qar_ar_core::Result close_panel(
    const std::string & key, const std::string & reason,
    const std::string & notice_reason = std::string());

  /// @brief The source window for @p key is gone: keep the stream, show the
  /// closed-source notice. **Whatever the close mode** — this is the explicit
  /// Notice operation, and what @ref close_panel does in Notice mode.
  ///
  /// **Does not close the video sender.** See the lifetime table on this class.
  /// Pointers pressed on the panel are cancelled first, so no widget is left
  /// holding a press.
  ///
  /// A stream that never got as far as creating a sender is simply erased
  /// instead: the rule is about not destroying senders, and there is none to
  /// destroy, so leaving a notice with nothing behind it would cost a slot for
  /// no benefit.
  ///
  /// @param reason One short phrase for the notice and the status line, e.g.
  ///        "the window was closed". Empty takes the default.
  qar_ar_core::Result tombstone_panel(const std::string & key, const std::string & reason);

  /// @brief Stop streaming @p key's widget: @ref close_panel with no reason.
  ///
  /// Every caller means the same thing by it ("this widget is finished"). What
  /// it does is the close mode's answer — destroy by default, a notice under
  /// `QAR_PANEL_CLOSE_MODE=notice`.
  qar_ar_core::Result remove_panel(const std::string & key);

  /// @brief The operator closed @p key's panel in AR: stop capturing it.
  ///
  /// Driven by `qar_gui_panels_subscribe_updates` reporting the panel's state
  /// reaching closed — the manager drains those from the input source on every
  /// @ref tick, so a host using QarCApiPanelInputSource needs no wiring of its
  /// own.
  ///
  /// **The sender is NOT destroyed here**, in either close mode: the AR side
  /// closing a panel is not the Qt window closing. Capture stops, the fact is
  /// remembered, and teardown waits for the Qt-side close (@ref close_panel) or
  /// session shutdown — whichever comes first. Either way the remembered fact is
  /// what makes the adapter SKIP `qar_gui_panels_close_panel`: the operator
  /// already closed that panel, and it is never closed twice.
  /// @return false if @p key is not streaming.
  bool note_ar_panel_closed(const std::string & key);

  /// @brief **SESSION SHUTDOWN.** Close every panel still held — destroying every
  /// remaining video sender, exactly once — and forget them.
  ///
  /// A panel already closed through @ref close_panel (Destroy mode) has left the
  /// manager and is not touched again, so nothing is closed or destroyed twice. A
  /// panel the operator closed in AR is closed here without a second
  /// `qar_gui_panels_close_panel`.
  ///
  /// In Notice mode this is the ONLY place a sender is destroyed while anything
  /// else still exists, and it is safe precisely because nothing else does: the
  /// peer is going away, so there is no other stream from it left to starve.
  void clear();

  bool has_panel(const std::string & key) const;

  /// @brief The close mode this manager runs in (QtPanelStreamingConfig::close_mode).
  PanelCloseMode close_mode() const { return config_.close_mode; }

  /// @brief Is @p key a panel whose source window is gone (a notice)? Destroy
  /// mode never leaves one behind (only an explicit @ref tombstone_panel does),
  /// so there this is false for every key.
  bool is_tombstoned(const std::string & key) const;

  std::vector<std::string> panel_keys() const;

  /// @brief The keys currently showing the closed-source notice.
  std::vector<std::string> tombstoned_panel_keys() const;

  /// @brief The keys of the panels the dialog sweep created, oldest first.
  /// A subset of @ref panel_keys.
  std::vector<std::string> dialog_panel_keys() const;

  /// @brief Move one panel in the room.
  qar_ar_core::Result set_panel_pose(const std::string & key, const QarPose & pose);

  /// @brief Show @p source's live video in place of @p region's own captured
  /// pixels, for @p key's panel — see
  /// @ref QtWidgetCapture::set_region_source for the full contract (region
  /// ownership, tombstone/revive behaviour) and its class file comment for
  /// why this exists at all (a native window embedded in a widget, as the image
  /// displays of a host such as an RViz plugin do, is something
  /// `QWidget::render()` cannot paint).
  /// @return Status::NotFound if @p key names no panel stream.
  qar_ar_core::Result set_panel_region_source(
    const std::string & key, QWidget * region, std::shared_ptr<LatestVideoFrame> source);

  /// @brief Stop substituting every region registered on @p key's panel.
  /// @return Status::NotFound if @p key names no panel stream.
  qar_ar_core::Result clear_panel_region_sources(const std::string & key);

  /// @brief Drain pending input, then run one capture/submit cycle across
  /// every panel. Input goes first so a click's visual response is captured
  /// in the same tick.
  ///
  /// The input half is skipped when
  /// @ref QtPanelStreamingConfig::pump_input_in_tick is false, in which case the
  /// host owes a @ref pump_input from a posted event — read that flag's note
  /// before changing it, the reason is a whole-stream freeze.
  /// @return the number of panels that actually submitted a frame.
  std::uint32_t tick();

  /// @brief Drain the input source and process every sample. Also called by
  /// @ref tick (unless the config says otherwise); a host with a faster input
  /// timer than its capture timer can call it directly.
  ///
  /// Re-entrant on purpose. A delivered click can spin a nested event loop
  /// (`QDialog::exec`), and the posted pump that carries the operator's NEXT
  /// AR click into that dialog then runs while this call is still on the stack.
  /// The reused scratch buffers are therefore swapped for locals below the top
  /// level, so an inner pump never stamps on the outer one's batch.
  /// @return the number of samples processed.
  std::uint32_t pump_input();

  /// @brief Close every dialog owned by @p key's widget tree.
  ///
  /// The programmatic escape hatch, for a host that wants an explicit "get me
  /// out of this" control: an application-modal dialog blocks the rest of the
  /// desktop, and the operator is wearing a headset. `QWidget::close()` is what
  /// the window's own close button does — for a QDialog it runs `reject()`,
  /// which unwinds `exec()` exactly as Cancel would. The dialog's own AR panel
  /// is retired by the next tick's sweep, because the dialog is hidden.
  /// @return the number of dialogs closed.
  std::uint32_t dismiss_owned_dialogs(const std::string & key);

  /// @brief Process one pointer sample: resolve it against the live panels
  /// (honouring the pointer's grab), run the state machine, inject the
  /// resulting actions and notify the sink.
  /// @return true if the sample resolved onto a panel.
  bool handle_sample(const qar_ar_core::PanelPointerSample & sample);

  /// @brief Route a room-space point (e.g. QarAppVolumeGestureEvent::action_point)
  /// to whichever panel it lands on, nearest first. Sugar over
  /// @ref handle_sample for one implicit pointer: a Hover is a hover move, a
  /// Click is an INSTANT tap.
  /// @return true if a panel was hit.
  bool route_room_point(const QarVector3 & point_room, PanelPointerEvent::Kind kind);

  /// @brief Route a room-space ray (a hand-ray source) the same way.
  bool route_room_ray(
    const QarVector3 & origin,
    const QarVector3 & direction,
    PanelPointerEvent::Kind kind);

  const PanelPointerStateMachine & pointer_state() const { return pointer_state_; }

  qar_ar_core::PanelDiagnosticsSnapshot diagnostics() const;

  /// @brief Start a new window for every panel's
  /// PanelStreamDiagnostics::submit_gap_* statistics. A host that logs them
  /// periodically calls this right after reading @ref diagnostics.
  void reset_submit_gap_windows();

private:
  /// One dialog promoted to a panel of its own, and who it belongs to.
  struct DialogPanel
  {
    std::string key;        ///< Its own key in @c streams_.
    std::string owner_key;  ///< The panel whose dialog it is.
    /// Weak: the dialog can be destroyed between ticks, and that is the signal
    /// to retire the panel rather than tick a dangling widget.
    QPointer<QWidget> dialog;
  };

  /// A close the Qt side asked for while the stream was still inside a pointer
  /// delivery: held until that delivery has returned. See @ref close_panel.
  struct DeferredClose
  {
    std::string key;
    std::string reason;
  };

  /// The Destroy-mode body of @ref close_panel (also what a deferred close runs):
  /// pointers released, adapter closed (input, panel, sender), entry dropped, one
  /// summary line logged.
  qar_ar_core::Result destroy_panel(const std::string & key, const std::string & reason);

  /// Run the closes @ref close_panel queued. Called at the top of @ref tick and on
  /// the way out of the outermost input frame; a close whose stream is STILL in a
  /// delivery (an outer frame further up the stack) stays queued.
  void run_deferred_closes();

  /// The summary line (and, when `qar_gui_panels_close_panel` failed, the
  /// Warning) for one Destroy-mode close. @p before is the stream's diagnostics as
  /// they stood just before the close, so the frame count and stream id are the
  /// ones that were live.
  void report_closed(
    const qar_ar_core::PanelStreamDiagnostics & before, const qar_ar_core::PanelCloseReport & report,
    const std::string & reason);

  /// Retire dead dialog panels, then promote newly visible owned dialogs.
  /// Called at the top of @ref tick, before any widget is painted, so a dialog
  /// destroyed since the last tick never reaches a capture.
  void sweep_dialog_panels();

  /// Retire ONE stale dialog panel and report that it did, so the caller can
  /// re-scan: retiring a dialog panel orphans the dialog panels IT owns.
  bool retire_one_stale_dialog_panel();

  /// Give every owned dialog that has none a panel of its own.
  void promote_owned_dialogs();

  /// Create the stream for @p dialog, owned by @p owner_key. Sized from the
  /// dialog, placed beside the owner, inheriting the owner's readability,
  /// pacing and input policy.
  void add_dialog_panel(const std::string & owner_key, QWidget * dialog);

  /// Is @p widget already streaming - as a host panel or as a dialog panel?
  bool widget_is_streamed(const QWidget * widget) const;

  /// @p base, or @p base + " #n" for the first n that is free.
  ///
  /// A TOMBSTONED key counts as free, and that is deliberate: it is what makes
  /// a dialog opened a second time land back on its own retired slot (which
  /// @ref add_panel then revives) instead of minting "dialog:Add #2",
  /// "#3", "#4"... — one permanent sender per open, which is precisely the
  /// unbounded growth the tombstone lifetime would otherwise cause.
  std::string unique_key(const std::string & base) const;

  /// Is @p key held by a stream that is still showing a real window?
  bool key_is_live(const std::string & key) const;

  /// @p key's stream, but only while it can still take input — end() for a
  /// notice or a panel the operator closed in AR. A click can still be
  /// delivered for a tombstoned panel (its QarOS click subscription outlives
  /// the widget, like everything else about the stream), and it must land
  /// nowhere rather than on a placeholder.
  std::map<std::string, std::unique_ptr<QtPanelStream>>::const_iterator
  find_live(const std::string & key) const;

  /// Panels currently showing the closed-source notice.
  std::uint32_t tombstoned_count() const;

  /// Would creating a NEW stream exceed the tombstone cap? Emits the warning
  /// and counts the refusal when it would.
  bool refuse_for_tombstone_cap(const std::string & key);

  /// Drain the input source's AR-side panel-closed notifications and apply
  /// them. Called from @ref tick.
  void pump_closed_panels();

  /// Flatten the live streams into the Qt-free form the pickers consume.
  std::vector<PanelHitCandidate> hit_candidates() const;

  /// Geometry: place a sample on a panel, keeping a grabbed pointer on the
  /// panel it pressed. Returns whether the target is a panel at all.
  bool resolve_sample(
    const qar_ar_core::PanelPointerSample & sample, ResolvedPointerSample & out, PanelHit & out_hit) const;

  /// Deliver actions to the panels' injectors and the sink.
  void dispatch_actions(const std::vector<PointerAction> & actions, const PanelHit & hit);

  void emit_diagnostic(
    qar_ar_core::DiagnosticSeverity severity,
    const std::string & category,
    const std::string & message,
    const std::string & panel_key);

  /// Say, once per unserved run, that @p stream has a live sender QarOS is not
  /// being served from. See @ref QtPanelStreamingConfig::unserved_warn_seconds.
  void report_unserved(QtPanelStream & stream);

  /// The retirement line for a panel that has just been closed and erased —
  /// the only place its stream id and served-frame count appear together.
  ///
  /// Retiring is also where the one thing we CANNOT do becomes visible. The C
  /// API has no call that withdraws a logical video stream:
  /// `qar_video_sender_handle_destroy` returns void and
  /// `qar_gui_panels_close_panel` closes the panel, not the stream behind it. A
  /// receiver that was attached is therefore left reconnecting — the runtime
  /// logs `ReconnectableVideoReceiver: connection lost for stream <id>,
  /// entering reconnecting state` followed by an attempt every ten seconds,
  /// each failing 507 BackendExhaustedRetriesToCreate, until the producer peer
  /// disconnects entirely. Nothing here can stop that loop; this line is what
  /// lets a reader recognise it with one search instead of reconstructing it
  /// from UUIDs. See qar_ogre_adapter docs/QAROS_INTEGRATION_NOTES.md,
  /// "Known SDK limitations".
  void report_retired(const qar_ar_core::PanelStreamDiagnostics & retired);

  QtPanelStreamingConfig config_;
  std::shared_ptr<IClock> clock_;
  /// Ordered so panel_keys()/diagnostics() are deterministic for tests.
  std::map<std::string, std::unique_ptr<QtPanelStream>> streams_;
  /// Dialog panels in creation order, so a dialog's offset from its owner is
  /// stable for as long as it is open.
  std::vector<DialogPanel> dialog_panels_;
  PanelPointerStateMachine pointer_state_;
  std::vector<qar_ar_core::PanelPointerSample> sample_scratch_;
  std::vector<PointerAction> action_scratch_;
  /// How many pump_input()/handle_sample() frames are live on this stack; > 1
  /// means a nested event loop re-entered us and the scratch is already in use.
  int input_depth_ = 0;
  std::uint64_t ticks_ = 0;
  /// Session totals behind PanelDiagnosticsSnapshot's tombstone counters.
  std::uint64_t tombstones_created_ = 0;
  std::uint64_t tombstones_revived_ = 0;
  std::uint64_t tombstone_cap_refusals_ = 0;
  /// Latched so the cap is reported once per crossing rather than once per
  /// refused call, like every other "say it once" diagnostic here.
  bool tombstone_cap_reported_ = false;
  /// Scratch for pump_closed_panels(), reused like the input batches.
  std::vector<std::string> closed_panel_scratch_;
  /// Closes waiting for a pointer delivery to unwind. Usually empty.
  std::vector<DeferredClose> deferred_closes_;
  /// Keys whose teardown is running right now (destroy_panel's stack), so a
  /// widget that answers the pointer release by asking for the same close again
  /// finds it already under way. Usually empty; a vector because teardowns nest.
  std::vector<std::string> closing_keys_;
  /// Session totals behind PanelDiagnosticsSnapshot's close counters.
  std::uint64_t panels_closed_ = 0;
  std::uint64_t close_panel_skipped_ = 0;
  std::uint64_t close_panel_failures_ = 0;
  std::uint64_t closes_deferred_ = 0;
  std::string last_error_;
};

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__QT_PANEL_STREAM_MANAGER_HPP_
