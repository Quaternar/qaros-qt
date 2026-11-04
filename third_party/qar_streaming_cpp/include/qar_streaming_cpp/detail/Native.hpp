#pragma once

// Reading C out-parameters into C++ values. Internal to this library.

#include <qar_streaming_cpp/Handle.hpp>
#include <qar_streaming_cpp/Values.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace qaros::detail
{

/// The C API has no length query for most strings; same bound as the C#
/// binding. Longer text comes back truncated.
inline constexpr size_t MaxStringLength = 512;

/// Collects the first failure of a sequence of C calls that fill one value.
class StatusChain
{
public:
	void Add(NativeResult result)
	{
		if(m_first.Ok())
		{
			m_first = Status(result);
		}
	}

	template<typename T>
	void Take(T* target, Result<T> result)
	{
		if(not result.Ok())
		{
			Add(result.GetStatus().Raw());
			return;
		}
		*target = std::move(result).Value();
	}

	[[nodiscard]]
	bool Ok() const noexcept
	{
		return m_first.Ok();
	}

	[[nodiscard]]
	const Status& First() const noexcept
	{
		return m_first;
	}

private:
	Status m_first;
};

/// Out-parameter for a C value (enum or struct), like OutPtr: the C call
/// writes the C value, `*target` receives it converted at the end of the full
/// expression.
template<typename Cpp>
class ValueOut
{
public:
	using Native = typename NativeOf<Cpp>::Type;

	explicit ValueOut(Cpp* target)
		: m_target(target)
		, m_native(ToNative(*target))
	{
	}

	ValueOut(const ValueOut&) = delete;
	ValueOut& operator=(const ValueOut&) = delete;

	~ValueOut() { *m_target = FromNative(m_native); }

	operator Native*() noexcept { return &m_native; }

private:
	Cpp* m_target;
	Native m_native;
};

template<typename Cpp>
ValueOut<Cpp>
OutValue(Cpp* target) noexcept
{
	return ValueOut<Cpp>(target);
}

/// In-parameter: the C value of `value`, alive to the end of the full
/// expression.
template<typename Cpp>
class ValueIn
{
public:
	using Native = typename NativeOf<Cpp>::Type;

	explicit ValueIn(const Cpp& value)
		: m_native(ToNative(value))
	{
	}

	ValueIn(const ValueIn&) = delete;
	ValueIn& operator=(const ValueIn&) = delete;

	operator const Native*() const noexcept { return &m_native; }

private:
	Native m_native;
};

template<typename Cpp>
ValueIn<Cpp>
InValue(const Cpp& value) noexcept
{
	return ValueIn<Cpp>(value);
}

template<typename Cpp>
std::vector<typename NativeOf<Cpp>::Type>
ToNativeAll(const std::vector<Cpp>& values) noexcept
{
	std::vector<typename NativeOf<Cpp>::Type> natives;
	natives.reserve(values.size());
	for(const Cpp& value : values)
	{
		natives.push_back(ToNative(value));
	}
	return natives;
}

template<typename Cpp>
Result<std::vector<Cpp>>
FromNativeAll(Result<std::vector<typename NativeOf<Cpp>::Type>> natives) noexcept
{
	if(not natives.Ok())
	{
		return Result<std::vector<Cpp>>(natives.GetStatus());
	}
	std::vector<Cpp> values;
	values.reserve(natives.Value().size());
	for(const auto& native : natives.Value())
	{
		values.push_back(FromNative(native));
	}
	return values;
}

/// `get(handle, buffer, size)`, the C string getter shape.
template<typename Get, typename Handle>
Result<std::string>
ReadString(Get get, Handle* handle) noexcept
{
	std::array<char, MaxStringLength> buffer = {};
	const Status status(get(handle, buffer.data(), buffer.size()));
	if(not status.Ok())
	{
		return Result<std::string>(status);
	}
	return std::string(buffer.data());
}

/// `count(handle, &n)` then `fill(handle, out, capacity, &written)`.
template<typename T, typename Count, typename Fill, typename Handle>
Result<std::vector<T>>
ReadCounted(Count count, Fill fill, Handle* handle) noexcept
{
	size_t total = 0;
	const Status counted(count(handle, &total));
	if(not counted.Ok())
	{
		return Result<std::vector<T>>(counted);
	}
	std::vector<T> values(total);
	if(total == 0)
	{
		return values;
	}
	size_t written = 0;
	const Status filled(fill(handle, values.data(), values.size(), &written));
	if(not filled.Ok())
	{
		return Result<std::vector<T>>(filled);
	}
	values.resize(std::min(written, values.size()));
	return values;
}

/// `get(handle, out, capacity, &total)` where a 0 capacity asks for the total.
template<typename T, typename Get, typename Handle>
Result<std::vector<T>>
ReadWithCapacity(Get get, Handle* handle) noexcept
{
	size_t total = 0;
	const Status counted(get(handle, nullptr, 0, &total));
	if(not counted.Ok())
	{
		return Result<std::vector<T>>(counted);
	}
	std::vector<T> values(total);
	if(total == 0)
	{
		return values;
	}
	size_t again = 0;
	const Status filled(get(handle, values.data(), values.size(), &again));
	if(not filled.Ok())
	{
		return Result<std::vector<T>>(filled);
	}
	values.resize(std::min(again, values.size()));
	return values;
}

/// Queries handles the caller owns (`qar_query_*`), reads each into a
/// snapshot with `Snapshot::Read(raw)`, then releases them.
template<typename Snapshot, typename Raw, typename Count, typename Query, typename Session>
Result<std::vector<Snapshot>>
QuerySnapshots(Count count, Query query, Session* session) noexcept
{
	size_t total = 0;
	const Status counted(count(session, &total));
	if(not counted.Ok())
	{
		return Result<std::vector<Snapshot>>(counted);
	}
	std::vector<Raw*> raws(total, nullptr);
	size_t written = 0;
	if(total > 0)
	{
		const Status queried(query(session, raws.data(), raws.size(), &written));
		if(not queried.Ok())
		{
			return Result<std::vector<Snapshot>>(queried);
		}
	}
	std::vector<UniquePtr<Raw>> owned;
	owned.reserve(written);
	for(size_t index = 0; index < std::min(written, raws.size()); ++index)
	{
		owned.emplace_back(raws[index]);
	}
	std::vector<Snapshot> snapshots;
	snapshots.reserve(owned.size());
	for(const UniquePtr<Raw>& raw : owned)
	{
		Result<Snapshot> snapshot = Snapshot::Read(raw.get());
		if(not snapshot.Ok())
		{
			return Result<std::vector<Snapshot>>(snapshot.GetStatus());
		}
		snapshots.push_back(std::move(snapshot).Value());
	}
	return snapshots;
}

} // namespace qaros::detail
