// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QT_WIDGET_CAPTURE_HPP_
#define QAR_QT_STREAMING__QT_WIDGET_CAPTURE_HPP_

/// @file qt_widget_capture.hpp
/// @brief Renders a live QWidget — plus any popup it owns — into a reusable CPU
/// pixel buffer.
///
/// ## Thread affinity — the whole reason this path is simple
///
/// `QWidget::render()` runs the widget's paint path, which Qt permits ONLY on
/// the GUI thread. Since `qar_video_sender_try_begin_frame` is documented
/// non-blocking, the entire capture+submit chain fits inside the existing QTimer
/// tick on that same thread: no executor, no render lock, no cross-thread
/// handoff. That is a deliberate asymmetry with qar_ogre_streaming, whose
/// stereo path needs the render thread and a lock (see qar_ogre_adapter
/// docs/ARCHITECTURE.md, "Threading model").
///
/// ## Why CPU and not a GPU/FBO route
///
/// `qar_video_sender_frame_opengl` exists and is exported by the shipped DLL, so
/// a GPU path is possible — but it would need an arbitrary QWidget tree
/// re-rendered through a QOpenGLPaintDevice against a GL context the host's own
/// renderer is already using on the same thread. A control panel is a few
/// hundred kB of mostly-static widgets; a memcpy of that is microseconds, and
/// the dirty check below means most ticks cost nothing at all. See
/// docs/ARCHITECTURE.md#capture-pipeline.
///
/// ## Popups — why a second render pass exists
///
/// A combo box list, a menu and a tooltip are **separate top-level windows**.
/// `QWidget::render()` paints the widget it is given and its children, so a
/// popup never enters the captured image by itself: in AR the operator would
/// open a combo and nothing would happen, while the list appeared on the desktop
/// behind the headset. Input routing to popups works regardless
/// (`QtPanelInputInjector`), so this is purely a capture gap, and it is closed
/// by @ref collect_panel_popups plus a per-tick composite. See
/// docs/ARCHITECTURE.md#popups-and-dialogs.
///
/// ## Dialogs — NOT composited, promoted to a panel of their own
///
/// A dialog (`Qt::Dialog`, often run through `QDialog::exec()`) is a top-level
/// window too, so it is invisible in the capture for the same reason a combo
/// list is. Treating it as a big popup — composite it, and DOCK the real window
/// onto the panel rectangle so picture and hit test agree — does not work: a
/// dock is tall and narrow, the dialog it opens (an "Add" box, say) is not, so the
/// dialog would arrive squeezed into somebody else's rectangle with only its top
/// half visible.
///
/// So dialogs are not composited at all. A dialog attributed to a streamed
/// panel becomes its OWN AR GUI panel, sized from the dialog, placed beside its
/// owner and retired when it hides — see @ref QtPanelStreamManager and
/// docs/ARCHITECTURE.md#popups-and-dialogs. This capture renders its widget and
/// composites POPUPS (a combo list, a menu, a tooltip), which belong where they
/// drop and are small by construction.
///
/// What this header keeps for dialogs is @ref PanelOverlayAttribution: deciding
/// WHICH panel owns a dialog is needed to choose the new panel's placement, to
/// block input to the owner while its modal dialog is up, and to tie the dialog
/// panel's lifetime to its owner's.
///
/// ## Region substitution — the OTHER thing `QWidget::render()` cannot paint
///
/// A popup or a dialog is at least a `QWidget` Qt can be asked to render
/// somewhere; a widget embedded with `QWidget::createWindowContainer()` is
/// not. That call wraps a **native platform window** so it can sit inside a
/// Qt layout — a host such as an RViz plugin does exactly this to embed the
/// window of its 3D renderer, and so does any camera or image display built the
/// same way. `QWidget::render()`
/// walks the Qt widget tree and paints each child's own `paintEvent`; a
/// container widget has no paint event of its own that shows what the native
/// window displays, so that rectangle always comes out solid — white in
/// practice.
///
/// There is no way to make Qt render that content, so the remedy is
/// substitution rather than capture: @ref set_region_source registers a
/// `LatestVideoFrame` (video_frame.hpp / latest_video_frame.hpp) as the thing
/// to paint over a given region instead. The region can be any live
/// descendant of the captured widget — typically the native-window container
/// itself — or the captured widget as a whole. See @ref capture for exactly where this
/// runs in the per-tick pipeline and why the order matters.

