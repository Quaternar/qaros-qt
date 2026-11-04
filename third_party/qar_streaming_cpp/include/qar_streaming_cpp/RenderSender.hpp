#pragma once

// C module: detail/render_stream_sender.h. Async methods: see Async.hpp.

#include <qar_streaming_cpp/Async.hpp>
#include <qar_streaming_cpp/SenderBuilders.hpp>
#include <qar_streaming_cpp/Subscription.hpp>

#include <cstddef>
#include <memory>
#include <optional>

namespace qaros
{

/// The poses and fields of view to render one frame with.
class RenderFrameInfo
{
public:
	RenderFrameInfo() noexcept = default;
	explicit RenderFrameInfo(RenderFrameInfoPtr owned) noexcept;

	[[nodiscard]]
	RenderFrameInfoHandle* Get() const noexcept;
	explicit operator bool() const noexcept;

	[[nodiscard]]
	Result<Pose> GetViewPose(size_t view_index) const noexcept;
	[[nodiscard]]
	Result<Fov> GetViewFov(size_t view_index) const noexcept;

private:
	RenderFrameInfoPtr m_handle;
};

/// A target asking for this application's content.
struct RenderStreamRequest
{
	PeerId TargetPeerId = {};
	StreamId StreamId = {};

	/// The app owns the delivered request: read, then released.
	static Result<RenderStreamRequest> FromCallback(RenderStreamRequestHandle* raw) noexcept;
};

/// Streams rendered frames to one target. Keeps its session alive.
class RenderSender
{
public:
	RenderSender() noexcept = default;
	RenderSender(RenderSenderPtr owned, std::shared_ptr<SessionHandle> session) noexcept;

	[[nodiscard]]
	RenderSenderHandle* Get() const noexcept;
	explicit operator bool() const noexcept;

	[[nodiscard]]
	Result<VideoFrameLayout> GetLayout() const noexcept;
	/// Not callable between a begin-frame and its ShowFrame().
	[[nodiscard]]
	Result<void> ChangeLayout(const VideoFrameLayout& layout) const noexcept;
	[[nodiscard]]
	Result<StreamId> GetStreamId() const noexcept;
	/// The newest hands data the device sent since the previous call.
	[[nodiscard]]
	Result<DeviceHands> GetLastHands() const noexcept;

	/// Blocks until the next frame may be rendered.
	[[nodiscard]]
	Result<RenderFrameInfo> BeginFrame(CancelTokenHandle* cancel = nullptr) const noexcept;
	/// Empty while no frame is due yet.
	[[nodiscard]]
	Result<std::optional<RenderFrameInfo>> TryBeginFrame() const noexcept;
	/// Completes with Result<RenderFrameInfo>; CallOptions::OnProgress is unused.
	template<typename Token = UseFutureT>
	auto BeginFrameAsync(CallOptions options = {}, Token&& token = Token{}) const noexcept;
	[[nodiscard]]
	Result<void> ShowFrame(const FrameShowBuilder& frame_show = FrameShowBuilder()) const noexcept;

	[[nodiscard]]
	Result<VideoFrameCpu> GetCpuFrame() const noexcept;
#ifdef QAR_ENABLE_D3D11
	[[nodiscard]]
	Result<VideoFrameD3D11> GetD3D11Frame() const noexcept;
#endif
#ifdef QAR_ENABLE_OPENGL
	[[nodiscard]]
	Result<VideoFrameOpenGl> GetOpenGlFrame() const noexcept;
#endif
#ifdef QAR_ENABLE_VULKAN
	[[nodiscard]]
	Result<VulkanFrame> GetVulkanFrame() const noexcept;
#endif

private:
	// Released after the sender.
	std::shared_ptr<SessionHandle> m_session;
	RenderSenderPtr m_handle;
};

/// Render senders of a session, and the requests that ask for them.
class RenderSenderControl
{
public:
	RenderSenderControl() = default;
	explicit RenderSenderControl(std::shared_ptr<SessionHandle> session) noexcept;

	[[nodiscard]]
	Result<RenderSender>
	Create(const RenderSenderBuilder& builder, CancelTokenHandle* cancel = nullptr) const noexcept;
	/// Completes with Result<RenderSender>; CallOptions::OnProgress is unused.
	template<typename Token = UseFutureT>
	auto CreateAsync(
		const RenderSenderBuilder& builder, CallOptions options = {}, Token&& token = Token{}
	) const noexcept;

	/// `fn(Result<RenderStreamRequest>)` on a library thread per request.
	template<typename Fn>
	Result<Subscription> SubscribeRequests(Fn&& fn) const noexcept;

private:
	std::shared_ptr<SessionHandle> m_session;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/RenderSender.inl>
