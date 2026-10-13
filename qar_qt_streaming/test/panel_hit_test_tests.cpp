// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The SPATIAL interaction seam: the per-panel path
// resolves a contact to content pixels itself, so a room-space point (an
// app-volume gesture's action point) or a hand ray has to be intersected
// against the panel ourselves.
//
// The local frame here IS QarOS panel space, because PanelPlacement::pose is
// handed verbatim to QarGuiPanelInit::pose: origin at the TOP-EDGE midpoint,
// +X to the VIEWER'S LEFT, -Z the front normal. A centred, +X-is-right frame
// would mirror every hit and put it half a panel height too low. The SDK
// documents QarOS panel space from the 2026-09-21 drop on.

#include <gtest/gtest.h>

#include "qar_qt_streaming/panel_hit_test.hpp"

#include <cmath>
#include <string>
#include <vector>

using qar_qt_streaming::hit_test_panel_point;
using qar_qt_streaming::intersect_panel_ray;
using qar_qt_streaming::panel_uv_to_widget_px;
using qar_qt_streaming::PanelHit;
using qar_qt_streaming::PanelHitCandidate;
using qar_ar_core::PanelPlacement;
using qar_qt_streaming::pick_panel_for_point;
using qar_qt_streaming::pick_panel_for_ray;

namespace
{

/// A 1.0 x 0.5 m panel whose TOP EDGE midpoint is the origin, unrotated: local
/// axes == room axes. The surface therefore hangs over y in [-0.5, 0] and its
/// visual centre is at room (0, -0.25, 0).
PanelPlacement
axis_aligned_panel()
{
  PanelPlacement placement;
  placement.pose.position = QarVector3{0.0f, 0.0f, 0.0f};
  placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  placement.size.width_meters = 1.0f;
  placement.size.height_meters = 0.5f;
  placement.size.content_scale = 1.0f;
  return placement;
}

// MSVC does not define M_PI without _USE_MATH_DEFINES; spell it out instead.
constexpr double kPi = 3.14159265358979323846;

/// Rotation of @p degrees about the room +Y axis, as a unit quaternion.
QarQuaternion
yaw(double degrees)
{
  const double half = degrees * kPi / 180.0 * 0.5;
  return QarQuaternion{0.0f, static_cast<float>(std::sin(half)), 0.0f,
    static_cast<float>(std::cos(half))};
}

}  // namespace

TEST(PanelHitTest, ThePoseSitsOnTheTopEdge)
{
  // The pose is the midpoint of the TOP edge, not the centre of the surface, so
  // the pose's own position is v = 0 and the surface hangs below it.
  const PanelHit at_pose =
    hit_test_panel_point(axis_aligned_panel(), QarVector3{0.0f, 0.0f, 0.0f});
  ASSERT_TRUE(at_pose.valid);
  EXPECT_TRUE(at_pose.inside);
  EXPECT_NEAR(at_pose.u, 0.5f, 1e-5f);
  EXPECT_NEAR(at_pose.v, 0.0f, 1e-5f);

  // Half a height BELOW the pose is the visual centre of the panel.
  const PanelHit centre =
    hit_test_panel_point(axis_aligned_panel(), QarVector3{0.0f, -0.25f, 0.0f});
  EXPECT_NEAR(centre.u, 0.5f, 1e-5f);
  EXPECT_NEAR(centre.v, 0.5f, 1e-5f);
}

TEST(PanelHitTest, UGrowsRightAndVGrowsDown)
{
  const PanelPlacement panel = axis_aligned_panel();

  // Panel-space +X runs to the VIEWER'S LEFT, and content x grows right, so a
  // point at local +X/4 is a QUARTER of the way across, not three quarters.
  const PanelHit plus_x = hit_test_panel_point(panel, QarVector3{0.25f, -0.25f, 0.0f});
  EXPECT_NEAR(plus_x.u, 0.25f, 1e-5f);
  EXPECT_NEAR(plus_x.v, 0.5f, 1e-5f);

  // ABOVE in room space must be a SMALLER v: v counts down from the top edge
  // while local +Y points up.
  const PanelHit up = hit_test_panel_point(panel, QarVector3{0.0f, -0.125f, 0.0f});
  EXPECT_NEAR(up.u, 0.5f, 1e-5f);
  EXPECT_NEAR(up.v, 0.25f, 1e-5f);
}