#include "qar_qt_streaming/latest_video_frame.hpp"
#include "qar_ar_core/panel_types.hpp"

#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QSize>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class QWidget;

namespace qar_qt_streaming
{

/// @brief Does @p popup logically belong to the widget tree rooted at @p root?
///
/// Walks `parentWidget()` upwards from @p popup looking for @p root. That walk,
/// and not `QWidget::isAncestorOf`, is the test: `isAncestorOf` additionally
/// requires both widgets to be *in the same window*, and a popup is by
/// definition its own window — so it answers false for exactly the case this
/// function exists to answer.
///
/// Ownership matters because two panels can stream at once, and the host has
/// menus of its own. A popup whose parent chain never reaches @p root is
/// somebody else's and must not bleed into this panel's image. A popup created
/// with no parent at all (`new QMenu()` rather than `new QMenu(this)`) cannot be
/// attributed to anyone and is therefore treated as NOT ours — the conservative
/// direction, since the cost is an invisible menu rather than a panel showing a
/// stranger's popup.
bool panel_tree_owns_popup(const QWidget * root, const QWidget * popup);

/// @brief Global top-left of @p popup the way Qt placed it - for the composite and the AR
/// injector. A popup-class window (Qt::Popup / Qt::ToolTip) carrying WA_DontShowOnScreen
/// (one the off-screen popup guard keeps off the desktop) answers its widget geometry: Qt
/// never moves such a widget's native window after creating it (setGeometry_sys skips it),
/// so once that QWindow has had a geometry event QWidget::mapToGlobal() would read the place
/// the list was FIRST opened at. Everything else answers QWidget::mapToGlobal(QPoint(0, 0)),
/// as Qt does. Never apply this to the panel itself: Qt places popups through the
/// panel's QWindow-based mapping, which on Windows differs from its widget geometry by the
/// frame offset.
QPoint popup_global_origin(const QWidget * popup);

/// @brief Decides which streamed panel owns a dialog whose parent chain reaches
/// **no** panel at all — the case the parent walk above cannot answer.
///
/// ### Why a second attribution path has to exist
///
/// @ref panel_tree_owns_popup works for popups because Qt parents a popup to the
/// widget that opened it: a combo list's parent really is the combo. Dialogs are
/// not built that way. An application often creates a dialog with the MAIN WINDOW as parent
/// (`new SomeDialog(..., this)` in the main window's own code), so when the operator
/// streams a dock and taps a button on it from AR that opens such a dialog, the chain
/// runs dialog -> main window and never passes through the dock. The parent walk answers
/// "not yours", correctly by its own rule, and the dialog is skipped: it opens on the
/// desktop and never reaches the headset.
///
/// ### Why "same top-level window" is NOT the fix
///
/// Every dock in that main window is equally a relative of the dialog, so a
/// window-level test would make it owned by **every** streamed panel at once.
/// Each would give it a panel of its own — the same dialog drawn in several
/// places, every copy competing for the operator's input. That is worse than
/// not drawing it at all.
///
/// ### The rule: causation, decided once
///
/// A dialog appears because something was activated. The panel that most
/// recently received an **activating** AR pointer action (press / release /
/// click — never a hover, which opens nothing) within @ref window_ms is the
/// panel that caused it, and becomes its owner. The decision is taken at the
/// FIRST sighting of the dialog and recorded; every later query returns the
/// recorded answer, so the attribution is:
///
///  * **exclusive** — one recorded owner means at most one panel ever adopts
///    that dialog, gives it a panel of its own, or blocks behind it;
///  * **stable** — later input on another panel cannot move a dialog that is
///    already on screen, so nothing flip-flops mid-interaction. (A decision is
///    dropped when the dialog is destroyed or hidden, so a dialog object shown a
///    second time is attributed afresh, to whoever caused *that* showing.)
///
/// Three refusals keep it honest:
///
///  * **no recent input, no owner.** A dialog opened with the local desktop
///    mouse is attributed to nobody — invisible in AR, taking no AR input. Safe
///    by default: the cost is a dialog the operator must reach on the desktop,
///    weighed against a panel adopting a stranger's window;
///  * **never steal a structurally owned dialog.** If the dialog's parent chain
///    passes through any registered panel (@ref register_panel), that panel owns
///    it by the parent walk and causation is not consulted — which is what keeps
///    "at most one panel" true even when one streamed panel is an ancestor of
///    another;
///  * **never adopt an ancestor.** A window the claiming panel itself lives in
///    is refused, so a panel streamed from inside a dialog cannot adopt the
///    dialog it is part of.
///
/// Popups are deliberately out of scope: Qt parents them properly, so the walk
/// already answers, and adopting an unparented one would let a context menu the
/// local operator opened land in AR.
///
/// @note GUI thread only, like everything else in this library. The state is
///       process-wide because the ownership question is asked from free
///       functions (@ref collect_panel_overlays, @ref panel_owned_modal_window)
///       that have no panel set to consult, and because the answer must be the
///       same for every caller — that is what makes it exclusive.
class PanelOverlayAttribution
{
public:
  /// The process-wide instance, created on first use.
  static PanelOverlayAttribution & instance();

