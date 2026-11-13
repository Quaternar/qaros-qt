// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The plain values of the seam: their defaults, their layout and the numeric values of the two
// enumerations, which are the values the runtime reports and accepts.

#include "qar_qt_streaming/seam/values.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

using namespace qar_qt_streaming::seam;

namespace
{

// The layout a host copies field by field: the same sizes and offsets as the runtime's C structs.
static_assert(sizeof(Vector3) == 12 && offsetof(Vector3, y) == 4 && offsetof(Vector3, z) == 8,
  "Vector3 is three floats");
static_assert(sizeof(Quaternion) == 16 && offsetof(Quaternion, w) == 12,
  "Quaternion is four floats, w last");
static_assert(sizeof(Pose) == 28 && offsetof(Pose, orientation) == 0 &&
  offsetof(Pose, position) == 16, "Pose is the orientation, then the position");
static_assert(sizeof(PanelSize) == 12 && offsetof(PanelSize, height_meters) == 4 &&
  offsetof(PanelSize, content_scale) == 8, "PanelSize is width, height, content scale");
static_assert(sizeof(TimePoint) == 8, "TimePoint is one 64-bit count");
static_assert(sizeof(PeerId) == 16 && kPeerIdLength == 16, "PeerId is sixteen bytes");
static_assert(std::is_standard_layout<Pose>::value && std::is_standard_layout<PeerId>::value,
  "the values are plain data");
static_assert(sizeof(PanelState) == sizeof(int) && sizeof(PixelFormat) == sizeof(int),
  "the enumerations are int sized");

}  // namespace

TEST(Values, AVector3StartsAtTheOrigin)
{
  const Vector3 v{};
  EXPECT_FLOAT_EQ(v.x, 0.0f);
  EXPECT_FLOAT_EQ(v.y, 0.0f);
  EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(Values, AQuaternionStartsAsTheIdentity)
{
  const Quaternion q{};
  EXPECT_FLOAT_EQ(q.x, 0.0f);
  EXPECT_FLOAT_EQ(q.y, 0.0f);
  EXPECT_FLOAT_EQ(q.z, 0.0f);
  EXPECT_FLOAT_EQ(q.w, 1.0f);
}

TEST(Values, APoseStartsAtTheOriginWithTheIdentityRotation)
{
  const Pose pose{};
  EXPECT_FLOAT_EQ(pose.orientation.w, 1.0f);
  EXPECT_FLOAT_EQ(pose.orientation.x, 0.0f);
  EXPECT_FLOAT_EQ(pose.position.x, 0.0f);
  EXPECT_FLOAT_EQ(pose.position.y, 0.0f);
  EXPECT_FLOAT_EQ(pose.position.z, 0.0f);
}

TEST(Values, APoseTakesItsOrientationBeforeItsPosition)
{
  const Pose pose{{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 2.0f, 3.0f}};
  EXPECT_FLOAT_EQ(pose.orientation.w, 1.0f);
  EXPECT_FLOAT_EQ(pose.position.x, 1.0f);
  EXPECT_FLOAT_EQ(pose.position.y, 2.0f);
  EXPECT_FLOAT_EQ(pose.position.z, 3.0f);
}

TEST(Values, APanelSizeDefaultsToAContentScaleOfOne)
{
  const PanelSize size{};
  EXPECT_FLOAT_EQ(size.width_meters, 0.0f);
  EXPECT_FLOAT_EQ(size.height_meters, 0.0f);
  EXPECT_FLOAT_EQ(size.content_scale, 1.0f);

  const PanelSize given{0.8f, 0.5f, 2.0f};
  EXPECT_FLOAT_EQ(given.width_meters, 0.8f);
  EXPECT_FLOAT_EQ(given.height_meters, 0.5f);
  EXPECT_FLOAT_EQ(given.content_scale, 2.0f);
}

TEST(Values, ATimePointStartsAtTheEpoch)
{
  const TimePoint t{};
  EXPECT_EQ(t.unix_ns, std::int64_t{0});
  const TimePoint later{1700000000123456789LL};
  EXPECT_EQ(later.unix_ns, std::int64_t{1700000000123456789LL});
}

TEST(Values, APeerIdStartsAsSixteenZeroBytes)
{
  const PeerId id{};
  for (std::size_t i = 0; i < kPeerIdLength; ++i) {
    EXPECT_EQ(id.data[i], std::uint8_t{0}) << "byte " << i;
  }
}

TEST(Values, PanelStatesCarryTheNumbersOfTheRuntime)
{
  EXPECT_EQ(static_cast<int>(PanelState::Visible), 0);
  EXPECT_EQ(static_cast<int>(PanelState::Minimized), 1);
  EXPECT_EQ(static_cast<int>(PanelState::Hidden), 2);
  EXPECT_EQ(static_cast<int>(PanelState::Closed), 3);
}

TEST(Values, PixelFormatsCarryTheNumbersOfTheRuntime)
{
  EXPECT_EQ(static_cast<int>(PixelFormat::R8G8B8A8), 51);
  EXPECT_EQ(static_cast<int>(PixelFormat::B8G8R8A8), 53);
}
