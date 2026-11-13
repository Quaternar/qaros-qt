#pragma once

// Every C handle as a std::unique_ptr with its own deleter, and the helpers to
// fill one from a C out-parameter or share it.
//
//   qaros::SessionPtr session;
//   qar_runtime_rejoin(runtime, &builder.Get(), nullptr, nullptr, nullptr, qaros::OutPtr(session));
//   std::shared_ptr<SessionHandle> shared = qaros::Share(std::move(session));
//
// The deleter releases the handle only; it never tears down what the handle
// refers to (a runtime, panel or volume lives on, see the C API per type).

#include <qar_streaming_cpp/Status.hpp>

#include <memory>

namespace qaros
{

/// Calls the matching `qar_*_handle_destroy`. Specialised per handle type.
template<typename T>
struct HandleDeleter;

#define QAROS_DECLARE_HANDLE_DELETER(C_TYPE, ALIAS)                                                \
	template<>                                                                                     \
	struct HandleDeleter<C_TYPE>                                                                   \
	{                                                                                              \
		void operator()(C_TYPE* raw) const noexcept;                                               \
	};                                                                                             \
	using ALIAS = std::unique_ptr<C_TYPE, HandleDeleter<C_TYPE>>;

QAROS_DECLARE_HANDLE_DELETER(CancelTokenHandle, CancelTokenPtr)
QAROS_DECLARE_HANDLE_DELETER(RuntimeHandle, RuntimePtr)
QAROS_DECLARE_HANDLE_DELETER(SessionHandle, SessionPtr)
QAROS_DECLARE_HANDLE_DELETER(OnboardingInviteHandle, OnboardingInvitePtr)
QAROS_DECLARE_HANDLE_DELETER(PeerSpecHandle, PeerSpecPtr)
QAROS_DECLARE_HANDLE_DELETER(RenderSenderHandle, RenderSenderPtr)
QAROS_DECLARE_HANDLE_DELETER(RenderStreamRequestHandle, RenderStreamRequestPtr)
QAROS_DECLARE_HANDLE_DELETER(RenderFrameInfoHandle, RenderFrameInfoPtr)
QAROS_DECLARE_HANDLE_DELETER(VideoSenderHandle, VideoSenderPtr)
QAROS_DECLARE_HANDLE_DELETER(GuiPanelHandle, GuiPanelPtr)
QAROS_DECLARE_HANDLE_DELETER(AppVolumeHandle, AppVolumePtr)

#undef QAROS_DECLARE_HANDLE_DELETER

template<typename T>
using UniquePtr = std::unique_ptr<T, HandleDeleter<T>>;

/// Shared ownership keeps the handle deleter.
template<typename T>
std::shared_ptr<T> Share(UniquePtr<T>&& owned) noexcept;

/// C++23 std::out_ptr for a UniquePtr: the C call writes the raw handle, the
/// pointer adopts it at the end of the full expression.
template<typename T>
class OutPtrT
{
public:
	explicit OutPtrT(UniquePtr<T>* owner) noexcept;
	OutPtrT(const OutPtrT&) = delete;
	OutPtrT& operator=(const OutPtrT&) = delete;
	~OutPtrT() noexcept;

	operator T**() noexcept;

private:
	UniquePtr<T>* m_owner;
	T* m_raw = nullptr;
};

template<typename T>
OutPtrT<T> OutPtr(UniquePtr<T>& owner) noexcept;

} // namespace qaros

#include <qar_streaming_cpp/detail/Handle.inl>
