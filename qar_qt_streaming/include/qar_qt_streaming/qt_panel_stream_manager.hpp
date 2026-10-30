// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QT_PANEL_STREAM_MANAGER_HPP_
#define QAR_QT_STREAMING__QT_PANEL_STREAM_MANAGER_HPP_

#include "qar_qt_streaming/clock.hpp"
#include "qar_qt_streaming/panel_close_mode.hpp"
#include "qar_qt_streaming/panel_resize_mode.hpp"
#include "qar_qt_streaming/panel_hit_test.hpp"
#include "qar_ar_core/common_name.hpp"
#include "qar_ar_core/panel_input_source.hpp"
#include "qar_qt_streaming/panel_pointer_state.hpp"
#include "qar_ar_core/panel_types.hpp"
#include "qar_ar_core/qar_panel_sender.hpp"
// qt_panel_stream.hpp pulls in latest_video_frame.hpp (and real Qt headers)
// transitively, AFTER panel_pointer_state.hpp above — see the ordering note
// in qt_panel_stream.hpp for why that order matters (`emit` as a method name).
#include "qar_qt_streaming/qt_panel_stream.hpp"

#include <QPointer>
#include <QRect>
#include <QSize>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
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

  /// @brief How much taller than it is a dialog is made on the desktop when it is promoted to a
  /// panel of its own: its height is multiplied by this, its width is left alone. 1.0, the
  /// default, changes nothing.
  ///
  /// The change is made before the dialog's stream is added, so the QarOS panel is created at
  /// the final size and is never rebuilt for it. It is made once for each dialog window: the
  /// same window promoted again after it was hidden keeps the height it has. Only a factor above
  /// 1.0 has an effect, and a dialog of a fixed height ignores it. A dialog size adjuster
  /// (QtPanelStreamManager::set_dialog_size_adjuster) takes its place: the factor is not applied
  /// while one is installed.
  double dialog_height_factor = 1.0;

  /// @brief The frame that QarOS draws around the video of the AR panels of this manager: four
  /// insets and the fit margin in metres (default PanelFrame{}, the frame of the QarOS desktop
  /// Visualizer).
  ///
  /// QarOS draws the video of a stream panel inside a frame of its own, which the SDK does not
  /// size, so the area under it must be a hair taller than the video (panel_geometry.hpp). A
  /// panel is therefore made as tall as its video needs at the width the frame leaves it, plus
  /// the fit margin, and keeps its width. The panel of a dialog that a streamed window opens
  /// (PanelStreamOptions::dialog_panel) keeps the height of its video and gets the width the
  /// frame and the margin need for that height instead. All four insets and the margin 0 gives a
  /// panel exactly as tall as its video and a dialog the width of its widget. The value is
  /// applied to every panel the manager makes in place of PanelStreamOptions::ar_frame. Every
  /// inset and the margin finite and not negative.
  PanelFrame ar_frame{};

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
  ///
  /// `Park` (`QAR_PANEL_CLOSE_MODE=park`) closes nothing while the session runs: the
  /// AR panel is hidden and kept, and adding the same key again shows it again. The host
  /// can change the mode while the manager runs (@ref QtPanelStreamManager::set_close_mode).
  PanelCloseMode close_mode = PanelCloseMode::Destroy;

  /// @brief The part of every panel's QarOS common name that tells programs, instances and PCs
  /// apart (qar_ar_core::make_panel_common_name). QarOS finds a panel again in the next session by
  /// that name, so a panel that was hidden when the last session ended is found and shown again.
  /// A scope with empty members is valid: the names are then built from the key and the
  /// placeholder of an empty label alone.
  qar_ar_core::NameScope name_scope{};

  /// @brief What a size change of a window does to its QarOS panel (see @ref PanelResizeMode):
  /// by default the panel stays and its video sender is swapped. The host reads
  /// `QAR_PANEL_RESIZE` with @ref parse_panel_resize_mode.
  PanelResizeMode resize_mode = PanelResizeMode::Swap;

  /// @brief How many tombstoned or parked panels this manager may hold at once. Only
  /// meaningful in @ref PanelCloseMode::Notice and @ref PanelCloseMode::Park: in the
  /// default Destroy mode nothing is ever tombstoned or parked, so nothing accumulates.
  /// The QarOS panels that size changes replaced and kept do not count here: they have a
  /// budget of their own, @ref max_replaced_panels.
  ///
  /// A tombstoned or parked panel (see @ref QtPanelStreamManager, "Panel lifetime")
  /// keeps its video sender and its QarOS GUI panel for the rest of the
  /// session by design: both modes exist for a runtime that does not survive
  /// closing a panel or destroying a sender mid-session. That makes them a resource
  /// that only ever grows, so it is bounded in two ways:
  ///
  ///  1. **Slot reuse first.** Re-adding a tombstoned or parked KEY revives that stream
  ///     onto the new widget rather than minting a sender — so an operator
  ///     opening and closing the same dock, or the same dialog, fifty times
  ///     costs exactly one sender. That is what bounds the case that actually
  ///     happens.
  ///  2. **This cap for the rest.** Once this many DISTINCT windows have been
  ///     tombstoned or parked, no new panel stream is created at all - not a
  ///     host-requested one and not a promoted dialog - and a Warning says so
  ///     naming the cap. Refusing to create is the only lever available:
  ///     closing an existing stream's sender to make room would destroy a
  ///     sender mid-session, which is what these modes exist to avoid. The
  ///     session has to end (or this number be raised) to reclaim them.
  ///
  /// 16 is chosen to be far more distinct windows than an operator streams in
  /// one session, and far fewer than a number of senders that would trouble
  /// the runtime. 0 disables the cap entirely.
  std::uint32_t max_tombstoned_panels = 16;

  /// @brief How many QarOS panels that size changes replaced this manager may keep hidden at
  /// once, over all streams. Only meaningful in @ref PanelCloseMode::Park (see
  /// @ref QtPanelStreamManager, "A size change in Park mode").
  ///
  /// A widget that changes size needs a new sender and a new QarOS panel. In Park mode the old
  /// panel is hidden and kept until the session ends instead of being closed, because closing a
  /// QarOS panel while the session runs may crash the desktop Visualizer. Each kept panel holds
  /// a video sender and a QarOS panel for the rest of the session, so they are bounded:
  ///
  ///  * While the session holds fewer kept panels than this, a size change keeps its old panel.
  ///    The Warning of the size change that takes the last slot says that the next ones close.
  ///  * At the limit a size change closes its old panel, as in the other modes, and a Warning
  ///    says so.
  ///
  /// The kept panels have this budget of their own: they do not count toward
  /// @ref max_tombstoned_panels and never block a new panel stream. 32 is far more size changes
  /// than a session normally makes once the host sizes its windows before it streams them. 0
  /// disables the limit.
  std::uint32_t max_replaced_panels = 32;

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
/// A dialog is made to fit its content before its panel exists: by the fixed
/// QtPanelStreamingConfig::dialog_height_factor, or, with a dialog size adjuster installed
/// (@ref QtPanelStreamManager::set_dialog_size_adjuster), by measuring its scroll areas over a
/// few rounds and streaming it once, at the size the fit ends with.
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
/// | Session shutdown (@ref clear, destructor) | every panel is hidden where QarOS can and left in place for the next session; a sender that cannot detach closes its panel | **destroyed** |
///
/// The accumulation Notice trades for is bounded by slot reuse plus
/// @ref QtPanelStreamingConfig::max_tombstoned_panels, and is visible through
/// PanelDiagnosticsSnapshot's tombstone counters. In Destroy mode the AR-side
/// close (the third column of the second row) still only suspends capture: the
/// sender is destroyed when the Qt window closes, or at shutdown, whichever is
/// first — and the AR close is then remembered so `close_panel` is not called a
/// second time.
///
/// **Park (`QAR_PANEL_CLOSE_MODE=park`).** For a runtime on which CLOSING a panel mid-session
/// is not safe, nothing is closed, while the session runs or when it ends. Every Qt-side
/// close - the dialog sweep, the host's sweep for a destroyed widget, @ref close_panel,
/// @ref remove_panel - PARKS the stream instead, and neither the QarOS panel nor the video
/// sender is touched:
///
/// | Event | What happens | QarOS panel and sender |
/// |---|---|---|
/// | Source widget destroyed, dock closed, dialog dismissed, @ref close_panel / @ref remove_panel, dialog sweep retiring a stale dialog | pointers pressed on the panel are released, then the panel is hidden in AR (the next tick calls `qar_gui_panels_set_state` with HIDDEN), capture stops and input is dropped; no notice is built; the window is remembered only while it exists | **kept** |
/// | The same key is added again (@ref add_panel), or a parked panel whose window still exists is shown (@ref set_panel_ar_visible) | the stream is revived onto the window, and the next tick applies the pose given and shows the panel again (a Warning if QarOS refuses the show); a dialog key is free for reuse, so opening the dialog again revives its stream | **re-used** |
/// | QarOS refuses the hide | the stream falls back to the closed-window notice, as in Notice mode, and a Warning names the key and the reason; it is never closed | **kept** |
/// | The operator closed the panel in AR before the Qt close | parked without any hide call; a revival tries to show the panel again | **kept** |
/// | Session shutdown (@ref clear, destructor) | every panel, parked ones included, is hidden where QarOS can and left in place for the next session; a sender that cannot detach closes its panel | **destroyed** |
///
/// Parked and notice panels count together against
/// @ref QtPanelStreamingConfig::max_tombstoned_panels; reviving one is always allowed. A stream
/// that never created a sender has no QarOS panel to keep and is dropped. The mode can be
/// switched while the manager runs (@ref set_close_mode); it applies to the closes that follow.
///
/// **A size change in Park mode.** A widget that changes size gets a new sender and a new QarOS
/// panel in every mode, because the layout of a sender is fixed at creation. In Destroy and
/// Notice mode the old panel is closed. In Park mode it is not closed: it is hidden in AR
/// (`qar_gui_panels_set_state` with HIDDEN, on the tick that makes the new panel) and kept with
/// its sender until the session ends, and one Info line says so. The kept panels have a budget
/// of their own, QtPanelStreamingConfig::max_replaced_panels, and do not count against the
/// cap above: the size change that takes the last slot says in a Warning that the next ones
/// close, and at the limit the old panel is closed as in the other modes and a Warning says
/// that. When QarOS refuses the hide, the old panel stays visible and kept, a Warning says so,
/// and the hide is asked for again on later ticks, three more times at most, a second apart; a
/// late success is logged. At @ref clear the kept panels are hidden and left in place with
/// the stream's own panel (a sender that cannot detach closes them); a stream closed in
/// Destroy mode closes them with it.
///
/// The residual risk of a kept panel that stayed visible: the input source keeps one
/// subscription for each key, so the subscription that reports a close in AR goes with the
/// panel when the new one is made. The operator closing a visible kept panel in AR is therefore
/// not seen, and the end of the session asks QarOS to hide it although the operator closed
/// it. A kept panel that was hidden cannot be closed in AR.
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
  /// If @p key names a PARKED stream (PanelCloseMode::Park), it is revived the same way: the
  /// QarOS panel and the sender that are standing behind the key are re-used, the stream
  /// takes @p widget (the window it was parked with or another one) and the pose of
  /// @p options, and the panel is asked to be shown again. This call makes no QarOS call: the
  /// next @ref tick applies the pose, carries the show out and sends a fresh frame, and a
  /// Warning names a show QarOS refuses. One Info line says that the show is asked for.
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
  ///  * @ref PanelCloseMode::Notice: @ref tombstone_panel, with @p notice_reason as the text.
  ///  * @ref PanelCloseMode::Park: pointers pressed on the panel are released, then the stream
  ///    is parked - hidden in AR by the next tick, capture and input stopped, the QarOS panel,
  ///    the sender and the subscriptions kept, no notice. The window is kept only while it
  ///    exists. One Info line names the key and @p reason. A stream that never created a
  ///    sender is simply dropped. If QarOS refuses the hide, the stream becomes the
  ///    closed-window notice of Notice mode (and a Warning says why); it is never closed.
  ///
  /// **Safe to call from inside the panel's own click handler, in all three modes** - which
  /// is where a Stop button pressed in AR on the very panel it stops calls it from. A
  /// stream with a pointer action still being delivered into its widget is not
  /// destroyed, parked or turned into a notice under that delivery (each of the three drops
  /// the stream's input injector, which is on the stack); the close is queued and runs as soon
  /// as the delivery has returned (on the way out of the outermost @ref pump_input /
  /// @ref handle_sample, or at the top of the next @ref tick), in the mode that is current
  /// then. The call still returns success.
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
  /// Carried out at once, and it drops the stream's input injector: a click handler that
  /// stops its own panel asks for @ref close_panel, which waits for the delivery to unwind.
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

  /// @brief Hide @p key's panel in AR (@p visible false) or show it again, without closing it.
  ///
  /// Records the request on the stream and makes **no QarOS call itself**: the next @ref tick
  /// carries it out (QtPanelStream::request_ar_visible has the whole contract). While the panel
  /// is hidden it is not captured, takes no input and is skipped by every hit test, so a pointer
  /// never lands on it; the QarOS panel, the video sender and the subscriptions stay, and a
  /// close (@ref close_panel) still closes them once. A refused request keeps the old state and
  /// shows in the stream's visibility counters; falling back to a close is the caller's call.
  /// Showing also tries to bring back a panel the operator closed in AR.
  /// @return NotFound if @p key is not streaming, InvalidState if it shows the closed-window
  ///         notice (a notice has no window to hide).
  qar_ar_core::Result set_panel_ar_visible(const std::string & key, bool visible);

  /// @brief Is @p key's panel hidden in AR? The state QarOS accepted, so a request made since
  /// the last @ref tick is not in it yet. False for an unknown key.
  bool is_ar_hidden(const std::string & key) const;

  /// @brief Is @p key a parked stream (PanelCloseMode::Park)? True from the Qt-side close that
  /// parked it, before the tick that carries out the hide, until a window is shown on it
  /// again. False for an unknown key.
  bool is_parked(const std::string & key) const;

  /// @brief The keys of the parked streams, in key order.
  std::vector<std::string> parked_panel_keys() const;

  /// @brief How many QarOS panels earlier size changes replaced and kept, over all streams (Park
  /// mode; see the class comment). They count toward
  /// @ref QtPanelStreamingConfig::max_replaced_panels and not toward
  /// @ref QtPanelStreamingConfig::max_tombstoned_panels.
  std::uint32_t replaced_panel_count() const;

  /// @brief Change what the Qt-side closes that follow do (QtPanelStreamingConfig::close_mode).
  /// Streams that are already parked or tombstoned stay as they are; a deferred close runs in
  /// the mode that is current when it runs. The host calls this when its visibility setting
  /// changes.
  void set_close_mode(PanelCloseMode mode) { config_.close_mode = mode; }

  /// @brief The height in metres that the AR panel of @p widget gets from this manager when it is
  /// added with @p options: the vertical part of QtPanelStreamingConfig::ar_frame plus the video
  /// at the width the frame leaves of the panel's plus the fit margin (the frame of @p options is
  /// replaced by the manager's, as add_panel does), or, for PanelStreamOptions::dialog_panel, the
  /// height of the video at the width the widget asks for. A host that places a panel before it
  /// is added uses this height. 0 when the widget has no size yet.
  float ar_panel_height_m(const QWidget & widget, const PanelStreamOptions & options) const;

  /// @brief What the host does to the pose of a dialog's panel, given its top-centre pose and
  /// its physical height in metres (as this library computes it: ar_panel_height_m(), the height
  /// of the video of the dialog). Every pose the dialog sweep works out -
  /// for a new dialog panel and for a revived one - goes through it before the panel takes it;
  /// the owner's pose stepped sideways is the argument. An empty function leaves the poses as
  /// they are (the default).
  using DialogPoseAdjuster = std::function<QarPose(const QarPose & top_centre, float height_m)>;
  void set_dialog_pose_adjuster(DialogPoseAdjuster adjuster)
  {
    dialog_pose_adjuster_ = std::move(adjuster);
  }

  /// @name Fitting a dialog to its content before its panel is made
  ///
  /// A dialog opened from a streamed panel gets a panel of its own, and a scroll bar inside it
  /// cannot be used in AR. A dialog size adjuster measures the dialog and sizes it, instead of
  /// the fixed QtPanelStreamingConfig::dialog_height_factor. While one is installed, a dialog
  /// the sweep finds (and the cap has not refused) is NOT streamed at once: on each tick, at most
  /// every @ref kDialogFitIntervalSeconds for each dialog, the adjuster is called with the dialog,
  /// the available geometry of its screen as @c room (the primary screen's when the dialog is on
  /// none, an empty rectangle when there is no screen at all) and the round, counting from 0.
  /// The adjuster may resize the dialog, and says whether it is done. The stream is added once,
  /// when a step says it is done or after @ref kDialogFitMaxRounds rounds, at the size the dialog
  /// has then, so the QarOS panel is made once at its final size and never rebuilt for the fit.
  /// A dialog that is hidden or destroyed while it is fitted never gets a panel. One Info line
  /// says it when the panel is made: `'<key>' fitted to <w>x<h> px in <r> round(s): <note>`.
  /// @{

  /// What one call of a dialog size adjuster found.
  struct DialogFitStep
  {
    /// The size of the dialog after the step.
    QSize size;
    /// The dialog is done: its stream is added now.
    bool settled = false;
    /// Said in the Info line when the stream is added (the note of the step that settled it):
    /// `no scroll bar`, `a scroll bar of <o> px is left (<reason>)`, `not confirmed (<reason>)`,
    /// `clamped to the screen (<room h> px)`.
    std::string note;
  };

  /// Sizes a dialog. Called on the GUI thread from tick(); it must not block and must not call
  /// back into the manager.
  using DialogSizeAdjuster =
    std::function<DialogFitStep(QWidget & dialog, const QRect & room, int round)>;

  /// @brief Install the adjuster; an empty one, the default, leaves the fixed height factor in
  /// charge. Dialogs that are being fitted when it is removed get their panels at their size
  /// then.
  void set_dialog_size_adjuster(DialogSizeAdjuster adjuster)
  {
    dialog_size_adjuster_ = std::move(adjuster);
  }

  /// The least time between two calls of the adjuster for one dialog, in seconds on the manager's
  /// clock, and the number of calls after which a dialog is streamed whatever the adjuster says.
  static constexpr double kDialogFitIntervalSeconds = 0.1;
  static constexpr int kDialogFitMaxRounds = 24;

  /// @brief The adjuster that fits a dialog to its scroll areas (scroll_census.hpp).
  ///
  /// Each call measures the dialog. Scroll areas that overflow vertically and do not scroll by
  /// design make it taller by their stacked overflow (the horizontal bar of such an area included)
  /// plus @p slack_px, to at most the available height of the screen less 40 px; the width never
  /// changes. A dialog that was made taller is moved up by as much as its lower edge then lies
  /// below the available geometry of its screen, never above the top of that geometry. Areas that
  /// scroll by design never make it grow, and the note names those that show a bar.
  ///
  /// A dialog is done when
  ///  * two calls in a row, at the same size, found nothing to remove and every area measured:
  ///    `no scroll bar`, or `a scroll bar of <o> px is left (a horizontal bar: a taller dialog
  ///    does not remove it)` when a horizontal bar is there that no height removes;
  ///  * growing it did not shrink the overflow (`a scroll bar of <o> px is left (growing the
  ///    dialog did not remove it)`), or it cannot grow any more (`clamped to the screen`);
  ///  * the round @p max_rounds is reached: `a scroll bar of <o> px is left (the fit gave up
  ///    after <n> round(s))` when a bar is left, `not confirmed (the round limit)` when the last
  ///    look found none but nothing confirmed it, `not confirmed (an area could not be measured)`
  ///    when an area could not be measured.
  static DialogSizeAdjuster make_census_dialog_adjuster(int slack_px = 8, int max_rounds = 6);
  /// @}

  /// @brief Take a scroll-area census of @p key's widget at the next frame its stream submits,
  /// and say it in one line labelled @p reason (QtPanelStream::request_scroll_census has the
  /// format). A host asks when the widget has just changed what it shows, say a tab was selected.
  /// @return false, and nothing is asked, if @p key is not streaming or its panel is not on show:
  /// parked, hidden in AR, closed in AR or showing its notice.
  bool request_scroll_census(const std::string & key, const std::string & reason);

  /// @brief **SESSION SHUTDOWN.** End every panel still held, exactly once, and forget them.
  ///
  /// In Destroy mode every panel is closed and every remaining video sender destroyed. In Notice
  /// and Park mode a panel whose sender supports IQarPanelSenderAdapter::detach is NOT closed: it
  /// is hidden in AR (`set_state(HIDDEN)`, unless it is hidden or closed already) and detached,
  /// its video sender and subscriptions released, so the next session finds it under its common
  /// name and shows it again. A sender that cannot detach is closed.
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

  /// One dialog window that a dialog size adjuster is fitting and that has no stream yet.
  struct FittingDialog
  {
    std::string owner_key;       ///< The panel whose dialog it is.
    QPointer<QWidget> dialog;    ///< Weak: a dialog that is closed meanwhile is dropped.
    int round = 0;               ///< Calls of the adjuster made so far.
    double next_seconds = 0.0;   ///< The adjuster is not called again before this instant.
  };

  /// A close the Qt side asked for while the stream was still inside a pointer
  /// delivery: held until that delivery has returned. See @ref close_panel.
  struct DeferredClose
  {
    std::string key;
    std::string reason;
    std::string notice_reason;  ///< what the notice says: Notice mode, or a park that falls back
  };

  /// The Destroy-mode body of @ref close_panel (also what a deferred close runs):
  /// pointers released, adapter closed (input, panel, sender), entry dropped, one
  /// summary line logged.
  qar_ar_core::Result destroy_panel(const std::string & key, const std::string & reason);

  /// The Park-mode body of @ref close_panel (also what a deferred close runs): pointers
  /// released, the stream parked (hide asked for, nothing closed), one Info line logged. A
  /// stream without a sender is dropped instead.
  qar_ar_core::Result park_panel(
    const std::string & key, const std::string & reason, const std::string & notice_reason);

  /// What a close of @p key does in the current close mode when it is carried out: the body of
  /// @ref close_panel after its deferral check, and what a deferred close runs.
  qar_ar_core::Result close_now(
    const std::string & key, const std::string & reason, const std::string & notice_reason);

  /// Show a window on the parked stream @p key again: the stream takes @p widget (null: the
  /// window it was parked with) and, when @p pose is given, that pose; the pointer state and
  /// the dialog attribution learn the window; the revival is counted and logged.
  qar_ar_core::Result revive_parked_panel(
    const std::string & key, QWidget * widget, const QarPose * pose);

  /// The hide a park asked for was refused: turn the parked stream of @p key into the
  /// closed-window notice and say why in a Warning. Never closes anything.
  void fall_back_to_notice(const std::string & key);

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
  /// pacing and input policy. With a dialog size adjuster installed the stream is not made
  /// here: the dialog is handed to @ref advance_dialog_fits, which makes it when the fit is done.
  void add_dialog_panel(const std::string & owner_key, QWidget * dialog);

  /// The stream of a dialog whose size is final: the options it inherits from its owner, the
  /// pose beside it, add_panel and the bookkeeping. False (after saying why) when the panel was
  /// refused.
  bool add_dialog_stream(
    const std::string & owner_key, QWidget * dialog, const std::string & key,
    const std::string & name);

  /// Call the adjuster for each dialog being fitted whose time has come, and make the streams of
  /// those that are done; drop those that were hidden or destroyed, or whose owner stopped
  /// streaming. Called from @ref tick with the instant it reads.
  void advance_dialog_fits(double now_seconds);

  /// Make the stream of a dialog whose fit is over, and say how the fit went.
  void finish_dialog_fit(const FittingDialog & fitting, const std::string & note);

  /// Is @p dialog being fitted?
  bool dialog_is_fitting(const QWidget * dialog) const;

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
  /// notice, a panel the operator closed in AR, one hidden in AR or a parked one. A click
  /// can still be delivered for a tombstoned panel (its QarOS click subscription outlives
  /// the widget, like everything else about the stream), and it must land
  /// nowhere rather than on a placeholder.
  std::map<std::string, std::unique_ptr<QtPanelStream>>::const_iterator
  find_live(const std::string & key) const;

  /// Panels that hold a sender and a QarOS panel for a window that is gone: notices and parked
  /// streams. This is what the tombstone cap counts; the panels that size changes replaced and
  /// kept are counted apart (@ref replaced_panel_count).
  std::uint32_t retained_count() const;

  /// May a stream that changes size keep, hidden, the QarOS panel it replaces instead of closing
  /// it? In Park mode while fewer than QtPanelStreamingConfig::max_replaced_panels are kept.
  /// Asked by the streams, once per rebuild.
  bool may_keep_replaced_panel() const;

  /// Is @p dialog a window that add_panel refused and that has been on show since? The sweep
  /// does not try it again on every tick.
  bool dialog_was_refused(const QWidget * dialog) const;

  /// Say, in one Info line, that @p count kept QarOS panels of @p key were closed with it.
  void report_replaced_closed(const std::string & key, std::size_t count);

  /// Say what the end of the session did with the QarOS panels of @p key: how many were left
  /// hidden in place, and a Warning for a hide that QarOS refused. @p replaced_kept is the number
  /// of kept panels the stream held; when nothing was left in place they were closed.
  void report_session_end(
    const std::string & key, std::size_t replaced_kept, const SessionEndReport & ended);

  /// Would creating a NEW stream exceed the tombstone cap? Emits the warning
  /// and counts the refusal when it would.
  bool refuse_for_tombstone_cap(const std::string & key);

  /// Say, once per crossing, that the cap is reached (see max_tombstoned_panels), without
  /// counting a refusal.
  void warn_cap_reached(const std::string & key);

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
  /// The dialog windows that QtPanelStreamingConfig::dialog_height_factor has made taller.
  /// Weak, and pruned when a window is added.
  std::vector<QPointer<QWidget>> grown_dialogs_;
  /// The dialog windows that add_panel refused (the cap was reached), so that the sweep tries
  /// each of them once and says so once. Weak. A window leaves the list when it is hidden or
  /// destroyed, and the list is emptied when a slot is free again.
  std::vector<QPointer<QWidget>> refused_dialogs_;
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
  /// Session totals behind PanelDiagnosticsSnapshot's park counters.
  std::uint64_t parks_ = 0;
  std::uint64_t park_revivals_ = 0;
  DialogPoseAdjuster dialog_pose_adjuster_;
  /// Empty: the height factor sizes dialogs. See set_dialog_size_adjuster.
  DialogSizeAdjuster dialog_size_adjuster_;
  /// The dialog windows being fitted, oldest first; they have no stream yet.
  std::vector<FittingDialog> fitting_dialogs_;
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

/// @brief The same as QtPanelStreamManager::make_census_dialog_adjuster, for a caller that
/// does not name the class.
inline QtPanelStreamManager::DialogSizeAdjuster
make_census_dialog_adjuster(int slack_px = 8, int max_rounds = 6)
{
  return QtPanelStreamManager::make_census_dialog_adjuster(slack_px, max_rounds);
}

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__QT_PANEL_STREAM_MANAGER_HPP_
