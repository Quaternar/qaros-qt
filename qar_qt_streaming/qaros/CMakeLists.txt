# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# qar_qt_streaming_qaros - the backend of the panel seam over the QarOS C++ SDK (qar-streaming-cpp):
# a panel sender, a panel sender factory and a pointer input source that stream a panel as a video
# stream and take its gestures.
#
# It needs the seam and the SDK headers, and no Qt: the Qt core and the backend meet only at the
# seam, in the application. Enforced by the layering guard (scripts/check_layering.py). The library
# defines no macro of the C header and expands no function table; the application does, once.

add_library(qar_qt_streaming_qaros STATIC
  src/qaros_panel_input_source.cpp
  src/qaros_panel_sender.cpp
  src/qaros_panel_sender_factory.cpp
  src/session_executor.cpp
)
add_library(qar_qt_streaming::qaros ALIAS qar_qt_streaming_qaros)

target_compile_features(qar_qt_streaming_qaros PUBLIC cxx_std_17)

target_include_directories(qar_qt_streaming_qaros
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# The seam is PUBLIC (the headers use its interfaces). The SDK headers are PUBLIC too: the factory
# takes a qaros::Session. In the build tree the targets come from the top-level project. The
# installed package creates the target qar_qt_streaming_sdk for its consumers (it stands for the
# SDK's qar-streaming-cpp); the name differs from the build tree's, because an export refuses a
# dependency whose target of the same name is not part of it.
target_link_libraries(qar_qt_streaming_qaros PUBLIC
  qar_qt_streaming::seam
  "$<BUILD_INTERFACE:qar-streaming-cpp>"
  "$<INSTALL_INTERFACE:qar_qt_streaming_sdk>"
)

# The macros of the C header the backend is compiled with. They are PRIVATE: the library defines
# none for its users, and the program that links it names the same set (the public headers include
# qar_qt_streaming/qaros/sdk_config_check.hpp, which turns a difference into a link error).
target_compile_definitions(qar_qt_streaming_qaros PRIVATE ${QAR_QT_STREAMING_SDK_DEFINES})

# A static library that is linked into a shared library (a plugin, for example)
# must be position-independent on the platforms where that matters (not Windows).
set_target_properties(qar_qt_streaming_qaros PROPERTIES POSITION_INDEPENDENT_CODE ON)
# The installed package calls the target qar_qt_streaming::qaros, like the alias of the build tree.
set_target_properties(qar_qt_streaming_qaros PROPERTIES EXPORT_NAME qaros)

qar_apply_warning_flags(qar_qt_streaming_qaros)
