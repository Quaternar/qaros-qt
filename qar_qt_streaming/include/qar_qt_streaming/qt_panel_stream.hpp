// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QT_PANEL_STREAM_HPP_
#define QAR_QT_STREAMING__QT_PANEL_STREAM_HPP_

#include "qar_qt_streaming/ar_readability.hpp"
#include "qar_qt_streaming/capture_gate.hpp"
#include "qar_qt_streaming/panel_geometry.hpp"
#include "qar_qt_streaming/panel_resize_mode.hpp"
#include "qar_qt_streaming/panel_tombstone.hpp"
#include "qar_ar_core/common_name.hpp"
#include "qar_ar_core/panel_types.hpp"
#include "qar_ar_core/qar_panel_sender.hpp"
#include "qar_qt_streaming/qt_panel_input_injector.hpp"
#include "qar_ar_core/timing_log.hpp"
// qt_widget_capture.hpp pulls in latest_video_frame.hpp (and real Qt headers)
// LAST, deliberately: panel_pointer_state.hpp — reached above via
// qt_panel_input_injector.hpp — declares a method named `emit`, which only
// parses because Qt's `emit` keyword-macro (from any real Qt header, e.g.
// <QImage> via LatestVideoFrame) does not exist yet at that point. Keep
// anything Qt-heavy below the Qt-free headers in this list.
#include "qar_qt_streaming/qt_widget_capture.hpp"

