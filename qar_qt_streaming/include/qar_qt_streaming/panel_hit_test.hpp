// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__PANEL_HIT_TEST_HPP_
#define QAR_QT_STREAMING__PANEL_HIT_TEST_HPP_

/// @file panel_hit_test.hpp
/// @brief Maps an AR-space interaction onto a panel pixel. Pure math, Qt-free.
///
/// ## Why this exists (the SPATIAL interaction path)
///
/// The per-panel path resolves a contact to content pixels itself
/// (`qar_gui_panels_subscribe_all_gesture_events`) and never comes through
/// here. This is the other path: `qar_app_volumes_subscribe_gesture_events`
/// delivers an action point — a 3D point in room/app-volume space, in metres —
/// keyed to an app VOLUME, not a panel, so it cannot say "panel X was
/// clicked"; the producer has to intersect that point against each panel's
/// known pose and size itself. That is what this header does.
/// @ref intersect_panel_ray is the same seam for a hand-ray source (hand joints
/// + isActivated), which the header already carries.
///
/// ## Panel-local frame — QarOS panel space, exactly
///
/// `PanelPlacement::pose` is handed VERBATIM to `QarGuiPanelInit::pose`
/// (qar_capi_panel_sender_adapter.cpp), so this file has no freedom: the local
/// frame it works in IS the runtime's panel space, and the runtime documents it
/// as
///
///   * origin at the midpoint of the panel's TOP EDGE — the top centre, not the
///     centre, so the surface hangs BELOW the pose over y in [-height, 0];
///   * +X along the top edge to the VIEWER'S LEFT, +Y up;
///   * **-Z** the front-face normal, pointing at the viewer (left x up points
///     away from the viewer, so a +Z viewer-facing normal would not be
///     right-handed). The renderer agrees: the surface sits at the interaction
///     volume's minimum Z and the volume grows along (0, 0, -1).
///
/// Content space — which is what `u` and `v` are — therefore runs BACKWARDS
/// along both panel axes: content x = 0 sits at panel-space +width/2 and
/// content y = 0 at panel-space 0. Hence `u = 0.5 - x/w` and `v = -y/h`, not
/// a centred, unmirrored pair, which would mirror every spatial hit
/// horizontally and put it half a panel height too low. The SDK documents this
/// frame (top-centre origin, front normal -Z) from the 2026-09-21 drop on.
///
/// @ref PanelHit::distance_m stays SIGNED along local +Z rather than being
/// folded into a front/back test. With -Z the front normal, a viewer in front
/// of the panel is at NEGATIVE distance. Nothing in this file branches on that
/// sign — both pickers compare magnitudes or ray travel — so the sign carries
/// no behaviour; it is documented here so a reader does not have to re-derive
/// it.

#include "qar_ar_core/panel_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace qar_qt_streaming
{

/// @brief Where an interaction landed on a panel.
struct PanelHit
{
  bool valid = false;   ///< The maths produced a point at all (ray not parallel etc.).
  bool inside = false;  ///< The point lies within the panel's rectangle.
  /// 0 at the LEFT edge as the VIEWER sees it, 1 at the right — i.e. content
  /// space, which runs along panel-space -X.
  float u = 0.0f;
  /// 0 at the TOP edge (where the pose sits), 1 at the bottom — content space,
  /// which runs along panel-space -Y.
  float v = 0.0f;
  /// Signed distance along the panel's local +Z. The front normal is -Z, so a
  /// viewer facing the panel is at a NEGATIVE distance.
  float distance_m = 0.0f;
};

/// @brief Project a room-space point onto a panel's plane.
///
/// This is the path for the app-volume gesture's action point
/// (`qar_app_volume_gesture_get_points`).
PanelHit hit_test_panel_point(const qar_ar_core::PanelPlacement & placement, const QarVector3 & point_room);

/// @brief Intersect a room-space ray with a panel's plane.
///
/// The path for a future hand-ray source. @p direction need not be normalized;
/// hits strictly behind the origin are reported `valid == false`.
PanelHit intersect_panel_ray(
  const qar_ar_core::PanelPlacement & placement, const QarVector3 & origin, const QarVector3 & direction);

/// @brief Convert a hit's normalized (u, v) to widget-local LOGICAL pixels.
///
/// Logical, not buffer, pixels: Qt event coordinates are logical, so this is
/// what a synthetic QMouseEvent would need. Clamps to the last addressable pixel
/// so a hit exactly on the right/bottom edge does not land out of bounds.
/// @return false if the hit is not inside, or the widget size is zero.
bool panel_uv_to_widget_px(
  const PanelHit & hit,
  std::uint32_t logical_width,
  std::uint32_t logical_height,
  std::int32_t & out_x,
  std::int32_t & out_y);

/// @brief Convert a hit to widget-local LOGICAL coordinates as real numbers,
/// UNCLAMPED: a hit outside the rectangle (a pressed pointer dragged past the
/// panel's edge) maps to coordinates outside [0, w) × [0, h), which is exactly
/// what Qt's implicit grab expects to receive. Requires only `hit.valid`.
/// @return false if the hit is invalid or the widget size is zero.
bool panel_hit_to_widget_point(
  const PanelHit & hit,
  std::uint32_t logical_width,
  std::uint32_t logical_height,
  double & out_x,
  double & out_y);

/// @brief The inverse of @ref hit_test_panel_point: panel-local (u, v) back to
/// the room-space point on the panel's plane. This is how a sample that arrives
/// already resolved to one panel is re-projected onto another — the panel the
/// pointer is grabbed by — through the one geometry path, not a second one.
QarVector3 panel_uv_to_room_point(const qar_ar_core::PanelPlacement & placement, float u, float v);

/// @brief One panel considered for routing, flattened out of the live streams.
///
/// Keeping the picker on a plain vector of these — rather than reaching into the
/// manager's QtPanelStream map — is what makes panel selection unit-testable
/// with no QApplication and no widgets.
struct PanelHitCandidate
{
  std::string key;
  qar_ar_core::PanelPlacement placement{};
  std::uint32_t logical_width = 0;
  std::uint32_t logical_height = 0;
};

/// @brief Which panel an interaction landed on, already resolved to a pixel.
struct PanelPick
{
  bool hit = false;
  std::string key;
  PanelHit where{};
  std::int32_t widget_x = 0;
  std::int32_t widget_y = 0;
};

/// @brief Pick the panel a room-space point lands on.
/// Overlapping panels: the one whose plane the point is CLOSEST to wins.
PanelPick pick_panel_for_point(
  const std::vector<PanelHitCandidate> & candidates, const QarVector3 & point_room);

/// @brief Pick the panel a room-space ray hits FIRST along its direction.
PanelPick pick_panel_for_ray(
  const std::vector<PanelHitCandidate> & candidates,
  const QarVector3 & origin,
  const QarVector3 & direction);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__PANEL_HIT_TEST_HPP_