TEST(PanelHitTest, CornersLandExactlyOnTheUnitSquare)
{
  const PanelPlacement panel = axis_aligned_panel();

  // Content (0, 0) -- the top-LEFT corner as the viewer sees it -- is at
  // panel-space +width/2 on the top edge, because +X points to their left.
  const PanelHit top_left = hit_test_panel_point(panel, QarVector3{0.5f, 0.0f, 0.0f});
  EXPECT_TRUE(top_left.inside);
  EXPECT_NEAR(top_left.u, 0.0f, 1e-5f);
  EXPECT_NEAR(top_left.v, 0.0f, 1e-5f);

  const PanelHit bottom_right = hit_test_panel_point(panel, QarVector3{-0.5f, -0.5f, 0.0f});
  EXPECT_TRUE(bottom_right.inside);
  EXPECT_NEAR(bottom_right.u, 1.0f, 1e-5f);
  EXPECT_NEAR(bottom_right.v, 1.0f, 1e-5f);
}

TEST(PanelHitTest, RoundTripsThroughPanelUvToRoomPoint)
{
  // The inverse must undo the forward map exactly, or a sample resolved against
  // one panel and re-projected onto the grabbed one drifts on every hop.
  const PanelPlacement panel = axis_aligned_panel();
  const QarVector3 room = qar_qt_streaming::panel_uv_to_room_point(panel, 0.25f, 0.75f);
  const PanelHit back = hit_test_panel_point(panel, room);
  ASSERT_TRUE(back.valid);
  EXPECT_NEAR(back.u, 0.25f, 1e-5f);
  EXPECT_NEAR(back.v, 0.75f, 1e-5f);
}

TEST(PanelHitTest, TheFrontOfThePanelIsAtNegativeLocalZ)
{
  // -Z is the front-face normal (2026-09-21 SDK header), so a viewer
  // standing in front of the panel reads a NEGATIVE signed distance. Nothing in
  // the pickers branches on it (they compare magnitudes) -- this pins the
  // convention so that stays deliberate.
  const PanelPlacement panel = axis_aligned_panel();
  const PanelHit in_front = hit_test_panel_point(panel, QarVector3{0.0f, -0.25f, -0.3f});
  EXPECT_LT(in_front.distance_m, 0.0f);
  const PanelHit behind = hit_test_panel_point(panel, QarVector3{0.0f, -0.25f, 0.3f});
  EXPECT_GT(behind.distance_m, 0.0f);
}

TEST(PanelHitTest, PointsOutsideTheRectangleAreValidButNotInside)
{
  const PanelHit hit = hit_test_panel_point(axis_aligned_panel(), QarVector3{0.9f, -0.25f, 0.0f});
  EXPECT_TRUE(hit.valid);
  EXPECT_FALSE(hit.inside);
}

TEST(PanelHitTest, OffPlaneDistanceIsReportedSigned)
{
  const PanelPlacement panel = axis_aligned_panel();
  EXPECT_NEAR(hit_test_panel_point(panel, QarVector3{0.0f, 0.0f, 0.3f}).distance_m, 0.3f, 1e-5f);
  EXPECT_NEAR(hit_test_panel_point(panel, QarVector3{0.0f, 0.0f, -0.3f}).distance_m, -0.3f, 1e-5f);
}

TEST(PanelHitTest, HonoursTheTranslationOfThePanel)
{
  PanelPlacement panel = axis_aligned_panel();
  panel.pose.position = QarVector3{2.0f, 1.0f, -3.0f};

  const PanelHit hit = hit_test_panel_point(panel, QarVector3{2.0f, 1.0f, -3.0f});
  EXPECT_TRUE(hit.inside);
  EXPECT_NEAR(hit.u, 0.5f, 1e-5f);
  EXPECT_NEAR(hit.v, 0.0f, 1e-5f) << "the pose is on the top edge";
}

TEST(PanelHitTest, HonoursTheRotationOfThePanel)
{
  PanelPlacement panel = axis_aligned_panel();
  panel.pose.orientation = yaw(90.0);

  // Yawed 90 degrees about +Y, the panel's local +X now points along room -Z.
  // A point a quarter-width along room -Z is therefore at local +X/4, which is
  // u = 0.25 (content x grows along panel-space -X).
  const PanelHit hit = hit_test_panel_point(panel, QarVector3{0.0f, -0.25f, -0.25f});
  ASSERT_TRUE(hit.valid);
  EXPECT_TRUE(hit.inside);
  EXPECT_NEAR(hit.u, 0.25f, 1e-4f);
  EXPECT_NEAR(hit.v, 0.5f, 1e-4f);
}

TEST(PanelHitTest, NonUnitQuaternionsAreNormalisedRatherThanScalingTheResult)
{
  PanelPlacement panel = axis_aligned_panel();
  // Same rotation as identity, but with a length-2 quaternion. Without the
  // defensive normalisation this would silently scale (u, v).
  panel.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 2.0f};

  const PanelHit hit = hit_test_panel_point(panel, QarVector3{0.25f, -0.25f, 0.0f});
  EXPECT_NEAR(hit.u, 0.25f, 1e-5f);
}

