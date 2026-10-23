// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/panel_close_mode.hpp"

#include <cstddef>

namespace qar_qt_streaming
{
namespace
{

bool
is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

char
to_lower_ascii(char c)
{
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// The value with surrounding whitespace dropped and ASCII letters lower-cased.
std::string
normalised(const char * value)
{
  const std::string raw(value);
  std::size_t begin = 0;
  std::size_t end = raw.size();
  while (begin < end && is_space(raw[begin])) {
    ++begin;
  }
  while (end > begin && is_space(raw[end - 1])) {
    --end;
  }
  std::string out = raw.substr(begin, end - begin);
  for (char & c : out) {
    c = to_lower_ascii(c);
  }
  return out;
}

}  // namespace

const char *
to_string(PanelCloseMode mode)
{
  switch (mode) {
    case PanelCloseMode::Destroy:
      return "destroy";
    case PanelCloseMode::Notice:
      return "notice";
    case PanelCloseMode::Park:
      return "park";
  }
  return "destroy";
}

PanelCloseModeChoice
parse_panel_close_mode(const char * value)
{
  PanelCloseModeChoice choice;
  if (value == nullptr) {
    return choice;
  }
  const std::string word = normalised(value);
  if (word.empty()) {
    return choice;  // set to nothing is the same as not set
  }
  if (word == "destroy") {
    choice.mode = PanelCloseMode::Destroy;
    choice.explicitly_chosen = true;
    return choice;
  }
  if (word == "notice") {
    choice.mode = PanelCloseMode::Notice;
    choice.explicitly_chosen = true;
    return choice;
  }
  if (word == "park") {
    choice.mode = PanelCloseMode::Park;
    choice.explicitly_chosen = true;
    return choice;
  }
  choice.warning = std::string(kPanelCloseModeEnv) + "='" + std::string(value) +
    "' is not a close mode (expected 'destroy', 'notice' or 'park'); using the default, '" +
    to_string(choice.mode) + "'.";
  return choice;
}

}  // namespace qar_qt_streaming
