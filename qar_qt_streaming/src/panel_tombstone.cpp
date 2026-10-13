// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/panel_tombstone.hpp"

#include <QColor>
#include <QFont>
#include <QFrame>
#include <QPalette>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <string>

namespace qar_qt_streaming
{

const char * const kTombstoneAnonymousName = "A streamed window";
const char * const kTombstoneDefaultReason =
  "This window has been closed, so its picture is no longer live.";
const char * const kTombstoneDismissLine = "You may now close this panel in AR.";

namespace
{

/// Characters the fit has to respect. The headline is the window's NAME and
/// must not be broken across lines — a half-shown name is exactly the thing the
/// notice exists to communicate — so it drives the fit, counted at the larger
/// size it is set in. The body lines word-wrap, so they never force a shrink.
std::size_t
headline_fit_chars(const std::string & name)
{
  const std::string & shown = name.empty() ? std::string(kTombstoneAnonymousName) : name;
  return static_cast<std::size_t>(
    std::ceil(static_cast<double>(shown.size()) * kTombstoneHeadlineScale));
}

/// Paint @p widget's whole rectangle in @p hex (a `#rrggbb` string).
void
set_background(QWidget * widget, const std::string & hex)
{
  widget->setAutoFillBackground(true);
  QPalette palette = widget->palette();
  palette.setColor(QPalette::Window, QColor(QString::fromStdString(hex)));
  widget->setPalette(palette);
}

/// Set @p label's text colour.
void
set_foreground(QWidget * widget, const std::string & hex)
{
  QPalette palette = widget->palette();
  palette.setColor(QPalette::WindowText, QColor(QString::fromStdString(hex)));
  widget->setPalette(palette);
}

}  // namespace

double
tombstone_body_font_px(
  std::uint32_t logical_width, std::size_t longest_line_chars, const PanelTombstoneStyle & style)
{
  if (logical_width == 0 || longest_line_chars == 0) {
    return style.body_font_px;  // nothing to fit against
  }
  const double available = static_cast<double>(logical_width) * kTombstoneWidthMargin;
  const double fits =
    available / (static_cast<double>(longest_line_chars) * kTombstoneAvgAdvanceRatio);
  // Never larger than the panel's own body size: the panel's metres were
  // derived for that size, and growing the text would only cost lines.
  double px = std::min(fits, style.body_font_px);
  // ...and never smaller than the reading floor. Below it the notice is present
  // but not readable, which serves nobody; a clipped-but-legible notice does.
  px = std::max(px, style.minimum_font_px);
  return px;
}

bool
tombstone_is_clipped(
  std::uint32_t logical_width, std::size_t longest_line_chars, const PanelTombstoneStyle & style)
{
  if (logical_width == 0 || longest_line_chars == 0) {
    return false;
  }
  const double available = static_cast<double>(logical_width) * kTombstoneWidthMargin;
  const double needed = static_cast<double>(longest_line_chars) * style.minimum_font_px *
    kTombstoneAvgAdvanceRatio;
  return needed > available;
}

QWidget *
make_panel_tombstone_widget(
  const PanelTombstoneText & text,
  const PanelTombstoneStyle & style,
  std::uint32_t logical_width,
  std::uint32_t logical_height)
{
  const double body_px =
    tombstone_body_font_px(logical_width, headline_fit_chars(text.panel_name), style);
  const int body_pt = std::max(1, static_cast<int>(std::lround(body_px)));
  const int headline_pt =
    std::max(body_pt, static_cast<int>(std::lround(body_px * kTombstoneHeadlineScale)));

  auto * root = new QWidget();
  // Parentless and never mapped: WA_DontShowOnScreen is what lets show() below
  // make the children isVisible() — which QWidget::render() requires — without
  // a window appearing on the operator's desktop behind the headset.
  root->setAttribute(Qt::WA_DontShowOnScreen, true);
  root->setObjectName(QStringLiteral("qarPanelTombstone"));
  // Palettes, not a stylesheet. A stylesheet `background` on a plain QWidget is
  // only honoured through WA_StyledBackground and the QStyleSheetStyle, which
  // makes what reaches the buffer depend on the host's active style; a palette
  // role is painted by QWidget::render itself, identically on every platform
  // plugin — including the offscreen one the tests run under. The AR look here
  // is three colours, so it is not worth paying for a style engine to get them.
  set_background(root, style.background);

  auto * layout = new QVBoxLayout(root);
  const int margin = std::max(body_pt, 8);
  layout->setContentsMargins(margin, margin, margin, margin);
  layout->setSpacing(std::max(body_pt / 2, 4));
  layout->addStretch(1);

  QFont headline_font = root->font();
  headline_font.setPixelSize(headline_pt);
  headline_font.setBold(true);
  QFont body_font = root->font();
  body_font.setPixelSize(body_pt);

  auto * name = new QLabel(
    QString::fromStdString(text.panel_name.empty() ? std::string(kTombstoneAnonymousName)
                                                   : text.panel_name),
    root);
  name->setObjectName(QStringLiteral("tombstoneName"));
  set_foreground(name, style.text);
  name->setFont(headline_font);
  name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  // Elide is not available on a plain QLabel, and wrapping a NAME would hide
  // which window this is about at exactly the moment it matters. The name is
  // what tombstone_body_font_px sized the notice around; if it still does not
  // fit, the stream reports it as clipped.
  name->setWordWrap(false);
  layout->addWidget(name);

  auto * rule = new QFrame(root);
  rule->setObjectName(QStringLiteral("tombstoneRule"));
  rule->setFrameShape(QFrame::NoFrame);
  rule->setFixedHeight(std::max(2, body_pt / 5));
  set_background(rule, style.accent);
  layout->addWidget(rule);

  auto * reason = new QLabel(
    QString::fromStdString(text.reason.empty() ? std::string(kTombstoneDefaultReason)
                                               : text.reason),
    root);
  reason->setObjectName(QStringLiteral("tombstoneReason"));
  set_foreground(reason, style.text_muted);
  reason->setFont(body_font);
  reason->setWordWrap(true);
  layout->addWidget(reason);

  auto * dismiss = new QLabel(QString::fromLatin1(kTombstoneDismissLine), root);
  dismiss->setObjectName(QStringLiteral("tombstoneDismiss"));
  set_foreground(dismiss, style.text);
  dismiss->setFont(body_font);
  dismiss->setWordWrap(true);
  layout->addWidget(dismiss);

  layout->addStretch(1);

  // Exactly the size the retired widget had, so the derived buffer, the panel's
  // metres and its content scale are all unchanged — a tombstone must never be
  // the reason a sender is rebuilt.
  root->resize(
    static_cast<int>(logical_width > 0 ? logical_width : 1),
    static_cast<int>(logical_height > 0 ? logical_height : 1));
  root->show();
  root->ensurePolished();
  if (root->layout() != nullptr) {
    root->layout()->activate();
  }
  return root;
}

}  // namespace qar_qt_streaming
