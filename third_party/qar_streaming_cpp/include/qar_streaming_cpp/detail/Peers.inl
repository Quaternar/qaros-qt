#pragma once

// Implementation of Peers.hpp.

#include <qar_streaming_cpp/detail/Native.hpp>

#include <utility>

namespace qaros
{

inline Result<PeerSpec>
PeerSpec::Read(PeerSpecHandle* handle) noexcept
{
	PeerSpec spec;
	detail::StatusChain chain;
	chain.Add(qar_peer_spec_get_id(handle, detail::OutValue(&spec.Id)));
	chain.Take(&spec.DisplayName, detail::ReadString(qar_peer_spec_get_display_name, handle));
	chain.Take(&spec.AppVersion, detail::ReadString(qar_peer_spec_get_app_version, handle));
	chain.Take(
		&spec.AppCustomPeerInfo, detail::ReadString(qar_peer_spec_get_app_custom_peer_info, handle)
	);
	chain.Add(qar_peer_spec_get_app_state(handle, detail::OutValue(&spec.AppState)));
	chain.Take(&spec.VersionId, detail::ReadString(qar_peer_spec_get_version_id, handle));
	chain.Take(&spec.RoomTag, detail::ReadString(qar_peer_spec_get_room_tag, handle));
	chain.Add(qar_peer_spec_is_target_app(handle, &spec.IsTargetApp));
	if(not chain.Ok())
	{
		return Result<PeerSpec>(chain.First());
	}
	return spec;
}

inline Result<PeerSpec>
PeerSpec::FromCallback(PeerSpecHandle* handle) noexcept
{
	return Read(handle);
}

inline PeerControl::PeerControl(std::shared_ptr<SessionHandle> session) noexcept
	: m_session(std::move(session))
{
}

inline Result<PeerSpec>
PeerControl::GetMySpec() const noexcept
{
	PeerSpecPtr me;
	const Status status(qar_session_get_my_spec(m_session.get(), OutPtr(me)));
	if(not status.Ok())
	{
		return Result<PeerSpec>(status);
	}
	return PeerSpec::Read(me.get());
}

inline Result<std::vector<PeerSpec>>
PeerControl::GetAll() const noexcept
{
	return detail::QuerySnapshots<PeerSpec, PeerSpecHandle>(
		qar_query_peer_specs_count, qar_query_peer_specs, m_session.get()
	);
}

inline Result<void>
PeerControl::UpdateDisplayName(const std::string& name) const noexcept
{
	return detail::ToVoid(qar_peer_update_display_name(m_session.get(), name.c_str()));
}

template<typename Fn>
Result<Subscription>
PeerControl::SubscribeUpdates(Fn&& fn) const noexcept
{
	const auto start =
		[session = m_session.get()](
			qar_peer_update_callback_t callback, void* user_state, CancelTokenHandle* token
		)
	{
		return qar_peer_subscribe_updates(session, callback, user_state, token);
	};
	return SubscribeSnapshots<PeerSpec, PeerSpecHandle>(std::forward<Fn>(fn), start);
}

} // namespace qaros