  /// @brief Make @p panel_root known, so a dialog its tree parents is never
  /// claimed by causation for somebody else. Idempotent; held weakly.
  void register_panel(QWidget * panel_root);

  /// @brief Forget @p panel_root. Decisions already naming it are left alone —
  /// they simply stop matching any streamed panel, which is the safe direction.
  void unregister_panel(QWidget * panel_root);

  /// @brief Record that @p panel_root just took an activating AR pointer action.
  ///
  /// Called by @c QtPanelInputInjector BEFORE the action is delivered, because
  /// the delivery is what opens the dialog: `QDialog::exec()` does not return
  /// until the operator dismisses it, so a record written afterwards would be
  /// written minutes too late. Also registers @p panel_root.
  void note_panel_input(QWidget * panel_root);

  /// @brief The panel that owns @p overlay by causation, deciding now if this is
  /// its first sighting. Null when nobody can be held responsible.
  ///
  /// Called from the manager's per-tick dialog sweep and nowhere else — see
  /// @ref PanelOverlayOptions::decide_attribution for why the input path must
  /// not be the one to decide.
  QWidget * owner_of(QWidget * overlay);

  /// @brief The decision already on file, without taking one.
  /// @param out_decided Optional; set true when a decision exists — including a
  ///        decision of "nobody", which is why the null return is ambiguous
  ///        without it.
  QWidget * decided_owner(const QWidget * overlay, bool * out_decided = nullptr) const;

  /// @brief How long after an activating action a new dialog still counts as
  /// caused by that panel. Default 1500 ms.
  ///
  /// The sighting normally follows the action by one capture tick (~16 ms at
  /// 60 Hz, and that tick keeps running inside `exec()` — see
  /// docs/ARCHITECTURE.md#popups-and-dialogs), so the window is far wider than the
  /// case it exists for needs. It is kept wide enough to survive a slow or
  /// briefly stalled host tick, and short enough that a dialog the local
  /// operator opens a second or two later is not mistaken for the AR
  /// operator's.
  double window_ms() const { return window_ms_; }
  void set_window_ms(double ms) { window_ms_ = ms; }

  /// @brief Forget every decision, every registered panel and the last input,
  /// and restore the default window. For tests, which share one process and must
  /// not inherit each other's attributions.
  void reset();

private:
  PanelOverlayAttribution() = default;

  struct Decision
  {
    QPointer<QWidget> overlay;
    QPointer<QWidget> owner;  ///< Null is a real answer: "nobody".
  };

  QWidget * decide(QWidget * overlay) const;
  /// Drop decisions whose dialog is gone or off screen, and panels that have
  /// been destroyed.
  void prune();

