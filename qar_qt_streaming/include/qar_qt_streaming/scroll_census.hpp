// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__SCROLL_CENSUS_HPP_
#define QAR_QT_STREAMING__SCROLL_CENSUS_HPP_

/// @file scroll_census.hpp
/// @brief Which scroll areas a streamed widget shows, and how far each one overflows.
///
/// A scroll bar in an AR panel is a defect the operator cannot work around: the pointer there
/// is a ray, and a wheel does not exist. A panel that has to be free of them needs to know, in
/// numbers, what every scroll area under its widget looks like: its range, its viewport, whether
/// a bar is shown and how many pixels of content are out of sight. This header is that
/// measurement, for Qt Widgets only (no host, no QarOS).
///
/// The measurement is RANGE-BASED on purpose. A scroll bar is shown by a call that Qt queues
/// after the bar's range changed, so the visibility of a bar lags behind its range; the range is
/// what the content needs and what the next frame will show. The census reads the ranges as they
/// are and never lays anything out, so it is the caller that chooses when to measure.

#include <QWidget>

#include <string>
#include <vector>

namespace qar_qt_streaming
{

/// The dynamic property that marks a widget, and everything under it, as one that scrolls by
/// design (a log, a description browser). Set it to true on the widget; set it to false on a
/// text widget to make it count as an ordinary area. The nearest widget from a scroll area up to
/// the census root that carries the property decides.
constexpr const char * kScrollsByDesignProperty = "qar_scrolls_by_design";

/// @brief One scroll area under the root of a census.
struct ScrollAreaCensus
{
  /// The Qt meta-object class name of the area, and its object name (possibly empty).
  std::string class_name;
  std::string object_name;
  /// The vertical scroll bar's range and page step, and the viewport's size, all in pixels
  /// except for an area that scrolls per item (see @ref per_item).
  int v_min = 0;
  int v_max = 0;
  int page_step = 0;
  int viewport_w = 0;
  int viewport_h = 0;
  /// A vertical bar is shown: its policy is not "always off" and its range is not empty.
  bool v_bar = false;
  /// The same for the horizontal bar.
  bool h_bar = false;
  /// The height of the horizontal bar when @ref h_bar, else 0.
  int h_bar_extent = 0;
  /// The vertical bar counts items, not pixels: an item view in per-item scrolling, or a plain
  /// text edit (its bar counts lines).
  bool per_item = false;
  /// For @ref per_item, the height in pixels of one item (the size hint of the first row, the
  /// line spacing of a plain text edit); 0 when it could not be measured.
  int row_h = 0;
  /// The content out of sight in pixels: the range of a per-pixel area, the range times
  /// @ref row_h of a per-item one; 0 when the range is empty. It is counted whatever the
  /// bar's policy, because content that is cut off needs the room as much as content behind a bar.
  int overflow_px = 0;
  /// The area, or a widget above it up to the root, carries @ref kScrollsByDesignProperty set
  /// to true, or the area is a text edit or browser that nothing has marked otherwise.
  bool by_design = false;
};

/// @brief The scroll areas under one widget.
struct ScrollCensus
{
  /// The areas the operator can see: visible relative to the root (a page of a stacked or tabbed
  /// widget that is not the current one is not), the root itself included when it is a scroll
  /// area. Areas inside another window parented into the root (a dialog, a popup) belong to that
  /// window and are not counted at all, and neither are the header views of item views.
  std::vector<ScrollAreaCensus> areas;
  /// Areas under the root that are not visible.
  int hidden_areas = 0;
  /// Visible per-item areas that have a range and no row height to turn it into pixels; their
  /// overflow is then counted as 0, so a fit that wants to be sure of "no bar" waits for this to
  /// be 0.
  int unmeasured = 0;
  /// What a taller widget needs for its areas to show no vertical bar: over the visible areas that
  /// are not by design and have a vertical overflow, the largest and the sum of
  /// @ref ScrollAreaCensus::overflow_px plus @ref ScrollAreaCensus::h_bar_extent when the area
  /// shows a horizontal bar too (that bar takes room from the viewport). An area whose only bar
  /// is a horizontal one adds nothing to either, because a taller widget keeps that bar; it is
  /// counted by @ref horizontal_only_px. The sum is the height that removes every vertical bar
  /// when the areas sit one above the other (two trees in a splitter), the largest when they sit
  /// side by side.
  int largest_overflow_px = 0;
  int stacked_overflow_px = 0;
  /// The largest extent of a horizontal bar over the visible areas that are not by design, show
  /// a horizontal bar and have no vertical overflow: a bar that no height removes. 0 when there
  /// is none.
  int horizontal_only_px = 0;
  /// Some area that is not by design shows a vertical bar; some area that is by design does.
  bool any_vertical_bar = false;
  bool any_vertical_bar_by_design = false;
};

/// @brief Measure the scroll areas of @p root and of everything under it.
///
/// Reads ranges, policies and sizes; creates nothing, lays nothing out and changes nothing.
/// GUI thread only, like every widget call.
ScrollCensus take_scroll_census(const QWidget & root);

/// @brief The areas of @p census as one line of text: for each area
/// `<Class> '<objectName>' v=<min>..<max> page=<step> viewport=<w>x<h> vbar=<0|1> hbar=<0|1>
/// <per-item|per-pixel> overflow=<px>`, followed by ` by-design` for an area that scrolls by
/// design, the areas joined by `; `. `none` when the census has no area.
std::string describe_scroll_census(const ScrollCensus & census);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__SCROLL_CENSUS_HPP_
