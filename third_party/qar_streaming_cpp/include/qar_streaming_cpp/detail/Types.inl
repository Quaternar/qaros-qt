#pragma once

// Implementation of Types.hpp.

#include <type_traits>

namespace qaros::native
{

namespace detail
{

template<typename T>
struct NoDefault : std::false_type
{
};

} // namespace detail

template<typename T>
T
Default() noexcept
{
	static_assert(detail::NoDefault<T>::value, "the C API has no default for this type");
	return T{};
}

template<>
inline AppVolumeId
Default<AppVolumeId>() noexcept
{
	return qar_app_volume_id_default();
}

template<>
inline AppVolumeInit
Default<AppVolumeInit>() noexcept
{
	return qar_app_volume_init_default();
}

template<>
inline AppVolumeSize
Default<AppVolumeSize>() noexcept
{
	return qar_app_volume_size_default();
}

template<>
inline ForgetInit
Default<ForgetInit>() noexcept
{
	return qar_forget_init_default();
}

template<>
inline Fov
Default<Fov>() noexcept
{
	return qar_fov_default();
}

template<>
inline GraphicsDeviceId
Default<GraphicsDeviceId>() noexcept
{
	return qar_graphics_device_id_default();
}

template<>
inline GuiPanelId
Default<GuiPanelId>() noexcept
{
	return qar_gui_panel_id_default();
}

template<>
inline GuiPanelInit
Default<GuiPanelInit>() noexcept
{
	return qar_gui_panel_init_default();
}

template<>
inline GuiPanelSize
Default<GuiPanelSize>() noexcept
{
	return qar_gui_panel_size_default();
}

template<>
inline LibraryInit
Default<LibraryInit>() noexcept
{
	return qar_library_init_default();
}

template<>
inline OnboardCodeExt
Default<OnboardCodeExt>() noexcept
{
	return qar_onboard_code_ext_default();
}

template<>
inline OnboardHostExt
Default<OnboardHostExt>() noexcept
{
	return qar_onboard_host_ext_default();
}

template<>
inline OnboardInit
Default<OnboardInit>() noexcept
{
	return qar_onboard_init_default();
}

template<>
inline OnboardInviteExt
Default<OnboardInviteExt>() noexcept
{
	return qar_onboard_invite_ext_default();
}

template<>
inline OnboardPeerIdExt
Default<OnboardPeerIdExt>() noexcept
{
	return qar_onboard_peer_id_ext_default();
}

template<>
inline OnboardingId
Default<OnboardingId>() noexcept
{
	return qar_onboarding_id_default();
}

template<>
inline PeerId
Default<PeerId>() noexcept
{
	return qar_peer_id_default();
}

template<>
inline Pose
Default<Pose>() noexcept
{
	return qar_pose_default();
}

template<>
inline Quaternion
Default<Quaternion>() noexcept
{
	return qar_quaternion_default();
}

template<>
inline RejoinInit
Default<RejoinInit>() noexcept
{
	return qar_rejoin_init_default();
}

template<>
inline RenderFrameShow
Default<RenderFrameShow>() noexcept
{
	return qar_render_frame_show_default();
}

template<>
inline RenderSenderInit
Default<RenderSenderInit>() noexcept
{
	return qar_render_sender_init_default();
}

template<>
inline RequestInviteInit
Default<RequestInviteInit>() noexcept
{
	return qar_request_invite_init_default();
}

template<>
inline RequestOnboardingCodesInit
Default<RequestOnboardingCodesInit>() noexcept
{
	return qar_request_onboarding_codes_init_default();
}

template<>
inline RuntimeInit
Default<RuntimeInit>() noexcept
{
	return qar_runtime_init_default();
}

template<>
inline SessionId
Default<SessionId>() noexcept
{
	return qar_session_id_default();
}

template<>
inline SessionInviteTargetAppInit
Default<SessionInviteTargetAppInit>() noexcept
{
	return qar_session_invite_target_app_init_default();
}

template<>
inline StreamId
Default<StreamId>() noexcept
{
	return qar_stream_id_default();
}

template<>
inline TimePoint
Default<TimePoint>() noexcept
{
	return qar_time_point_default();
}

template<>
inline Vector3
Default<Vector3>() noexcept
{
	return qar_vector3_default();
}

template<>
inline VideoFrameCpu
Default<VideoFrameCpu>() noexcept
{
	return qar_video_frame_cpu_default();
}

template<>
inline VideoFrameLayout
Default<VideoFrameLayout>() noexcept
{
	return qar_video_frame_layout_default();
}

template<>
inline VideoSenderInit
Default<VideoSenderInit>() noexcept
{
	return qar_video_sender_init_default();
}

#ifdef QAR_ENABLE_D3D11
template<>
inline VideoFrameD3D11
Default<VideoFrameD3D11>() noexcept
{
	return qar_video_frame_d3d11_default();
}
#endif

#ifdef QAR_ENABLE_OPENGL
template<>
inline VideoFrameOpenGl
Default<VideoFrameOpenGl>() noexcept
{
	return qar_video_frame_opengl_default();
}
#endif

#ifdef QAR_ENABLE_VULKAN
template<>
inline VideoFrameVulkan
Default<VideoFrameVulkan>() noexcept
{
	return qar_video_frame_vulkan_default();
}
#endif

template<>
inline AppVolumeGestureAllowedPeersExt
Default<AppVolumeGestureAllowedPeersExt>() noexcept
{
	return qar_app_volume_gesture_allowed_peers_ext_default();
}

template<>
inline AppVolumeGestureConfiguration
Default<AppVolumeGestureConfiguration>() noexcept
{
	return qar_app_volume_gesture_configuration_default();
}

template<>
inline GuiPanelStreamIdExt
Default<GuiPanelStreamIdExt>() noexcept
{
	return qar_gui_panel_stream_id_ext_default();
}

template<>
inline GuiPanelUrlExt
Default<GuiPanelUrlExt>() noexcept
{
	return qar_gui_panel_url_ext_default();
}

template<>
inline NearFar
Default<NearFar>() noexcept
{
	return qar_near_far_default();
}

template<>
inline PeerPresentation
Default<PeerPresentation>() noexcept
{
	return qar_peer_presentation_default();
}

#ifdef QAR_ENABLE_D3D11
template<>
inline StreamParamsD3D11
Default<StreamParamsD3D11>() noexcept
{
	return qar_stream_params_d3d11_default();
}
#endif

#ifdef QAR_ENABLE_OPENGL
template<>
inline StreamParamsOpenGlEgl
Default<StreamParamsOpenGlEgl>() noexcept
{
	return qar_stream_params_opengl_egl_default();
}

template<>
inline StreamParamsOpenGlGlx
Default<StreamParamsOpenGlGlx>() noexcept
{
	return qar_stream_params_opengl_glx_default();
}

template<>
inline StreamParamsOpenGlWgl
Default<StreamParamsOpenGlWgl>() noexcept
{
	return qar_stream_params_opengl_wgl_default();
}
#endif

#ifdef QAR_ENABLE_VULKAN
template<>
inline StreamParamsVulkan
Default<StreamParamsVulkan>() noexcept
{
	return qar_stream_params_vulkan_default();
}
#endif

} // namespace qaros::native