  std::vector<Decision> decisions_;
  std::vector<QPointer<QWidget>> panels_;
  QPointer<QWidget> last_input_panel_;
  /// Steady, not the action's timestamp: AR timestamps come from a remote clock
  /// and may be zero, while this window is about how long ago it happened HERE.
  std::chrono::steady_clock::time_point last_input_at_{};
  bool has_input_ = false;
  double window_ms_ = 1500.0;
};

/// @brief Does @p root own @p overlay by EITHER rule — the parent walk first,
/// then, for dialog-class windows only, @ref PanelOverlayAttribution.
///
/// This is the ownership test the scans use. The parent walk decides every
/// popup, tooltip and genuinely child dialog; attribution is consulted only
/// where that walk says no.
///
/// @param may_decide False asks the attribution only for decisions it has
///        already taken. See @ref PanelOverlayOptions::decide_attribution — the
///        capture decides, the input path reads.
bool panel_tree_owns_overlay(QWidget * root, QWidget * overlay, bool may_decide = true);

/// @brief Every currently visible popup window owned by the tree under @p root.
///
/// Ordered parents before children (by popup depth), so an open submenu paints
/// over the menu that spawned it. `QApplication::activePopupWidget()` alone is
/// not enough: it returns only the innermost popup, which would drop the parent
/// menu of an open submenu, and it is null for tooltips (`Qt::ToolTip` is not
/// `Qt::Popup`). The full scan is gated on "is anything popped up at all", so an
/// ordinary tick pays two static calls and no iteration.
///
/// @param include_tooltips Also collect `Qt::ToolTip` windows (Qt parents the
///        tooltip label to the widget it describes on this platform, so they are
///        attributable; where a platform parents it elsewhere it is simply not
///        collected).
///
/// A parentless popup is attributable to nobody - except one the off-screen popup
/// guard keeps off the desktop (offscreen_popup_guard.hpp), which is followed through
/// its focus proxy: QCompleter re-parents its list to null and points it at the line
/// edit, so the parent walk alone could never find it.
std::vector<QWidget *> collect_panel_popups(QWidget * root, bool include_tooltips = true);

/// @brief Which kinds of owned top-level window a scan should return.
struct PanelOverlayOptions
{
  /// `Qt::Popup`: combo box lists, menus, completers.
  bool popups = true;
  /// `Qt::ToolTip`.
  bool tooltips = true;
  /// `Qt::Dialog` / `Qt::Sheet` / `Qt::Drawer`: an "Add" box and friends,
  /// modal or not.
  bool dialogs = true;
  /// Also accept a dialog attributed to this panel by @ref
  /// PanelOverlayAttribution — a dialog parented to the main window, which
  /// no parent walk can reach. False => the strict parent walk only.
  bool attribute_by_input = true;
  /// May this scan TAKE an attribution decision, or only read decisions already
  /// taken?
  ///
  /// Only the manager's per-tick dialog sweep decides, because promoting the
  /// dialog to a panel of its own is what makes it visible in AR. The input path
  /// reads, so a pointer can never be routed into a dialog the operator cannot
  /// yet see — the same "what is seen at (x, y) is what is clicked at (x, y)"
  /// invariant popup clipping keeps. It also closes a real hazard: the injector
  /// records the activating action BEFORE delivering it, so a scan that decided
  /// from the input path would let a press land on an unattributed dialog and
  /// adopt it with that very press.
  bool decide_attribution = true;
};

/// @brief Every visible top-level window owned by the tree under @p root.
///
/// The generalisation of @ref collect_panel_popups. Ordering is popups and
/// tooltips first (by popup depth, parents before children) and dialogs last, so
/// a combo list opened INSIDE a dialog paints over the dialog and the dialog
/// paints over the panel — which is the stacking the operator sees on the
/// desktop.
///
/// Unlike the popup scan there is no "is anything popped up at all" fast path
/// for dialogs: Qt exposes no global "a dialog is open" predicate that also
/// covers non-modal ones, so the top-level list is walked whenever
/// @c options.dialogs is set. That list is a handful of windows in a typical
/// application and the walk is a pointer compare per entry.
std::vector<QWidget *> collect_panel_overlays(
  QWidget * root, PanelOverlayOptions options = PanelOverlayOptions{});

/// @brief The modal window owned by @p root's tree that is currently blocking
/// input, or null.
///
/// `QApplication::activeModalWidget()` answers "what is the topmost modal
/// window in this application", which is the right question — but it says
/// nothing about ownership, and a modal dialog belonging to some OTHER part of
/// the application must not silently swallow this panel's AR clicks. This filters that
/// answer through @ref panel_tree_owns_popup.
///
/// Used by `QtPanelInputInjector` to reproduce Qt's own rule that a widget
/// behind an application-modal window receives no pointer input.
///
/// Attribution is consulted read-only here (see
/// @ref PanelOverlayOptions::decide_attribution): a modal dialog no panel has
/// composited yet blocks nobody, because no operator can see it.
QWidget * panel_owned_modal_window(QWidget * root);

/// @brief Is @p widget a dialog-class top-level window?
bool is_dialog_window(const QWidget * widget);

/// @brief Grabs one widget into a reusable RGBA8888 buffer, with a dirty check.
///
/// The buffer is allocated once per size and reused, so a steady-state tick does
/// no allocation. @ref capture reports whether the pixels actually changed; an
/// idle control panel therefore submits nothing at all.
///
/// @warning The widget must be VISIBLE. `QWidget::render()` draws only children
/// that are `isVisible()`, and visibility is inherited — so capturing a widget
/// that was never shown yields its background and nothing else. A docked
/// panel satisfies this; a panel the operator has closed does not, which is why
/// @ref widget_visible is reported through to diagnostics rather than silently
/// streaming a blank rectangle.
class QtWidgetCapture
{
public:
  /// @param widget Borrowed, must outlive this object (the owner is responsible
  ///        for destroying the capture when the widget goes away).
  explicit QtWidgetCapture(QWidget * widget);

