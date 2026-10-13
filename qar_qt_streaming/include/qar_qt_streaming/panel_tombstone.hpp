// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__PANEL_TOMBSTONE_HPP_
#define QAR_QT_STREAMING__PANEL_TOMBSTONE_HPP_

/// @file panel_tombstone.hpp
/// @brief The notice a panel shows once its source window is gone.
///
/// ## Why a tombstone exists at all
///
/// A QarOS runtime that does not survive destroying a video sender
/// mid-session needs the sender kept alive: destroying one while a receiver is
/// still attached puts that receiver into `reconnecting`, and a reconnecting
/// receiver raises 603 out of the QarOS mixer's frame callback — which aborts
/// the whole callback and starves *every other* stream the mixer serves, the
/// stereo 3D scene included. The current runtime does survive it, so
/// PanelCloseMode::Destroy is the default; PanelCloseMode::Notice
/// (panel_close_mode.hpp) is the escape hatch for a runtime that does not, and
/// this header is that mode's notice. See qar_ogre_adapter
/// docs/QAROS_INTEGRATION_NOTES.md, "Teardown and destroy order".
///
/// So when the streamed widget closes in that mode, the stream
/// must keep running. What it streams instead is this: a whole-panel notice
/// saying the source window has closed and the operator may now close the panel
/// in AR. Dismissal is theirs — nothing on this side closes the panel.
///
/// ## Why an owned QWidget and not a drawing routine
///
/// The stream already knows how to capture a QWidget: render into the reused
/// RGBA buffer, hash it, submit it when it changes, at the panel's derived
/// device pixel ratio. Swapping the *widget* for a placeholder we own reuses
/// every one of those steps unchanged, renders real text in production (the
/// offscreen QPA plugin used by the test suite rasterises no glyphs, but the
/// platform plugin of a desktop application does), and adds no drawing code at
/// all. The alternative — painting a notice into the buffer by hand — would
/// duplicate the capture path's geometry, DPR and dirty-check handling for a
/// picture made of three lines of text.
///
/// The placeholder is parentless and carries `Qt::WA_DontShowOnScreen`, so
/// `show()` makes it *logically* visible — which is what `QWidget::render()`
/// requires of children — without the platform ever mapping a window onto the
/// operator's desktop.
///
/// ## Sizing: the panel's readability is inherited, not recomputed
///
/// A tombstone replaces a widget whose panel is already sized: the sender's
/// buffer layout, the panel's metres and its content scale are all fixed and
/// must not change (a new layout means a new sender, which is the one thing
/// this whole mechanism exists to avoid). The notice is therefore laid out in
/// the SAME logical pixels the source widget used, which means the panel's
/// metres-per-logical-pixel — chosen by ar_readability.hpp so that body text
/// subtends a comfortable ~22 arc-minutes — carries over exactly. Text at the
/// panel's body size reads here precisely as it read there.
///
/// The one thing that does have to be computed is what happens when the notice
/// is longer than the panel is wide. @ref tombstone_body_font_px shrinks it to
/// fit, and refuses to go below the ISO 9241-303 minimum: past that the notice
/// would be present but unreadable, which is worse than a clipped one that can
/// still be read.

#include <cstdint>
#include <string>

class QWidget;

namespace qar_qt_streaming
{

/// @brief Mean glyph advance as a fraction of the font's pixel size.
///
/// Measured across the UI faces this runs against (Segoe UI, Roboto, DejaVu
/// Sans) for mixed-case sentence text. Used instead of `QFontMetrics` so the
/// fit arithmetic is pure, Qt-free and exactly testable — and so it gives the
/// same answer on a host whose platform plugin has no font engine at all.
constexpr double kTombstoneAvgAdvanceRatio = 0.55;

/// @brief Fraction of the panel width the notice's longest line may occupy,
/// leaving a margin so the text does not run into the panel edge.
constexpr double kTombstoneWidthMargin = 0.86;

/// @brief The headline ("the window that closed") relative to the body size.
constexpr double kTombstoneHeadlineScale = 1.45;

/// @brief How the notice is drawn. The defaults are the colours of a dark AR
/// theme, given by value: qar_qt_streaming links Qt and nothing else, and a
/// three-colour notice is not worth a dependency on a theme library.
struct PanelTombstoneStyle
{
  /// Logical pixel size of the panel's body text — the same number
  /// ar_readability.hpp sized the panel's metres against. The notice reads at
  /// this size wherever it fits.
  double body_font_px = 12.0;

