// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/qt_widget_capture.hpp"

#include "qar_qt_streaming/offscreen_popup_guard.hpp"
#include "qar_qt_streaming/video_frame.hpp"

#include <QApplication>
#include <QColor>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QString>
#include <QToolTip>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

namespace qar_qt_streaming
{

namespace
{

/// How long an activating AR action keeps a panel eligible to be blamed for a
/// dialog that appears. See PanelOverlayAttribution::window_ms().
constexpr double kDefaultAttributionWindowMs = 1500.0;

constexpr std::uint64_t kOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kPrime = 1099511628211ULL;

/// FNV-1a over an arbitrary run of bytes, continuing from @p seed.
std::uint64_t
hash_bytes(const std::uint8_t * data, std::size_t size, std::uint64_t seed)
{
  std::uint64_t hash = seed;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= kPrime;
  }
  return hash;
}

/// Is @p widget one of the window kinds that paints outside its parent?
///
/// `windowType()` masks the flags down to the one kind, so a combo box list
/// (`Qt::Popup | Qt::FramelessWindowHint`) still answers `Qt::Popup`. Tooltips
/// are a different kind entirely, which is why they need naming separately —
/// and why `QApplication::activePopupWidget()` alone never sees one.
bool
is_popup_window(const QWidget * widget, bool include_tooltips)
{
  const Qt::WindowType type = widget->windowType();
  if (type == Qt::Popup) {
    return true;
  }
  return include_tooltips && type == Qt::ToolTip;
}

/// How many popup windows sit between @p widget and the panel — a submenu's
/// chain passes through the menu that opened it, so this orders parents first.
int
popup_depth(const QWidget * widget, bool include_tooltips)
{
  int depth = 0;
  for (const QWidget * parent = widget->parentWidget(); parent != nullptr;
    parent = parent->parentWidget())
  {
    if (is_popup_window(parent, include_tooltips)) {
      ++depth;
    }
  }
  return depth;
}

/// Does @p widget carry a window flag that makes it a dialog-class top level?
///
/// `windowType()` masks the flags to the single kind, so a dialog with
/// `Qt::WindowStaysOnTopHint` or a custom title bar still answers `Qt::Dialog`.
/// Sheets and drawers are the macOS spellings of the same idea and are folded in
/// so a host built there behaves the same.
bool
is_dialog_type(const QWidget * widget)
{
  const Qt::WindowType type = widget->windowType();
  return type == Qt::Dialog || type == Qt::Sheet || type == Qt::Drawer;
}

/// Does a DIALOG window sit between @p popup and @p root?
///
/// A combo list opened inside a dialog is parented to a widget inside that
/// dialog, so the parent walk from the list reaches the panel that owns the
/// dialog as well. Since the dialog is a panel in its OWN right, the list
/// belongs to THAT panel: drawing it into the owner too would put it at
/// coordinates that mean nothing there, over a dialog the owner is not drawing.
/// The nearest dialog ancestor wins, so a popup lands in exactly one image.
///
/// The walk starts at the popup's PARENT, so a dialog is never a barrier to
/// itself, and stops at @p root, so a panel streamed from inside a dialog still
/// gets its own popups.
bool
dialog_between(const QWidget * root, const QWidget * popup)
{
  for (const QWidget * widget = popup->parentWidget();
    widget != nullptr && widget != root;
    widget = widget->parentWidget())
  {
    if (is_dialog_type(widget)) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool
is_dialog_window(const QWidget * widget)
{
  return widget != nullptr && is_dialog_type(widget);
}

std::uint64_t
hash_pixels(
  const std::uint8_t * data,
  std::uint32_t width,
  std::uint32_t height,
  std::uint32_t stride_bytes)
{
  std::uint64_t hash = kOffsetBasis;
  if (data == nullptr) {
    return hash;
  }
  // Row-by-row rather than one flat pass: Qt pads scanlines to a 4-byte boundary,
  // and hashing that padding would make an unchanged panel look dirty whenever
  // the uninitialised tail happened to differ.
  const std::uint32_t row_bytes = width * 4u;
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::uint8_t * row = data + static_cast<std::size_t>(y) * stride_bytes;
    hash = hash_bytes(row, row_bytes, hash);
  }
  return hash;
}

bool
panel_tree_owns_popup(const QWidget * root, const QWidget * popup)
{
  if (root == nullptr || popup == nullptr || root == popup) {
    return false;
  }
  // parentWidget() and NOT isAncestorOf(): the latter also demands that both
  // widgets live in the same window, and a popup is its own window, so it
  // answers false for every popup ever opened from this panel.
  for (const QWidget * widget = popup; widget != nullptr; widget = widget->parentWidget()) {
    if (widget == root) {
      return true;
    }
  }
  return false;
}

QPoint
popup_global_origin(const QWidget * popup)
{
  if (popup == nullptr) {
    return QPoint();
  }
  const Qt::WindowType type = popup->windowType();
  if (popup->isWindow() && (type == Qt::Popup || type == Qt::ToolTip) &&
    popup->testAttribute(Qt::WA_DontShowOnScreen))
  {
    return popup->geometry().topLeft();
  }
  return popup->mapToGlobal(QPoint(0, 0));
}

namespace
{

/// A popup the guard keeps off the desktop may have no parent at all (QCompleter
/// re-parents its list to nullptr and points it at the line edit with setFocusProxy):
/// follow the parent walk, then the focus proxy of the parentless top, through popups.
bool
offscreen_popup_owned_by(const QWidget * root, const QWidget * popup)
{
  const QWidget * current = popup;
  for (int depth = 0; current != nullptr && depth < 16; ++depth) {
    if (panel_tree_owns_popup(root, current)) {
      return true;
    }
    const QWidget * top = current;
    while (top->parentWidget() != nullptr) {
      top = top->parentWidget();
    }
    if (!is_offscreen_popup(top)) {
      return false;  // a desktop popup without a parent stays attributable to nobody
    }
    current = top->focusProxy();
  }
  return false;
}

}  // namespace

// --- attribution -------------------------------------------------------------

PanelOverlayAttribution &
PanelOverlayAttribution::instance()
{
  // A function-local static, so it is built on first use — which is after
  // QApplication in every host — and torn down after main() has already taken
  // the widgets with it, by which time every QPointer here is null.
  static PanelOverlayAttribution attribution;
  return attribution;
}

void
PanelOverlayAttribution::register_panel(QWidget * panel_root)
{
  if (panel_root == nullptr) {
    return;
  }
  prune();
  for (const QPointer<QWidget> & panel : panels_) {
    if (panel.data() == panel_root) {
      return;
    }
  }
  panels_.emplace_back(panel_root);
}

void
PanelOverlayAttribution::unregister_panel(QWidget * panel_root)
{
  panels_.erase(
    std::remove_if(
      panels_.begin(), panels_.end(),
      [panel_root](const QPointer<QWidget> & panel) {
        return panel.isNull() || panel.data() == panel_root;
      }),
    panels_.end());
}

void
PanelOverlayAttribution::note_panel_input(QWidget * panel_root)
{
  if (panel_root == nullptr) {
    return;
  }
  register_panel(panel_root);
  last_input_panel_ = panel_root;
  last_input_at_ = std::chrono::steady_clock::now();
  has_input_ = true;
}

QWidget *
PanelOverlayAttribution::owner_of(QWidget * overlay)
{
  if (overlay == nullptr) {
    return nullptr;
  }
  prune();
  for (const Decision & decision : decisions_) {
    if (decision.overlay.data() == overlay) {
      // Decided already — and never revisited while the dialog is on screen.
      // That is the whole of the stability guarantee: input landing on another
      // panel afterwards cannot move a dialog the operator is already using.
      return decision.owner.data();
    }
  }
  Decision fresh;
  fresh.overlay = overlay;
  fresh.owner = decide(overlay);
  decisions_.push_back(fresh);
  return fresh.owner.data();
}

QWidget *
PanelOverlayAttribution::decided_owner(const QWidget * overlay, bool * out_decided) const
{
  if (out_decided != nullptr) {
    *out_decided = false;
  }
  for (const Decision & decision : decisions_) {
    if (!decision.overlay.isNull() && decision.overlay.data() == overlay) {
      if (out_decided != nullptr) {
        *out_decided = true;
      }
      return decision.owner.data();
    }
  }
  return nullptr;
}

void
PanelOverlayAttribution::reset()
{
  decisions_.clear();
  panels_.clear();
  last_input_panel_.clear();
  last_input_at_ = std::chrono::steady_clock::time_point{};
  has_input_ = false;
  window_ms_ = kDefaultAttributionWindowMs;
}

QWidget *
PanelOverlayAttribution::decide(QWidget * overlay) const
{
  // Never steal what the parent walk already answers: a dialog parented INTO a
  // streamed panel belongs to that panel, whoever was clicking at the time.
  // This is what keeps "at most one panel adopts it" true even when one
  // streamed panel is an ancestor of another.
  for (const QPointer<QWidget> & panel : panels_) {
    if (!panel.isNull() && panel_tree_owns_popup(panel.data(), overlay)) {
      return nullptr;
    }
  }
  if (!has_input_ || last_input_panel_.isNull()) {
    return nullptr;  // nobody has ever clicked from AR: not ours to claim
  }
  const auto age = std::chrono::steady_clock::now() - last_input_at_;
  if (std::chrono::duration<double, std::milli>(age).count() > window_ms_) {
    // The local operator's own mouse, most likely. Attributed to nobody.
    return nullptr;
  }
  QWidget * candidate = last_input_panel_.data();
  // A panel streamed from inside a dialog must not adopt the dialog it is part
  // of (never adopt an ancestor).
  if (overlay == candidate->window() || overlay->isAncestorOf(candidate)) {
    return nullptr;
  }
  return candidate;
}

void
PanelOverlayAttribution::prune()
{
  // A decision lives as long as the showing it was taken for. Dropping it when
  // the dialog hides means a dialog OBJECT that is shown again is attributed to
  // whoever caused that second showing, rather than inheriting an answer from a
  // minute ago; dropping it when the dialog dies means a new dialog at the same
  // address cannot inherit one either.
  decisions_.erase(
    std::remove_if(
      decisions_.begin(), decisions_.end(),
      [](const Decision & decision) {
        return decision.overlay.isNull() || !decision.overlay->isVisible();
      }),
    decisions_.end());
  panels_.erase(
    std::remove_if(
      panels_.begin(), panels_.end(),
      [](const QPointer<QWidget> & panel) { return panel.isNull(); }),
    panels_.end());
}

bool
panel_tree_owns_overlay(QWidget * root, QWidget * overlay, bool may_decide)
{
  // The parent walk first, always: it is the strict answer and it is free.
  if (panel_tree_owns_popup(root, overlay)) {
    return true;
  }
  if (root == nullptr || overlay == nullptr || !is_dialog_type(overlay)) {
    // Popups and tooltips stop here: for them the parent walk above is the whole answer
    // of this function, and the caller also accepts a window that the popup guard marked
    // (offscreen_popup_guard.hpp). A tooltip on Windows is neither, because QToolTip
    // parents its label to the desktop there. Adopting an unparented window would put a
    // menu the LOCAL operator opened onto an AR panel.
    return false;
  }
  PanelOverlayAttribution & attribution = PanelOverlayAttribution::instance();
  QWidget * const owner = may_decide ?
    attribution.owner_of(overlay) : attribution.decided_owner(overlay);
  return owner == root;
}

std::vector<QWidget *>
collect_panel_popups(QWidget * root, bool include_tooltips)
{
  PanelOverlayOptions options;
  options.popups = true;
  options.tooltips = include_tooltips;
  options.dialogs = false;
  return collect_panel_overlays(root, options);
}

std::vector<QWidget *>
collect_panel_overlays(QWidget * root, PanelOverlayOptions options)
{
  std::vector<QWidget *> overlays;
  if (root == nullptr || QApplication::instance() == nullptr) {
    return overlays;
  }

  // Fast path for the overwhelmingly common tick: nothing is popped up, so the
  // scan over every top-level widget in the host application never runs. It only
  // applies when dialogs are NOT wanted — Qt publishes no equivalent "is any
  // dialog open" predicate that also covers modeless ones.
  if (!options.dialogs) {
    const bool popup_open = options.popups && QApplication::activePopupWidget() != nullptr;
    const bool tooltip_open = options.tooltips && QToolTip::isVisible();
    if (!popup_open && !tooltip_open) {
      return overlays;
    }
  }

  std::vector<QWidget *> popups;
  std::vector<QWidget *> dialogs;
  const QWidgetList tops = QApplication::topLevelWidgets();
  for (QWidget * candidate : tops) {
    if (candidate == nullptr || candidate == root || !candidate->isVisible()) {
      continue;
    }
    const bool popup_like =
      (options.popups || options.tooltips) && is_popup_window(candidate, options.tooltips);
    // A popup is never also a dialog (windowType() is one kind), so the two
    // buckets cannot both claim a window.
    const bool dialog_like = options.dialogs && is_dialog_type(candidate);
    if (!popup_like && !dialog_like) {
      continue;
    }
    if (popup_like && !options.popups && candidate->windowType() == Qt::Popup) {
      continue;  // tooltips only
    }
    // Attribution, not just the parent walk: an application often parents its
    // dialogs to the MAIN WINDOW, so the walk from such a dialog never reaches
    // the streamed dock. See PanelOverlayAttribution.
    bool owned = options.attribute_by_input ?
      panel_tree_owns_overlay(root, candidate, options.decide_attribution) :
      panel_tree_owns_popup(root, candidate);
    if (!owned && popup_like && is_offscreen_popup(candidate)) {
      owned = offscreen_popup_owned_by(root, candidate);
    }
    if (!owned) {
      continue;
    }
    if (popup_like && dialog_between(root, candidate)) {
      continue;  // it belongs to the dialog it was opened in, which has its own panel
    }
    (popup_like ? popups : dialogs).push_back(candidate);
  }

  const bool include_tooltips = options.tooltips;
  std::stable_sort(
    popups.begin(), popups.end(),
    [include_tooltips](const QWidget * a, const QWidget * b) {
      return popup_depth(a, include_tooltips) < popup_depth(b, include_tooltips);
    });
  // Dialogs paint UNDER popups: a combo list opened inside a dialog has to end
  // up on top of it, and that list is collected into `popups` above.
  std::stable_sort(
    dialogs.begin(), dialogs.end(),
    [](const QWidget * a, const QWidget * b) {
      return popup_depth(a, true) < popup_depth(b, true);
    });

  overlays.reserve(popups.size() + dialogs.size());
  overlays.insert(overlays.end(), dialogs.begin(), dialogs.end());
  overlays.insert(overlays.end(), popups.begin(), popups.end());
  return overlays;
}

QWidget *
panel_owned_modal_window(QWidget * root)
{
  if (root == nullptr || QApplication::instance() == nullptr) {
    return nullptr;
  }
  // activeModalWidget() is the topmost modal window in the APPLICATION; it says
  // nothing about who opened it, and the application has modal dialogs of its own
  // that must not swallow this panel's AR input.
  QWidget * modal = QApplication::activeModalWidget();
  if (modal == nullptr || !modal->isVisible()) {
    return nullptr;
  }
  // The same ownership question the capture asks, so a dialog attributed to this
  // panel blocks input behind it exactly as a structurally owned one does — the
  // operator must not be able to click past a dialog they can see. Read-only:
  // this is the input path, and it never decides an attribution of its own.
  return panel_tree_owns_overlay(root, modal, false) ? modal : nullptr;
}

QtWidgetCapture::QtWidgetCapture(QWidget * widget)
: widget_(widget)
{
}

void
QtWidgetCapture::set_widget(QWidget * widget)
{
  if (widget_ == widget) {
    return;
  }
  widget_ = widget;
  // The previous hash described a different widget entirely, so there is no
  // "unchanged" answer that means anything: the next capture must be sent.
  invalidate();
}

void
QtWidgetCapture::invalidate()
{
  has_previous_ = false;
  content_changed_ = false;
}

void
QtWidgetCapture::set_region_source(QWidget * region, std::shared_ptr<LatestVideoFrame> source)
{
  if (region == nullptr) {
    return;
  }
  for (RegionSource & entry : region_sources_) {
    if (entry.region == region) {
      entry.source = std::move(source);
      return;
    }
  }
  region_sources_.push_back(RegionSource{QPointer<QWidget>(region), std::move(source)});
}

void
QtWidgetCapture::clear_region_source(QWidget * region)
{
  region_sources_.erase(
    std::remove_if(
      region_sources_.begin(), region_sources_.end(),
      [region](const RegionSource & entry) {return entry.region == region;}),
    region_sources_.end());
}

void
QtWidgetCapture::clear_region_sources()
{
  region_sources_.clear();
}

std::uint64_t
QtWidgetCapture::region_frame_sequence(bool & any) const
{
  any = false;
  std::uint64_t sum = 0;
  for (const RegionSource & entry : region_sources_) {
    if (entry.source && !entry.region.isNull() && entry.region->isVisible()) {
      any = true;
      sum += entry.source->sequence();
    }
  }
  return sum;
}

bool
QtWidgetCapture::newest_region_frame_trace(VideoFrameTrace & out) const
{
  bool found = false;
  for (const RegionSource & entry : region_sources_) {
    if (entry.source && !entry.region.isNull() && entry.region->isVisible()) {
      const VideoFrameTrace trace = entry.source->latest_trace();
      if (trace.valid && (!found || trace.mailbox_us > out.mailbox_us)) {
        out = trace;
        found = true;
      }
    }
  }
  return found;
}

bool
QtWidgetCapture::capture(
  std::uint32_t buffer_width, std::uint32_t buffer_height, double device_pixel_ratio)
{
  content_changed_ = false;
  if (widget_ == nullptr) {
    last_error_ = "no widget";
    return false;
  }
  if (buffer_width == 0 || buffer_height == 0) {
    last_error_ = "zero buffer size";
    return false;
  }
  if (!(device_pixel_ratio > 0.0)) {
    last_error_ = "device_pixel_ratio must be positive";
    return false;
  }

  const auto started = std::chrono::steady_clock::now();

  const QSize target(static_cast<int>(buffer_width), static_cast<int>(buffer_height));
  if (image_.size() != target || image_.format() != QImage::Format_RGBA8888) {
    image_ = QImage(target, QImage::Format_RGBA8888);
    if (image_.isNull()) {
      last_error_ = "failed to allocate the capture buffer";
      return false;
    }
    // A resize invalidates the dirty comparison: the previous hash described a
    // differently shaped buffer.
    has_previous_ = false;
  }

  // Setting the ratio makes QPainter treat the image as dpr x the logical size,
  // so the widget paints its logical geometry into the full device-pixel buffer.
  // Without it the widget would render at 1:1 into the top-left corner.
  image_.setDevicePixelRatio(device_pixel_ratio);
  // The alignment slack (buffer rounded up to an even size) and any widget
  // transparency need a defined colour rather than whatever was left in the
  // reused buffer.
  image_.fill(Qt::transparent);

  // render() only draws children that are isVisible(), and visibility is
  // inherited — a widget that was never shown paints its background and nothing
  // else. Report it so the operator sees a reason for a blank AR panel.
  widget_visible_ = widget_->isVisible();

  widget_->render(
    &image_, QPoint(), QRegion(),
    QWidget::DrawWindowBackground | QWidget::DrawChildren);

  // Region substitution (variant b) BEFORE popups: a combo list or menu opened
  // over an embedded native-window container must still end up on top of the
  // video that replaces it, exactly as it would sit on top of the real
  // (uncapturable) native window on the desktop.
  const std::uint64_t region_hash =
    composite_region_sources(buffer_width, buffer_height, device_pixel_ratio);

  // Popups are separate top-level windows, so they arrive in a second pass, on
  // top of the base widget — which is the order the operator sees on the desktop.
  const std::uint64_t popup_hash =
    composite_owned_popups(buffer_width, buffer_height, device_pixel_ratio);

  std::uint64_t hash = hash_pixels(
    image_.constBits(),
    buffer_width,
    buffer_height,
    static_cast<std::uint32_t>(image_.bytesPerLine()));
  // Fold both the region-substitution and the popup geometry into the SAME
  // hash rather than tracking them separately: both are part of this frame's
  // content, and a platform that renders no glyphs and no list styling
  // (offscreen QPA) could otherwise change one without moving a single pixel.
  hash = hash_bytes(
    reinterpret_cast<const std::uint8_t *>(&region_hash), sizeof(region_hash), hash);
  hash = hash_bytes(
    reinterpret_cast<const std::uint8_t *>(&popup_hash), sizeof(popup_hash), hash);

  content_changed_ = !has_previous_ || hash != content_hash_;
  content_hash_ = hash;
  has_previous_ = true;

  const auto elapsed = std::chrono::steady_clock::now() - started;
  last_capture_ms_ = std::chrono::duration<double, std::milli>(elapsed).count();
  last_error_.clear();
  return true;
}

std::uint64_t
QtWidgetCapture::composite_owned_popups(
  std::uint32_t buffer_width, std::uint32_t buffer_height, double ratio)
{
  composited_popup_count_ = 0;
  popup_clipped_ = false;
  owned_popup_count_ = 0;
  offscreen_popup_count_ = 0;
  popup_summary_.clear();

  std::uint64_t geometry_hash = kOffsetBasis;
  if (widget_ == nullptr || image_.isNull()) {
    return geometry_hash;
  }
  if (!composite_popups_) {
    return geometry_hash;
  }

  // Popups and tooltips ONLY. A dialog owned by this panel gets a panel of its
  // own (QtPanelStreamManager's dialog sweep), so compositing it here would draw
  // it twice — once squeezed into somebody else's rectangle.
  const std::vector<QWidget *> popups = collect_panel_popups(widget_, composite_tooltips_);
  if (popups.empty()) {
    return geometry_hash;
  }

  owned_popup_count_ = static_cast<std::uint32_t>(popups.size());
  for (const QWidget * owned : popups) {
    if (is_offscreen_popup(owned)) {
      ++offscreen_popup_count_;
    }
  }
  {
    // The topmost popup, for the one log line a popup opening earns.
    const QWidget * top = popups.back();
    const QPoint at = widget_->mapFromGlobal(popup_global_origin(top));
    popup_summary_ = std::string(top->metaObject()->className()) + " " +
      std::to_string(top->width()) + "x" + std::to_string(top->height()) + " at (" +
      std::to_string(at.x()) + "," + std::to_string(at.y()) + ")";
  }

  // Paint through a shallow view over the SAME memory with no device pixel
  // ratio, so every coordinate below is a device pixel and nothing depends on
  // how QPainter would have scaled a logical one. The blend is still Qt's, so a
  // translucent or rounded popup composites correctly over the panel.
  QImage view(
    image_.bits(), image_.width(), image_.height(), image_.bytesPerLine(),
    QImage::Format_RGBA8888);
  QPainter painter(&view);
  if (!painter.isActive()) {
    return geometry_hash;
  }

  const QRect panel_rect(0, 0, static_cast<int>(buffer_width), static_cast<int>(buffer_height));

  for (QWidget * popup : popups) {
    const QSize logical_size = popup->size();
    if (logical_size.isEmpty()) {
      continue;
    }

    // Qt placed the popup in global coordinates (on the desktop, or only in this
    // image when it is kept off the desktop); the panel's own frame is the only one
    // the AR image and the AR hit test share, so go through global coordinates.
    const QPoint local = widget_->mapFromGlobal(popup_global_origin(popup));
    const QRect dest(
      static_cast<int>(std::lround(local.x() * ratio)),
      static_cast<int>(std::lround(local.y() * ratio)),
      static_cast<int>(std::lround(logical_size.width() * ratio)),
      static_cast<int>(std::lround(logical_size.height() * ratio)));

    // Hash the geometry whether or not it ends up drawn: a list scrolling,
    // resizing or sliding off the panel edge is a change the receiver must see.
    const std::int32_t rect_values[4] = {dest.x(), dest.y(), dest.width(), dest.height()};
    geometry_hash = hash_bytes(
      reinterpret_cast<const std::uint8_t *>(rect_values), sizeof(rect_values), geometry_hash);

    // Clip rather than shift — see QtWidgetCapture::popup_clipped(). The
    // intersection is computed explicitly instead of leaning on QPainter's own
    // clip, so "off the panel" is a decision this code makes and reports.
    const QRect visible = dest.intersected(panel_rect);
    if (visible != dest) {
      popup_clipped_ = true;
    }
    if (visible.isEmpty()) {
      continue;
    }

    const QSize device_size(dest.width(), dest.height());
    if (popup_image_.size() != device_size || popup_image_.format() != QImage::Format_RGBA8888) {
      popup_image_ = QImage(device_size, QImage::Format_RGBA8888);
      if (popup_image_.isNull()) {
        continue;
      }
    }
    // Same trick as the base widget: the ratio makes the popup paint its logical
    // geometry across the whole device-pixel scratch.
    popup_image_.setDevicePixelRatio(ratio);
    popup_image_.fill(Qt::transparent);
    popup->render(
      &popup_image_, QPoint(), QRegion(),
      QWidget::DrawWindowBackground | QWidget::DrawChildren);

    // The scratch now holds DEVICE pixels and the blit below is 1:1, so drop the
    // ratio first or QPainter would helpfully scale it back down again.
    popup_image_.setDevicePixelRatio(1.0);
    painter.drawImage(visible.topLeft(), popup_image_, visible.translated(-dest.topLeft()));
    ++composited_popup_count_;
  }

  return geometry_hash;
}

std::uint64_t
QtWidgetCapture::composite_region_sources(
  std::uint32_t buffer_width, std::uint32_t buffer_height, double ratio)
{
  substituted_region_count_ = 0;

  // Dead regions (Qt nulled the QPointer) are gone for good; drop them now
  // rather than re-checking a null every capture for the rest of the stream's
  // life.
  region_sources_.erase(
    std::remove_if(
      region_sources_.begin(), region_sources_.end(),
      [](const RegionSource & entry) {return entry.region.isNull();}),
    region_sources_.end());

  std::uint64_t hash = kOffsetBasis;
  if (widget_ == nullptr || image_.isNull() || region_sources_.empty()) {
    return hash;
  }

  // Same trick as composite_owned_popups: a shallow view over image_'s OWN
  // memory with no device pixel ratio, so every coordinate here is already a
  // device pixel.
  QImage view(
    image_.bits(), image_.width(), image_.height(), image_.bytesPerLine(),
    QImage::Format_RGBA8888);
  QPainter painter(&view);
  if (!painter.isActive()) {
    return hash;
  }

  const QRect panel_rect(0, 0, static_cast<int>(buffer_width), static_cast<int>(buffer_height));

  for (RegionSource & entry : region_sources_) {
    QWidget * region = entry.region.data();
    if (region == nullptr) {
      continue;
    }
    const bool is_widget_itself = (region == widget_);
    // The same ownership test the popup scan uses: is `region` widget_ itself,
    // or still reachable by walking parentWidget() up from it? While
    // tombstoned (panel_tombstone.hpp), widget_ points at an unrelated
    // placeholder tree, so a region registered against the real widget simply
    // stops matching here -- no substitution, no crash -- and matches again
    // the moment the stream is revived onto a tree it is really part of.
    if (!is_widget_itself && !panel_tree_owns_popup(widget_, region)) {
      continue;
    }
    if (!region->isVisible()) {
      continue;
    }

    const QPoint local = is_widget_itself ? QPoint(0, 0) : region->mapTo(widget_, QPoint());
    const QSize logical_size = is_widget_itself ? widget_->rect().size() : region->size();
    if (logical_size.isEmpty()) {
      continue;
    }

    const QRect dest(
      static_cast<int>(std::lround(local.x() * ratio)),
      static_cast<int>(std::lround(local.y() * ratio)),
      static_cast<int>(std::lround(logical_size.width() * ratio)),
      static_cast<int>(std::lround(logical_size.height() * ratio)));

    const std::int32_t rect_values[4] = {dest.x(), dest.y(), dest.width(), dest.height()};
    hash = hash_bytes(
      reinterpret_cast<const std::uint8_t *>(rect_values), sizeof(rect_values), hash);

    const QRect clipped = dest.intersected(panel_rect);
    if (clipped.isEmpty()) {
      continue;
    }

    // The region's own rectangle first, in black: a frame narrower than the
    // region (or none at all) must not leave the previous tick's pixels, or
    // whatever the ordinary widget render painted there, showing through.
    painter.fillRect(clipped, Qt::black);

    const std::shared_ptr<const QImage> frame = entry.source ? entry.source->latest() : nullptr;
    if (frame && !frame->isNull()) {
      const QRect fit = aspect_fit_rect(frame->size(), clipped);
      painter.drawImage(fit, *frame);
      const std::uint64_t sequence = entry.source->sequence();
      hash = hash_bytes(
        reinterpret_cast<const std::uint8_t *>(&sequence), sizeof(sequence), hash);
    } else {
      // No frame yet: the same "waiting" presentation VideoPanelWidget gives
      // (a dark grey fill, distinct from the letterbox black above, so the
      // state does not read as simply blank even where a platform rasterizes
      // no glyphs for the status text), so a region reads the same whether it
      // is a whole panel of its own or substituted here.
      painter.fillRect(clipped, QColor(0x22, 0x22, 0x22));
      const std::string status = entry.source ? entry.source->status() : std::string();
      const QString message = status.empty()
        ? QStringLiteral("Waiting for video...")
        : QString::fromStdString(status);
      painter.setPen(QColor(0xC0, 0xC0, 0xC0));
      painter.drawText(clipped, Qt::AlignCenter | Qt::TextWordWrap, message);
      // Hashed as text, not as pixels: the offscreen QPA plugin this suite
      // runs under rasterizes no glyphs, so a status change would otherwise be
      // invisible to the dirty check exactly like popup text would be.
      const QByteArray utf8 = message.toUtf8();
      hash = hash_bytes(
        reinterpret_cast<const std::uint8_t *>(utf8.constData()),
        static_cast<std::size_t>(utf8.size()), hash);
    }
    ++substituted_region_count_;
  }

  return hash;
}

qar_ar_core::PanelPixels
QtWidgetCapture::pixels() const
{
  qar_ar_core::PanelPixels out;
  if (image_.isNull()) {
    return out;
  }
  out.data = image_.constBits();
  out.width = static_cast<std::uint32_t>(image_.width());
  out.height = static_cast<std::uint32_t>(image_.height());
  out.stride_bytes = static_cast<std::uint32_t>(image_.bytesPerLine());
  return out;
}

}  // namespace qar_qt_streaming
