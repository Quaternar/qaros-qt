// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QAROS__CONVERSIONS_HPP_
#define QAR_QT_STREAMING__QAROS__CONVERSIONS_HPP_

/// @file conversions.hpp
/// @brief The values of the panel seam (standard library only) and the values of the QarOS C++
/// SDK, converted field by field in both directions.
///
/// The seam mirrors the SDK's structs with the same fields in the same order, so a drift of
/// either side is a compile error: the `static_assert`s below compare the sizes, the offsets and
/// the enumerator values of both sides.

#include "qar_qt_streaming/seam/values.hpp"

#include <qar_streaming_cpp/Values.hpp>

#include <cstddef>

namespace qar_qt_streaming::qaros_backend
{

static_assert(sizeof(seam::Vector3) == sizeof(qaros::Vector3));
static_assert(offsetof(seam::Vector3, x) == offsetof(qaros::Vector3, X));
static_assert(offsetof(seam::Vector3, y) == offsetof(qaros::Vector3, Y));
static_assert(offsetof(seam::Vector3, z) == offsetof(qaros::Vector3, Z));
static_assert(sizeof(seam::Quaternion) == sizeof(qaros::Quaternion));
static_assert(offsetof(seam::Quaternion, x) == offsetof(qaros::Quaternion, X));
static_assert(offsetof(seam::Quaternion, y) == offsetof(qaros::Quaternion, Y));
static_assert(offsetof(seam::Quaternion, z) == offsetof(qaros::Quaternion, Z));
static_assert(offsetof(seam::Quaternion, w) == offsetof(qaros::Quaternion, W));
static_assert(sizeof(seam::Pose) == sizeof(qaros::Pose));
static_assert(offsetof(seam::Pose, orientation) == offsetof(qaros::Pose, Orientation));
static_assert(offsetof(seam::Pose, position) == offsetof(qaros::Pose, Position));
static_assert(sizeof(seam::PanelSize) == sizeof(qaros::GuiPanelSize));
static_assert(
  offsetof(seam::PanelSize, width_meters) == offsetof(qaros::GuiPanelSize, WidthMeters));
static_assert(
  offsetof(seam::PanelSize, height_meters) == offsetof(qaros::GuiPanelSize, HeightMeters));
static_assert(
  offsetof(seam::PanelSize, content_scale) == offsetof(qaros::GuiPanelSize, ContentScale));
static_assert(sizeof(seam::PeerId) == sizeof(qaros::PeerId));
static_assert(sizeof(seam::PeerId::data) == sizeof(qaros::PeerId::Bytes));

static_assert(static_cast<int>(seam::PanelState::Visible) ==
  static_cast<int>(qaros::GuiPanelState::Visible));
static_assert(static_cast<int>(seam::PanelState::Minimized) ==
  static_cast<int>(qaros::GuiPanelState::Minimized));
static_assert(static_cast<int>(seam::PanelState::Hidden) ==
  static_cast<int>(qaros::GuiPanelState::Hidden));
static_assert(static_cast<int>(seam::PanelState::Closed) ==
  static_cast<int>(qaros::GuiPanelState::Closed));
static_assert(static_cast<int>(seam::PixelFormat::R8G8B8A8) ==
  static_cast<int>(qaros::PixelFormat::R8G8B8A8));
static_assert(static_cast<int>(seam::PixelFormat::B8G8R8A8) ==
  static_cast<int>(qaros::PixelFormat::B8G8R8A8));

inline qaros::Vector3
to_sdk(const seam::Vector3 & value)
{
  return qaros::Vector3{value.x, value.y, value.z};
}

inline seam::Vector3
to_seam(const qaros::Vector3 & value)
{
  return seam::Vector3{value.X, value.Y, value.Z};
}

inline qaros::Quaternion
to_sdk(const seam::Quaternion & value)
{
  return qaros::Quaternion{value.x, value.y, value.z, value.w};
}

inline seam::Quaternion
to_seam(const qaros::Quaternion & value)
{
  return seam::Quaternion{value.X, value.Y, value.Z, value.W};
}

inline qaros::Pose
to_sdk(const seam::Pose & value)
{
  return qaros::Pose{to_sdk(value.orientation), to_sdk(value.position)};
}

inline seam::Pose
to_seam(const qaros::Pose & value)
{
  return seam::Pose{to_seam(value.Orientation), to_seam(value.Position)};
}

inline qaros::GuiPanelSize
to_sdk(const seam::PanelSize & value)
{
  return qaros::GuiPanelSize{value.width_meters, value.height_meters, value.content_scale};
}

inline seam::PanelSize
to_seam(const qaros::GuiPanelSize & value)
{
  return seam::PanelSize{value.WidthMeters, value.HeightMeters, value.ContentScale};
}

inline qaros::GuiPanelState
to_sdk(seam::PanelState value)
{
  return static_cast<qaros::GuiPanelState>(static_cast<int>(value));
}

inline seam::PanelState
to_seam(qaros::GuiPanelState value)
{
  return static_cast<seam::PanelState>(static_cast<int>(value));
}

inline qaros::PixelFormat
to_sdk(seam::PixelFormat value)
{
  return static_cast<qaros::PixelFormat>(static_cast<int>(value));
}

inline seam::PeerId
to_seam(const qaros::PeerId & value)
{
  seam::PeerId out;
  for (std::size_t i = 0; i < seam::kPeerIdLength; ++i) {
    out.data[i] = value.Bytes[i];
  }
  return out;
}

}  // namespace qar_qt_streaming::qaros_backend

#endif  // QAR_QT_STREAMING__QAROS__CONVERSIONS_HPP_