  /// ISO 9241-303's reading minimum expressed at this panel's scale
  /// (`ReadabilityRecommendation::minimum_legible_font_px`). The fit never
  /// shrinks text below it.
  double minimum_font_px = 8.7;

  /// `#0F141A` — the panel background.
  std::string background = "#0F141A";
  /// `#F2F5F7` — the headline and the dismissal instruction.
  std::string text = "#F2F5F7";
  /// `#A9B4BF` — the explanatory line.
  std::string text_muted = "#A9B4BF";
  /// `#FFC857` — the rule under the headline; this panel is not an error, it is
  /// stale.
  std::string accent = "#FFC857";
};

/// @brief What the notice says, and about which window.
struct PanelTombstoneText
{
  /// The window that closed, in whatever name is known: the panel key, or a
  /// dialog's title. Empty when nothing is known — the notice then omits the
  /// name rather than inventing one.
  ///
  /// Naming it is not decoration. An operator with two docks and two dialogs in
  /// the room sees four rectangles; a notice that does not say which one died
  /// cannot be acted on.
  std::string panel_name;

  /// Why it closed, in one short phrase, e.g. "the window was closed".
  /// Empty falls back to @ref kTombstoneDefaultReason.
  std::string reason;
};

/// @brief The headline when no name is known.
extern const char * const kTombstoneAnonymousName;
/// @brief The default @ref PanelTombstoneText::reason.
extern const char * const kTombstoneDefaultReason;
/// @brief The dismissal line. The whole point of the notice: closing the panel
/// is the operator's to do, and nothing on this side will do it.
extern const char * const kTombstoneDismissLine;

/// @brief Body font size, in logical pixels, for a notice in a panel this wide.
///
/// Pure arithmetic, deliberately: the fit has to be decidable in a unit test on
/// a host with no font engine (see @ref kTombstoneAvgAdvanceRatio).
///
/// @param logical_width       Panel width in Qt logical pixels — the width the
///                            live sender's buffer was built for.
/// @param longest_line_chars  Characters in the longest line the notice must
///                            show on one line. The headline counts at
///                            @ref kTombstoneHeadlineScale, since it is set
///                            larger; pass its length already scaled if it is
///                            the longest.
/// @return @ref PanelTombstoneStyle::body_font_px where it fits, shrunk to fit
///         where it does not, and never below
///         @ref PanelTombstoneStyle::minimum_font_px. A zero width or zero
///         length yields the body size unchanged (there is nothing to fit).
double tombstone_body_font_px(
  std::uint32_t logical_width,
  std::size_t longest_line_chars,
  const PanelTombstoneStyle & style);

/// @brief True when the notice had to be clipped: it does not fit @p
/// logical_width even at @ref PanelTombstoneStyle::minimum_font_px.
///
/// Reported through diagnostics rather than silently accepted — a notice the
/// operator can only half-read is still better than a frozen last frame, but
/// they should be able to find out that the panel was too narrow for it.
bool tombstone_is_clipped(
  std::uint32_t logical_width,
  std::size_t longest_line_chars,
  const PanelTombstoneStyle & style);

/// @brief Build the placeholder widget for a closed source window.
///
/// The returned widget is:
///  * **owned by the caller** — it is parentless, and the caller (one
///    QtPanelStream) deletes it;
///  * sized to exactly @p logical_width x @p logical_height, so the capture
///    keeps producing the buffer the live sender was built for and no sender
///    rebuild is ever provoked by a tombstone;
///  * already `show()`n behind `Qt::WA_DontShowOnScreen`, laid out and
///    polished, so the first capture after the swap draws real content;
///  * input-inert: it has no interactive children, and the stream drops its
///    injector when it tombstones.
///
/// @return null only if Qt could not allocate it.
QWidget * make_panel_tombstone_widget(
  const PanelTombstoneText & text,
  const PanelTombstoneStyle & style,
  std::uint32_t logical_width,
  std::uint32_t logical_height);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__PANEL_TOMBSTONE_HPP_
