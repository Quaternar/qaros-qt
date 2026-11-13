#pragma once

// Implementation of GuiPanels.hpp.

#include <qar_streaming_cpp/detail/Native.hpp>

#include <utility>

namespace qaros
{

inline Result<GuiPanel>
GuiPanel::Read(GuiPanelHandle* handle) noexcept
{
	GuiPanel panel;
	detail::StatusChain chain;
	chain.Add(qar_gui_panel_get_id(handle, detail::OutValue(&panel.Id)));
	chain.Take(&panel.DisplayName, detail::ReadString(qar_gui_panel_get_display_name, handle));
	chain.Add(qar_gui_panel_get_pose(handle, detail::OutValue(&panel.Pose)));
	chain.Add(qar_gui_panel_get_size(handle, detail::OutValue(&panel.Size)));
	chain.Add(qar_gui_panel_get_content_type(handle, detail::OutValue(&panel.ContentType)));
	chain.Add(qar_gui_panel_get_state(handle, detail::OutValue(&panel.State)));
	chain.Take(
		&panel.VisibleToPeers,
		detail::FromNativeAll<PeerId>(detail::ReadCounted<native::PeerId>(
			qar_gui_panel_get_visible_to_peers_count, qar_gui_panel_get_visible_to_peers, handle
		))
	);
	ReadContent(handle, &panel, &chain);
	if(not chain.Ok())
	{
		return Result<GuiPanel>(chain.First());
	}
	return panel;
}

inline Result<GuiPanel>
GuiPanel::FromCallback(GuiPanelHandle* handle) noexcept
{
	return Read(handle);
}

inline void
GuiPanel::ReadContent(GuiPanelHandle* handle, GuiPanel* panel, detail::StatusChain* chain) noexcept
{
	if(panel->ContentType == GuiPanelContentType::WebsiteUri)
	{
		std::string uri;
		chain->Take(&uri, detail::ReadString(qar_gui_panel_get_content_uri, handle));
		panel->ContentUri = std::move(uri);
		return;
	}
	if(panel->ContentType == GuiPanelContentType::StreamId)
	{
		StreamId stream_id;
		chain->Add(qar_gui_panel_get_content_stream_id(handle, detail::OutValue(&stream_id)));
		panel->ContentStreamId = stream_id;
	}
}

inline Result<GuiPanelGesture>
GuiPanelGesture::FromCallback(GuiPanelGestureHandle* gesture) noexcept
{
	GuiPanelGesture value;
	detail::StatusChain chain;
	chain.Add(qar_gui_panel_gesture_get_lifecycle(
		gesture, detail::OutValue(&value.SourcePeerId), &value.GestureId
	));
	chain.Add(qar_gui_panel_gesture_get_classification(
		gesture, detail::OutValue(&value.Kind), detail::OutValue(&value.Phase)
	));
	chain.Add(qar_gui_panel_gesture_get_positions(
		gesture, detail::OutValue(&value.Position), detail::OutValue(&value.StartPosition)
	));
	chain.Add(qar_gui_panel_gesture_get_panel_id(gesture, detail::OutValue(&value.PanelId)));
	chain.Add(qar_gui_panel_gesture_get_timestamp(gesture, detail::OutValue(&value.Timestamp)));
	chain.Add(qar_gui_panel_gesture_get_handedness(gesture, detail::OutValue(&value.Handedness)));
	chain.Add(qar_gui_panel_gesture_get_tap_count(gesture, &value.TapCount));
	if(not chain.Ok())
	{
		return Result<GuiPanelGesture>(chain.First());
	}
	return value;
}

inline GuiPanelControl::GuiPanelControl(std::shared_ptr<SessionHandle> session) noexcept
	: m_session(std::move(session))
{
}

inline Result<GuiPanelId>
GuiPanelControl::GetOrCreate(const GuiPanelBuilder& builder) const noexcept
{
	GuiPanelId id;
	const Status status(
		qar_gui_panels_get_or_create(m_session.get(), &builder.Get(), detail::OutValue(&id))
	);
	if(not status.Ok())
	{
		return Result<GuiPanelId>(status);
	}
	return id;
}

inline Result<std::vector<GuiPanel>>
GuiPanelControl::GetAll() const noexcept
{
	return detail::QuerySnapshots<GuiPanel, GuiPanelHandle>(
		qar_query_gui_panels_count, qar_query_gui_panels, m_session.get()
	);
}

inline Result<void>
GuiPanelControl::UpdatePose(const GuiPanelId& id, const Pose& pose) const noexcept
{
	return detail::ToVoid(
		qar_gui_panels_update_pose(m_session.get(), detail::InValue(id), detail::InValue(pose))
	);
}

inline Result<void>
GuiPanelControl::ChangeSize(const GuiPanelId& id, const GuiPanelSize& size) const noexcept
{
	return detail::ToVoid(
		qar_gui_panels_change_size(m_session.get(), detail::InValue(id), detail::InValue(size))
	);
}

inline Result<void>
GuiPanelControl::SetState(const GuiPanelId& id, GuiPanelState state) const noexcept
{
	return detail::ToVoid(
		qar_gui_panels_set_state(m_session.get(), detail::InValue(id), ToNative(state))
	);
}

inline Result<void>
GuiPanelControl::Close(const GuiPanelId& id) const noexcept
{
	return detail::ToVoid(qar_gui_panels_close_panel(m_session.get(), detail::InValue(id)));
}

inline Result<void>
GuiPanelControl::NavigateToUri(const GuiPanelId& id, const std::string& uri) const noexcept
{
	return detail::ToVoid(
		qar_gui_panels_navigate_to_uri(m_session.get(), detail::InValue(id), uri.c_str())
	);
}

inline Result<void>
GuiPanelControl::SetStream(const GuiPanelId& id, const StreamId& stream_id) const noexcept
{
	return detail::ToVoid(
		qar_gui_panels_set_stream(m_session.get(), detail::InValue(id), detail::InValue(stream_id))
	);
}

inline Result<void>
GuiPanelControl::UpdateVisibleTo(
	const GuiPanelId& id, const std::vector<PeerId>& additions, const std::vector<PeerId>& removals
) const noexcept
{
	const std::vector<native::PeerId> added_ids = detail::ToNativeAll(additions);
	const std::vector<native::PeerId> removed_ids = detail::ToNativeAll(removals);
	const std::vector<const native::PeerId*> added = Pointers(added_ids);
	const std::vector<const native::PeerId*> removed = Pointers(removed_ids);
	return detail::ToVoid(qar_gui_panels_update_visible_to(
		m_session.get(),
		detail::InValue(id),
		const_cast<const native::PeerId**>(added.data()),
		added.size(),
		const_cast<const native::PeerId**>(removed.data()),
		removed.size()
	));
}

template<typename Fn>
Result<Subscription>
GuiPanelControl::SubscribeUpdates(Fn&& fn) const noexcept
{
	const auto start =
		[session = m_session.get()](
			qar_gui_panel_update_callback_t callback, void* user_state, CancelTokenHandle* token
		)
	{
		return qar_gui_panels_subscribe_updates(session, callback, user_state, token);
	};
	return SubscribeSnapshots<GuiPanel, GuiPanelHandle>(std::forward<Fn>(fn), start);
}

template<typename Fn>
Result<Subscription>
GuiPanelControl::SubscribeUpdates(const GuiPanelId& id, Fn&& fn) const noexcept
{
	const auto start =
		[session = m_session.get(), id = ToNative(id)](
			qar_gui_panel_update_callback_t callback, void* user_state, CancelTokenHandle* token
		)
	{
		return qar_gui_panels_subscribe_panel_updates(session, &id, callback, user_state, token);
	};
	return SubscribeSnapshots<GuiPanel, GuiPanelHandle>(std::forward<Fn>(fn), start);
}

template<typename Fn>
Result<Subscription>
GuiPanelControl::SubscribeGestureEvents(
	const GuiPanelId& id, GuiPanelGestureKind kind, Fn&& fn
) const noexcept
{
	const auto start = [session = m_session.get(), id = ToNative(id), kind](
						   qar_gui_panel_gesture_event_callback_t callback,
						   void* user_state,
						   CancelTokenHandle* token
					   )
	{
		return qar_gui_panels_subscribe_gesture_events(
			session, &id, ToNative(kind), callback, user_state, token
		);
	};
	return SubscribeSnapshots<GuiPanelGesture, GuiPanelGestureHandle>(std::forward<Fn>(fn), start);
}

template<typename Fn>
Result<Subscription>
GuiPanelControl::SubscribeAllGestureEvents(const GuiPanelId& id, Fn&& fn) const noexcept
{
	const auto start = [session = m_session.get(), id = ToNative(id)](
						   qar_gui_panel_gesture_event_callback_t callback,
						   void* user_state,
						   CancelTokenHandle* token
					   )
	{
		return qar_gui_panels_subscribe_all_gesture_events(
			session, &id, callback, user_state, token
		);
	};
	return SubscribeSnapshots<GuiPanelGesture, GuiPanelGestureHandle>(std::forward<Fn>(fn), start);
}

inline std::vector<const native::PeerId*>
GuiPanelControl::Pointers(const std::vector<native::PeerId>& peers) noexcept
{
	std::vector<const native::PeerId*> pointers;
	pointers.reserve(peers.size());
	for(const native::PeerId& peer : peers)
	{
		pointers.push_back(&peer);
	}
	return pointers;
}

} // namespace qaros
