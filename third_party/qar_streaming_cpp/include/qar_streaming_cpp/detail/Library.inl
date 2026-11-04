#pragma once

// Implementation of Library.hpp.

#include <qar_streaming_cpp/detail/Chain.hpp>

#include <utility>

namespace qaros
{

inline LibraryBuilder::LibraryBuilder() noexcept
	: m_init(native::Default<native::LibraryInit>())
{
}

inline LibraryBuilder&
LibraryBuilder::WithConsoleLogging(bool enabled) noexcept
{
	m_init.enable_console_logging = enabled;
	return *this;
}

inline LibraryBuilder&
LibraryBuilder::WithLogFolder(std::string path) noexcept
{
	m_logFolder = std::move(path);
	Sync();
	return *this;
}

inline LibraryBuilder&
LibraryBuilder::WithLogSeverity(LogSeverity severity) noexcept
{
	m_init.log_severity = ToNative(severity);
	return *this;
}

inline const native::LibraryInit&
LibraryBuilder::Get() const noexcept
{
	return m_init;
}

inline void
LibraryBuilder::Sync() noexcept
{
	if(m_logFolder.has_value())
	{
		m_init.log_folder_path = m_logFolder->c_str();
	}
}

inline Result<Library>
Library::Init(const LibraryBuilder& builder) noexcept
{
	const Status status(qar_library_init(&builder.Get()));
	if(not status.Ok())
	{
		return Result<Library>(status);
	}
	Library library;
	library.m_active = true;
	return library;
}

inline Library::Library(Library&& other) noexcept
	: m_active(std::exchange(other.m_active, false))
{
}

inline Library&
Library::operator=(Library&& other) noexcept
{
	if(this != &other)
	{
		[[maybe_unused]]
		const Result<void> dropped = Shutdown();
		m_active = std::exchange(other.m_active, false);
	}
	return *this;
}

inline Library::~Library() noexcept
{
	[[maybe_unused]]
	const Result<void> dropped = Shutdown();
}

inline Result<void>
Library::Shutdown() noexcept
{
	if(not std::exchange(m_active, false))
	{
		return Result<void>();
	}
	return detail::ToVoid(qar_library_destroy());
}

} // namespace qaros