TEST(PanelHitTest, ZeroSizedPanelYieldsNoHit)
{
  PanelPlacement panel = axis_aligned_panel();
  panel.size.width_meters = 0.0f;
  EXPECT_FALSE(hit_test_panel_point(panel, QarVector3{0.0f, 0.0f, 0.0f}).valid);
}

TEST(PanelRay, StraightOnRayHitsTheCentre)
{
  // Aimed at the visual centre, which is half a height below the pose.
  const PanelHit hit = intersect_panel_ray(
    axis_aligned_panel(), QarVector3{0.0f, -0.25f, 2.0f}, QarVector3{0.0f, 0.0f, -1.0f});
  ASSERT_TRUE(hit.valid);
  EXPECT_TRUE(hit.inside);
  EXPECT_NEAR(hit.u, 0.5f, 1e-5f);
  EXPECT_NEAR(hit.v, 0.5f, 1e-5f);
  // For a ray, distance is travel ALONG the ray to the plane.
  EXPECT_NEAR(hit.distance_m, 2.0f, 1e-5f);
}

TEST(PanelRay, RayParallelToThePanelNeverLands)
{
  const PanelHit hit = intersect_panel_ray(
    axis_aligned_panel(), QarVector3{0.0f, -0.25f, 2.0f}, QarVector3{1.0f, 0.0f, 0.0f});
  EXPECT_FALSE(hit.valid);
}

TEST(PanelRay, RayPointingAwayFromThePanelDoesNotHitItBehindTheOrigin)
{
  const PanelHit hit = intersect_panel_ray(
    axis_aligned_panel(), QarVector3{0.0f, 0.0f, 2.0f}, QarVector3{0.0f, 0.0f, 1.0f});
  EXPECT_FALSE(hit.valid);
}

TEST(PanelRay, UnnormalisedDirectionGivesTheSameUv)
{
  const PanelHit unit = intersect_panel_ray(
    axis_aligned_panel(), QarVector3{0.1f, -0.2f, 2.0f}, QarVector3{0.0f, 0.0f, -1.0f});
  const PanelHit scaled = intersect_panel_ray(
    axis_aligned_panel(), QarVector3{0.1f, -0.2f, 2.0f}, QarVector3{0.0f, 0.0f, -7.0f});
  ASSERT_TRUE(unit.valid);
  ASSERT_TRUE(scaled.valid);
  EXPECT_NEAR(unit.u, scaled.u, 1e-5f);
  EXPECT_NEAR(unit.v, scaled.v, 1e-5f);
}

TEST(PanelUvToWidgetPx, MapsTheUnitSquareOntoPixels)
{
  PanelHit hit;
  hit.valid = true;
  hit.inside = true;
  hit.u = 0.5f;
  hit.v = 0.5f;

  std::int32_t x = 0;
  std::int32_t y = 0;
  ASSERT_TRUE(panel_uv_to_widget_px(hit, 400, 600, x, y));
  EXPECT_EQ(x, 200);
  EXPECT_EQ(y, 300);
}

TEST(PanelUvToWidgetPx, TheBottomRightEdgeClampsToTheLastAddressablePixel)
{
  PanelHit hit;
  hit.valid = true;
  hit.inside = true;
  hit.u = 1.0f;
  hit.v = 1.0f;

  std::int32_t x = 0;
  std::int32_t y = 0;
  ASSERT_TRUE(panel_uv_to_widget_px(hit, 400, 600, x, y));
  EXPECT_EQ(x, 399);
  EXPECT_EQ(y, 599);
}

TEST(PanelUvToWidgetPx, RefusesMissesAndZeroSizedWidgets)
{
  std::int32_t x = 0;
  std::int32_t y = 0;

  PanelHit outside;
  outside.valid = true;
  outside.inside = false;
  EXPECT_FALSE(panel_uv_to_widget_px(outside, 400, 600, x, y));

  PanelHit good;
  good.valid = true;
  good.inside = true;
  EXPECT_FALSE(panel_uv_to_widget_px(good, 0, 600, x, y));
}

namespace
{

PanelHitCandidate
candidate(const std::string & key, float z, float width = 1.0f)
{
  PanelHitCandidate c;
  c.key = key;
  c.placement = axis_aligned_panel();
  c.placement.size.width_meters = width;
  c.placement.pose.position = QarVector3{0.0f, 0.0f, z};
  c.logical_width = 400;
  c.logical_height = 600;
  return c;
}

}  // namespace

