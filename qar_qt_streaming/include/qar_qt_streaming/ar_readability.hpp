// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__AR_READABILITY_HPP_
#define QAR_QT_STREAMING__AR_READABILITY_HPP_

/// @file ar_readability.hpp
/// @brief Sizes a streamed panel so its text is readable in AR. Pure math,
/// Qt-free, in the style of panel_geometry.hpp.
///
/// panel_geometry.hpp answers "how many buffer pixels and how many metres does
/// THIS widget get". This header answers the question that comes before it:
/// how many metres should one logical pixel be, and how many buffer pixels
/// should back it, so that the panel's text is comfortably readable from where
/// the AR user stands. Both answers are ANGULAR: legibility is a property of
/// the size of text on the retina, not on the panel, so a panel meant to be
/// read from 2 m has to be physically larger than one read at arm's length —
/// and needs no more pixels, because it covers the same angle.
///
/// ## The constants, with the arithmetic
///
/// A length L at distance D subtends L / D radians (small-angle form; the exact
/// 2·atan(L / 2D) differs by < 0.01 % at these sizes), and
///
///   1 arc-minute = 1/60 degree = pi / 10800 rad = 2.90888e-4 rad.
///
/// * **Acuity ~ 1 arc-minute.** The 20/20 Snellen criterion: a letter 5 arc-min
///   tall with 1-arc-min strokes is the limit of RECOGNITION for normal vision.
///   It is a floor for what can be resolved at all, not a target for reading.
/// * **Comfortable reading ~ 20-30 arc-min of character (cap) height.** ISO
///   9241-303 gives 16 arc-min as the minimum and 20-22 as the recommended
///   character height for sustained reading; the upper end of the range is what
///   large-print material uses. The default target is 22 arc-min.
/// * **Cap height ~ 0.70 em.** Common UI faces put their capitals at about 70 %
///   of the font size (Segoe UI 0.70, Arial 0.716, Helvetica 0.717), so a
///   12 px body font has ~8.4 px capitals.
/// * **Device resolution ~ 45 pixels per degree.** Enterprise AR headsets sit
///   near 45-47 ppd (HoloLens 2 ~47), so one device pixel is 60/45 = 1.33
///   arc-min. Consumer VR is coarser (~25 ppd); the field is configurable.
///
/// ## Worked example (the arm's-length preset)
///
///   arcmin per logical px = 22 / (0.70 × 12)             = 2.619
///   metres per logical px = 0.65 m × 2.619 × 2.90888e-4  = 4.95e-4 m (0.50 mm)
///   render scale          = ceil_half(2.619 / 1.333)     = ceil_half(1.96) = 2.0
///   min legible font      = 12 px × 16 / 22              = 8.7 px
///
/// So a 500 px-wide panel comes out 0.25 m wide at 0.65 m, is captured at 2×
/// so each device pixel sees at least one source pixel, and anything under
/// ~9 px in the UI is below the ISO minimum at that distance.
///
/// Note that the render scale does NOT depend on the viewing distance: the
/// profile fixes the angular size of the text, so the number of source pixels
/// needed per device pixel is the same at 0.4 m and at 2 m. Only the physical
/// size changes.

#include <string>

namespace qar_qt_streaming
{

/// @brief pi / 10800: radians per arc-minute.
constexpr double kRadiansPerArcmin = 2.908882086657216e-4;

/// @brief 20/20 acuity: the finest stroke a normal eye resolves, in arc-minutes.
constexpr double kAcuityArcmin = 1.0;

/// @brief Strokes per capital letter in the Snellen model (a 5×5 grid); it
/// turns a stroke-resolution floor into a cap-height floor.
constexpr double kSnellenStrokesPerCap = 5.0;

/// @brief ISO 9241-303 minimum character height for reading, in arc-minutes.
constexpr double kMinimumReadingArcmin = 16.0;

/// @brief ISO 9241-303 recommended character height (20-22), in arc-minutes.
constexpr double kComfortableReadingArcmin = 22.0;

/// @brief Cap height as a fraction of the font size for common UI faces.
constexpr double kDefaultCapHeightRatio = 0.70;

/// @brief Qt's default body font on a 96 dpi desktop: 9 pt = 12 logical px.
constexpr double kDefaultBodyFontPx = 12.0;

/// @brief Angular resolution of a typical enterprise AR headset.
constexpr double kDefaultDevicePixelsPerDegree = 45.0;

/// @brief The AR viewing condition a panel is sized for.
struct ReadabilityProfile
{
  /// Expected eye-to-panel distance, metres.
  double viewing_distance_m = 0.65;