#include <QPointer>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace qar_qt_streaming
{

/// @brief Per-panel policy the host chooses when it adds a widget.
struct PanelStreamOptions
{
  /// Shown by QarOS as the panel title. Empty => the manager uses the key.
  std::string display_name;

  /// Where the panel hangs in the room. Its @c size is OVERWRITTEN each tick
  /// from the widget's real geometry (see panel_geometry.hpp); only the pose is
  /// the host's to choose.
  qar_ar_core::PanelPlacement placement{};

  double meters_per_logical_px = kDefaultMetersPerLogicalPx;

  /// The frame that QarOS draws around the video of a stream panel: four insets and the fit
  /// margin in metres (see panel_geometry.hpp). The AR panel is made as tall as its video needs
  /// inside the frame, plus the margin, and keeps its width; the panel of a @ref dialog_panel
  /// keeps its height and gets the width the frame needs instead. All insets and the margin 0 =
  /// no frame. A QtPanelStreamManager sets this from QtPanelStreamingConfig::ar_frame for every
  /// panel it makes.
  PanelFrame ar_frame{};

  /// Scope of the QarOS common name of this panel (qar_ar_core::make_panel_common_name). A
  /// QtPanelStreamManager sets it from QtPanelStreamingConfig::name_scope for every panel it
  /// makes.
  qar_ar_core::NameScope name_scope{};

  /// What a size change does to the QarOS panel. A QtPanelStreamManager sets it from
  /// QtPanelStreamingConfig::resize_mode for every panel it makes.
  PanelResizeMode resize_mode = PanelResizeMode::Swap;

  /// The panel is the panel of a dialog window (QtPanelStreamManager makes the panels of the
  /// dialogs that a streamed window opens): it keeps the height of its video and fits its width
  /// to the frame (see PanelGeometryInput::narrow_for_frame). The size line of the stream says
  /// so.
  bool dialog_panel = false;

  /// Supersampling of the capture on top of the display's device pixel ratio
  /// (see ar_readability.hpp; @ref apply_readability fills both this and the
  /// metres from a viewing profile). 1.0 = capture at the display's own ratio.
  double render_scale = 1.0;

  std::uint32_t buffer_alignment = kDefaultBufferAlignment;
  std::uint32_t max_buffer_dimension = kDefaultMaxBufferDimension;

  /// @brief Capture/submit ceiling. 15 Hz, not 60: a control panel is text and
  /// buttons, and every capture is a full widget repaint on the GUI thread. The
  /// dirty check below means an idle panel costs one repaint per tick and no
  /// network traffic at all.
  std::uint32_t max_fps = 15;

  /// @brief Capture a panel whose content is video (a VideoPanelWidget, or any
  /// registered region source) when a frame ARRIVES, with @ref max_fps only an
  /// upper bound applied with a tolerance of half the observed tick interval,
  /// instead of on the strict @ref max_fps interval.
  ///
  /// The strict interval evaluated on a coarse tick (Windows' 15.6 ms timer
  /// granularity while the host window is occluded) turns a 30 fps cap into 21 fps; see
  /// capture_gate.hpp. No effect on a panel with no video source, or with
  /// @ref max_fps 0. False restores the strict limiter for video panels too.
  bool capture_on_frame_arrival = true;

  /// @brief Resend an UNCHANGED frame at least this often, in seconds.
  ///
  /// Purely defensive: nothing in the SDK says a video stream needs periodic
  /// frames, but a receiver that joins mid-stream would otherwise see nothing
  /// until the operator happened to touch the panel. 0 disables.
  double keepalive_seconds = 1.0;

  /// @brief How AR pointer actions reach the widget tree. See
  /// qt_panel_input_injector.hpp for the modes and the default.
  PanelInputOptions input{};

  /// @name Resize hysteresis
  ///
  /// Adopting a new widget size is NOT cheap: the sender's buffer layout is
  /// fixed at creation, so the sender, the QarOS panel and its click/hover
  /// subscription are all torn down and built again (and the input source burns
  /// two never-reused callback slots doing it). Reacting to every intermediate
  /// size is therefore the difference between one rebuild and hundreds:
  ///
  ///  * a dock splitter being dragged walks the widget through every width in
  ///    between, and
  ///  * the panel can resize ITSELF — a status label inside the streamed widget
  ///    whose text grows changes the widget's size hint, which changes the size,
  ///    which rewrites the status label. Such a feedback loop rebuilds the panel
  ///    continuously: its sender never keeps a destination, and the churn starves
  ///    the 3D stream's factory.
  ///
  /// So a size change is adopted only when it is both SIGNIFICANT (at least
  /// @ref resize_threshold_px in a dimension) and SETTLED (the same new size
  /// seen for at least @ref resize_settle_seconds AND across at least
  /// @ref resize_settle_ticks consecutive ticks). Until then the panel keeps
  /// streaming at the size its live sender was built for, so the captured buffer
  /// always matches the sender's layout. Zeroing all three adopts (rebuilds on)
  /// every change.
  /// @{

  /// Buffer-pixel delta below which a change is not worth a rebuild at all. A
  /// few pixels of clipping or background slack are invisible; a rebuild is not.
  std::uint32_t resize_threshold_px = 8;

  /// How long the new size must hold, in the same monotonic seconds @ref tick
  /// is given, before it is adopted. Long enough to outlast a self-resize loop
  /// (which cycles at the rate its status text is rewritten) and a dragged
  /// splitter, short enough that a real resize snaps in while the hand is still
  /// on the mouse.
  double resize_settle_seconds = 0.5;

  /// ...and across at least this many consecutive ticks, so a panel that is
  /// ticked rarely still gets corroboration rather than a single sample.
  std::uint32_t resize_settle_ticks = 3;

  /// @}
};

/// @brief Size a panel for a viewing condition: sets the metres per logical
/// pixel and the render scale from an ar_readability recommendation.
void apply_readability(const ReadabilityRecommendation & recommendation, PanelStreamOptions & options);

/// @brief The height in metres that the AR panel of @p widget gets when it is streamed with
/// @p options: the geometry the stream computes (PanelGeometry::height_meters) for the size
/// the widget has now. A host that places a panel before the stream exists (to keep its lower
/// edge off the floor, say) asks for it here, so that it uses the height that is sent.
/// @return 0 when the widget has no size yet or the options give no valid geometry.
float ar_panel_height_m(const QWidget & widget, const PanelStreamOptions & options);

/// @brief What a size rebuild did with the QarOS panel it replaced.
enum class ReplacedPanelFate
{
  Closed,       ///< closed together with its video sender
  HiddenKept,   ///< hidden in AR and kept; closed with the stream
  VisibleKept,  ///< could not be hidden, so it is still shown; kept and closed with the stream
  Swapped,      ///< left in place: only its video sender went, the new sender joined the same panel
};

/// @brief The record of one size rebuild; see QtPanelStream::take_size_rebuild.
struct SizeRebuildReport
{
  ReplacedPanelFate fate = ReplacedPanelFate::Closed;
  /// Why QarOS did not hide the old panel; set when @ref fate is VisibleKept.
  std::string hide_error;
};

/// @brief What QtPanelStream::end_session did with the QarOS panels of a stream.
struct SessionEndReport
{
  std::size_t panels = 0;          ///< QarOS panels the stream held (the current and kept ones)
  std::size_t left_in_place = 0;   ///< hidden where QarOS can, then detached, not closed
  std::size_t closed = 0;          ///< closed, because their sender cannot detach
  std::size_t hide_failures = 0;   ///< panels that could not be hidden before they were detached
  std::string hide_error;          ///< why the first of them could not
};

/// @brief One QWidget streamed as one QarOS GUI panel.
///
/// Owns the capture buffer, the QarOS-side adapter and (when input is enabled)
/// the input injector, and runs the whole per-tick cycle. Everything here is
/// Qt-GUI-thread-only (see qt_widget_capture.hpp); there is no lock and no
/// cross-thread handoff.
class QtPanelStream
{
public:
  /// @param widget  Borrowed; must outlive this stream.
  /// @param factory How to reach QarOS. The adapter is created lazily on the
  ///        first tick, and RE-created if the widget resizes, because
  ///        QarGuiPanelStreamIdExt allocates the receiver from the sender's
  ///        layout and that layout is fixed at creation.
  QtPanelStream(
    std::string key,
    QWidget * widget,
    PanelStreamOptions options,
    std::shared_ptr<qar_ar_core::IQarPanelSenderFactory> factory);

  ~QtPanelStream();

  QtPanelStream(const QtPanelStream &) = delete;
  QtPanelStream & operator=(const QtPanelStream &) = delete;

  /// @brief Run one capture/submit cycle.
  /// @param now_seconds Monotonic time, read once per tick by the manager.
  /// @return true if a frame was actually submitted to QarOS this tick.
  bool tick(double now_seconds);

  /// @brief Move the panel in the room. Size is derived, so only pose is taken.
  void set_pose(const QarPose & pose);

  /// @brief Move the panel in the room on the next @ref tick: the pose is taken now, the
  /// QarOS call (IQarPanelSenderAdapter::set_placement) is left to that tick, which makes it
  /// before anything else, so a pose that comes from a desktop click makes no QarOS call there.
  /// A sender made before that tick is built with the pose and needs no call.
  void set_pose_on_next_tick(const QarPose & pose);

  /// @brief Close the QarOS panel and DESTROY the video sender. Idempotent. Also
  /// lets go of any pointer the injector still holds pressed.
  ///
  /// The order of the QarOS half is the adapter's, and is the contract
  /// (IQarPanelSenderAdapter::close): input subscriptions, then
  /// `qar_gui_panels_close_panel` — which is SKIPPED here when the operator
  /// already closed this panel in AR (@ref capture_suspended), so a panel is
  /// never closed twice — then the sender.
  ///
  /// Who calls it:
  ///  * QtPanelStreamManager::close_panel in Destroy mode (the default), when the
  ///    source window closes — the current QarOS runtime survives destroying a
  ///    stream mid-session;
  ///  * QtPanelStreamManager::clear and this object's destructor, at session
  ///    shutdown.
  ///
  /// In Notice mode (`QAR_PANEL_CLOSE_MODE=notice`) a closed source window calls
  /// @ref tombstone instead, because on a runtime that does not survive it,
  /// destroying a sender mid-session leaves any attached receiver
  /// `reconnecting`, which raises 603 out of the QarOS mixer's frame callback
  /// and starves EVERY other stream it serves, the stereo 3D scene included
  /// (see panel_tombstone.hpp). In Park mode (`QAR_PANEL_CLOSE_MODE=park`) a closed source
  /// window calls @ref park instead, which closes nothing; the end of the session hides and
  /// detaches it (@ref end_session).
  ///
  /// An AR-side panel close calls @ref suspend_capture, never this.
  ///
  /// The panels that size rebuilds replaced and kept (@ref set_replaced_panel_policy) are closed
  /// here too, once each, after the current one; the report is the current adapter's.
  ///
  /// @return what the adapter did (an empty report when there was no adapter, or
  ///         it was already closed).
  qar_ar_core::PanelCloseReport close();

  /// @brief The source window is gone: keep the stream, show a notice instead.
  ///
  /// Swaps the captured widget for an owned placeholder (panel_tombstone.hpp)
  /// saying which window closed and that the operator may now close the panel
  /// in AR. The adapter — the video sender and the QarOS panel behind it — is
  /// **kept**, which is the entire point; the stream goes on capturing and
  /// submitting, it just has nothing live to show.
  ///
  /// Also drops the input injector: a notice has nothing to click, and the
  /// widget it injected into may already be destroyed. Geometry is frozen
  /// at the size the live sender was built for, so a tombstone can never
  /// provoke a sender rebuild.
  ///
  /// Idempotent — a second call keeps the first notice.
  /// @param text Which window closed and why. An empty name yields the
  ///        anonymous headline; see PanelTombstoneText.
  void tombstone(const PanelTombstoneText & text);

  /// @brief The operator closed this panel in AR: stop capturing, keep the
  /// sender.
  ///
  /// Observed through `qar_gui_panels_subscribe_updates` (panel state reaching
  /// closed). There is nothing left to show the frames to, so capture stops —
  /// but the sender is NOT destroyed here: the AR side closing a panel is not the
  /// Qt window closing, and a receiver may still be attached to the stream. It is
  /// released by the Qt-side close or at session shutdown, and whichever does so
  /// then skips `qar_gui_panels_close_panel` (see @ref close). Idempotent.
  void suspend_capture();

  /// @brief Re-adopt a tombstoned slot for a new live widget.
  ///
  /// The bound on tombstone growth: an operator who closes and re-opens the
  /// same panel (or re-opens the same dialog) re-uses the sender that is
  /// already there instead of stranding one tombstone per cycle. The adapter is
  /// kept when the new widget's geometry still matches the layout it was built
  /// for; a genuinely different size goes through the usual rebuild, the same
  /// as a resize.
  ///
  /// @return false if this stream is not tombstoned, or @p widget is null.
  bool revive(QWidget * widget);

  /// @brief Take this stream out of use WITHOUT closing anything. This is what a Qt-side
  /// close does in PanelCloseMode::Park.
  ///
  /// Releases whatever the input injector holds and drops it, asks for the panel to be hidden
  /// in AR (@ref request_ar_visible, carried out by the next @ref tick) and marks the stream
  /// @ref parked. The QarOS panel, the video sender and the subscriptions stay; capture stops
  /// and input is dropped; no notice is built. The window is remembered only while it exists
  /// (a guarded pointer), so a window destroyed later leaves the stream parked without one.
  /// A panel the operator already closed in AR is parked without any QarOS call.
  ///
  /// If QarOS refuses the hide, the stream stays parked but visible and
  /// @ref park_hide_refused turns true: the manager then falls back to the tombstone, which
  /// needs @p notice_reason.
  ///
  /// Idempotent; a tombstoned stream is left as it is.
  /// @param notice_reason What the closed-window notice says if the hide is refused.
  void park(std::string notice_reason);

  /// @brief True from @ref park until a window is shown on the stream again.
  bool parked() const { return diagnostics_.parked; }

  /// @brief True when the hide a @ref park asked for was refused by QarOS (or the sender
  /// cannot hide a panel at all), so the panel is still shown.
  bool park_hide_refused() const;

  /// @brief What the closed-window notice says if the manager falls back to the tombstone.
  const std::string & park_notice_reason() const { return park_notice_reason_; }

  /// @brief The refusal @ref park_hide_refused reports has been dealt with (the notice was
  /// built, or could not be): it is not reported again.
  void acknowledge_park_failure() { park_failures_seen_ = diagnostics_.visibility_failures; }

  /// @brief Show a window on a parked stream again: the same QarOS panel and sender, no new
  /// ones.
  ///
  /// @param widget The window to show. Null re-uses the window the stream was parked with,
  ///        if it still exists. The input injector is made again for it, the capture is aimed
  ///        at it, and the panel is asked to be shown (@ref request_ar_visible); the next
  ///        @ref tick carries that out and sends a fresh frame. A window of another size goes
  ///        through the usual resize hysteresis.
  /// @return false if the stream is not parked, or no window is given and the parked one is
  ///         gone.
  bool revive_parked(QWidget * widget);

  /// @brief True once, after the tick that carried out the show a @ref revive_parked asked
  /// for, when QarOS refused it (or the sender cannot show a panel at all): the panel is still
  /// hidden. False while that tick is still to come, when the show worked, and for a stream
  /// that was not revived. The verdict is given once per revival.
  bool take_revival_refusal();

  /// @brief True while this stream is showing the closed-source notice.
  bool tombstoned() const { return diagnostics_.tombstoned; }

  /// @brief True once the AR side closed the panel and capture stopped.
  bool capture_suspended() const { return diagnostics_.ar_panel_closed; }

  /// @brief Hide the panel in AR (@p visible false) or show it again, without closing it.
  ///
  /// Only records the request, the last one before a tick winning; the next @ref tick carries
  /// it out first, so the QarOS call (IQarPanelSenderAdapter::set_state) happens on the host's
  /// render tick like every other. A stream that has no adapter yet carries it out right after
  /// creating one. While hidden the stream captures nothing and submits nothing, drops its
  /// input with the reason "hidden in AR", and keeps the QarOS panel, the video sender and the
  /// subscriptions. A show forces one capture and submit, so the receiver gets a fresh frame,
  /// and also tries to bring back a panel the operator closed in AR: when QarOS accepts it,
  /// capture resumes.
  ///
  /// A request the adapter refuses, or cannot carry out at all, keeps the old state; it is
  /// counted in PanelStreamDiagnostics::visibility_failures with the reason in
  /// last_visibility_error, and the host falls back to closing the panel.
  void request_ar_visible(bool visible);

  /// @brief True while the panel is hidden in AR: a hide QarOS accepted and no show since.
  bool ar_hidden() const { return diagnostics_.ar_hidden; }

  /// @brief True while this stream still holds a live QarOS sender — including
  /// while tombstoned or suspended, which is the whole point, or one that a size rebuild
  /// replaced and kept (see @ref set_replaced_panel_policy).
  bool has_adapter() const { return adapter_ != nullptr || !replaced_adapters_.empty(); }

  /// @brief Lets a size rebuild keep the QarOS panel it replaces instead of closing it.
  ///
  /// A widget that changes size needs a new sender and a new QarOS panel, because the sender's
  /// layout is fixed at creation. By default the old panel is closed (with its sender) before
  /// the new one is made. When @p may_keep answers true, the old panel is hidden in AR
  /// (`set_state(HIDDEN)`) and kept together with its sender and its subscriptions, and the new
  /// sender and panel are made; @ref close closes every kept panel, once. A hide that QarOS
  /// refuses, or a sender that cannot hide a panel, leaves the old panel visible and it is kept
  /// all the same: a rebuild that keeps never closes anything. The function is asked once per
  /// rebuild, on the tick that makes it. An empty function, the default, closes.
  ///
  /// A hide that QarOS refused is asked for again on later ticks, @ref kKeptHideRetries more
  /// times at most and @ref kKeptHideRetrySeconds apart (a sender that cannot hide a panel at
  /// all is not asked again); a late success is said once (@ref take_notes), and so is the end
  /// of the attempts (@ref take_warnings). The residual risk is that of a kept panel that stays
  /// visible: the input source keeps one subscription for each key, so the subscription that
  /// reports a close in AR goes with the panel when the new one is made. The operator closing
  /// that panel in AR is not seen, and @ref close closes it a second time.
  void set_replaced_panel_policy(std::function<bool()> may_keep);

  /// @brief Lets a size change swap the video sender on the QarOS panel instead of replacing the
  /// panel.
  ///
  /// With @ref PanelStreamOptions::resize_mode Swap and a sender whose
  /// IQarPanelSenderAdapter::supports_detach is true, a size change first detaches the old sender
  /// (its video sender and its click/hover subscription go; the QarOS panel stays), then makes the
  /// new sender with the same common name, which the factory attaches to that same QarOS panel.
  /// The old sender is never kept, so a key never has two live senders on one panel. The function
  /// is asked once per rebuild and answers whether the swap is allowed; an empty function, the
  /// default, allows it. A sender that cannot detach takes the path of
  /// @ref set_replaced_panel_policy.
  void set_swap_policy(std::function<bool()> swap_allowed);

  /// @brief End of the session: leave the QarOS panels where they are for the next session.
  ///
  /// Releases the injector, then for the current sender and every kept one: a sender that
  /// supports detach has its panel hidden (`set_state(HIDDEN)`, unless it is hidden already or
  /// the operator closed it) and is detached; a sender that does not is closed, as @ref close
  /// does. Nothing is closed that can be left. The outcomes the senders have queued are taken
  /// first (a hide that was accepted and refused since the last tick is asked again). Idempotent:
  /// the stream holds no sender afterwards.
  SessionEndReport end_session();

  /// How many more times a hide that QarOS refused is asked for, and the least time between two
  /// tries, in seconds on the clock @ref tick is given.
  static constexpr std::uint32_t kKeptHideRetries = 3;
  static constexpr double kKeptHideRetrySeconds = 1.0;

  /// The least time, in seconds on the clock @ref tick is given, between the failure of a sender's
  /// creation (reported through IQarPanelSenderAdapter::drain_results) and the next try.
  static constexpr double kCreateRetrySeconds = 1.0;

  /// @brief How many QarOS panels earlier size rebuilds replaced and kept.
  std::size_t replaced_panel_count() const { return replaced_adapters_.size(); }

  /// @brief True once for each size rebuild, from the tick that made the new sender on: what
  /// became of the panel it replaced. The manager logs it.
  bool take_size_rebuild(SizeRebuildReport & report);

  /// @name Region substitution (variant b)
  /// Forwarded to @ref QtWidgetCapture::set_region_source and friends — see
  /// that class's file comment for why this exists (a native window embedded in
  /// a widget is something `QWidget::render()` cannot paint).
  /// @{
  void set_region_source(QWidget * region, std::shared_ptr<LatestVideoFrame> source)
  {
    capture_.set_region_source(region, std::move(source));
  }
  void clear_region_source(QWidget * region) { capture_.clear_region_source(region); }
  void clear_region_sources() { capture_.clear_region_sources(); }
  std::uint32_t substituted_region_count() const { return capture_.substituted_region_count(); }
  /// @}

  /// @name Popups over this panel at the last capture
  /// Forwarded from @ref QtWidgetCapture; the manager logs their 0 <-> N edges.
  /// @{
  std::uint32_t owned_popup_count() const { return capture_.owned_popup_count(); }
  std::uint32_t offscreen_popup_count() const { return capture_.offscreen_popup_count(); }
  const std::string & popup_summary() const { return capture_.popup_summary(); }
  /// @}

  /// @brief Turn one semantic pointer action into Qt events in the widget.
  ///
  /// The events are SENT, not posted, so the widget's own handler runs inside
  /// this call — and a handler is allowed to do anything, including ask for the
  /// very panel it is being clicked through to be closed (the Stop button, pressed
  /// in AR). @ref delivering is how the manager knows not to destroy this stream
  /// out from under that handler.
  /// @return false if the panel takes no input or the injector dropped it;
  ///         the input counters in @ref diagnostics say why.
  bool deliver_input(const PointerAction & action);

  /// @brief True while @ref deliver_input is on the stack for this stream (the
  /// count, not a flag: a handler can open a nested event loop that delivers
  /// again). A stream that is delivering must not be destroyed.
  bool delivering() const { return deliveries_in_flight_ > 0; }

  /// @brief Record that the input source produced one pointer sample for this
  /// panel. Counted BEFORE the state machine, the hit test or the injector can
  /// discard it, so the number answers "did anything arrive at all" on its own.
  void note_input_sample() { ++diagnostics_.input_samples_received; }

  /// @brief Latch "the unserved condition has been reported" so the manager
  /// says it once per run rather than once per tick. Cleared by the next
  /// successful submit, and by a sender rebuild.
  void mark_unserved_reported() { diagnostics_.unserved_reported = true; }

  /// @name Burst after an unserved streak
  ///
  /// A receiver that attaches to a stream after a long silence can drop the first frame it
  /// sees, and an unchanged panel would then wait a whole keepalive interval for the next one.
  /// When a streak of at least @ref kBurstStreakTicks ticks on which QarOS offered no
  /// destination ends, the frame that ends it and the next @ref kBurstFrames - 1 serviceable
  /// ones are sent even if the widget has not changed.
  /// @{
  static constexpr std::uint64_t kBurstStreakTicks = 10;
  static constexpr std::uint32_t kBurstFrames = 3;
  /// @}

  /// @brief Take the log lines this stream wants said: the mean luminance of the first frame
  /// it ever submitted, and of the first one after each unserved streak that earned a burst.
  /// Each line names the stream: `'<key>' frame luminance <0-255> (first frame)` or
  /// `'<key>' frame luminance <0-255> (first frame after <n> unserved ticks)`. The manager
  /// passes them to its diagnostics sink after every tick. The late success of a hide that QarOS
  /// refused when a size change replaced a panel is a note too (@ref set_replaced_panel_policy),
  /// and so are the scroll-area census lines (@ref request_scroll_census).
  std::vector<std::string> take_notes();
  bool has_notes() const { return !notes_.empty(); }

  /// @brief Take the lines this stream wants said as Warnings: the end of the attempts to hide a
  /// kept panel that stayed visible (@ref set_replaced_panel_policy), and a vertical scroll bar
  /// that is still there 1.5 s after the first frame (@ref kCensusFollowUpSeconds). The manager
  /// passes them to its diagnostics sink after every tick.
  std::vector<std::string> take_warnings();
  bool has_warnings() const { return !warnings_.empty(); }

  /// @name Scroll-area census
  ///
  /// The widget's scroll areas are measured (scroll_census.hpp) at the first frame the stream
  /// submits and again @ref kCensusFollowUpSeconds later on the clock @ref tick is given, and the
  /// same pair is made again for the first frame after the stream is revived (@ref revive,
  /// @ref revive_parked) or gets a new sender because the widget changed size. Each census is one
  /// note: `'<key>' scroll census (<when>): <n> area(s), <h> hidden: <areas>`, where `<when>` is
  /// `first frame`, `+1.5 s` or the reason of a request, `<n>` the visible areas, `<h>` the
  /// hidden ones and `<areas>` what describe_scroll_census says. The second census also says
  /// what is still wrong: a Warning for every area that is not by design and shows a vertical
  /// bar (`'<key>' still shows a vertical scroll bar 1.5 s after its first frame: <Class>
  /// '<objectName>' overflows by <o> px (<p> % of its <h> px viewport)`), a note for every one
  /// that is (`'<key>' 1.5 s after its first frame an area that scrolls by design shows a
  /// vertical scroll bar: ...`). Nothing runs on a timer of its own.
  ///
  /// The second census describes the panel as the operator sees it: while the panel is hidden in
  /// AR or was closed there it stays armed, and it is taken at the first tick after the panel is
  /// on show again, so no Warning names a panel nobody can see. A parked or retired stream drops
  /// it.
  /// @{

  /// Seconds after the first frame at which the second census of a pair is taken.
  static constexpr double kCensusFollowUpSeconds = 1.5;

  /// @brief Take one more census, labelled @p reason, at the NEXT frame this stream submits (a
  /// request made again before that frame replaces the earlier reason). A host asks for it when
  /// the widget has just changed what it shows, say a tab of it was selected.
  void request_scroll_census(std::string reason);
  /// @}

  const std::string & key() const { return key_; }
  /// The window being streamed, or null once it was destroyed (a parked stream outlives it).
  QWidget * widget() const { return widget_.data(); }
  /// @brief The policy this stream was added with. A dialog panel inherits its
  /// owner's readability, pacing and input options from here, so a dialog reads
  /// at the same scale as the panel that opened it.
  const PanelStreamOptions & options() const { return options_; }
  /// @brief The placement that is sent to QarOS: the pose, and the size of the AR panel (its
  /// height holds the vertical part of the frame and the fit margin, a dialog's width is fitted
  /// to them; see PanelStreamOptions::ar_frame).
  const qar_ar_core::PanelPlacement & placement() const { return placement_; }
  /// @brief The placement that pointer hit tests (panel_hit_test.hpp) work on: the rectangle of
  /// the widget in the panel. QarOS draws the video (the whole buffer) at the width the frame
  /// leaves, starting at the left inset, and centres it vertically between the top and the
  /// bottom inset, so the rectangle is the part of the video that the widget covers, a buffer
  /// pixel being `video width / buffer width` wide: its width is that times the content width,
  /// its height that times the content height, it starts the left inset right of the left edge
  /// of the panel and its top edge is the top inset plus half of what the video leaves of the
  /// area under it (the fit margin for a panel that has the size its own frame asks for) below
  /// the top edge of the panel (the pose is moved along the panel's own axes). The same point of
  /// the video lands on the same widget pixel whatever the frame is, and a point in the frame or
  /// in a band above or below the video lands on no pixel. Empty (no size) until the first
  /// geometry has been adopted.
  qar_ar_core::PanelPlacement hit_placement() const;
  const qar_ar_core::PanelStreamDiagnostics & diagnostics() const { return diagnostics_; }

  /// @brief Start a new window for the submit-gap statistics
  /// (PanelStreamDiagnostics::submit_gap_*); the last submit time is kept, so
  /// the first gap of the new window still spans the boundary.
  void reset_submit_gap_window()
  {
    diagnostics_.submit_gap_max_ms = 0.0;
    diagnostics_.submit_gap_sum_ms = 0.0;
    diagnostics_.submit_gap_count = 0;
  }

  /// @brief Null when the panel was added with input disabled.
  QtPanelInputInjector * input_injector() const { return injector_.get(); }

  /// @brief Widget size in Qt LOGICAL pixels as of the last tick — what
  /// panel_uv_to_widget_px needs to turn a hit into a widget coordinate.
  std::uint32_t logical_width() const { return logical_width_; }
  std::uint32_t logical_height() const { return logical_height_; }

private:
  /// Recompute geometry from the widget's current size/DPI. Returns false (and
  /// records the reason) when the widget is not laid out yet.
  /// @param now_seconds Same instant @ref tick was given; the resize hysteresis
  ///        is timed against it, so a fake clock makes it exactly reproducible.
  bool refresh_geometry(double now_seconds);

  /// @brief The hysteresis decision: may @p desired replace the geometry the
  /// live adapter was built for, right now?
  ///
  /// Pure bookkeeping over @ref PanelStreamOptions::resize_threshold_px /
  /// `resize_settle_seconds` / `resize_settle_ticks`; see those for the rule.
  /// Returns true for the first geometry of all (there is nothing to protect
  /// yet) and for an unchanged size.
  bool adopt_geometry(const PanelGeometry & desired, double now_seconds);

  /// Forget the size currently being timed. Also clears the diagnostic.
  void clear_pending_resize();

  /// Create the QarOS adapter for the current geometry, replacing any existing
  /// one. Returns false and records the factory's reason on failure.
  /// @param now_seconds Same instant @ref tick was given.
  bool ensure_adapter(double now_seconds);

  /// Takes the sender in @c adapter_ out of use because the size changed: closes it, or hides
  /// and keeps it when the replaced-panel policy says so. Leaves @c adapter_ empty.
  /// @param now_seconds Same instant @ref tick was given; the retries of a refused hide are
  ///        timed against it.
  void retire_adapter_for_rebuild(double now_seconds);

  /// The common name of the QarOS panel for a sender of the given buffer size: the scope and the
  /// key, plus "@<w>x<h>" in PanelResizeMode::NewPanel.
  std::string common_name_for(std::uint32_t buffer_width, std::uint32_t buffer_height) const;

  /// Takes the kept panel that has @p desc's common name and buffer size out of the kept list and
  /// makes it the current one (shown again when it was hidden). A common name is one QarOS panel,
  /// so a second sender for it is never made while a kept one holds it. Only in
  /// PanelResizeMode::NewPanel; false when no kept panel matches.
  bool revive_kept_panel(const qar_ar_core::PanelDesc & desc);

  /// Asks again for the hide of every kept panel whose hide QarOS refused, when its time has
  /// come; says a late success, and the end of the attempts.
  void retry_kept_hides(double now_seconds);

  /// Takes the outcomes of the calls the senders only queued
  /// (IQarPanelSenderAdapter::drain_results) and applies them: a creation that failed ends the
  /// sender (it is made again after @ref kCreateRetrySeconds), a creation that succeeded gives the
  /// stream its id, a refused placement is recorded, and a refused hide or show undoes what was
  /// assumed when the call was accepted. Called first on every tick, so that nothing below acts on
  /// a stale assumption.
  /// @param now_seconds Same instant @ref tick was given.
  void drain_adapter_results(double now_seconds);

  /// One hide or show a sender accepted and whose outcome is still to come.
  struct StateAsk
  {
    bool changes_flag = false;     ///< it changed diagnostics_.ar_hidden, to be undone on refusal
    bool previous_hidden = false;  ///< what diagnostics_.ar_hidden was before
    bool revival = false;          ///< it is the show of a revived parked panel
  };
  void note_state_ask(const StateAsk & ask);

  void record_error(std::string message);
  void sync_input_diagnostics();

  /// Carry out the request @ref request_ar_visible recorded, if there is an adapter to carry
  /// it out on; otherwise the request stays pending. See PanelStreamDiagnostics::ar_hidden.
  void apply_visibility_request();

  /// Carry out the pose @ref set_pose_on_next_tick recorded, if there is an adapter to carry
  /// it out on; otherwise it stays pending (the sender made next is built with it).
  void apply_pending_pose();

  /// Count one refused or impossible hide/show and keep why.
  void record_visibility_failure(std::string message);

  /// Bookkeeping for a frame QarOS has just accepted: burst and luminance lines, the brightness
  /// of the frame and the scroll-area census.
  /// @param unserved_ticks The not-ready streak this frame ended.
  /// @param now_seconds Same instant @ref tick was given.
  void note_submitted_frame(std::uint64_t unserved_ticks, double now_seconds);

  /// Measure the brightness of the frame that was just submitted, at most once a second, and
  /// put it into the diagnostics (PanelStreamDiagnostics::luminance_all and the fields after it).
  void note_brightness(double now_seconds);

  /// Take the censuses a submitted frame owes: the first of a pair, and a requested one.
  void note_scroll_census(double now_seconds);

  /// Measure the scroll areas of the widget and say it, as one note; @p follow_up adds the
  /// Warning and note for every vertical bar that is still shown.
  void take_census(const std::string & when, bool follow_up);

  /// Queue a line for take_notes / take_warnings; a stream nobody drains does not grow.
  void say_note(std::string line);
  void say_warning(std::string line);

  /// Sum of the sequences of every LatestVideoFrame this panel shows (a
  /// VideoPanelWidget's own source plus the live region sources); @p driven says
  /// whether there is at least one. A change means a frame has arrived.
  std::uint64_t frame_sequence(bool & driven) const;

  /// The trace of the newest frame among those sources (largest `mailbox_us`); `valid` is
  /// false when per-frame timing was off while the frame was decoded. Per-frame timing only.
  VideoFrameTrace newest_frame_trace() const;

  /// The sink this stream writes its per-capture rows to (`video_<key>.csv` in
  /// QAR_VIDEO_TIMING_DIR), or null when timing is off. Probed once, on the first
  /// frame-driven capture; see timing_log.hpp for why a row costs next to nothing.
  qar_ar_core::TimingSink * timing_sink();

  std::string key_;
  /// Guarded: a parked stream outlives its window.
  QPointer<QWidget> widget_;
  PanelStreamOptions options_;
  std::shared_ptr<qar_ar_core::IQarPanelSenderFactory> factory_;
  std::shared_ptr<qar_ar_core::IQarPanelSenderAdapter> adapter_;
  /// A sender (and QarOS panel) that a size rebuild replaced and kept. Idle: nothing is captured
  /// for it, and @ref close closes it once.
  struct KeptPanel
  {
    std::shared_ptr<qar_ar_core::IQarPanelSenderAdapter> adapter;
    bool hidden = false;                ///< QarOS has accepted the hide
    std::uint32_t retries_left = 0;     ///< times a refused hide is still to be asked for again
    std::uint32_t attempts = 0;         ///< times the hide has been asked for, the first included
    double next_retry_seconds = 0.0;    ///< not before this instant
    std::string common_name;            ///< the common name its QarOS panel has
    std::uint32_t buffer_width = 0;     ///< the buffer size its sender was made for
    std::uint32_t buffer_height = 0;
  };
  /// The panels that size rebuilds replaced and kept, oldest first.
  std::vector<KeptPanel> replaced_adapters_;
  /// The hides and shows of adapter_ that were accepted and whose outcome has not been drained,
  /// oldest first. A sender that reports through return values never drains, so the list is capped.
  std::deque<StateAsk> state_asks_;
  /// A show of a revived parked panel was refused after it had been accepted;
  /// take_revival_refusal says so.
  bool late_revival_refusal_ = false;
  /// No new sender before this instant after a creation failed.
  bool has_create_retry_ = false;
  double create_retry_at_seconds_ = 0.0;
  std::function<bool()> replaced_panel_policy_;
  std::function<bool()> swap_policy_;
  /// The common name of the QarOS panel behind adapter_.
  std::string adapter_common_name_;
  /// A size rebuild nobody has taken the record of yet, and the record.
  bool size_rebuild_pending_ = false;
  SizeRebuildReport size_rebuild_;
  QtWidgetCapture capture_;
  std::unique_ptr<QtPanelInputInjector> injector_;

  /// The notice shown once the source window is gone. OWNED — unlike
  /// @c widget_, which is always borrowed. Non-null exactly while tombstoned,
  /// and @c widget_ points at it then.
  std::unique_ptr<QWidget> tombstone_widget_;

  qar_ar_core::PanelPlacement placement_{};
  PanelGeometry geometry_{};
  std::uint32_t logical_width_ = 0;
  std::uint32_t logical_height_ = 0;

  /// Buffer size the CURRENT adapter was created with. A widget resize changes
  /// geometry_ but not this, and the difference is what triggers a re-create.
  std::uint32_t adapter_buffer_width_ = 0;
  std::uint32_t adapter_buffer_height_ = 0;

  /// The buffer size the widget asked for on the PREVIOUS tick, so the ignored
  /// and pending counters count size CHANGES rather than ticks — a widget
  /// parked a few pixels off the committed size would otherwise add one
  /// "ignored resize" per tick for the life of the session.
  std::uint32_t last_desired_buffer_width_ = 0;
  std::uint32_t last_desired_buffer_height_ = 0;

  /// The size the hysteresis is currently timing, and since when. Only
  /// meaningful while @c resize_pending_.
  bool resize_pending_ = false;
  std::uint32_t pending_buffer_width_ = 0;
  std::uint32_t pending_buffer_height_ = 0;
  double pending_since_seconds_ = 0.0;
  std::uint32_t pending_ticks_ = 0;

  /// When the current sender's "has never submitted a frame" run began, on the
  /// clock tick() is given. Only meaningful while @c has_unserved_since_.
  double unserved_since_seconds_ = 0.0;
  bool has_unserved_since_ = false;

  /// When the capture gate last let a capture through (despite the name: pacing
  /// sheds ticks BEFORE this is updated).
  double last_tick_seconds_ = 0.0;
  bool has_ticked_ = false;
  double last_submit_seconds_ = 0.0;
  bool has_submitted_ = false;

  /// Capture-gate state (capture_gate.hpp). The estimator sees every tick; the
  /// sequences are sums over this panel's video sources.
  TickIntervalEstimator tick_interval_;
  bool has_frame_observation_ = false;
  std::uint64_t frame_sequence_observed_ = 0;   ///< as of the previous tick
  double frame_change_seconds_ = 0.0;           ///< when it last changed, at tick resolution
  std::uint64_t frame_sequence_captured_ = 0;   ///< as of the last capture let through

  qar_ar_core::PanelStreamDiagnostics diagnostics_{};
  double capture_ms_total_ = 0.0;

  /// Pointer deliveries currently on the stack; see @ref delivering.
  int deliveries_in_flight_ = 0;

  /// A hide or show asked for and not yet carried out, and which one.
  bool visibility_requested_ = false;
  bool requested_visible_ = true;
  /// What the notice says if the hide a park asked for is refused, and how many refusals the
  /// stream had counted when it was parked.
  std::string park_notice_reason_;
  std::uint64_t park_failures_seen_ = 0;
  /// A revival whose show has not been carried out yet, and how many refusals the stream had
  /// counted when it was revived; see take_revival_refusal.
  bool revival_pending_ = false;
  std::uint64_t revival_failures_seen_ = 0;
  /// A pose recorded by set_pose_on_next_tick that no tick has carried out yet.
  bool pose_pending_ = false;
  /// Frames still owed to the receiver after an unserved streak; see kBurstStreakTicks.
  std::uint32_t burst_remaining_ = 0;
  /// The luminance of the first submitted frame has been put into notes_.
  bool first_frame_noted_ = false;
  /// When the brightness of a submitted frame was last measured, on the clock tick() is given.
  bool brightness_measured_ = false;
  double brightness_seconds_ = 0.0;
  /// The next submitted frame opens a census pair; the second census of the pair is armed and
  /// due at census_follow_up_seconds_.
  bool census_pair_pending_ = true;
  bool census_follow_up_armed_ = false;
  double census_follow_up_seconds_ = 0.0;
  /// A census asked for by request_scroll_census, and its label.
  bool census_requested_ = false;
  std::string census_request_reason_;
  std::vector<std::string> notes_;
  /// Lines to be said as Warnings; see take_warnings.
  std::vector<std::string> warnings_;
  /// Input dropped because the panel was hidden. Kept apart from the injector's own count,
  /// which sync_input_diagnostics() copies over the diagnostics, so neither loses the other.
  std::uint64_t hidden_input_drops_ = 0;
  std::uint64_t injector_drops_seen_ = 0;

  /// @name Per-frame stage timing (QAR_VIDEO_TIMING_DIR)
  /// @{
  std::shared_ptr<qar_ar_core::TimingSink> timing_sink_;
  bool timing_probed_ = false;
  /// The run of consecutive "QarOS had no destination ready" capture ticks in progress:
  /// when its first tick happened (epoch us, 0 = no run) and how many ticks so far. Closed
  /// by the next tick whose try_begin_frame says ready.
  std::int64_t not_ready_run_start_us_ = 0;
  std::int64_t not_ready_run_count_ = 0;
  /// @}
};

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__QT_PANEL_STREAM_HPP_
