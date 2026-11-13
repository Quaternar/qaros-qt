#pragma once

// Implementation of Status.hpp.

#include <array>
#include <utility>

namespace qaros
{

inline Status::Status(NativeResult result) noexcept
	: m_result(result)
{
}

inline Status
Status::Error(StatusCode code, const char* message) noexcept
{
	return Status(qar_result_error(ToNative(code), message));
}

inline bool
Status::Ok() const noexcept
{
	return m_result.code == QAR_STATUS_SUCCESS;
}

inline Status::
operator bool() const noexcept
{
	return Ok();
}

inline StatusCode
Status::Code() const noexcept
{
	return FromNative(m_result.code);
}

inline bool
Status::HasCode(StatusCode code) const noexcept
{
	return qar_result_has_code(m_result, ToNative(code));
}

inline NativeResult
Status::Raw() const noexcept
{
	return m_result;
}

inline std::string
Status::Message() const noexcept
{
	std::array<char, 2048> buffer = {};
	qar_result_message(m_result, buffer.data(), buffer.size());
	return std::string(buffer.data());
}

inline void
Status::LogIfError() const noexcept
{
	qar_result_log_if_error(m_result);
}

template<typename T>
Result<T>::Result(Status status) noexcept
	: m_status(status)
{
}

template<typename T>
Result<T>::Result(T value) noexcept
	: m_value(std::move(value))
{
}

template<typename T>
bool
Result<T>::Ok() const noexcept
{
	return m_status.Ok();
}

template<typename T>
Result<T>::
operator bool() const noexcept
{
	return Ok();
}

template<typename T>
const Status&
Result<T>::GetStatus() const noexcept
{
	return m_status;
}

template<typename T>
T&
Result<T>::Value() & noexcept
{
	return m_value;
}

template<typename T>
const T&
Result<T>::Value() const& noexcept
{
	return m_value;
}

template<typename T>
T&&
Result<T>::Value() && noexcept
{
	return std::move(m_value);
}

template<typename T>
T*
Result<T>::operator->() noexcept
{
	return &m_value;
}

template<typename T>
const T*
Result<T>::operator->() const noexcept
{
	return &m_value;
}

inline Result<void>::Result(Status status) noexcept
	: m_status(status)
{
}

inline bool
Result<void>::Ok() const noexcept
{
	return m_status.Ok();
}

inline Result<void>::
operator bool() const noexcept
{
	return Ok();
}

inline const Status&
Result<void>::GetStatus() const noexcept
{
	return m_status;
}

namespace detail
{

/// A C call that returns only its result.
inline Result<void>
ToVoid(NativeResult result) noexcept
{
	return Result<void>(Status(result));
}

} // namespace detail

} // namespace qaros
