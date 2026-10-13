// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QT_PANEL_STREAM_HPP_
#define QAR_QT_STREAMING__QT_PANEL_STREAM_HPP_

#include "qar_qt_streaming/ar_readability.hpp"
#include "qar_qt_streaming/capture_gate.hpp"
#include "qar_qt_streaming/panel_geometry.hpp"
#include "qar_qt_streaming/panel_tombstone.hpp"
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

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

class QWidget;

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
  /// (see panel_tombstone.hpp).
  ///
  /// An AR-side panel close calls @ref suspend_capture, never this.
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

  /// @brief True while this stream is showing the closed-source notice.
  bool tombstoned() const { return diagnostics_.tombstoned; }

  /// @brief True once the AR side closed the panel and capture stopped.
  bool capture_suspended() const { return diagnostics_.ar_panel_closed; }

  /// @brief True while this stream still holds a live QarOS sender — including
  /// while tombstoned or suspended, which is the whole point.
  bool has_adapter() const { return adapter_ != nullptr; }

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

  const std::string & key() const { return key_; }
  QWidget * widget() const { return widget_; }
  /// @brief The policy this stream was added with. A dialog panel inherits its
  /// owner's readability, pacing and input options from here, so a dialog reads
  /// at the same scale as the panel that opened it.
  const PanelStreamOptions & options() const { return options_; }
  const qar_ar_core::PanelPlacement & placement() const { return placement_; }
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
  bool ensure_adapter();

  void record_error(std::string message);
  void sync_input_diagnostics();

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
  QWidget * widget_ = nullptr;
  PanelStreamOptions options_;
  std::shared_ptr<qar_ar_core::IQarPanelSenderFactory> factory_;
  std::shared_ptr<qar_ar_core::IQarPanelSenderAdapter> adapter_;
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
