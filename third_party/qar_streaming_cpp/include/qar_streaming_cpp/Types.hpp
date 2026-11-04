#pragma once

// The C API's own types, for interop and for the builders' internals.
// - Opaque C handles are `<Name>Handle`: the plain names are the C++ classes
//   that own them.
// - Every other C struct lives in `qaros::native`. The C++ API never takes or
//   returns one: its value types are the structs of Values.hpp, converted with
//   ToNative / FromNative.

#include <qar_streaming_cpp/Config.hpp>
#include <qar_streaming_cpp/Enums.hpp>

#include <cstddef>
#include <cstdint>

namespace qaros
{

// Opaque handles
using AppVolumeGestureHandle = QarAppVolumeGesture;
using AppVolumeHandle = QarAppVolume;
using CancelTokenHandle = QarCancelToken;
using GuiPanelGestureHandle = QarGuiPanelGesture;
using GuiPanelHandle = QarGuiPanel;
using OnboardingInviteHandle = QarOnboardingInvite;
using PeerSpecHandle = QarPeerSpec;
using RenderFrameInfoHandle = QarRenderFrameInfo;
using RenderSenderHandle = QarRenderSender;
using RenderStreamRequestHandle = QarRenderStreamRequest;
using RuntimeHandle = QarRuntime;
using SessionHandle = QarSession;
using VideoSenderHandle = QarVideoSender;

/// The C API's result; Status wraps it.
using NativeResult = QarResult;

using AppVolumeGestureModeId = uint32_t;
inline constexpr AppVolumeGestureModeId DefaultGestureModeId = QAR_APP_VOLUME_GESTURE_MODE_DEFAULT;
inline constexpr size_t MaxGestureMappingRules = QAR_MAX_APP_VOLUME_GESTURE_MAPPING_RULES;
inline constexpr size_t MaxFrameViews = QAR_MAX_FRAME_VIEWS;
inline constexpr size_t MaxFrameTextures = QAR_MAX_FRAME_TEXTURES;
inline constexpr size_t HandJointCount = QAR_HAND_JOINT_COUNT;

namespace native
{

using StructureHeader = QarStructureHeader;

// Values
using AppVolumeEditingStatus = QarAppVolumeEditingStatus;
using AppVolumeId = QarAppVolumeId;
using AppVolumeSize = QarAppVolumeSize;
using AppWorldAnchor = QarAppWorldAnchor;
using Color = QarColor;
using DeviceHandsWithJoints = QarDeviceHandsWithJoints;
using Fov = QarFov;
using GeoAnchorFrame = QarGeoAnchorFrame;
using GraphicsDeviceId = QarGraphicsDeviceId;
using GuiPanelId = QarGuiPanelId;
using GuiPanelPoint = QarGuiPanelPoint;
using GuiPanelSize = QarGuiPanelSize;
using HandJointLocation = QarHandJointLocation;
using HandJoints = QarHandJoints;
using HandJointVelocity = QarHandJointVelocity;
using NearFar = QarNearFar;
using OnboardingId = QarOnboardingId;
using PeerId = QarPeerId;
using Pose = QarPose;
using Quaternion = QarQuaternion;
using SessionId = QarSessionId;
using StreamId = QarStreamId;
using TextureSize = QarTextureSize;
using TimePoint = QarTimePoint;
using Vector3 = QarVector3;
using Vector3d = QarVector3d;
using VideoFrameCpu = QarVideoFrameCpu;
using VideoFrameLayout = QarVideoFrameLayout;
using VideoFrameView = QarVideoFrameView;
using VideoTextureCpu = QarVideoTextureCpu;

// Gesture configuration
using AppVolumeGestureAllowedPeersExt = QarAppVolumeGestureAllowedPeersExt;
using AppVolumeGestureConfiguration = QarAppVolumeGestureConfiguration;
using AppVolumeGestureMappingRule = QarAppVolumeGestureMappingRule;

// Frame show
using RenderFrameShow = QarRenderFrameShow;
using RenderFrameShowViewOverridesExt = QarRenderFrameShowViewOverridesExt;
using RenderFrameView = QarRenderFrameView;
using RenderFrameViewOverride = QarRenderFrameViewOverride;

// Graphics APIs
#ifdef QAR_ENABLE_D3D11
using StreamParamsD3D11 = QarStreamParamsD3D11;
using VideoFrameD3D11 = QarVideoFrameD3D11;
#endif
#ifdef QAR_ENABLE_OPENGL
using OpenGlContextCallbacks = QarOpenGlContextCallbacks;
using StreamParamsOpenGlEgl = QarStreamParamsOpenGlEgl;
using StreamParamsOpenGlGlx = QarStreamParamsOpenGlGlx;
using StreamParamsOpenGlWgl = QarStreamParamsOpenGlWgl;
using VideoFrameOpenGl = QarVideoFrameOpenGl;
#endif
#ifdef QAR_ENABLE_VULKAN
using StreamParamsVulkan = QarStreamParamsVulkan;
using VideoFrameVulkan = QarVideoFrameVulkan;
#endif

// Init structs and their chained extensions
using AppVolumeInit = QarAppVolumeInit;
using ForgetInit = QarForgetInit;
using GuiPanelInit = QarGuiPanelInit;
using GuiPanelStreamIdExt = QarGuiPanelStreamIdExt;
using GuiPanelUrlExt = QarGuiPanelUrlExt;
using LibraryInit = QarLibraryInit;
using OnboardCodeExt = QarOnboardCodeExt;
using OnboardHostExt = QarOnboardHostExt;
using OnboardInit = QarOnboardInit;
using OnboardInviteExt = QarOnboardInviteExt;
using OnboardPeerIdExt = QarOnboardPeerIdExt;
using PeerPresentation = QarPeerPresentation;
using RejoinInit = QarRejoinInit;
using RenderSenderInit = QarRenderSenderInit;
using RequestInviteInit = QarRequestInviteInit;
using RequestOnboardingCodesInit = QarRequestOnboardingCodesInit;
using RuntimeInit = QarRuntimeInit;
using SessionInviteTargetAppInit = QarSessionInviteTargetAppInit;
using VideoSenderInit = QarVideoSenderInit;

/// The C API's default value of `T` (its `qar_*_default()`).
template<typename T>
T Default() noexcept;

} // namespace native

} // namespace qaros

#include <qar_streaming_cpp/detail/Types.inl>
