#pragma once

// Implementation of Handle.hpp.

#include <utility>

namespace qaros
{

#define QAROS_DEFINE_HANDLE_DELETER(C_TYPE, DESTROY_FN)                                            \
	inline void HandleDeleter<C_TYPE>::operator()(C_TYPE* raw) const noexcept                      \
	{                                                                                              \
		DESTROY_FN(raw);                                                                           \
	}

QAROS_DEFINE_HANDLE_DELETER(CancelTokenHandle, qar_cancel_token_handle_destroy)
QAROS_DEFINE_HANDLE_DELETER(RuntimeHandle, qar_runtime_handle_destroy)
QAROS_DEFINE_HANDLE_DELETER(SessionHandle, qar_session_handle_destroy)
QAROS_DEFINE_HANDLE_DELETER(OnboardingInviteHandle, qar_onboarding_invite_handle_destroy)
QAROS_DEFINE_HANDLE_DELETER(PeerSpecHandle, qar_peer_spec_handle_destroy)
QAROS_DEFINE_HANDLE_DELETER(RenderSenderHandle, qar_render_stream_handle_destroy)
QAROS_DEFINE_HANDLE_DELETER(RenderStreamRequestHandle, qar_render_request_handle_destroy)
QAROS_DEFINE_HANDLE_DELETER(RenderFrameInfoHandle, qar_render_frame_info_handle_destroy)
QAROS_DEFINE_HANDLE_DELETER(VideoSenderHandle, qar_video_sender_handle_destroy)
QAROS_DEFINE_HANDLE_DELETER(GuiPanelHandle, qar_gui_panel_handle_destroy)
QAROS_DEFINE_HANDLE_DELETER(AppVolumeHandle, qar_app_volume_handle_destroy)

#undef QAROS_DEFINE_HANDLE_DELETER

template<typename T>
std::shared_ptr<T>
Share(UniquePtr<T>&& owned) noexcept
{
	return std::shared_ptr<T>(std::move(owned));
}

template<typename T>
OutPtrT<T>::OutPtrT(UniquePtr<T>* owner) noexcept
	: m_owner(owner)
{
}

template<typename T>
OutPtrT<T>::~OutPtrT() noexcept
{
	m_owner->reset(m_raw);
}

template<typename T>
OutPtrT<T>::
operator T**() noexcept
{
	return &m_raw;
}

template<typename T>
OutPtrT<T>
OutPtr(UniquePtr<T>& owner) noexcept
{
	return OutPtrT<T>(&owner);
}

} // namespace qaros