  QtWidgetCapture(const QtWidgetCapture &) = delete;
  QtWidgetCapture & operator=(const QtWidgetCapture &) = delete;

  /// @brief Point the capture at a DIFFERENT widget, invalidating the dirty
  /// check so the next capture is always sent.
  ///
  /// Exists for the tombstone swap (panel_tombstone.hpp): when a streamed
  /// window closes, the stream must go on streaming — it captures an owned
  /// placeholder from then on instead of the widget that went away. Retargeting
  /// rather than rebuilding the capture keeps the allocated buffer, which is
  /// the same size either way, and keeps the object identity the stream holds.
  void set_widget(QWidget * widget);

  /// @brief The widget currently being captured. Borrowed; may be null.
  QWidget * widget() const { return widget_; }

  /// @brief Render the widget into a @p buffer_width x @p buffer_height buffer.
  ///
  /// Popups owned by the widget tree (@ref collect_panel_popups) are composited
  /// on top afterwards, at the position Qt actually put them, mapped through the
  /// panel's own coordinates. Dialogs are not: they get panels of their own.
  ///
  /// @param device_pixel_ratio Set on the target image so Qt scales the widget's
  ///        logical coordinates up to the buffer's device pixels — this is what
  ///        keeps HiDPI text crisp instead of upscaled-blurry. Popups use the
  ///        same ratio (dpr times the render scale), so a supersampled panel
  ///        gets a supersampled list.
  /// @return false if the widget is gone, the size is zero, or Qt failed to
  ///         paint; @ref last_error explains.
  bool capture(
    std::uint32_t buffer_width, std::uint32_t buffer_height, double device_pixel_ratio);

  /// @brief True when the most recent @ref capture differed from the one before.
  /// Always true for the first successful capture and after any resize.
  ///
  /// The comparison covers the COMPOSITED image, so a popup opening or closing is
  /// a change and the frame is resent. It also folds in the popups' panel-local
  /// geometry: a host whose platform rasterizes no glyphs (the offscreen QPA
  /// plugin, and therefore this library's own test suite) can otherwise open a
  /// list over a same-coloured background and change not one byte, which would
  /// strand the popup off the stream until something else moved. Geometry in the
  /// hash also covers a list being scrolled or resized in place.
  bool content_changed() const { return content_changed_; }

