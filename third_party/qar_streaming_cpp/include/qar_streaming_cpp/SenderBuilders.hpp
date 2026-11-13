#pragma once

// Builders of the render and video sender inits with their graphics API
// extension, and of native::RenderFrameShow with its view overrides (C modules
// detail/render_stream_sender.h, detail/video_sender.h). Keep a builder alive
// until the call has started; not copyable: the chain points into it.

#include <qar_streaming_cpp/Values.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace qaros
{

/// One view of a rendered frame: what it holds, for which eye, in which
/// texture.
struct RenderFrameView
{
	VideoFrameViewType Type = VideoFrameViewType::Color;
	VideoFrameViewEye Eye = VideoFrameViewEye::None;
	uint32_t TextureIndex = 0;
};

native::RenderFrameView ToNative(const RenderFrameView& view) noexcept;
RenderFrameView FromNative(const native::RenderFrameView& view) noexcept;

namespace detail
{

/// The graphics API of a sender and the one parameter extension it chains.
class GraphicsBackend
{
public:
	GraphicsBackend() noexcept = default;
	GraphicsBackend(const GraphicsBackend&) = delete;
	GraphicsBackend& operator=(const GraphicsBackend&) = delete;

	void UseCpu() noexcept;
#ifdef QAR_ENABLE_D3D11
	void UseD3D11(const D3D11Device& device) noexcept;
#endif
#ifdef QAR_ENABLE_VULKAN
	void UseVulkan(VkPhysicalDevice physical_device, VkDevice device) noexcept;
#endif
#ifdef QAR_ENABLE_OPENGL
	void UseOpenGlEgl(const OpenGlEglContext& context) noexcept;
	void UseOpenGlWgl(const OpenGlWglContext& context) noexcept;
	void UseOpenGlGlx(const OpenGlGlxContext& context) noexcept;
#endif

	[[nodiscard]]
	GraphicsApi Api() const noexcept;
	/// Null for the CPU.
	[[nodiscard]]
	native::StructureHeader* Extension() noexcept;

private:
	GraphicsApi m_api = GraphicsApi::Cpu;
	native::StructureHeader* m_extension = nullptr;
#ifdef QAR_ENABLE_D3D11
	std::optional<native::StreamParamsD3D11> m_d3d11;
#endif
#ifdef QAR_ENABLE_VULKAN
	std::optional<native::StreamParamsVulkan> m_vulkan;
#endif
#ifdef QAR_ENABLE_OPENGL
	std::optional<native::StreamParamsOpenGlEgl> m_openGlEgl;
	std::optional<native::StreamParamsOpenGlWgl> m_openGlWgl;
	std::optional<native::StreamParamsOpenGlGlx> m_openGlGlx;
#endif
};

} // namespace detail

/// The options of RenderSenderControl::Create. The graphics API is one of
/// WithCpu (the default), WithD3D11, WithVulkan or WithOpenGl*; the later call
/// replaces the earlier one.
class RenderSenderBuilder
{
public:
	/// Renders for `target` the content of `app_volume`.
	RenderSenderBuilder(const PeerId& target, const AppVolumeId& app_volume) noexcept;
	RenderSenderBuilder(const RenderSenderBuilder&) = delete;
	RenderSenderBuilder& operator=(const RenderSenderBuilder&) = delete;

	RenderSenderBuilder& WithTextureLayout(FrameLayout layout) noexcept;
	/// Replaces the default views; every frame comes in this order.
	RenderSenderBuilder& WithViews(std::vector<RenderFrameView> views) noexcept;
	RenderSenderBuilder& WithColorFormat(PixelFormat format) noexcept;
	RenderSenderBuilder& WithDepthFormat(PixelFormat format) noexcept;
	RenderSenderBuilder& WithAutoReconnects(bool enabled) noexcept;

	RenderSenderBuilder& WithCpu() noexcept;
#ifdef QAR_ENABLE_D3D11
	RenderSenderBuilder& WithD3D11(const D3D11Device& device) noexcept;
#endif
#ifdef QAR_ENABLE_VULKAN
	RenderSenderBuilder& WithVulkan(VkPhysicalDevice physical_device, VkDevice device) noexcept;
#endif
#ifdef QAR_ENABLE_OPENGL
	RenderSenderBuilder& WithOpenGlEgl(const OpenGlEglContext& context) noexcept;
	RenderSenderBuilder& WithOpenGlWgl(const OpenGlWglContext& context) noexcept;
	RenderSenderBuilder& WithOpenGlGlx(const OpenGlGlxContext& context) noexcept;
#endif

	/// Fails with more than MaxFrameViews views.
	[[nodiscard]]
	Result<void> Validate() const noexcept;
	[[nodiscard]]
	const native::RenderSenderInit& Get() const noexcept;

private:
	void Sync() noexcept;

	native::RenderSenderInit m_init;
	native::AppVolumeId m_appVolume;
	std::optional<std::vector<RenderFrameView>> m_views;
	detail::GraphicsBackend m_backend;
};

/// The options of VideoSenderControl::Create. Graphics API as for
/// RenderSenderBuilder.
class VideoSenderBuilder
{
public:
	explicit VideoSenderBuilder(VideoFrameLayout layout) noexcept;
	VideoSenderBuilder(const VideoSenderBuilder&) = delete;
	VideoSenderBuilder& operator=(const VideoSenderBuilder&) = delete;

	/// Reconnects a dropped transfer for up to `timeout`.
	VideoSenderBuilder& WithReconnects(bool enabled, std::chrono::milliseconds timeout) noexcept;

	VideoSenderBuilder& WithCpu() noexcept;
#ifdef QAR_ENABLE_D3D11
	VideoSenderBuilder& WithD3D11(const D3D11Device& device) noexcept;
#endif
#ifdef QAR_ENABLE_VULKAN
	VideoSenderBuilder& WithVulkan(VkPhysicalDevice physical_device, VkDevice device) noexcept;
#endif
#ifdef QAR_ENABLE_OPENGL
	VideoSenderBuilder& WithOpenGlEgl(const OpenGlEglContext& context) noexcept;
	VideoSenderBuilder& WithOpenGlWgl(const OpenGlWglContext& context) noexcept;
	VideoSenderBuilder& WithOpenGlGlx(const OpenGlGlxContext& context) noexcept;
#endif

	/// Fails when the layout does.
	[[nodiscard]]
	Result<void> Validate() const noexcept;
	[[nodiscard]]
	const native::VideoSenderInit& Get() const noexcept;

private:
	void Sync() noexcept;

	native::VideoSenderInit m_init;
	VideoFrameLayout m_layout;
	detail::GraphicsBackend m_backend;
};

/// The options of RenderSender::ShowFrame.
class FrameShowBuilder
{
public:
	FrameShowBuilder() noexcept;
	FrameShowBuilder(const FrameShowBuilder&) = delete;
	FrameShowBuilder& operator=(const FrameShowBuilder&) = delete;

	/// The near and far planes the frame was rendered with.
	FrameShowBuilder& WithNearFar(float near_plane, float far_plane) noexcept;
	FrameShowBuilder& WithDepthScale(float scale) noexcept;
	/// The pose view `view_index` was really rendered with, when it differs
	/// from the frame info's.
	FrameShowBuilder& OverrideViewPose(size_t view_index, const Pose& pose) noexcept;
	/// The field of view view `view_index` was really rendered with.
	FrameShowBuilder& OverrideViewFov(size_t view_index, const Fov& fov) noexcept;

	/// Fails with overrides for more than MaxFrameViews views.
	[[nodiscard]]
	Result<void> Validate() const noexcept;
	[[nodiscard]]
	const native::RenderFrameShow& Get() const noexcept;

private:
	native::RenderFrameViewOverride& OverrideOf(size_t view_index) noexcept;
	void Sync() noexcept;

	native::RenderFrameShow m_show;
	std::vector<native::RenderFrameViewOverride> m_overrides;
	native::RenderFrameShowViewOverridesExt m_overridesExt;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/SenderBuilders.inl>
