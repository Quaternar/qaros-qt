// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QAROS__SDK_CONFIG_CHECK_HPP_
#define QAR_QT_STREAMING__QAROS__SDK_CONFIG_CHECK_HPP_

/// @file sdk_config_check.hpp
/// @brief Makes a disagreement about the QarOS C header's configuration macros a link error.
///
/// The C header changes what it declares with `QAR_ENABLE_DYNAMIC_LOADING`, `QAR_ENABLE_D3D11`,
/// `QAR_ENABLE_OPENGL` and `QAR_ENABLE_VULKAN`. The qaros backend is compiled with one set and the
/// program that links it with another one only by mistake. Every translation unit that includes
/// this header records its set, and the MSVC linker refuses to link two translation units that
/// recorded different ones. The backend's sources and its public headers include it, so every
/// translation unit that includes a header of the backend is compared with the backend; so should
/// the translation unit of the program that expands the function tables of the C header. Other
/// compilers ignore the header.

#if defined(_MSC_VER)

#ifdef QAR_ENABLE_DYNAMIC_LOADING
#pragma detect_mismatch("qar_enable_dynamic_loading", "1")
#else
#pragma detect_mismatch("qar_enable_dynamic_loading", "0")
#endif

// The C header turns QAR_ENABLE_D3D11 on by itself on Windows, so the value recorded here must
// not depend on whether a translation unit includes the C header before or after this header.
#if defined(QAR_ENABLE_D3D11) || defined(_WIN32) || defined(_WIN64)
#pragma detect_mismatch("qar_enable_d3d11", "1")
#else
#pragma detect_mismatch("qar_enable_d3d11", "0")
#endif

#ifdef QAR_ENABLE_OPENGL
#pragma detect_mismatch("qar_enable_opengl", "1")
#else
#pragma detect_mismatch("qar_enable_opengl", "0")
#endif

#ifdef QAR_ENABLE_VULKAN
#pragma detect_mismatch("qar_enable_vulkan", "1")
#else
#pragma detect_mismatch("qar_enable_vulkan", "0")
#endif

#endif  // defined(_MSC_VER)

#endif  // QAR_QT_STREAMING__QAROS__SDK_CONFIG_CHECK_HPP_
