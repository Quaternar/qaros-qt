// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__ENLARGED_CONTROLS_STYLE_HPP_
#define QAR_QT_STREAMING__ENLARGED_CONTROLS_STYLE_HPP_

/// @file enlarged_controls_style.hpp
/// @brief A QStyle that draws the controls of a widget tree a factor larger than the text.
///
/// A panel that is read and touched in AR needs bigger targets, but not bigger text: the text
/// is already sized for the AR user by the metres one logical pixel covers (ar_readability.hpp).
/// Scaling the whole widget enlarges both. EnlargedControlsStyle enlarges only the controls
/// (check and radio indicators, the buttons of a spin box, the arrows of a combo box, slider,
/// scroll bar and tree), so a copy of a panel that uses it shows the same text at the same size
/// next to controls that are `factor` times larger. apply_style_to_tree gives the style to every
/// widget of a tree, because QWidget::setStyle reaches the one widget only.

#include <QProxyStyle>
#include <QString>

#include <memory>

class QWidget;

namespace qar_qt_streaming
{

/// The least and the most factor; a value outside is clamped, a value that is not finite is 2.
inline constexpr double kEnlargedControlsMinFactor = 1.0;
inline constexpr double kEnlargedControlsMaxFactor = 4.0;
inline constexpr double kEnlargedControlsDefaultFactor = 2.0;

/// The base style when the caller names none, or names one QStyleFactory does not know.
inline constexpr const char * kEnlargedControlsDefaultBase = "fusion";

/// @brief The factor the style uses for @p wanted: clamped to the limits above; 2 when it is
/// not a finite number.
double enlarged_controls_factor(double wanted);

/// @brief Draws the controls of the widgets that use it `factor` times their size, the text
/// unchanged.
///
/// The controls are the check and radio indicators (also those an item view draws), the buttons
/// of a spin box, the arrow of a combo box, slider and scroll bar, the expand arrows of a tree,
/// the arrows of headers and tool buttons. Each of them is drawn by a second, unproxied
/// instance of the base style at its rectangle divided by the factor, on the painter scaled by
/// the factor; the sub-control rectangles and the hit test come from the same instance and are
/// scaled the same way, so what is drawn, where the widget lays out its text and where a press
/// lands agree. Text is drawn by the widgets with their own font, which the style does not touch.
/// Icon sizes are not scaled: icons belong with the text.
class EnlargedControlsStyle : public QProxyStyle
{
public:
  /// @param factor clamped by enlarged_controls_factor().
  /// @param base_key the QStyleFactory key of the style that draws the controls; an empty or
  ///        unknown key gives kEnlargedControlsDefaultBase.
  explicit EnlargedControlsStyle(
    double factor = kEnlargedControlsDefaultFactor,
    const QString & base_key = QString::fromLatin1(kEnlargedControlsDefaultBase));
  ~EnlargedControlsStyle() override;

  /// The factor in use (after the clamp).
  double factor() const;

  /// The key of the base style in use (after the fallback to kEnlargedControlsDefaultBase).
  QString base_key() const;

  int pixelMetric(
    PixelMetric metric, const QStyleOption * option = nullptr,
    const QWidget * widget = nullptr) const override;
  void drawPrimitive(
    PrimitiveElement element, const QStyleOption * option, QPainter * painter,
    const QWidget * widget = nullptr) const override;
  void drawComplexControl(
    ComplexControl control, const QStyleOptionComplex * option, QPainter * painter,
    const QWidget * widget = nullptr) const override;
  QRect subControlRect(
    ComplexControl control, const QStyleOptionComplex * option, SubControl sub,
    const QWidget * widget = nullptr) const override;
  SubControl hitTestComplexControl(
    ComplexControl control, const QStyleOptionComplex * option, const QPoint & pos,
    const QWidget * widget = nullptr) const override;
  QSize sizeFromContents(
    ContentsType type, const QStyleOption * option, const QSize & size,
    const QWidget * widget = nullptr) const override;

  using QProxyStyle::polish;
  using QProxyStyle::unpolish;
  /// Makes the rows of a combo box popup as tall as the combo box, gives the label of a form row
  /// the height of the row with its text centred, and keeps an item view that shows no horizontal
  /// scroll bar at horizontal scroll value 0. unpolish() puts each of them back.
  void polish(QWidget * widget) override;
  void unpolish(QWidget * widget) override;

  /// Whether pixelMetric() multiplies @p metric by the factor.
  static bool scales_metric(PixelMetric metric);

  /// Whether drawPrimitive() draws @p element at the factor.
  static bool scales_primitive(PrimitiveElement element);

  /// Whether drawComplexControl(), subControlRect() and hitTestComplexControl() work at the
  /// factor for @p control.
  static bool scales_complex_control(ComplexControl control);

private:
  /// Fills the sub-control the pointer is on, or that is pressed, of a spin box or combo box.
  void paint_active_part(
    ComplexControl control, const QStyleOptionComplex * option, QPainter * painter,
    const QWidget * widget) const;

  std::unique_ptr<QStyle> plain_;  ///< same key, not proxied: never calls back into this style
  double factor_;
  QString base_key_;
};

/// The object name of the spreader apply_style_to_tree() makes, so remove_style_from_tree()
/// finds it.
inline constexpr const char * kStyleSpreaderObjectName = "qar_style_spreader";

/// @brief Gives @p style to @p root, to every widget below it and to every widget made or
/// re-parented below it later (QEvent::ChildAdded on each widget of the tree). QWidget::setStyle
/// alone reaches only the one widget. The spreader is a child of @p root and ends with it; the
/// style is NOT owned and must outlive every widget of the tree. A tree that has a spreader
/// already gets the new one in place of it. Qt GUI thread only.
/// @return the number of widgets that were given the style (0 for a null argument).
int apply_style_to_tree(QWidget * root, QStyle * style);

/// @brief Takes the style of apply_style_to_tree off every widget below @p root again (they use
/// the application's style) and removes the spreader. A tree without a spreader is left alone.
/// @return the number of widgets changed.
int remove_style_from_tree(QWidget * root);

/// @brief The enlarged-controls style @p widget is drawn with, or null when it uses any other
/// style (the application's, say, or none given by apply_style_to_tree). A window below a tree
/// that was given a style has it as well, because the spreader reaches the windows made below the
/// root too; this is how a host tells such a window from one that is not drawn with the style.
const EnlargedControlsStyle * enlarged_controls_style_of(const QWidget * widget);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__ENLARGED_CONTROLS_STYLE_HPP_
