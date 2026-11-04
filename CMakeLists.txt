# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# qar_qt_streaming: Qt widgets as QarOS AR GUI panels. It needs Qt (5.15, or 6.5 and newer) and, for
# the qaros backend, the headers of the QarOS C++ SDK (qar-streaming-cpp) with the QarOS C header.
# It needs no ROS 2, no RViz and no Ogre.
#
# Three libraries and one example:
#   seam               plain C++ types and interfaces (standard library only)
#   qar_qt_streaming   the Qt core over the seam (Qt only)
#   qaros              the backend over the SDK (SDK headers only)
#   qt_panel_example   a Qt application that shows the panels of the backend
#
# The project is also a subproject: add_subdirectory(<this folder>) or FetchContent_Declare with the
# source ZIP builds only the libraries (tests, examples and install rules are off unless asked for).
cmake_minimum_required(VERSION 3.22)
project(qar_qt_streaming VERSION 0.2.0
  DESCRIPTION "Qt widgets as QarOS AR GUI panels"
  LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/QarBuildSupport.cmake")

# The oldest QarOS SDK this version is written for (the C API with onboarding, version 0.9). The C
# headers carry no version macro, so the number is recorded in the package configuration only.
set(QAR_QT_STREAMING_MIN_SDK "0.9")

option(QAR_BUILD_TESTS "Build the unit tests and the layering guard tests" ${PROJECT_IS_TOP_LEVEL})
option(QAR_BUILD_EXAMPLES "Build the examples under examples/" ${PROJECT_IS_TOP_LEVEL})
option(QAR_BUILD_DOC_SNIPPETS "Build the compile-checked documentation snippets" ${PROJECT_IS_TOP_LEVEL})
option(QAR_INSTALL "Generate the install rules and the CMake package" ${PROJECT_IS_TOP_LEVEL})
option(QAR_QT_STREAMING_WITH_QAROS "Build the qaros backend (needs the QarOS C++ SDK headers)" ON)

if(PROJECT_IS_TOP_LEVEL)
  qar_set_default_build_type()
  qar_set_default_install_prefix()
endif()

qar_use_qt(COMPONENTS Core Gui Widgets)

# ---------------------------------------------------------------------------------------------
# The QarOS SDK headers for the qaros backend: the targets qar-streaming-c-headers (the C header
# qar_streaming.h) and qar-streaming-cpp (the C++ wrapper over it, header only). Where they come
# from, first match wins:
#   1. targets of these names that a parent project already defines;
#   2. -DQAR_STREAMING_CPP_INCLUDE_DIR=<folder>, else <QAR_SDK_ROOT>/include (-D, else the
#      environment variable), when the folder holds qar_streaming_cpp/Config.hpp and qar_streaming.h;
#   3. the copies in third_party/ of this repository.
# The library defines no QAR_ENABLE_* macro of the C header itself; the backend gets
# QAR_QT_STREAMING_SDK_DEFINES privately.
# ---------------------------------------------------------------------------------------------
set(QAR_STREAMING_CPP_INCLUDE_DIR "" CACHE PATH
  "Folder with qar_streaming_cpp/Config.hpp (default: <QAR_SDK_ROOT>/include, else third_party/)")
set(QAR_QT_STREAMING_SDK_DEFINES "QAR_ENABLE_DYNAMIC_LOADING" CACHE STRING
  "Macros of the QarOS C header that the qaros backend is compiled with")
if(QAR_QT_STREAMING_WITH_QAROS)
  if(TARGET qar-streaming-cpp AND TARGET qar-streaming-c-headers)
    set(QAR_QT_STREAMING_SDK_SOURCE "targets of the parent project")
  else()
    qar_resolve_location(QAR_SDK_ROOT ENV QAR_SDK_ROOT WHAT "QarOS SDK")
    set(_qar_sdk_dirs "${QAR_STREAMING_CPP_INCLUDE_DIR}")
    if(QAR_SDK_ROOT)
      list(APPEND _qar_sdk_dirs "${QAR_SDK_ROOT}/include")
    endif()
    set(_qar_sdk_cpp_dir "")
    foreach(_qar_candidate IN LISTS _qar_sdk_dirs)
      if(NOT _qar_sdk_cpp_dir AND EXISTS "${_qar_candidate}/qar_streaming_cpp/Config.hpp"
          AND EXISTS "${_qar_candidate}/qar_streaming.h")
        set(_qar_sdk_cpp_dir "${_qar_candidate}")
      endif()
    endforeach()
    if(QAR_STREAMING_CPP_INCLUDE_DIR AND NOT _qar_sdk_cpp_dir)
      message(FATAL_ERROR
        "QAR_STREAMING_CPP_INCLUDE_DIR (${QAR_STREAMING_CPP_INCLUDE_DIR}) has no "
        "qar_streaming_cpp/Config.hpp and qar_streaming.h.")
    endif()
    if(_qar_sdk_cpp_dir)
      set(_qar_sdk_c_dir "${_qar_sdk_cpp_dir}")
      set(QAR_QT_STREAMING_SDK_SOURCE "${_qar_sdk_cpp_dir}")
    else()
      set(_qar_sdk_cpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qar_streaming_cpp/include")
      set(_qar_sdk_c_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qar/include")
      set(QAR_QT_STREAMING_SDK_SOURCE "third_party/")
    endif()
    foreach(_qar_check
        "${_qar_sdk_cpp_dir}/qar_streaming_cpp/Config.hpp" "${_qar_sdk_c_dir}/qar_streaming.h")
      if(NOT EXISTS "${_qar_check}")
        message(FATAL_ERROR "QarOS SDK header not found: ${_qar_check}")
      endif()
    endforeach()
    add_library(qar-streaming-c-headers INTERFACE)
    target_include_directories(qar-streaming-c-headers INTERFACE
      "$<BUILD_INTERFACE:${_qar_sdk_c_dir}>")
    add_library(qar-streaming-cpp INTERFACE)
    target_include_directories(qar-streaming-cpp INTERFACE
      "$<BUILD_INTERFACE:${_qar_sdk_cpp_dir}>")
    target_link_libraries(qar-streaming-cpp INTERFACE qar-streaming-c-headers)
    target_compile_features(qar-streaming-cpp INTERFACE cxx_std_17)
    set(QAR_QT_STREAMING_SDK_CPP_DIR "${_qar_sdk_cpp_dir}")
    set(QAR_QT_STREAMING_SDK_C_DIR "${_qar_sdk_c_dir}")
  endif()
  message(STATUS "QarOS SDK headers: ${QAR_QT_STREAMING_SDK_SOURCE}")
endif()

add_subdirectory(qar_qt_streaming)
if(QAR_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
# The documentation snippets are built when the documentation folder is present.
if(QAR_BUILD_DOC_SNIPPETS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/snippets/CMakeLists.txt")
  add_subdirectory(docs/snippets)
endif()

if(QAR_BUILD_TESTS)
  enable_testing()
  qar_require_googletest()
  add_subdirectory(qar_qt_streaming/seam/test)
  if(TARGET qar_qt_streaming_qaros)
    add_subdirectory(qar_qt_streaming/qaros/test)
  endif()
  add_subdirectory(qar_qt_streaming/test)
  qar_add_layering_guard_tests(MODULES qar_qt_streaming examples)
endif()

# The source ZIP: `cpack --config CPackSourceConfig.cmake` in a configured build folder. The
# ignore patterns are anchored at this source folder, so a source folder below a folder that is
# itself called build or out is packed completely.
if(PROJECT_IS_TOP_LEVEL)
  string(REGEX REPLACE "([][+.*()^$?|\\\\])" "\\\\\\1" _qar_src_re "${CMAKE_CURRENT_SOURCE_DIR}")
  set(CPACK_SOURCE_GENERATOR "ZIP")
  set(CPACK_SOURCE_PACKAGE_FILE_NAME "qar_qt_streaming-${PROJECT_VERSION}-src")
  set(CPACK_PACKAGE_NAME "qar_qt_streaming")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_SOURCE_IGNORE_FILES
    "${_qar_src_re}/\\.git/" "${_qar_src_re}/\\.git$" "${_qar_src_re}/build/" "${_qar_src_re}/out/"
    "${_qar_src_re}/CMakeUserPresets\\.json$")
  include(CPack)
endif()

qar_install_enabled(_qar_install)
if(_qar_install)
  set(QAR_QT_STREAMING_QAROS_INSTALLED OFF)
  set(_qar_install_targets qar_qt_streaming_seam qar_qt_streaming)
  set(_qar_install_headers qar_qt_streaming/seam/include/ qar_qt_streaming/include/)
  if(TARGET qar_qt_streaming_qaros)
    set(QAR_QT_STREAMING_QAROS_INSTALLED ON)
    list(APPEND _qar_install_targets qar_qt_streaming_qaros)
    list(APPEND _qar_install_headers qar_qt_streaming/qaros/include/)
  endif()
  install(TARGETS ${_qar_install_targets} EXPORT qar_qt_streamingTargets
    ARCHIVE DESTINATION lib
    INCLUDES DESTINATION include)
  install(DIRECTORY ${_qar_install_headers} DESTINATION include)
  # The SDK headers are installed with the package only when they come from third_party/; headers
  # from an SDK installation stay where they are and the package configuration records the folder.
  set(QAR_QT_STREAMING_SDK_CONFIG_DIR "")
  if(QAR_QT_STREAMING_WITH_QAROS AND DEFINED QAR_QT_STREAMING_SDK_CPP_DIR)
    if(QAR_QT_STREAMING_SDK_SOURCE STREQUAL "third_party/")
      install(DIRECTORY third_party/qar_streaming_cpp/include/ third_party/qar/include/
        DESTINATION share/qar_qt_streaming/sdk/include)
      install(FILES third_party/qar_streaming_cpp/README.md third_party/qar/LICENSE
        DESTINATION share/qar_qt_streaming/sdk)
    else()
      set(QAR_QT_STREAMING_SDK_CONFIG_DIR "${QAR_QT_STREAMING_SDK_CPP_DIR}")
    endif()
  endif()
  install(FILES LICENSE DESTINATION share/qar_qt_streaming OPTIONAL)
  qar_install_package(qar_qt_streaming EXPORT qar_qt_streamingTargets NAMESPACE qar_qt_streaming::)
endif()
