// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__PANEL_GEOMETRY_HPP_
#define QAR_QT_STREAMING__PANEL_GEOMETRY_HPP_

/// @file panel_geometry.hpp
/// @brief Reconciles Qt's logical pixels + devicePixelRatio with QarOS's
/// metres + content_scale. Pure math, Qt-free, so it is unit-testable without a
/// QApplication.
///
/// Three quantities must agree or AR panels come out blurry, the wrong
/// physical size, or hover/click land off the content they are visibly over:
///
///   * Qt LOGICAL size      — QWidget::size(), device-independent pixels
///   * Qt device pixel ratio — QWidget::devicePixelRatioF() (1.0/1.25/1.5/2.0…)
///   * QarGuiPanelSize      — width_meters, height_meters, content_scale
///
/// The rule this header implements:
/// @code
///   ratio                 = dpr * render_scale
///   buffer_w              = align_up(round(logical_w * ratio), alignment)
///   width_meters          = logical_w * meters_per_logical_px
///   content_scale         = 1.0     // sent to QarOS verbatim - see below
///   effective_pixel_ratio = ratio   // reported for diagnostics, never sent
/// @endcode
///
/// Physical AR size tracks LOGICAL pixels, never buffer pixels. Otherwise a
/// panel would physically double in size the moment the operator dragged the
/// host window onto a 200 % display — backwards: a HiDPI display should make a panel
/// SHARPER, not BIGGER.
///
/// `render_scale` (see ar_readability.hpp) is a second, deliberate multiplier
/// on top of the display's ratio: it supersamples the capture so small text
/// survives the encoder's and the headset's resampling. It rides the same
/// invariant — more pixels behind the same physical size — which is why it
/// folds into `effective_pixel_ratio` and never into the metres or
/// content_scale.
///
/// `content_scale` is NOT a resolution knob: the SDK header
/// (third_party/qar/include/qar_streaming.h, `QarGuiPanelSize::content_scale`)
/// defines it as a ZOOM — "1.0 fits the content across the full panel surface,
/// 2.0 shows it at twice the size so only half of it fits, and 0.5 shows it at
/// half size" — and a contact is mapped back to content pixels through it.
/// Sending the supersample ratio through it would be a zoom > 1 that crops the
/// far edge, and contacts would reach only the part that is shown. QarOS
/// already learns the raster's resolution from the video image itself, so this
/// field is always sent as 1.0 (fit the raster to the panel); the supersample
/// ratio travels separately, in `effective_pixel_ratio`, for diagnostics only.
/// See qar_ogre_adapter docs/QAROS_INTEGRATION_NOTES.md, "Panel pixel
/// coordinates and content_scale".
///
/// Buffer dimensions are aligned UP to an even number of pixels because the
/// shipped QarOS runtime encodes panel content as video (GStreamer/WebRTC) and
/// odd dimensions are widely rejected by such encoders. The few slack pixels are
/// left as background; the widget is rendered top-left anchored.

#include <cstdint>
#include <string>

namespace qar_qt_streaming
{

/// @brief Default physical size of one Qt logical pixel in the AR room.
///
/// 0.0012 m/px makes a 500 px-wide panel 0.60 m across, which is what an
/// operator standing at arm's length can read without leaning in.
///
/// It is a PHYSICAL size only: the buffer size, the device-pixel-ratio
/// invariant and content_scale are untouched, so a larger value spreads the
/// same pixels over more metres and therefore a larger angle. Bigger text out
/// of the same buffer, at fewer source pixels per degree — which is the trade a
/// host can decline per panel, or replace outright with ar_readability.hpp's
/// derivation from an explicit viewing distance.
constexpr double kDefaultMetersPerLogicalPx = 0.0012;

/// @brief Encoder-friendly pixel alignment for the streamed buffer.
constexpr std::uint32_t kDefaultBufferAlignment = 2;

/// @brief Upper bound on either buffer dimension, to keep one oversized dock
/// widget from asking QarOS for a texture no encoder will take.
constexpr std::uint32_t kDefaultMaxBufferDimension = 4096;

/// @brief What the caller knows: Qt's view of the widget.
struct PanelGeometryInput
{
  std::uint32_t logical_width = 0;   ///< QWidget::width(), logical px
  std::uint32_t logical_height = 0;  ///< QWidget::height(), logical px
  double device_pixel_ratio = 1.0;   ///< QWidget::devicePixelRatioF()
  double meters_per_logical_px = kDefaultMetersPerLogicalPx;
  /// Supersampling on top of the display's ratio (ar_readability.hpp).
  /// 1.0 = none. Changes buffer pixels and effective_pixel_ratio, never the
  /// metres or content_scale (see @ref PanelGeometry::content_scale).
  double render_scale = 1.0;
  std::uint32_t buffer_alignment = kDefaultBufferAlignment;
  std::uint32_t max_buffer_dimension = kDefaultMaxBufferDimension;
};

/// @brief What QarOS needs: a pixel buffer size and a physical panel size.
struct PanelGeometry
{
  std::uint32_t buffer_width = 0;
  std::uint32_t buffer_height = 0;

  /// @brief How much of that buffer the widget actually covers, in buffer px.
  ///
  /// `buffer_width`/`buffer_height` are rounded UP to the encoder's alignment,
  /// and the widget paints only `round(logical * ratio)` of them — the slack on
  /// the right and bottom edges is transparent padding (see qt_widget_capture).
  /// Anything converting a position in the streamed image back to a widget
  /// coordinate must normalise against THIS, not the padded buffer; at the
  /// default 2-pixel alignment the difference is a pixel at the far edge, which
  /// is exactly the sort of error nobody finds by looking. Pointer input
  /// travels in that direction.
  std::uint32_t content_width = 0;
  std::uint32_t content_height = 0;

  float width_meters = 0.0f;
  float height_meters = 0.0f;

  /// @brief What is SENT to QarOS as QarGuiPanelSize::content_scale. Always
  /// 1.0 - it is a ZOOM in the SDK header, not a resolution, and QarOS
  /// already learns the raster's resolution from the video image itself (see
  /// the file comment). The supersample ratio is @ref effective_pixel_ratio,
  /// never this field.
  float content_scale = 1.0f;

  /// @brief Effective ratio actually used, after clamping to
  /// @ref PanelGeometryInput::max_buffer_dimension. Equals the requested dpr
  /// unless the widget was too large, in which case the buffer is scaled down
  /// and this reports what the pixels really represent. This is the
  /// supersample ratio; it is diagnostics-only — @ref content_scale is what
  /// QarOS is actually sent.
  double effective_pixel_ratio = 1.0;
};

/// @brief Round @p value up to the next multiple of @p alignment (>= 1).
std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment);

/// @brief Compute the buffer size + physical size for a widget.
/// @param[out] out_geometry filled only on success.
/// @param[out] out_error human-readable reason on failure.
/// @return false for a zero-sized widget or a non-finite/non-positive ratio.
bool compute_panel_geometry(
  const PanelGeometryInput & input, PanelGeometry & out_geometry, std::string & out_error);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__PANEL_GEOMETRY_HPP_
