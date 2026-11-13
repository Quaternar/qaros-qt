#pragma once

#include <qar_streaming_cpp/Handle.hpp>

#include <chrono>

namespace qaros
{

/// A C cancel token. Destroying it cancels what it owns and waits for a
/// running subscription callback (C API "Ending a subscription").
class CancelToken
{
public:
	CancelToken() noexcept = default;
	explicit CancelToken(CancelTokenPtr owned) noexcept;

	static Result<CancelToken> Create() noexcept;
	/// Cancels itself after `timeout`.
	static Result<CancelToken> Create(std::chrono::milliseconds timeout) noexcept;

	[[nodiscard]]
	CancelTokenHandle* Get() const noexcept;
	explicit operator bool() const noexcept;

	[[nodiscard]]
	Result<void> Cancel() const noexcept;
	[[nodiscard]]
	Result<void> CancelAfter(std::chrono::milliseconds timeout) const noexcept;
	[[nodiscard]]
	bool IsCancelled() const noexcept;
	[[nodiscard]]
	bool IsTimeout() const noexcept;

	/// Destroys the token now (cancelling and waiting, see the class comment).
	void Reset() noexcept;

private:
	CancelTokenPtr m_handle;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/CancelToken.inl>
