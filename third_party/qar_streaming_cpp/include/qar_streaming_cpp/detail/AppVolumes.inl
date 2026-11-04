#pragma once

// Implementation of AppVolumes.hpp.

#include <qar_streaming_cpp/detail/Native.hpp>

namespace qaros
{

inline Result<AppVolumeGestureState>
AppVolumeGestureState::Read(AppVolumeHandle* handle, const std::vector<PeerId>& used_by) noexcept
{
	AppVolumeGestureState state;
	detail::StatusChain chain;
	chain.Take(
		&state.AllowedPeers,
		detail::FromNativeAll<PeerId>(detail::ReadWithCapacity<native::PeerId>(
			qar_app_volume_get_gesture_allowed_peers, handle
		))
	);
	PeerId holder;
	bool is_held = false;
	chain.Add(qar_app_volume_get_gesture_holder(handle, detail::OutValue(&holder), &is_held));
	if(is_held)
	{
		state.Holder = holder;
	}
	std::vector<AppVolumeGestureModeId> mode_ids;
	chain.Take(
		&mode_ids,
		detail::ReadWithCapacity<AppVolumeGestureModeId>(
			qar_app_volume_get_gesture_mode_ids, handle
		)
	);
	for(AppVolumeGestureModeId mode_id : mode_ids)
	{
		native::AppVolumeGestureConfiguration config =
			native::Default<native::AppVolumeGestureConfiguration>();
		chain.Add(qar_app_volume_get_gesture_mode(handle, mode_id, &config));
		state.Modes.emplace_back(mode_id, FromNative(config));
	}
	chain.Add(
		qar_app_volume_get_active_gesture_mode(handle, nullptr, &state.SelectedDefaultModeId)
	);
	for(const PeerId& peer : used_by)
	{
		AppVolumeGestureModeId mode_id = DefaultGestureModeId;
		chain.Add(qar_app_volume_get_active_gesture_mode(handle, detail::InValue(peer), &mode_id));
		state.ActiveModeByPeer.emplace_back(peer, mode_id);
	}
	if(not chain.Ok())
	{
		return Result<AppVolumeGestureState>(chain.First());
	}
	return state;
}

inline Result<AppVolume>
AppVolume::Read(AppVolumeHandle* handle) noexcept
{
	AppVolume volume;
	detail::StatusChain chain;
	chain.Add(qar_app_volume_get_id(handle, detail::OutValue(&volume.Id)));
	chain.Take(&volume.DisplayName, detail::ReadString(qar_app_volume_get_display_name, handle));
	chain.Add(qar_app_volume_get_pose(handle, detail::OutValue(&volume.Pose)));
	chain.Add(qar_app_volume_get_size(handle, detail::OutValue(&volume.Size)));
	chain.Add(qar_app_volume_get_lifetime_status(handle, detail::OutValue(&volume.LifetimeStatus)));
	chain.Add(qar_app_volume_get_editing_status(handle, detail::OutValue(&volume.EditingStatus)));
	chain.Take(
		&volume.UsedByPeers,
		detail::FromNativeAll<PeerId>(detail::ReadCounted<native::PeerId>(
			qar_app_volume_get_used_by_peers_count, qar_app_volume_get_used_by_peers, handle
		))
	);
	chain.Add(qar_app_volume_get_app_pose(handle, detail::OutValue(&volume.AppPose)));
	chain.Add(qar_app_volume_get_app_scale(handle, &volume.AppScale));
	native::AppWorldAnchor anchor = {};
	chain.Add(qar_app_volume_get_app_world_anchor(handle, &anchor));
	volume.AppWorldAnchor = FromNative(anchor);
	native::AppVolumeGestureConfiguration configuration =
		native::Default<native::AppVolumeGestureConfiguration>();
	chain.Add(qar_app_volume_get_gesture_configuration(handle, &configuration));
	volume.GestureConfiguration = FromNative(configuration);
	chain.Take(&volume.GestureState, AppVolumeGestureState::Read(handle, volume.UsedByPeers));
	if(not chain.Ok())
	{
		return Result<AppVolume>(chain.First());
	}
	return volume;
}

inline Result<AppVolume>
AppVolume::FromCallback(AppVolumeHandle* handle) noexcept
{
	return Read(handle);
}

inline Result<AppVolumeGesture>
AppVolumeGesture::FromCallback(AppVolumeGestureHandle* gesture) noexcept
{
	AppVolumeGesture value;
	detail::StatusChain chain;
	chain.Add(qar_app_volume_gesture_get_lifecycle(
		gesture, detail::OutValue(&value.SourcePeerId), detail::OutValue(&value.TargetVolumeId)
	));
	chain.Add(qar_app_volume_gesture_get_classification(
		gesture, detail::OutValue(&value.Kind), detail::OutValue(&value.Phase)
	));
	ReadPoints(gesture, &value, &chain);
	chain.Add(qar_app_volume_gesture_get_deltas(
		gesture,
		detail::OutValue(&value.TranslationDelta),
		detail::OutValue(&value.RotationDelta),
		&value.WasMappedToAppTransform
	));
	chain.Add(qar_app_volume_gesture_get_timestamp(gesture, detail::OutValue(&value.Timestamp)));
	chain.Add(qar_app_volume_gesture_get_handedness(gesture, detail::OutValue(&value.Handedness)));
	chain.Add(qar_app_volume_gesture_get_gesture_id(gesture, &value.GestureId));
	chain.Add(qar_app_volume_gesture_get_mode(gesture, &value.ModeId));
	if(not chain.Ok())
	{
		return Result<AppVolumeGesture>(chain.First());
	}
	return value;
}

inline void
AppVolumeGesture::ReadPoints(
	AppVolumeGestureHandle* gesture, AppVolumeGesture* value, detail::StatusChain* chain
) noexcept
{
	Vector3 action = {};
	Vector3 start = {};
	bool has_action = false;
	bool has_start = false;
	chain->Add(qar_app_volume_gesture_get_points(
		gesture, detail::OutValue(&action), &has_action, detail::OutValue(&start), &has_start
	));
	if(has_action)
	{
		value->ActionPoint = action;
	}
	if(has_start)
	{
		value->StartPoint = start;
	}
}

inline AppVolumeControl::AppVolumeControl(std::shared_ptr<SessionHandle> session) noexcept
	: m_session(std::move(session))
{
}

inline Result<AppVolumeId>
AppVolumeControl::GetOrCreate(const AppVolumeBuilder& builder) const noexcept
{
	const Result<void> valid = builder.Validate();
	if(not valid.Ok())
	{
		return Result<AppVolumeId>(valid.GetStatus());
	}
	AppVolumeId id;
	const Status status(
		qar_app_volumes_get_or_create(m_session.get(), &builder.Get(), detail::OutValue(&id))
	);
	if(not status.Ok())
	{
		return Result<AppVolumeId>(status);
	}
	return id;
}

inline Result<std::vector<AppVolume>>
AppVolumeControl::GetAll() const noexcept
{
	return detail::QuerySnapshots<AppVolume, AppVolumeHandle>(
		qar_query_app_volumes_count, qar_query_app_volumes, m_session.get()
	);
}

inline Result<void>
AppVolumeControl::Close(const AppVolumeId& id) const noexcept
{
	return detail::ToVoid(qar_app_volumes_close_volume(m_session.get(), detail::InValue(id)));
}

inline Result<void>
AppVolumeControl::ChangeDisplayName(const AppVolumeId& id, const std::string& name) const noexcept
{
	return detail::ToVoid(
		qar_app_volumes_change_display_name(m_session.get(), detail::InValue(id), name.c_str())
	);
}

inline Result<void>
AppVolumeControl::ChangeSize(const AppVolumeId& id, const AppVolumeSize& size) const noexcept
{
	return detail::ToVoid(
		qar_app_volumes_change_size(m_session.get(), detail::InValue(id), detail::InValue(size))
	);
}

inline Result<void>
AppVolumeControl::ChangePose(const AppVolumeId& id, const Pose& pose) const noexcept
{
	return detail::ToVoid(
		qar_app_volumes_change_pose(m_session.get(), detail::InValue(id), detail::InValue(pose))
	);
}

inline Result<void>
AppVolumeControl::UpdateUsedByPeers(
	const AppVolumeId& id, const std::vector<PeerId>& additions, const std::vector<PeerId>& removals
) const noexcept
{
	const std::vector<native::PeerId> added = detail::ToNativeAll(additions);
	const std::vector<native::PeerId> removed = detail::ToNativeAll(removals);
	return detail::ToVoid(qar_app_volumes_update_used_by_peers(
		m_session.get(),
		detail::InValue(id),
		added.data(),
		added.size(),
		removed.data(),
		removed.size()
	));
}

inline Result<void>
AppVolumeControl::StartEditing(const AppVolumeId& id) const noexcept
{
	return detail::ToVoid(qar_app_volumes_start_editing(m_session.get(), detail::InValue(id)));
}

inline Result<void>
AppVolumeControl::StopEditing(const AppVolumeId& id) const noexcept
{
	return detail::ToVoid(qar_app_volumes_stop_editing(m_session.get(), detail::InValue(id)));
}

inline Result<void>
AppVolumeControl::ChangeAppPose(const AppVolumeId& id, const Pose& app_pose) const noexcept
{
	return detail::ToVoid(qar_app_volumes_change_app_pose(
		m_session.get(), detail::InValue(id), detail::InValue(app_pose)
	));
}

inline Result<void>
AppVolumeControl::ChangeAppScale(const AppVolumeId& id, float scale) const noexcept
{
	return detail::ToVoid(
		qar_app_volumes_change_app_scale(m_session.get(), detail::InValue(id), scale)
	);
}

inline Result<void>
AppVolumeControl::ChangeAppWorldAnchor(
	const AppVolumeId& id, const GeoAnchorFrame& anchor
) const noexcept
{
	return detail::ToVoid(qar_app_volumes_change_app_world_anchor(
		m_session.get(), detail::InValue(id), detail::InValue(anchor)
	));
}

inline Result<void>
AppVolumeControl::ClearAppWorldAnchor(const AppVolumeId& id) const noexcept
{
	return detail::ToVoid(
		qar_app_volumes_clear_app_world_anchor(m_session.get(), detail::InValue(id))
	);
}

inline Result<Pose>
AppVolumeControl::GetLatestPose(const AppVolumeId& id) const noexcept
{
	return Latest<Pose>(qar_app_volume_get_latest_pose, id);
}

inline Result<AppVolumeSize>
AppVolumeControl::GetLatestSize(const AppVolumeId& id) const noexcept
{
	return Latest<AppVolumeSize>(qar_app_volume_get_latest_size, id);
}

inline Result<Pose>
AppVolumeControl::GetLatestAppPose(const AppVolumeId& id) const noexcept
{
	return Latest<Pose>(qar_app_volume_get_latest_app_pose, id);
}

inline Result<float>
AppVolumeControl::GetLatestAppScale(const AppVolumeId& id) const noexcept
{
	float scale = 1.0f;
	const Status status(
		qar_app_volume_get_latest_app_scale(m_session.get(), detail::InValue(id), &scale)
	);
	if(not status.Ok())
	{
		return Result<float>(status);
	}
	return scale;
}

inline Result<void>
AppVolumeControl::ChangeGestureConfiguration(
	const AppVolumeId& id, const GestureConfigurationBuilder& configuration
) const noexcept
{
	return detail::ToVoid(qar_app_volumes_change_gesture_configuration(
		m_session.get(), detail::InValue(id), &configuration.Get()
	));
}

inline Result<void>
AppVolumeControl::ChangeGestureMode(
	const AppVolumeId& id,
	AppVolumeGestureModeId mode_id,
	const GestureConfigurationBuilder& configuration
) const noexcept
{
	return detail::ToVoid(qar_app_volumes_change_gesture_mode(
		m_session.get(), detail::InValue(id), mode_id, &configuration.Get()
	));
}

inline Result<void>
AppVolumeControl::RemoveGestureMode(
	const AppVolumeId& id, AppVolumeGestureModeId mode_id
) const noexcept
{
	return detail::ToVoid(
		qar_app_volumes_remove_gesture_mode(m_session.get(), detail::InValue(id), mode_id)
	);
}

inline Result<void>
AppVolumeControl::SelectGestureMode(
	const AppVolumeId& id, const std::optional<PeerId>& peer, AppVolumeGestureModeId mode_id
) const noexcept
{
	const std::optional<native::PeerId> native_peer =
		peer.has_value() ? std::optional<native::PeerId>(ToNative(peer.value())) : std::nullopt;
	const native::PeerId* peer_pointer = native_peer.has_value() ? &native_peer.value() : nullptr;
	return detail::ToVoid(qar_app_volumes_select_gesture_mode(
		m_session.get(), detail::InValue(id), peer_pointer, mode_id
	));
}

inline Result<void>
AppVolumeControl::ClearPeerGestureMode(const AppVolumeId& id, const PeerId& peer) const noexcept
{
	return detail::ToVoid(qar_app_volumes_clear_peer_gesture_mode(
		m_session.get(), detail::InValue(id), detail::InValue(peer)
	));
}

inline Result<void>
AppVolumeControl::ClearAllPeerGestureModes(const AppVolumeId& id) const noexcept
{
	return detail::ToVoid(
		qar_app_volumes_clear_all_peer_gesture_modes(m_session.get(), detail::InValue(id))
	);
}

template<typename Fn>
Result<Subscription>
AppVolumeControl::SubscribeUpdates(Fn&& fn) const noexcept
{
	const auto start =
		[session = m_session.get()](
			qar_app_volume_update_callback_t callback, void* user_state, CancelTokenHandle* token
		)
	{
		return qar_app_volumes_subscribe_updates(session, callback, user_state, token);
	};
	return SubscribeSnapshots<AppVolume, AppVolumeHandle>(std::forward<Fn>(fn), start);
}

template<typename Fn>
Result<Subscription>
AppVolumeControl::SubscribeGestureEvents(
	const AppVolumeId& id, GestureKind kind, Fn&& fn
) const noexcept
{
	const auto start = [session = m_session.get(), id = ToNative(id), kind](
						   qar_app_volume_gesture_event_callback_t callback,
						   void* user_state,
						   CancelTokenHandle* token
					   )
	{
		return qar_app_volumes_subscribe_gesture_events(
			session, &id, ToNative(kind), callback, user_state, token
		);
	};
	return SubscribeSnapshots<AppVolumeGesture, AppVolumeGestureHandle>(
		std::forward<Fn>(fn), start
	);
}

template<typename Fn>
Result<Subscription>
AppVolumeControl::SubscribeAllGestureEvents(const AppVolumeId& id, Fn&& fn) const noexcept
{
	const auto start = [session = m_session.get(), id = ToNative(id)](
						   qar_app_volume_gesture_event_callback_t callback,
						   void* user_state,
						   CancelTokenHandle* token
					   )
	{
		return qar_app_volumes_subscribe_all_gesture_events(
			session, &id, callback, user_state, token
		);
	};
	return SubscribeSnapshots<AppVolumeGesture, AppVolumeGestureHandle>(
		std::forward<Fn>(fn), start
	);
}

template<typename T, typename Get>
Result<T>
AppVolumeControl::Latest(Get get, const AppVolumeId& id) const noexcept
{
	T value = {};
	const Status status(get(m_session.get(), detail::InValue(id), detail::OutValue(&value)));
	if(not status.Ok())
	{
		return Result<T>(status);
	}
	return value;
}

} // namespace qaros