  /// Target angular CAP height of body text, arc-minutes. See the file comment
  /// for why 22 (ISO 9241-303's recommended range is 20-22).
  double text_height_arcmin = kComfortableReadingArcmin;

  /// The AR device's angular resolution, pixels per degree.
  double device_pixels_per_degree = kDefaultDevicePixelsPerDegree;

  /// Source pixels wanted per device pixel. 1.0 matches the device's pixel
  /// density, which is enough to keep every stroke resolvable; 1.5-2.0 buys
  /// sharper edges after the encoder's and the headset's resampling, at a
  /// quadratic bandwidth cost (2.0 => four times the pixels of 1.0).
  double supersample = 1.0;

  /// Logical pixel size of the UI's body font (what the widgets actually use).
  double body_font_px = kDefaultBodyFontPx;

  /// Cap height / font size for the UI's typeface.
  double cap_height_ratio = kDefaultCapHeightRatio;

  /// Upper bound on the recommended render scale, so an extreme profile does
  /// not ask for a buffer no encoder will take. panel_geometry clamps again
  /// against max_buffer_dimension for the concrete widget.
  double max_render_scale = 4.0;
};

/// @brief Named viewing conditions.
enum class ReadabilityPreset
{
  Near,        ///< Handheld / close inspection: 0.40 m.
  ArmsLength,  ///< A panel the wearer reaches out to touch: 0.65 m.
  WallMounted  ///< A status board across the room: 2.0 m.
};

/// @brief The profile for a preset. Only the distance differs between them;
/// everything angular is shared, which is the point.
ReadabilityProfile readability_preset(ReadabilityPreset preset);

/// @brief What panel_geometry needs, derived from a profile.
struct ReadabilityRecommendation
{
  /// Feed to PanelGeometryInput::meters_per_logical_px.
  double meters_per_logical_px = 0.0;

  /// Feed to PanelGeometryInput::render_scale: buffer pixels per logical
  /// pixel on top of the display's own device pixel ratio. Quantised UP to a
  /// half step (1.0, 1.5, 2.0, ...) and clamped to [1, max_render_scale].
  double render_scale = 1.0;

  /// Smallest logical font size still at the reading minimum from this
  /// distance. Text below it is resolvable but not comfortably readable; a
  /// host can enforce it with a stylesheet or a font override.
  double min_legible_font_px = 0.0;

  /// Angular size of one logical pixel, arc-minutes (for diagnostics).
  double arcmin_per_logical_px = 0.0;

  /// Angular size of one device pixel, arc-minutes (for diagnostics).
  double arcmin_per_device_px = 0.0;
};

/// @brief Derive the sizing recommendation for a profile.
/// @param[out] out filled only on success.
/// @param[out] out_error human-readable reason on failure.
/// @return false if any profile field is non-finite or non-positive.
bool recommend_readability(
  const ReadabilityProfile & profile, ReadabilityRecommendation & out, std::string & out_error);

/// @brief Angle subtended by @p length_m seen from @p distance_m, arc-minutes.
double subtended_arcmin(double length_m, double distance_m);

/// @brief Round @p value up to the next multiple of 0.5.
double ceil_to_half(double value);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__AR_READABILITY_HPP_
