#pragma once

// Implementation of CancelToken.hpp.

#include <cstdint>
#include <utility>

namespace qaros
{

inline CancelToken::CancelToken(CancelTokenPtr owned) noexcept
	: m_handle(std::move(owned))
{
}

inline Result<CancelToken>
CancelToken::Create() noexcept
{
	CancelTokenPtr token;
	const Status status(qar_cancel_token_create(OutPtr(token)));
	if(not status.Ok())
	{
		return Result<CancelToken>(status);
	}
	return CancelToken(std::move(token));
}

inline Result<CancelToken>
CancelToken::Create(std::chrono::milliseconds timeout) noexcept
{
	CancelTokenPtr token;
	const Status status(
		qar_cancel_token_create_with_timeout(OutPtr(token), static_cast<uint32_t>(timeout.count()))
	);
	if(not status.Ok())
	{
		return Result<CancelToken>(status);
	}
	return CancelToken(std::move(token));
}

inline CancelTokenHandle*
CancelToken::Get() const noexcept
{
	return m_handle.get();
}

inline CancelToken::
operator bool() const noexcept
{
	return m_handle != nullptr;
}

inline Result<void>
CancelToken::Cancel() const noexcept
{
	return detail::ToVoid(qar_cancel_token_cancel(m_handle.get()));
}

inline Result<void>
CancelToken::CancelAfter(std::chrono::milliseconds timeout) const noexcept
{
	return detail::ToVoid(
		qar_cancel_token_cancel_after(m_handle.get(), static_cast<uint32_t>(timeout.count()))
	);
}

inline bool
CancelToken::IsCancelled() const noexcept
{
	return qar_cancel_token_is_cancelled(m_handle.get());
}

inline bool
CancelToken::IsTimeout() const noexcept
{
	return qar_cancel_token_is_timeout(m_handle.get());
}

inline void
CancelToken::Reset() noexcept
{
	m_handle.reset();
}

} // namespace qaros
