#pragma once

// Implementation of Session.hpp.

#include <qar_streaming_cpp/detail/Completion.hpp>
#include <qar_streaming_cpp/detail/Native.hpp>

#include <utility>

namespace qaros
{

namespace detail
{

inline void
OnInviteRequested(NativeResult status, OnboardingInviteHandle* invite, void* user_state) noexcept
{
	const Status result(status);
	if(not result.Ok())
	{
		Finish<OnboardingInvite>(user_state, Result<OnboardingInvite>(result));
		return;
	}
	Finish<OnboardingInvite>(user_state, OnboardingInvite(OnboardingInvitePtr(invite)));
}

inline void
OnTargetAppAnswered(NativeResult status, const native::PeerId* peer_id, void* user_state) noexcept
{
	const Status result(status);
	if(not result.Ok())
	{
		Finish<PeerId>(user_state, Result<PeerId>(result));
		return;
	}
	Finish<PeerId>(user_state, FromNative(*peer_id));
}

template<typename Get>
Result<std::string>
BuildConnectionString(Get get, const std::string& host) noexcept
{
	return ReadString(get, host.c_str());
}

} // namespace detail

inline Session::Session(SessionPtr owned) noexcept
	: m_handle(Share(std::move(owned)))
{
}

inline SessionHandle*
Session::Get() const noexcept
{
	return m_handle.get();
}

inline Session::
operator bool() const noexcept
{
	return m_handle != nullptr;
}

inline PeerControl
Session::Peers() const noexcept
{
	return PeerControl(m_handle);
}

inline GuiPanelControl
Session::GuiPanels() const noexcept
{
	return GuiPanelControl(m_handle);
}

inline AppVolumeControl
Session::AppVolumes() const noexcept
{
	return AppVolumeControl(m_handle);
}

inline RenderSenderControl
Session::RenderSenders() const noexcept
{
	return RenderSenderControl(m_handle);
}

inline VideoSenderControl
Session::VideoSenders() const noexcept
{
	return VideoSenderControl(m_handle);
}

inline Result<SessionId>
Session::GetId() const noexcept
{
	SessionId id;
	const Status status(qar_session_get_id(m_handle.get(), detail::OutValue(&id)));
	if(not status.Ok())
	{
		return Result<SessionId>(status);
	}
	return id;
}

inline Result<std::optional<GraphicsDeviceId>>
Session::GetGraphicsDeviceId() const noexcept
{
	native::GraphicsDeviceId device = native::Default<native::GraphicsDeviceId>();
	bool present = false;
	const Status status(qar_session_get_graphics_device_id(m_handle.get(), &device, &present));
	if(not status.Ok())
	{
		return Result<std::optional<GraphicsDeviceId>>(status);
	}
	if(not present)
	{
		return std::optional<GraphicsDeviceId>();
	}
	return std::optional<GraphicsDeviceId>(FromNative(device));
}

inline Result<PeerId>
Session::FindRenderTarget(const std::string& connection) const noexcept
{
	PeerId peer;
	const Status status(
		qar_session_find_render_target(m_handle.get(), connection.c_str(), detail::OutValue(&peer))
	);
	if(not status.Ok())
	{
		return Result<PeerId>(status);
	}
	return peer;
}

inline Result<OnboardingInvite>
Session::RequestOnboardingInvite(const CallOptions& options) const noexcept
{
	const native::RequestInviteInit init = native::Default<native::RequestInviteInit>();
	OnboardingInvitePtr invite;
	const Status status(qar_session_request_onboarding_invite(
		m_handle.get(),
		&init,
		detail::BlockingProgressCallback(options),
		detail::BlockingProgressState(options),
		options.Cancel,
		OutPtr(invite)
	));
	if(not status.Ok())
	{
		return Result<OnboardingInvite>(status);
	}
	return OnboardingInvite(std::move(invite));
}

template<typename Token>
auto
Session::RequestOnboardingInviteAsync(CallOptions options, Token&& token) const noexcept
{
	using V = OnboardingInvite;
	const qar_progress_callback_t progress = detail::AsyncProgressCallback<V>(options);
	const auto start =
		[session = m_handle.get(), progress](void* user_state, CancelTokenHandle* cancel)
	{
		const native::RequestInviteInit init = native::Default<native::RequestInviteInit>();
		return qar_session_request_onboarding_invite_async(
			session, &init, &detail::OnInviteRequested, progress, user_state, cancel
		);
	};
	return detail::Initiate<V>(start, std::move(options), nullptr, std::forward<Token>(token));
}

inline Result<std::vector<OnboardingInvite>>
Session::RequestOnboardingCodes(
	uint32_t count,
	std::chrono::seconds lifetime,
	const std::optional<PeerId>& planned_peer,
	const CallOptions& options
) const noexcept
{
	native::RequestOnboardingCodesInit init = native::Default<native::RequestOnboardingCodesInit>();
	init.count = count;
	init.lifetime_seconds = static_cast<uint32_t>(lifetime.count());
	if(planned_peer.has_value())
	{
		init.planned_peer_id = ToNative(planned_peer.value());
	}
	std::vector<OnboardingInviteHandle*> raws(count > 0 ? count : 1, nullptr);
	const Status status(qar_session_request_onboarding_codes(
		m_handle.get(),
		&init,
		detail::BlockingProgressCallback(options),
		detail::BlockingProgressState(options),
		options.Cancel,
		raws.data()
	));
	std::vector<OnboardingInvite> invites;
	invites.reserve(raws.size());
	for(OnboardingInviteHandle* raw : raws)
	{
		if(raw != nullptr)
		{
			invites.emplace_back(OnboardingInvitePtr(raw));
		}
	}
	if(not status.Ok())
	{
		return Result<std::vector<OnboardingInvite>>(status);
	}
	return invites;
}

inline Result<PeerId>
Session::InviteTargetApp(
	const std::string& connection_string, const CallOptions& options
) const noexcept
{
	native::SessionInviteTargetAppInit init = native::Default<native::SessionInviteTargetAppInit>();
	init.connection_string = connection_string.c_str();
	PeerId peer;
	const Status status(qar_session_invite_target_app(
		m_handle.get(),
		&init,
		detail::BlockingProgressCallback(options),
		detail::BlockingProgressState(options),
		options.Cancel,
		detail::OutValue(&peer)
	));
	if(not status.Ok())
	{
		return Result<PeerId>(status);
	}
	return peer;
}

template<typename Token>
auto
Session::InviteTargetAppAsync(
	std::string connection_string, CallOptions options, Token&& token
) const noexcept
{
	using V = PeerId;
	const qar_progress_callback_t progress = detail::AsyncProgressCallback<V>(options);
	const auto start = [session = m_handle.get(),
						connection = std::move(connection_string),
						progress](void* user_state, CancelTokenHandle* cancel)
	{
		native::SessionInviteTargetAppInit init =
			native::Default<native::SessionInviteTargetAppInit>();
		init.connection_string = connection.c_str();
		return qar_session_invite_target_app_async(
			session, &init, &detail::OnTargetAppAnswered, progress, user_state, cancel
		);
	};
	return detail::Initiate<V>(start, std::move(options), nullptr, std::forward<Token>(token));
}

inline Result<void>
Session::KillTargetApp(const PeerId& target_app, const CallOptions& options) const noexcept
{
	return detail::ToVoid(qar_session_kill_target_app(
		m_handle.get(),
		detail::InValue(target_app),
		detail::BlockingProgressCallback(options),
		detail::BlockingProgressState(options),
		options.Cancel
	));
}

template<typename Token>
auto
Session::KillTargetAppAsync(PeerId target_app, CallOptions options, Token&& token) const noexcept
{
	using V = PeerId;
	const qar_progress_callback_t progress = detail::AsyncProgressCallback<V>(options);
	const auto start = [session = m_handle.get(),
						target_app = ToNative(target_app),
						progress](void* user_state, CancelTokenHandle* cancel)
	{
		return qar_session_kill_target_app_async(
			session, &target_app, &detail::OnTargetAppAnswered, progress, user_state, cancel
		);
	};
	return detail::Initiate<V>(start, std::move(options), nullptr, std::forward<Token>(token));
}

inline Result<std::string>
TargetAppConnectionString::HoloLens(const std::string& host) noexcept
{
	return detail::BuildConnectionString(qar_target_app_connection_string_hololens, host);
}

inline Result<std::string>
TargetAppConnectionString::Quest(const std::string& host) noexcept
{
	return detail::BuildConnectionString(qar_target_app_connection_string_quest, host);
}

inline Result<std::string>
TargetAppConnectionString::Android(const std::string& host) noexcept
{
	return detail::BuildConnectionString(qar_target_app_connection_string_android, host);
}

} // namespace qaros
