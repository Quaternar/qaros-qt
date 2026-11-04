// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__SEAM__VALUES_HPP_
#define QAR_QT_STREAMING__SEAM__VALUES_HPP_

/// @file values.hpp
/// @brief The plain values of the panel seam: vectors, poses, panel sizes, panel states, pixel
/// formats, peer ids and time points. Standard library only.
///
/// The field names, the field order and the enumerator values equal those of the QarOS C types
/// they stand for (`QarVector3`, `QarQuaternion`, `QarPose`, `QarGuiPanelSize`, `QarTimePoint`,
/// `QarPeerId`, `QarGuiPanelState`, `QarPixelFormat`), so that a host converts a value by copying
/// its fields. The seam includes no QarOS header: a host that holds the C or the C++ SDK types
/// converts at the call of the seam, and a `static_assert` on the size and the offsets of both
/// sides makes a drift of either side a compile error.

#include <cstddef>
#include <cstdint>

namespace qar_qt_streaming::seam
{

/// @brief A point or a direction in 3D space; the space is named by the field that holds it.
struct Vector3
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

/// @brief A rotation as a Hamilton quaternion, stored x, y, z, w. The default is the identity.
struct Quaternion
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
};

/// @brief An orientation and a position; the space is named by the field that holds it.
struct Pose
{
  Quaternion orientation;
  Vector3 position;
};

/// @brief The size of a panel: the extent of its surface in metres and the zoom of its content.
struct PanelSize
{
  /// Extent of the panel surface along panel-space X, in metres.
  float width_meters = 0.0f;
  /// Extent of the panel surface along panel-space -Y, in metres.
  float height_meters = 0.0f;
  /// Panel-content-space-per-panel-space zoom of the rendered raster; 1.0 fits the content across
  /// the full panel surface.
  float content_scale = 1.0f;
};

/// @brief An instant as nanoseconds since the Unix epoch.
struct TimePoint
{
  std::int64_t unix_ns = 0;
};

/// @brief The number of bytes of a peer id.
inline constexpr std::size_t kPeerIdLength = 16;

/// @brief The identifier of a remote peer: a fixed-size opaque value.
struct PeerId
{
  std::uint8_t data[kPeerIdLength] = {};
};

/// @brief Whether a panel is shown, minimised, hidden or closed. The numeric values are those of
/// the runtime, which reports and accepts them as integers.
enum class PanelState : int
{
  Visible = 0,
  Minimized = 1,
  Hidden = 2,
  Closed = 3
};

/// @brief The pixel layout of a panel's video. The numeric values are those of the runtime.
enum class PixelFormat : int
{
  R8G8B8A8 = 51,
  B8G8R8A8 = 53
};

}  // namespace qar_qt_streaming::seam

#endif  // QAR_QT_STREAMING__SEAM__VALUES_HPP_