  /// @brief The most recent captured frame, borrowed. Valid until the next
  /// @ref capture or destruction.
  qar_ar_core::PanelPixels pixels() const;

  /// @brief Wall time the most recent @ref capture took, in milliseconds.
  double last_capture_ms() const { return last_capture_ms_; }

  /// @brief Whether the widget was visible at the most recent @ref capture.
  /// False means the frame holds only the widget's background (see the class
  /// warning) — the panel will look blank in AR.
  bool widget_visible() const { return widget_visible_; }

  /// @brief Composite owned popups into the captured image (default true).
  /// An escape hatch for a host that would rather stream the bare widget.
  void set_composite_popups(bool enabled) { composite_popups_ = enabled; }
  bool composite_popups() const { return composite_popups_; }

  /// @brief Also composite `Qt::ToolTip` windows (default true).
  void set_composite_tooltips(bool enabled) { composite_tooltips_ = enabled; }
  bool composite_tooltips() const { return composite_tooltips_; }

  /// @brief How many owned popups the most recent @ref capture actually drew. A
  /// popup that fell entirely outside the panel is collected but not drawn, so
  /// it is not counted.
  ///
  /// Dialogs are never counted here because they are never drawn here: an owned
  /// dialog gets a panel of its own (see the class note on dialogs).
  std::uint32_t composited_popup_count() const { return composited_popup_count_; }

  /// @brief Popups/tooltips owned by this panel at the last capture, whether or not any of
  /// their pixels landed inside it (0 with compositing off). The manager logs its 0 <-> N
  /// edges.
  std::uint32_t owned_popup_count() const { return owned_popup_count_; }

  /// @brief Of those, how many the off-screen popup guard keeps off the desktop.
  std::uint32_t offscreen_popup_count() const { return offscreen_popup_count_; }

  /// @brief "<Qt class> WxH at (x,y)" of the topmost of those, in panel logical pixels; ""
  /// if none.
  const std::string & popup_summary() const { return popup_summary_; }

  /// @brief True when the most recent capture had to cut a popup at the panel
  /// edge — the operator is seeing only part of that list in AR.
  ///
  /// ### Why clip, and not shift the popup inward
  ///
  /// Qt positions a popup against the **desktop screen**, not against the panel,
  /// so a list opened near the panel's bottom edge really does hang off it. It
  /// can be clipped or slid back inside the panel before drawing. This capture
  /// clips, because the AR input path (`panel_hit_test` into
  /// `QtPanelInputInjector`) maps a panel-local point to a GLOBAL point and hands
  /// it to the popup *where Qt actually put it*. Shifting only the drawing would
  /// desynchronise the picture from the hit test: every item the operator aimed
  /// at would select a different one, silently — the worst failure mode
  /// available. Clipping keeps "what you see at (x, y) is what you click at
  /// (x, y)" exactly true; a list too tall for its panel comes out visibly cut
  /// rather than invisibly mis-aimed, and a host can fix that by giving the panel
  /// room. (Moving the real popup window would keep picture and hit test in step,
  /// but then the capture path would be rearranging desktop windows behind the
  /// local operator's back, and fighting Qt's own placement every tick.)
  bool popup_clipped() const { return popup_clipped_; }

  const std::string & last_error() const { return last_error_; }

  /// @brief Forget the previous frame, so the next capture always reports
  /// changed. Used after a sender is (re)created, since QarOS has seen nothing.
  void invalidate();

  /// @name Region substitution (variant b)
  /// See the class file comment, "Region substitution", for why this exists.
  /// @{

  /// @brief Show @p source's latest video frame in place of @p region's own
  /// captured pixels, from the next @ref capture on.
  ///
  /// @p region must be @ref widget() itself or (typically) one of its
  /// descendants — e.g. an embedded native-window container; it is re-checked, live, on
  /// every capture, so it is safe to register before the widget tree it
  /// belongs to is even reachable from @ref widget() yet (nothing is drawn
  /// until it is). Held via `QPointer`, so @p region may be destroyed later
  /// with no call needed here first. Calling this again for a @p region
  /// already registered replaces its source.
  void set_region_source(QWidget * region, std::shared_ptr<LatestVideoFrame> source);

