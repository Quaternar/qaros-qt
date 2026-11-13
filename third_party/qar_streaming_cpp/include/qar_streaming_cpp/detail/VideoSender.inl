#pragma once

// Implementation of VideoSender.hpp.

#include <qar_streaming_cpp/detail/Native.hpp>

#include <utility>

namespace qaros
{

inline VideoSender::VideoSender(
	VideoSenderPtr owned, std::shared_ptr<SessionHandle> session
) noexcept
	: m_session(std::move(session))
	, m_handle(std::move(owned))
{
}

inline VideoSenderHandle*
VideoSender::Get() const noexcept
{
	return m_handle.get();
}

inline VideoSender::
operator bool() const noexcept
{
	return m_handle != nullptr;
}

inline Result<StreamId>
VideoSender::GetStreamId() const noexcept
{
	native::StreamId stream_id = native::Default<native::StreamId>();
	const Status status(qar_video_sender_stream_id(m_handle.get(), &stream_id));
	if(not status.Ok())
	{
		return Result<StreamId>(status);
	}
	return FromNative(stream_id);
}

inline Result<VideoFrameLayout>
VideoSender::GetLayout() const noexcept
{
	native::VideoFrameLayout layout = native::Default<native::VideoFrameLayout>();
	const Status status(qar_video_sender_layout(m_handle.get(), &layout));
	if(not status.Ok())
	{
		return Result<VideoFrameLayout>(status);
	}
	return FromNative(layout);
}

inline Result<void>
VideoSender::ChangeLayout(const VideoFrameLayout& layout) const noexcept
{
	const Result<void> valid = layout.Validate();
	if(not valid.Ok())
	{
		return valid;
	}
	return detail::ToVoid(qar_video_sender_change_layout(m_handle.get(), detail::InValue(layout)));
}

inline Result<bool>
VideoSender::TryBeginFrame() const noexcept
{
	bool ready = false;
	const Status status(qar_video_sender_try_begin_frame(m_handle.get(), &ready));
	if(not status.Ok())
	{
		return Result<bool>(status);
	}
	return ready;
}

inline Result<void>
VideoSender::ShowFrame() const noexcept
{
	return detail::ToVoid(qar_video_sender_show_frame(m_handle.get()));
}

inline Result<VideoFrameCpu>
VideoSender::GetCpuFrame() const noexcept
{
	native::VideoFrameCpu frame = native::Default<native::VideoFrameCpu>();
	const Status status(qar_video_sender_frame_cpu(m_handle.get(), &frame));
	if(not status.Ok())
	{
		return Result<VideoFrameCpu>(status);
	}
	return FromNative(frame);
}

#ifdef QAR_ENABLE_D3D11
inline Result<VideoFrameD3D11>
VideoSender::GetD3D11Frame() const noexcept
{
	native::VideoFrameD3D11 frame = native::Default<native::VideoFrameD3D11>();
	const Status status(qar_video_sender_frame_d3d11(m_handle.get(), &frame));
	if(not status.Ok())
	{
		return Result<VideoFrameD3D11>(status);
	}
	return FromNative(frame);
}
#endif

#ifdef QAR_ENABLE_OPENGL
inline Result<VideoFrameOpenGl>
VideoSender::GetOpenGlFrame() const noexcept
{
	native::VideoFrameOpenGl frame = native::Default<native::VideoFrameOpenGl>();
	const Status status(qar_video_sender_frame_opengl(m_handle.get(), &frame));
	if(not status.Ok())
	{
		return Result<VideoFrameOpenGl>(status);
	}
	return FromNative(frame);
}
#endif

#ifdef QAR_ENABLE_VULKAN
inline Result<VulkanFrame>
VideoSender::GetVulkanFrame() const noexcept
{
	native::VideoFrameVulkan* frame = nullptr;
	const Status status(qar_video_sender_frame_vulkan(m_handle.get(), &frame));
	if(not status.Ok())
	{
		return Result<VulkanFrame>(status);
	}
	return VulkanFrame(frame);
}
#endif

inline VideoSenderControl::VideoSenderControl(std::shared_ptr<SessionHandle> session) noexcept
	: m_session(std::move(session))
{
}

inline Result<VideoSender>
VideoSenderControl::Create(const VideoSenderBuilder& builder) const noexcept
{
	const Result<void> valid = builder.Validate();
	if(not valid.Ok())
	{
		return Result<VideoSender>(valid.GetStatus());
	}
	VideoSenderPtr sender;
	const Status status(qar_video_sender_create(m_session.get(), &builder.Get(), OutPtr(sender)));
	if(not status.Ok())
	{
		return Result<VideoSender>(status);
	}
	return VideoSender(std::move(sender), m_session);
}

} // namespace qaros
