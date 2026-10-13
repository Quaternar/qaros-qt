// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/offscreen_panel.hpp"

#include "qar_qt_streaming/offscreen_popup_guard.hpp"

#include <QGuiApplication>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QWidget>

#include <algorithm>
#include <cstddef>
#include <string>

namespace qar_qt_streaming
{
namespace
{

/// Digits beyond this stop accumulating, so an absurdly long number saturates
/// instead of overflowing; it is far above kMaxOffscreenSide and still fits an int.
constexpr long long kSaturate = 1000000000LL;

bool
is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

bool
is_digit(char c)
{
  return c >= '0' && c <= '9';
}

void
skip_spaces(const std::string & text, std::size_t & pos)
{
  while (pos < text.size() && is_space(text[pos])) {
    ++pos;
  }
}

/// Read one unsigned decimal number at @p pos, saturating at kSaturate.
/// @return false (leaving @p pos where it was) if there is no digit there.
bool
read_number(const std::string & text, std::size_t & pos, long long & out)
{
  const std::size_t begin = pos;
  long long value = 0;
  while (pos < text.size() && is_digit(text[pos])) {
    if (value < kSaturate) {
      value = value * 10 + (text[pos] - '0');
    }
    ++pos;
  }
  if (pos == begin) {
    return false;
  }
  out = std::min(value, kSaturate);
  return true;
}

std::string
size_text(int width, int height)
{
  return std::to_string(width) + "x" + std::to_string(height);
}

}  // namespace

int
clamp_offscreen_side(int side)
{
  return std::clamp(side, kMinOffscreenSide, kMaxOffscreenSide);
}

OffscreenSizeChoice
parse_offscreen_size(
  const char * value, int default_width, int default_height, const char * source)
{
  OffscreenSizeChoice choice;
  choice.width = clamp_offscreen_side(default_width);
  choice.height = clamp_offscreen_side(default_height);
  if (value == nullptr) {
    return choice;
  }

  const std::string raw(value);
  std::size_t first = 0;
  std::size_t last = raw.size();
  while (first < last && is_space(raw[first])) {
    ++first;
  }
  while (last > first && is_space(raw[last - 1])) {
    --last;
  }
  if (first == last) {
    return choice;  // set to nothing is the same as not set
  }
  const std::string text = raw.substr(first, last - first);
  const std::string name = (source != nullptr && source[0] != '\0') ? source : "size";

  // NUMBER [spaces] ('x' | 'X') [spaces] NUMBER, and nothing else.
  std::size_t pos = 0;
  long long requested_width = 0;
  long long requested_height = 0;
  bool well_formed = read_number(text, pos, requested_width);
  skip_spaces(text, pos);
  if (well_formed) {
    well_formed = pos < text.size() && (text[pos] == 'x' || text[pos] == 'X');
    if (well_formed) {
      ++pos;
      skip_spaces(text, pos);
      well_formed = read_number(text, pos, requested_height);
    }
  }
  if (well_formed) {
    well_formed = pos == text.size();
  }
  if (!well_formed) {
    choice.warning = name + "='" + raw +
      "' is not a size (expected WIDTHxHEIGHT in pixels, e.g. " +
      size_text(kDefaultOffscreenWidth, kDefaultOffscreenHeight) +
      "); using the default " + size_text(choice.width, choice.height) + ".";
    return choice;
  }

  choice.explicitly_chosen = true;
  choice.width = clamp_offscreen_side(static_cast<int>(requested_width));
  choice.height = clamp_offscreen_side(static_cast<int>(requested_height));
  choice.clamped = choice.width != requested_width || choice.height != requested_height;
  if (choice.clamped) {
    choice.warning = name + "='" + raw + "' has a side outside " +
      std::to_string(kMinOffscreenSide) + ".." + std::to_string(kMaxOffscreenSide) +
      " pixels; using " + size_text(choice.width, choice.height) + ".";
  }
  return choice;
}

bool
show_offscreen(QWidget * widget, int width, int height)
{
  if (widget == nullptr || widget->parentWidget() != nullptr) {
    return false;
  }

  // Its popups must stay off the desktop too: they are separate top-level windows that do
  // not inherit WA_DontShowOnScreen (offscreen_popup_guard.hpp). Installed before the
  // widget can open any.
  install_offscreen_popup_guard();

  // The window flag FIRST: changing a window's flags re-parents it, which hides it
  // (and drops a native window that was already made), so everything that must
  // survive goes on afterwards. A call that finds the flag already set is a no-op.
  widget->setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);

  // Mapped as far as Qt is concerned (isVisible, layout, paint, events), never put
  // on the desktop.
  widget->setAttribute(Qt::WA_DontShowOnScreen, true);
  // A visible parentless window with this set keeps the application alive after its
  // main window closes. Qt only ever CLEARS it on its own (for tool/popup windows), so
  // it has to be cleared here explicitly.
  widget->setAttribute(Qt::WA_QuitOnClose, false);

  // Never changes size again: a size change is a sender rebuild. See the file comment.
  widget->setFixedSize(clamp_offscreen_side(width), clamp_offscreen_side(height));

  // On the primary screen, where Qt will place a popup relative to it and the
  // capture will map that popup back into the panel.
  if (const QScreen * screen = QGuiApplication::primaryScreen()) {
    widget->move(screen->availableGeometry().topLeft());
  }

  widget->show();
  return true;
}

bool
is_offscreen_panel(const QWidget * widget)
{
  return widget != nullptr && widget->isWindow() &&
         widget->testAttribute(Qt::WA_DontShowOnScreen) &&
         !widget->testAttribute(Qt::WA_QuitOnClose) &&
         widget->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus);
}

}  // namespace qar_qt_streaming
