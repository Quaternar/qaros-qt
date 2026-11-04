#pragma once

// Implementation of RenderSender.hpp.

#include <qar_streaming_cpp/detail/Completion.hpp>
#include <qar_streaming_cpp/detail/Native.hpp>

#include <utility>

namespace qaros
{

namespace detail
{

inline void
OnFrameBegun(NativeResult status, RenderFrameInfoHandle* frame_info, void* user_state) noexcept
{
	const Status result(status);
	if(not result.Ok())
	{
		Finish<RenderFrameInfo>(user_state, Result<RenderFrameInfo>(result));
		return;
	}
	Finish<RenderFrameInfo>(user_state, RenderFrameInfo(RenderFrameInfoPtr(frame_info)));
}

/// The create call's context is the session the sender keeps alive.
inline void
OnRenderSenderCreated(NativeResult status, RenderSenderHandle* sender, void* user_state) noexcept
{
	const Status result(status);
	if(not result.Ok())
	{
		Finish<RenderSender>(user_state, Result<RenderSender>(result));
		return;
	}
	std::shared_ptr<SessionHandle> session =
		AsCompletion<RenderSender>(user_state)->Context<SessionHandle>();
	Finish<RenderSender>(user_state, RenderSender(RenderSenderPtr(sender), std::move(session)));
}

} // namespace detail

// RenderFrameInfo

inline RenderFrameInfo::RenderFrameInfo(RenderFrameInfoPtr owned) noexcept
	: m_handle(std::move(owned))
{
}

inline RenderFrameInfoHandle*
RenderFrameInfo::Get() const noexcept
{
	return m_handle.get();
}

inline RenderFrameInfo::
operator bool() const noexcept
{
	return m_handle != nullptr;
}

inline Result<Pose>
RenderFrameInfo::GetViewPose(size_t view_index) const noexcept
{
	native::Pose pose = native::Default<native::Pose>();
	const Status status(qar_render_frame_info_get_view_pose(m_handle.get(), view_index, &pose));
	if(not status.Ok())
	{
		return Result<Pose>(status);
	}
	return FromNative(pose);
}

inline Result<Fov>
RenderFrameInfo::GetViewFov(size_t view_index) const noexcept
{
	native::Fov fov = native::Default<native::Fov>();
	const Status status(qar_render_frame_info_get_view_fov(m_handle.get(), view_index, &fov));
	if(not status.Ok())
	{
		return Result<Fov>(status);
	}
	return FromNative(fov);
}

// RenderStreamRequest

inline Result<RenderStreamRequest>
RenderStreamRequest::FromCallback(RenderStreamRequestHandle* raw) noexcept
{
	const RenderStreamRequestPtr owned(raw);
	RenderStreamRequest request;
	detail::StatusChain chain;
	chain.Add(
		qar_render_request_get_target_peer_id(owned.get(), detail::OutValue(&request.TargetPeerId))
	);
	chain.Add(qar_render_request_get_stream_id(owned.get(), detail::OutValue(&request.StreamId)));
	if(not chain.Ok())
	{
		return Result<RenderStreamRequest>(chain.First());
	}
	return request;
}

// RenderSender

inline RenderSender::RenderSender(
	RenderSenderPtr owned, std::shared_ptr<SessionHandle> session
) noexcept
	: m_session(std::move(session))
	, m_handle(std::move(owned))
{
}

inline RenderSenderHandle*
RenderSender::Get() const noexcept
{
	return m_handle.get();
}

inline RenderSender::
operator bool() const noexcept
{
	return m_handle != nullptr;
}

inline Result<VideoFrameLayout>
RenderSender::GetLayout() const noexcept
{
	native::VideoFrameLayout layout = native::Default<native::VideoFrameLayout>();
	const Status status(qar_render_sender_layout(m_handle.get(), &layout));
	if(not status.Ok())
	{
		return Result<VideoFrameLayout>(status);
	}
	return FromNative(layout);
}

inline Result<void>
RenderSender::ChangeLayout(const VideoFrameLayout& layout) const noexcept
{
	const Result<void> valid = layout.Validate();
	if(not valid.Ok())
	{
		return valid;
	}
	return detail::ToVoid(qar_render_sender_change_layout(m_handle.get(), detail::InValue(layout)));
}

inline Result<StreamId>
RenderSender::GetStreamId() const noexcept
{
	native::StreamId stream_id = native::Default<native::StreamId>();
	const Status status(qar_render_sender_stream_id(m_handle.get(), &stream_id));
	if(not status.Ok())
	{
		return Result<StreamId>(status);
	}
	return FromNative(stream_id);
}

inline Result<DeviceHands>
RenderSender::GetLastHands() const noexcept
{
	native::DeviceHandsWithJoints hands = {};
	const Status status(qar_render_sender_last_hands(m_handle.get(), &hands));
	if(not status.Ok())
	{
		return Result<DeviceHands>(status);
	}
	return FromNative(hands);
}

inline Result<RenderFrameInfo>
RenderSender::BeginFrame(CancelTokenHandle* cancel) const noexcept
{
	RenderFrameInfoPtr info;
	const Status status(qar_render_sender_begin_frame(m_handle.get(), cancel, OutPtr(info)));
	if(not status.Ok())
	{
		return Result<RenderFrameInfo>(status);
	}
	return RenderFrameInfo(std::move(info));
}

inline Result<std::optional<RenderFrameInfo>>
RenderSender::TryBeginFrame() const noexcept
{
	RenderFrameInfoPtr info;
	bool ready = false;
	const Status status(qar_render_sender_try_begin_frame(m_handle.get(), OutPtr(info), &ready));
	if(not status.Ok())
	{
		return Result<std::optional<RenderFrameInfo>>(status);
	}
	if(not ready)
	{
		return std::optional<RenderFrameInfo>();
	}
	return std::optional<RenderFrameInfo>(RenderFrameInfo(std::move(info)));
}

template<typename Token>
auto
RenderSender::BeginFrameAsync(CallOptions options, Token&& token) const noexcept
{
	const auto start = [sender = m_handle.get()](void* user_state, CancelTokenHandle* cancel)
	{
		return qar_render_sender_begin_frame_async(
			sender, &detail::OnFrameBegun, user_state, cancel
		);
	};
	return detail::Initiate<RenderFrameInfo>(
		start, std::move(options), nullptr, std::forward<Token>(token)
	);
}

inline Result<void>
RenderSender::ShowFrame(const FrameShowBuilder& frame_show) const noexcept
{
	const Result<void> valid = frame_show.Validate();
	if(not valid.Ok())
	{
		return valid;
	}
	return detail::ToVoid(qar_render_sender_show_frame(m_handle.get(), &frame_show.Get()));
}

inline Result<VideoFrameCpu>
RenderSender::GetCpuFrame() const noexcept
{
	native::VideoFrameCpu frame = native::Default<native::VideoFrameCpu>();
	const Status status(qar_render_sender_frame_cpu(m_handle.get(), &frame));
	if(not status.Ok())
	{
		return Result<VideoFrameCpu>(status);
	}
	return FromNative(frame);
}

#ifdef QAR_ENABLE_D3D11
inline Result<VideoFrameD3D11>
RenderSender::GetD3D11Frame() const noexcept
{
	native::VideoFrameD3D11 frame = native::Default<native::VideoFrameD3D11>();
	const Status status(qar_render_sender_frame_d3d11(m_handle.get(), &frame));
	if(not status.Ok())
	{
		return Result<VideoFrameD3D11>(status);
	}
	return FromNative(frame);
}
#endif

#ifdef QAR_ENABLE_OPENGL
inline Result<VideoFrameOpenGl>
RenderSender::GetOpenGlFrame() const noexcept
{
	native::VideoFrameOpenGl frame = native::Default<native::VideoFrameOpenGl>();
	const Status status(qar_render_sender_frame_opengl(m_handle.get(), &frame));
	if(not status.Ok())
	{
		return Result<VideoFrameOpenGl>(status);
	}
	return FromNative(frame);
}
#endif

#ifdef QAR_ENABLE_VULKAN
inline Result<VulkanFrame>
RenderSender::GetVulkanFrame() const noexcept
{
	native::VideoFrameVulkan* frame = nullptr;
	const Status status(qar_render_sender_frame_vulkan(m_handle.get(), &frame));
	if(not status.Ok())
	{
		return Result<VulkanFrame>(status);
	}
	return VulkanFrame(frame);
}
#endif

// RenderSenderControl

inline RenderSenderControl::RenderSenderControl(std::shared_ptr<SessionHandle> session) noexcept
	: m_session(std::move(session))
{
}

inline Result<RenderSender>
RenderSenderControl::Create(
	const RenderSenderBuilder& builder, CancelTokenHandle* cancel
) const noexcept
{
	const Result<void> valid = builder.Validate();
	if(not valid.Ok())
	{
		return Result<RenderSender>(valid.GetStatus());
	}
	// The C call never writes its init.
	native::RenderSenderInit* init = const_cast<native::RenderSenderInit*>(&builder.Get());
	RenderSenderPtr sender;
	const Status status(qar_render_sender_create(m_session.get(), init, cancel, OutPtr(sender)));
	if(not status.Ok())
	{
		return Result<RenderSender>(status);
	}
	return RenderSender(std::move(sender), m_session);
}

template<typename Token>
auto
RenderSenderControl::CreateAsync(
	const RenderSenderBuilder& builder, CallOptions options, Token&& token
) const noexcept
{
	// The C call never writes its init.
	const auto start =
		[session = m_session.get(), builder = &builder](void* user_state, CancelTokenHandle* cancel)
	{
		const Result<void> valid = builder->Validate();
		if(not valid.Ok())
		{
			return valid.GetStatus().Raw();
		}
		return qar_render_sender_create_async(
			session,
			const_cast<native::RenderSenderInit*>(&builder->Get()),
			&detail::OnRenderSenderCreated,
			user_state,
			cancel
		);
	};
	return detail::Initiate<RenderSender>(
		start, std::move(options), m_session, std::forward<Token>(token)
	);
}

template<typename Fn>
Result<Subscription>
RenderSenderControl::SubscribeRequests(Fn&& fn) const noexcept
{
	const auto start = [session = m_session.get()](
						   qar_render_sender_request_callback_t callback,
						   void* user_state,
						   CancelTokenHandle* token
					   )
	{
		return qar_render_sender_subscribe_requests(session, callback, user_state, token);
	};
	return SubscribeSnapshots<RenderStreamRequest, RenderStreamRequestHandle>(
		std::forward<Fn>(fn), start
	);
}

} // namespace qaros
