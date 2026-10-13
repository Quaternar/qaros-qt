// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/panel_hit_test.hpp"

#include <algorithm>
#include <cmath>

namespace qar_qt_streaming
{
namespace
{

QarVector3
subtract(const QarVector3 & a, const QarVector3 & b)
{
  return QarVector3{a.x - b.x, a.y - b.y, a.z - b.z};
}

QarVector3
cross(const QarVector3 & a, const QarVector3 & b)
{
  return QarVector3{
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x};
}

/// Rotate @p v by @p q (room-ward) or, with @p inverse, by its CONJUGATE (a
/// room-space vector into the panel's local frame). Uses the standard
/// v_out = v + 2 * cross(qv, cross(qv, v) + qw * v) identity, with qv negated
/// for the conjugate. The quaternion is normalized defensively: QarPose values
/// arrive from the runtime, and a slightly non-unit quaternion would silently
/// scale the result — showing up as a mis-scaled (u, v) rather than an obvious
/// failure.
QarVector3
rotate_by(const QarQuaternion & q, const QarVector3 & v, bool inverse)
{
  const double len = std::sqrt(
    static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
    static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w);
  if (!std::isfinite(len) || len <= 0.0) {
    return v;
  }
  const float inv = static_cast<float>((inverse ? -1.0 : 1.0) / len);
  const QarVector3 qv{q.x * inv, q.y * inv, q.z * inv};
  const float qw = static_cast<float>(q.w / len);

  const QarVector3 t = cross(qv, v);
  const QarVector3 inner{t.x + qw * v.x, t.y + qw * v.y, t.z + qw * v.z};
  const QarVector3 outer = cross(qv, inner);
  return QarVector3{v.x + 2.0f * outer.x, v.y + 2.0f * outer.y, v.z + 2.0f * outer.z};
}

QarVector3
rotate_into_local(const QarQuaternion & q, const QarVector3 & v)
{
  return rotate_by(q, v, true);
}

QarVector3
rotate_into_room(const QarQuaternion & q, const QarVector3 & v)
{
  return rotate_by(q, v, false);
}

/// Turn a point already expressed in the panel's local frame into a PanelHit.
PanelHit
local_point_to_hit(const QarVector3 & local, const QarGuiPanelSize & size)
{
  PanelHit hit;
  if (!(size.width_meters > 0.0f) || !(size.height_meters > 0.0f)) {
    return hit;
  }
  hit.valid = true;
  // QarOS panel space, not a centred frame: the pose sits on the TOP EDGE and
  // +X points to the VIEWER'S LEFT, so content space runs backwards along both
  // axes (see panel_hit_test.hpp). u = 0 is therefore at local x = +w/2 and
  // v = 0 at local y = 0, with the surface hanging over y in [-h, 0].
  hit.u = 0.5f - local.x / size.width_meters;
  hit.v = -local.y / size.height_meters;
  hit.distance_m = local.z;
  hit.inside = hit.u >= 0.0f && hit.u <= 1.0f && hit.v >= 0.0f && hit.v <= 1.0f;
  return hit;
}

}  // namespace

PanelHit
hit_test_panel_point(const qar_ar_core::PanelPlacement & placement, const QarVector3 & point_room)
{
  const QarVector3 relative = subtract(point_room, placement.pose.position);
  const QarVector3 local = rotate_into_local(placement.pose.orientation, relative);
  return local_point_to_hit(local, placement.size);
}

PanelHit
intersect_panel_ray(
  const qar_ar_core::PanelPlacement & placement, const QarVector3 & origin, const QarVector3 & direction)
{
  const QarVector3 relative = subtract(origin, placement.pose.position);
  const QarVector3 local_origin = rotate_into_local(placement.pose.orientation, relative);
  const QarVector3 local_dir = rotate_into_local(placement.pose.orientation, direction);

  PanelHit hit;
  // A ray parallel to the panel plane never lands on a pixel. The epsilon is on
  // the DIRECTION component, so it is scale-free for a normalized ray and still
  // conservative for an unnormalized one.
  constexpr float kParallelEpsilon = 1e-6f;
  if (!std::isfinite(local_dir.z) || std::fabs(local_dir.z) < kParallelEpsilon) {
    return hit;
  }
  const float t = -local_origin.z / local_dir.z;
  if (!std::isfinite(t) || t < 0.0f) {
    return hit;  // plane is behind the ray origin
  }
  const QarVector3 on_plane{
    local_origin.x + t * local_dir.x,
    local_origin.y + t * local_dir.y,
    0.0f};
  PanelHit result = local_point_to_hit(on_plane, placement.size);
  if (result.valid) {
    // For a ray the useful distance is how far along the ray the plane was met,
    // not the (identically zero) offset from the plane.
    result.distance_m = t;
  }
  return result;
}

bool
panel_uv_to_widget_px(
  const PanelHit & hit,
  std::uint32_t logical_width,
  std::uint32_t logical_height,
  std::int32_t & out_x,
  std::int32_t & out_y)
{
  if (!hit.valid || !hit.inside || logical_width == 0 || logical_height == 0) {
    return false;
  }
  const auto to_px = [](float normalized, std::uint32_t extent) {
    const float scaled = normalized * static_cast<float>(extent);
    const std::int32_t px = static_cast<std::int32_t>(std::floor(scaled));
    return std::clamp(px, 0, static_cast<std::int32_t>(extent) - 1);
  };
  out_x = to_px(hit.u, logical_width);
  out_y = to_px(hit.v, logical_height);
  return true;
}

bool
panel_hit_to_widget_point(
  const PanelHit & hit,
  std::uint32_t logical_width,
  std::uint32_t logical_height,
  double & out_x,
  double & out_y)
{
  if (!hit.valid || logical_width == 0 || logical_height == 0) {
    return false;
  }
  out_x = static_cast<double>(hit.u) * static_cast<double>(logical_width);
  out_y = static_cast<double>(hit.v) * static_cast<double>(logical_height);
  return true;
}

QarVector3
panel_uv_to_room_point(const qar_ar_core::PanelPlacement & placement, float u, float v)
{
  // Mirror of local_point_to_hit: u = 0.5 - x / w, v = -y / h.
  const QarVector3 local{
    (0.5f - u) * placement.size.width_meters,
    -v * placement.size.height_meters,
    0.0f};
  const QarVector3 rotated = rotate_into_room(placement.pose.orientation, local);
  const QarVector3 & p = placement.pose.position;
  return QarVector3{rotated.x + p.x, rotated.y + p.y, rotated.z + p.z};
}

namespace
{

/// Shared tail of both pickers: turn the winning candidate into a PanelPick.
PanelPick
finish_pick(const PanelHitCandidate & candidate, const PanelHit & hit)
{
  PanelPick pick;
  if (!panel_uv_to_widget_px(
      hit, candidate.logical_width, candidate.logical_height, pick.widget_x, pick.widget_y))
  {
    return pick;
  }
  pick.hit = true;
  pick.key = candidate.key;
  pick.where = hit;
  return pick;
}

}  // namespace

PanelPick
pick_panel_for_point(
  const std::vector<PanelHitCandidate> & candidates, const QarVector3 & point_room)
{
  const PanelHitCandidate * best = nullptr;
  PanelHit best_hit{};
  for (const auto & candidate : candidates) {
    const PanelHit hit = hit_test_panel_point(candidate.placement, point_room);
    if (!hit.valid || !hit.inside) {
      continue;
    }
    if (best == nullptr || std::fabs(hit.distance_m) < std::fabs(best_hit.distance_m)) {
      best = &candidate;
      best_hit = hit;
    }
  }
  return best == nullptr ? PanelPick{} : finish_pick(*best, best_hit);
}

PanelPick
pick_panel_for_ray(
  const std::vector<PanelHitCandidate> & candidates,
  const QarVector3 & origin,
  const QarVector3 & direction)
{
  const PanelHitCandidate * best = nullptr;
  PanelHit best_hit{};
  for (const auto & candidate : candidates) {
    const PanelHit hit = intersect_panel_ray(candidate.placement, origin, direction);
    if (!hit.valid || !hit.inside) {
      continue;
    }
    // distance_m is travel ALONG the ray here, so the smallest one is the panel
    // the wearer would actually see their pointer land on.
    if (best == nullptr || hit.distance_m < best_hit.distance_m) {
      best = &candidate;
      best_hit = hit;
    }
  }
  return best == nullptr ? PanelPick{} : finish_pick(*best, best_hit);
}

}  // namespace qar_qt_streaming
