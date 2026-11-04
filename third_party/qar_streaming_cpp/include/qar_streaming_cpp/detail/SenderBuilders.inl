#pragma once

// Implementation of SenderBuilders.hpp.

#include <qar_streaming_cpp/detail/Chain.hpp>

#include <algorithm>
#include <utility>

namespace qaros
{

inline native::RenderFrameView
ToNative(const RenderFrameView& view) noexcept
{
	native::RenderFrameView native = {};
	native.data_type = ToNative(view.Type);
	native.eye = ToNative(view.Eye);
	native.texture_index = view.TextureIndex;
	return native;
}

inline RenderFrameView
FromNative(const native::RenderFrameView& view) noexcept
{
	RenderFrameView value;
	value.Type = FromNative(view.data_type);
	value.Eye = FromNative(view.eye);
	value.TextureIndex = view.texture_index;
	return value;
}

namespace detail
{

inline Result<void>
TooManyViews(size_t count, const char* message) noexcept
{
	if(count > MaxFrameViews)
	{
		return Result<void>(Status::Error(StatusCode::ArgumentNotSupported, message));
	}
	return Result<void>();
}

// GraphicsBackend

inline void
GraphicsBackend::UseCpu() noexcept
{
	m_api = GraphicsApi::Cpu;
	m_extension = nullptr;
}

#ifdef QAR_ENABLE_D3D11
inline void
GraphicsBackend::UseD3D11(const D3D11Device& device) noexcept
{
	m_d3d11 = native::Default<native::StreamParamsD3D11>();
	m_d3d11->d3d11_device = device.Device;
	m_d3d11->d3d11_context = device.Context;
	m_d3d11->color_bind_flags = device.ColorBindFlags;
	m_d3d11->depth_bind_flags = device.DepthBindFlags;
	m_d3d11->acquire_keyed_mutex_sync = device.AcquireKeyedMutexSync;
	m_api = GraphicsApi::D3D11;
	m_extension = &m_d3d11->header;
}
#endif

#ifdef QAR_ENABLE_VULKAN
inline void
GraphicsBackend::UseVulkan(VkPhysicalDevice physical_device, VkDevice device) noexcept
{
	m_vulkan = native::Default<native::StreamParamsVulkan>();
	m_vulkan->physical_device = physical_device;
	m_vulkan->device = device;
	m_api = GraphicsApi::Vulkan;
	m_extension = &m_vulkan->header;
}
#endif

#ifdef QAR_ENABLE_OPENGL
inline void
GraphicsBackend::UseOpenGlEgl(const OpenGlEglContext& context) noexcept
{
	m_openGlEgl = native::Default<native::StreamParamsOpenGlEgl>();
	m_openGlEgl->display = context.Display;
	m_openGlEgl->draw_surface = context.DrawSurface;
	m_openGlEgl->read_surface = context.ReadSurface;
	m_openGlEgl->context = context.Context;
	m_openGlEgl->callbacks = ToNative(context.Callbacks);
	m_api = GraphicsApi::OpenGl;
	m_extension = &m_openGlEgl->header;
}

inline void
GraphicsBackend::UseOpenGlWgl(const OpenGlWglContext& context) noexcept
{
	m_openGlWgl = native::Default<native::StreamParamsOpenGlWgl>();
	m_openGlWgl->device_context = context.DeviceContext;
	m_openGlWgl->context = context.Context;
	m_openGlWgl->callbacks = ToNative(context.Callbacks);
	m_api = GraphicsApi::OpenGl;
	m_extension = &m_openGlWgl->header;
}

inline void
GraphicsBackend::UseOpenGlGlx(const OpenGlGlxContext& context) noexcept
{
	m_openGlGlx = native::Default<native::StreamParamsOpenGlGlx>();
	m_openGlGlx->display = context.Display;
	m_openGlGlx->drawable = context.Drawable;
	m_openGlGlx->context = context.Context;
	m_openGlGlx->callbacks = ToNative(context.Callbacks);
	m_api = GraphicsApi::OpenGl;
	m_extension = &m_openGlGlx->header;
}
#endif

inline GraphicsApi
GraphicsBackend::Api() const noexcept
{
	return m_api;
}

inline native::StructureHeader*
GraphicsBackend::Extension() noexcept
{
	return m_extension;
}

} // namespace detail

// RenderSenderBuilder

inline RenderSenderBuilder::RenderSenderBuilder(
	const PeerId& target, const AppVolumeId& app_volume
) noexcept
	: m_init(native::Default<native::RenderSenderInit>())
	, m_appVolume(ToNative(app_volume))
{
	m_init.peer_id = ToNative(target);
	Sync();
}

inline RenderSenderBuilder&
RenderSenderBuilder::WithTextureLayout(FrameLayout layout) noexcept
{
	m_init.texture_layout = ToNative(layout);
	return *this;
}

inline RenderSenderBuilder&
RenderSenderBuilder::WithViews(std::vector<RenderFrameView> views) noexcept
{
	m_views = std::move(views);
	Sync();
	return *this;
}

inline RenderSenderBuilder&
RenderSenderBuilder::WithColorFormat(PixelFormat format) noexcept
{
	m_init.color_format = ToNative(format);
	return *this;
}

inline RenderSenderBuilder&
RenderSenderBuilder::WithDepthFormat(PixelFormat format) noexcept
{
	m_init.depth_format = ToNative(format);
	return *this;
}

inline RenderSenderBuilder&
RenderSenderBuilder::WithAutoReconnects(bool enabled) noexcept
{
	m_init.enable_auto_reconnects = enabled;
	return *this;
}

inline RenderSenderBuilder&
RenderSenderBuilder::WithCpu() noexcept
{
	m_backend.UseCpu();
	Sync();
	return *this;
}

#ifdef QAR_ENABLE_D3D11
inline RenderSenderBuilder&
RenderSenderBuilder::WithD3D11(const D3D11Device& device) noexcept
{
	m_backend.UseD3D11(device);
	Sync();
	return *this;
}
#endif

#ifdef QAR_ENABLE_VULKAN
inline RenderSenderBuilder&
RenderSenderBuilder::WithVulkan(VkPhysicalDevice physical_device, VkDevice device) noexcept
{
	m_backend.UseVulkan(physical_device, device);
	Sync();
	return *this;
}
#endif

#ifdef QAR_ENABLE_OPENGL
inline RenderSenderBuilder&
RenderSenderBuilder::WithOpenGlEgl(const OpenGlEglContext& context) noexcept
{
	m_backend.UseOpenGlEgl(context);
	Sync();
	return *this;
}

inline RenderSenderBuilder&
RenderSenderBuilder::WithOpenGlWgl(const OpenGlWglContext& context) noexcept
{
	m_backend.UseOpenGlWgl(context);
	Sync();
	return *this;
}

inline RenderSenderBuilder&
RenderSenderBuilder::WithOpenGlGlx(const OpenGlGlxContext& context) noexcept
{
	m_backend.UseOpenGlGlx(context);
	Sync();
	return *this;
}
#endif

inline Result<void>
RenderSenderBuilder::Validate() const noexcept
{
	const size_t count = m_views.has_value() ? m_views->size() : 0;
	return detail::TooManyViews(count, "render sender has more views than MaxFrameViews");
}

inline const native::RenderSenderInit&
RenderSenderBuilder::Get() const noexcept
{
	return m_init;
}

inline void
RenderSenderBuilder::Sync() noexcept
{
	m_init.app_volume_id = &m_appVolume;
	if(m_views.has_value())
	{
		const size_t count = std::min(m_views->size(), MaxFrameViews);
		for(size_t index = 0; index < count; ++index)
		{
			m_init.frame_views[index] = ToNative((*m_views)[index]);
		}
		m_init.frame_views_count = count;
	}
	m_init.graphics_api = ToNative(m_backend.Api());
	detail::Link(&m_init.header, { m_backend.Extension() });
}

// VideoSenderBuilder

inline VideoSenderBuilder::VideoSenderBuilder(VideoFrameLayout layout) noexcept
	: m_init(native::Default<native::VideoSenderInit>())
	, m_layout(std::move(layout))
{
	m_init.layout = ToNative(m_layout);
	Sync();
}

inline VideoSenderBuilder&
VideoSenderBuilder::WithReconnects(bool enabled, std::chrono::milliseconds timeout) noexcept
{
	m_init.enable_reconnects = enabled;
	m_init.reconnect_timeout_ms = static_cast<uint64_t>(timeout.count());
	return *this;
}

inline VideoSenderBuilder&
VideoSenderBuilder::WithCpu() noexcept
{
	m_backend.UseCpu();
	Sync();
	return *this;
}

#ifdef QAR_ENABLE_D3D11
inline VideoSenderBuilder&
VideoSenderBuilder::WithD3D11(const D3D11Device& device) noexcept
{
	m_backend.UseD3D11(device);
	Sync();
	return *this;
}
#endif

#ifdef QAR_ENABLE_VULKAN
inline VideoSenderBuilder&
VideoSenderBuilder::WithVulkan(VkPhysicalDevice physical_device, VkDevice device) noexcept
{
	m_backend.UseVulkan(physical_device, device);
	Sync();
	return *this;
}
#endif

#ifdef QAR_ENABLE_OPENGL
inline VideoSenderBuilder&
VideoSenderBuilder::WithOpenGlEgl(const OpenGlEglContext& context) noexcept
{
	m_backend.UseOpenGlEgl(context);
	Sync();
	return *this;
}

inline VideoSenderBuilder&
VideoSenderBuilder::WithOpenGlWgl(const OpenGlWglContext& context) noexcept
{
	m_backend.UseOpenGlWgl(context);
	Sync();
	return *this;
}

inline VideoSenderBuilder&
VideoSenderBuilder::WithOpenGlGlx(const OpenGlGlxContext& context) noexcept
{
	m_backend.UseOpenGlGlx(context);
	Sync();
	return *this;
}
#endif

inline Result<void>
VideoSenderBuilder::Validate() const noexcept
{
	return m_layout.Validate();
}

inline const native::VideoSenderInit&
VideoSenderBuilder::Get() const noexcept
{
	return m_init;
}

inline void
VideoSenderBuilder::Sync() noexcept
{
	m_init.graphics_api = ToNative(m_backend.Api());
	detail::Link(&m_init.header, { m_backend.Extension() });
}

// FrameShowBuilder

inline FrameShowBuilder::FrameShowBuilder() noexcept
	: m_show(native::Default<native::RenderFrameShow>())
	, m_overridesExt()
{
	m_overridesExt.header.type = QAR_STRUCTURE_TYPE_RENDERING_END_FRAME_VIEW_OVERRIDES_EXT;
	Sync();
}

inline FrameShowBuilder&
FrameShowBuilder::WithNearFar(float near_plane, float far_plane) noexcept
{
	m_show.rendered_near_far.near_plane = near_plane;
	m_show.rendered_near_far.far_plane = far_plane;
	return *this;
}

inline FrameShowBuilder&
FrameShowBuilder::WithDepthScale(float scale) noexcept
{
	m_show.depth_scale = scale;
	return *this;
}

inline FrameShowBuilder&
FrameShowBuilder::OverrideViewPose(size_t view_index, const Pose& pose) noexcept
{
	native::RenderFrameViewOverride& view = OverrideOf(view_index);
	view.override_pose = true;
	view.pose = ToNative(pose);
	Sync();
	return *this;
}

inline FrameShowBuilder&
FrameShowBuilder::OverrideViewFov(size_t view_index, const Fov& fov) noexcept
{
	native::RenderFrameViewOverride& view = OverrideOf(view_index);
	view.override_fov = true;
	view.fov = ToNative(fov);
	Sync();
	return *this;
}

inline Result<void>
FrameShowBuilder::Validate() const noexcept
{
	return detail::TooManyViews(
		m_overrides.size(), "frame show overrides more views than MaxFrameViews"
	);
}

inline const native::RenderFrameShow&
FrameShowBuilder::Get() const noexcept
{
	return m_show;
}

inline native::RenderFrameViewOverride&
FrameShowBuilder::OverrideOf(size_t view_index) noexcept
{
	const auto same_view = [view_index](const native::RenderFrameViewOverride& view)
	{
		return view.view_index == view_index;
	};
	const auto found = std::find_if(m_overrides.begin(), m_overrides.end(), same_view);
	if(found != m_overrides.end())
	{
		return *found;
	}
	native::RenderFrameViewOverride view = {};
	view.view_index = view_index;
	view.pose = native::Default<native::Pose>();
	view.fov = native::Default<native::Fov>();
	m_overrides.push_back(view);
	return m_overrides.back();
}

inline void
FrameShowBuilder::Sync() noexcept
{
	const size_t count = std::min(m_overrides.size(), MaxFrameViews);
	std::copy_n(m_overrides.begin(), count, m_overridesExt.view_overrides);
	m_overridesExt.view_overrides_count = count;
	native::StructureHeader* overrides = count > 0 ? &m_overridesExt.header : nullptr;
	detail::Link(&m_show.header, { overrides });
}

} // namespace qaros
