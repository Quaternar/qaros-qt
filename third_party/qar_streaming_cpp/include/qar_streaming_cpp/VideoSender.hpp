#pragma once

// C module: detail/video_sender.h

#include <qar_streaming_cpp/Handle.hpp>
#include <qar_streaming_cpp/SenderBuilders.hpp>
#include <qar_streaming_cpp/Values.hpp>

#include <memory>

namespace qaros
{

/// Streams video frames the application produces. Keeps its session alive.
class VideoSender
{
public:
	VideoSender() noexcept = default;
	VideoSender(VideoSenderPtr owned, std::shared_ptr<SessionHandle> session) noexcept;

	[[nodiscard]]
	VideoSenderHandle* Get() const noexcept;
	explicit operator bool() const noexcept;

	[[nodiscard]]
	Result<StreamId> GetStreamId() const noexcept;
	[[nodiscard]]
	Result<VideoFrameLayout> GetLayout() const noexcept;
	[[nodiscard]]
	Result<void> ChangeLayout(const VideoFrameLayout& layout) const noexcept;
	/// Whether a frame may be written now.
	[[nodiscard]]
	Result<bool> TryBeginFrame() const noexcept;
	[[nodiscard]]
	Result<void> ShowFrame() const noexcept;

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
	VideoSenderPtr m_handle;
};

/// Video senders of a session.
class VideoSenderControl
{
public:
	VideoSenderControl() = default;
	explicit VideoSenderControl(std::shared_ptr<SessionHandle> session) noexcept;

	[[nodiscard]]
	Result<VideoSender> Create(const VideoSenderBuilder& builder) const noexcept;

private:
	std::shared_ptr<SessionHandle> m_session;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/VideoSender.inl>