TEST(PanelPicker, PicksNothingWhenThePointMissesEveryPanel)
{
  const auto pick = pick_panel_for_point({candidate("a", 0.0f)}, QarVector3{5.0f, 0.0f, 0.0f});
  EXPECT_FALSE(pick.hit);
}

TEST(PanelPicker, PicksThePanelWhosePlaneThePointIsClosestTo)
{
  const std::vector<PanelHitCandidate> candidates{candidate("far", 0.0f), candidate("near", 0.9f)};
  const auto pick = pick_panel_for_point(candidates, QarVector3{0.0f, -0.25f, 1.0f});
  ASSERT_TRUE(pick.hit);
  EXPECT_EQ(pick.key, "near");
}

TEST(PanelPicker, ResolvesThePickToAWidgetPixel)
{
  const auto pick =
    pick_panel_for_point({candidate("a", 0.0f)}, QarVector3{0.0f, -0.25f, 0.0f});
  ASSERT_TRUE(pick.hit);
  EXPECT_EQ(pick.widget_x, 200);
  EXPECT_EQ(pick.widget_y, 300);
}

TEST(PanelPicker, RayPicksTheFirstPanelAlongItsDirection)
{
  const std::vector<PanelHitCandidate> candidates{candidate("back", 0.0f), candidate("front", 1.0f)};
  const auto pick = pick_panel_for_ray(
    candidates, QarVector3{0.0f, -0.25f, 3.0f}, QarVector3{0.0f, 0.0f, -1.0f});
  ASSERT_TRUE(pick.hit);
  EXPECT_EQ(pick.key, "front");
}

TEST(PanelPicker, EmptyCandidateListIsAMiss)
{
  EXPECT_FALSE(pick_panel_for_point({}, QarVector3{0.0f, -0.25f, 0.0f}).hit);
  EXPECT_FALSE(
    pick_panel_for_ray({}, QarVector3{0.0f, -0.25f, 1.0f}, QarVector3{0.0f, 0.0f, -1.0f}).hit);
}

TEST(PanelHitToWidgetPoint, IsUnclampedSoADragPastTheEdgeKeepsItsCoordinates)
{
  PanelHit hit;
  hit.valid = true;
  hit.inside = false;
  hit.u = -0.25f;
  hit.v = 1.5f;

  double x = 0.0;
  double y = 0.0;
  ASSERT_TRUE(qar_qt_streaming::panel_hit_to_widget_point(hit, 400, 600, x, y));
  EXPECT_DOUBLE_EQ(x, -100.0);
  EXPECT_DOUBLE_EQ(y, 900.0);

  hit.valid = false;
  EXPECT_FALSE(qar_qt_streaming::panel_hit_to_widget_point(hit, 400, 600, x, y));
  hit.valid = true;
  EXPECT_FALSE(qar_qt_streaming::panel_hit_to_widget_point(hit, 0, 600, x, y));
}

TEST(PanelHitToWidgetPoint, KeepsSubPixelPrecision)
{
  PanelHit hit;
  hit.valid = true;
  hit.inside = true;
  hit.u = 0.5f;
  hit.v = 0.25f;
  double x = 0.0;
  double y = 0.0;
  ASSERT_TRUE(qar_qt_streaming::panel_hit_to_widget_point(hit, 401, 601, x, y));
  EXPECT_NEAR(x, 200.5, 1e-9);
  EXPECT_NEAR(y, 150.25, 1e-9);
}

TEST(PanelUvToRoomPoint, InvertsTheHitTestThroughTranslationAndRotation)
{
  PanelPlacement panel = axis_aligned_panel();
  panel.pose.position = QarVector3{2.0f, 1.0f, -3.0f};
  panel.pose.orientation = yaw(37.0);

  const float u = 0.8f;
  const float v = 0.3f;
  const QarVector3 point = qar_qt_streaming::panel_uv_to_room_point(panel, u, v);
  const PanelHit hit = hit_test_panel_point(panel, point);
  ASSERT_TRUE(hit.valid);
  EXPECT_TRUE(hit.inside);
  EXPECT_NEAR(hit.u, u, 1e-5f);
  EXPECT_NEAR(hit.v, v, 1e-5f);
  EXPECT_NEAR(hit.distance_m, 0.0f, 1e-5f) << "the point lies ON the plane";

  // Corners map to the panel's physical extent: 1.0 x 0.5 m, unrotated. The
  // viewer's top-LEFT is panel-space +width/2 on the top edge, because +X runs
  // to their left and the pose sits on that edge.
  const QarVector3 top_left =
    qar_qt_streaming::panel_uv_to_room_point(axis_aligned_panel(), 0.0f, 0.0f);
  EXPECT_NEAR(top_left.x, 0.5f, 1e-6f);
  EXPECT_NEAR(top_left.y, 0.0f, 1e-6f);
}
