#pragma once

// Header-only C++ helpers over the QAROS C API (qar_streaming.h).
//
// - C++17 or newer.
// - Asio is optional: define QAR_STREAMING_CPP_WITH_ASIO (standalone asio) or
//   QAR_STREAMING_CPP_WITH_BOOST_ASIO (Boost.Asio) before including any header
//   of this library to make every async operation accept asio completion tokens.
// - Works with both ways of consuming the C API (static link, or
//   QAR_ENABLE_DYNAMIC_LOADING): it only calls the qar_* functions of the header.

#if defined(_MSVC_LANG)
#define QAR_STREAMING_CPP_CPLUSPLUS _MSVC_LANG
#else
#define QAR_STREAMING_CPP_CPLUSPLUS __cplusplus
#endif

#if QAR_STREAMING_CPP_CPLUSPLUS < 201703L
#error "qar_streaming_cpp requires C++17 or newer"
#endif

// Override to include the C API from somewhere else (e.g. the split v0 headers).
#ifdef QAR_STREAMING_CPP_C_HEADER
#include QAR_STREAMING_CPP_C_HEADER
#else
#include <qar_streaming.h>
#endif

#if defined(QAR_STREAMING_CPP_WITH_BOOST_ASIO)
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>
#define QAR_STREAMING_CPP_HAS_ASIO 1
namespace qaros::detail
{
namespace net = ::boost::asio;
} // namespace qaros::detail
#elif defined(QAR_STREAMING_CPP_WITH_ASIO)
#include <asio/associated_cancellation_slot.hpp>
#include <asio/associated_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/dispatch.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/post.hpp>
#define QAR_STREAMING_CPP_HAS_ASIO 1
namespace qaros::detail
{
namespace net = ::asio;
} // namespace qaros::detail
#else
#define QAR_STREAMING_CPP_HAS_ASIO 0
#endif