  /// @brief Stop substituting @p region alone. A no-op if it was not registered.
  void clear_region_source(QWidget * region);

  /// @brief Stop substituting every registered region.
  void clear_region_sources();

  /// @brief How many regions the most recent @ref capture actually drew video
  /// (or status text) into — i.e. were live, a descendant of @ref widget(),
  /// and @c isVisible().
  std::uint32_t substituted_region_count() const { return substituted_region_count_; }

  /// @brief Summed `LatestVideoFrame::sequence()` of every registered region
  /// source whose region is alive and visible: a number that changes exactly when
  /// any of them has a new frame to show (sequences only ever grow; a source
  /// appearing or disappearing also changes it, which at worst costs one extra
  /// capture). The capture gate's "frame arrived" signal — see capture_gate.hpp.
  /// @param[out] any True when at least one such source exists.
  std::uint64_t region_frame_sequence(bool & any) const;

  /// @brief The trace (see VideoFrameTrace) of the most recently published frame among the
  /// live, visible region sources - the one with the largest `mailbox_us`. Per-frame timing
  /// only (QAR_VIDEO_TIMING_DIR); stays untouched when no such source has a valid trace.
  /// @return true when @p out was set.
  bool newest_region_frame_trace(VideoFrameTrace & out) const;

  /// @}

private:
  /// Draw every owned popup and tooltip over the already-rendered base image.
  /// @return a hash of the popups' panel-local geometry, folded into the dirty
  ///         check by the caller.
  std::uint64_t composite_owned_popups(
    std::uint32_t buffer_width, std::uint32_t buffer_height, double ratio);

  /// Draw every registered, live, visible region's substituted video (or
  /// status text) over the already-rendered base image, BEFORE popups (an
  /// open popup must still paint over the video underneath it).
  /// @return a hash folding in each drawn region's geometry plus its frame
  ///         sequence (or its status text when there is no frame yet), so a
  ///         new video frame counts as changed content exactly like an
  ///         opening popup does.
  std::uint64_t composite_region_sources(
    std::uint32_t buffer_width, std::uint32_t buffer_height, double ratio);

  /// One region's video substitution. `region` is null once Qt has destroyed
  /// the widget it pointed to; @ref composite_region_sources prunes those
  /// lazily rather than requiring the owner to call @ref clear_region_source
  /// first.
  struct RegionSource
  {
    QPointer<QWidget> region;
    std::shared_ptr<LatestVideoFrame> source;
  };

  QWidget * widget_ = nullptr;
  QImage image_;
  /// Scratch a popup is rendered into before being blitted into @ref image_.
  /// Reused across popups and across ticks, so a steady popup allocates nothing.
  QImage popup_image_;
  std::uint64_t content_hash_ = 0;
  bool has_previous_ = false;
  bool content_changed_ = false;
  bool widget_visible_ = false;
  bool composite_popups_ = true;
  bool composite_tooltips_ = true;
  std::uint32_t composited_popup_count_ = 0;
  std::uint32_t owned_popup_count_ = 0;
  std::uint32_t offscreen_popup_count_ = 0;
  std::string popup_summary_;
  bool popup_clipped_ = false;
  double last_capture_ms_ = 0.0;
  std::string last_error_;
  std::vector<RegionSource> region_sources_;
  std::uint32_t substituted_region_count_ = 0;
};

/// @brief FNV-1a over a pixel buffer, row by row so padding bytes never count.
///
/// Exposed for unit tests: the dirty check is the thing that keeps an idle panel
/// off the network, so it needs coverage that does not require a QApplication.
std::uint64_t hash_pixels(
  const std::uint8_t * data,
  std::uint32_t width,
  std::uint32_t height,
  std::uint32_t stride_bytes);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__QT_WIDGET_CAPTURE_HPP_
