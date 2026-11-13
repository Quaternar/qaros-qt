#pragma once

#include <qar_streaming_cpp/Types.hpp>

#include <string>
#include <type_traits>

namespace qaros
{

/// The C API's result (NativeResult) with C++ accessors. No exceptions: failures are values.
class Status
{
public:
	Status() noexcept = default;
	explicit Status(NativeResult result) noexcept;

	/// An error raised by the wrapper itself, kept in the library's error store.
	static Status Error(StatusCode code, const char* message) noexcept;

	[[nodiscard]]
	bool Ok() const noexcept;
	explicit operator bool() const noexcept;
	[[nodiscard]]
	StatusCode Code() const noexcept;
	/// Whether this result or any cause it wraps carries `code`.
	[[nodiscard]]
	bool HasCode(StatusCode code) const noexcept;
	[[nodiscard]]
	NativeResult Raw() const noexcept;
	[[nodiscard]]
	std::string Message() const noexcept;
	void LogIfError() const noexcept;

private:
	NativeResult m_result = { QAR_STATUS_SUCCESS, 0 };
};

/// A Status and, on success, a value. `T` must be default-constructible: the
/// C API hands out handles and ids, both of which have an empty state.
template<typename T>
class Result
{
public:
	static_assert(std::is_default_constructible_v<T>);

	Result() = default;
	explicit Result(Status status) noexcept;
	Result(T value) noexcept;

	[[nodiscard]]
	bool Ok() const noexcept;
	explicit operator bool() const noexcept;
	[[nodiscard]]
	const Status& GetStatus() const noexcept;
	[[nodiscard]]
	T& Value() & noexcept;
	[[nodiscard]]
	const T& Value() const& noexcept;
	[[nodiscard]]
	T&& Value() && noexcept;
	T* operator->() noexcept;
	const T* operator->() const noexcept;

private:
	Status m_status = {};
	T m_value = {};
};

/// The result of a call that returns nothing but can fail.
template<>
class Result<void>
{
public:
	Result() noexcept = default;
	explicit Result(Status status) noexcept;

	[[nodiscard]]
	bool Ok() const noexcept;
	explicit operator bool() const noexcept;
	[[nodiscard]]
	const Status& GetStatus() const noexcept;

private:
	Status m_status = {};
};

} // namespace qaros

#include <qar_streaming_cpp/detail/Status.inl>
