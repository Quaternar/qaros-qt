// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/scroll_census.hpp"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QFontMetrics>
#include <QHeaderView>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextEdit>
#include <QVariant>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace qar_qt_streaming
{
namespace
{

/// Names go into log lines that are read by tools: keep them ASCII, show anything else as '?'.
std::string
ascii_only(std::string text)
{
  for (char & c : text) {
    if (static_cast<unsigned char>(c) >= 0x80) {
      c = '?';
    }
  }
  return text;
}

/// True when a window other than @p root lies between @p area (included) and @p root (excluded):
/// a dialog or a popup parented into the tree is a panel of its own, and its areas are its own.
bool
inside_foreign_window(const QWidget * area, const QWidget & root)
{
  for (const QWidget * w = area; w != nullptr && w != &root; w = w->parentWidget()) {
    if (w->isWindow()) {
      return true;
    }
  }
  return false;
}

/// The nearest widget from @p area up to @p root that carries kScrollsByDesignProperty decides;
/// without one, a text edit (a text browser is one) scrolls by design and nothing else does.
bool
scrolls_by_design(const QAbstractScrollArea & area, const QWidget & root)
{
  for (const QWidget * w = &area; w != nullptr; w = w->parentWidget()) {
    const QVariant mark = w->property(kScrollsByDesignProperty);
    if (mark.isValid()) {
      return mark.toBool();
    }
    if (w == &root) {
      break;
    }
  }
  return qobject_cast<const QTextEdit *>(&area) != nullptr ||
         qobject_cast<const QPlainTextEdit *>(&area) != nullptr;
}

ScrollAreaCensus
measure_area(const QAbstractScrollArea & area, const QWidget & root)
{
  ScrollAreaCensus out;
  out.class_name = ascii_only(std::string(area.metaObject()->className()));
  out.object_name = ascii_only(area.objectName().toStdString());

  const QScrollBar * const vbar = area.verticalScrollBar();
  out.v_min = vbar->minimum();
  out.v_max = vbar->maximum();
  out.page_step = vbar->pageStep();
  const int v_range = std::max(0, out.v_max - out.v_min);
  if (const QWidget * const viewport = area.viewport()) {
    out.viewport_w = viewport->width();
    out.viewport_h = viewport->height();
  }
  // Range, not visibility: the bar itself is shown by a call Qt queues after the range changed.
  out.v_bar = area.verticalScrollBarPolicy() != Qt::ScrollBarAlwaysOff && v_range > 0;

  const QScrollBar * const hbar = area.horizontalScrollBar();
  out.h_bar = area.horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff &&
    hbar->maximum() > hbar->minimum();
  out.h_bar_extent = out.h_bar ? std::max(0, hbar->sizeHint().height()) : 0;

  // What one step of the vertical bar is worth in pixels: an item view in per-item mode counts
  // rows, a plain text edit counts lines.
  if (const auto * view = qobject_cast<const QAbstractItemView *>(&area)) {
    if (view->verticalScrollMode() == QAbstractItemView::ScrollPerItem) {
      out.per_item = true;
      out.row_h = std::max(0, view->sizeHintForRow(0));
    }
  } else if (qobject_cast<const QPlainTextEdit *>(&area) != nullptr) {
    out.per_item = true;
    out.row_h = std::max(0, QFontMetrics(area.font()).lineSpacing());
  }
  if (v_range > 0) {
    out.overflow_px = out.per_item ? v_range * out.row_h : v_range;
  }
  out.by_design = scrolls_by_design(area, root);
  return out;
}

}  // namespace

ScrollCensus
take_scroll_census(const QWidget & root)
{
  ScrollCensus census;

  std::vector<const QAbstractScrollArea *> candidates;
  if (const auto * self = qobject_cast<const QAbstractScrollArea *>(&root)) {
    candidates.push_back(self);
  }
  for (const QAbstractScrollArea * area : root.findChildren<QAbstractScrollArea *>()) {
    candidates.push_back(area);
  }

  for (const QAbstractScrollArea * area : candidates) {
    // A header view follows the item view that owns it and is never scrolled on its own.
    if (qobject_cast<const QHeaderView *>(area) != nullptr) {
      continue;
    }
    if (inside_foreign_window(area, root)) {
      continue;
    }
    // The root is judged by its own state: isVisibleTo() would look above it.
    const bool is_root = area == &root;
    if (is_root && root.isHidden()) {
      continue;
    }
    if (!is_root && !area->isVisibleTo(&root)) {
      ++census.hidden_areas;
      continue;
    }

    ScrollAreaCensus measured = measure_area(*area, root);
    if (measured.per_item && measured.v_max > measured.v_min && measured.row_h <= 0) {
      ++census.unmeasured;
    }
    if (measured.by_design) {
      census.any_vertical_bar_by_design = census.any_vertical_bar_by_design || measured.v_bar;
    } else {
      if (measured.overflow_px > 0) {
        // The horizontal bar of an area that has to grow takes room from its viewport as well.
        const int cost = measured.overflow_px + (measured.h_bar ? measured.h_bar_extent : 0);
        census.largest_overflow_px = std::max(census.largest_overflow_px, cost);
        census.stacked_overflow_px += cost;
      } else if (measured.h_bar) {
        // A taller widget keeps a horizontal bar that no vertical overflow goes with.
        census.horizontal_only_px = std::max(census.horizontal_only_px, measured.h_bar_extent);
      }
      census.any_vertical_bar = census.any_vertical_bar || measured.v_bar;
    }
    census.areas.push_back(std::move(measured));
  }
  return census;
}

std::string
describe_scroll_census(const ScrollCensus & census)
{
  if (census.areas.empty()) {
    return "none";
  }
  std::string out;
  for (const ScrollAreaCensus & area : census.areas) {
    if (!out.empty()) {
      out += "; ";
    }
    out += area.class_name + " '" + area.object_name + "' v=" + std::to_string(area.v_min) +
      ".." + std::to_string(area.v_max) + " page=" + std::to_string(area.page_step) +
      " viewport=" + std::to_string(area.viewport_w) + "x" + std::to_string(area.viewport_h) +
      " vbar=" + (area.v_bar ? "1" : "0") + " hbar=" + (area.h_bar ? "1" : "0") + " " +
      (area.per_item ? "per-item" : "per-pixel") + " overflow=" + std::to_string(area.overflow_px) +
      (area.by_design ? " by-design" : "");
  }
  return out;
}

}  // namespace qar_qt_streaming
