# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# qar_qt_streaming - reusable Qt GUI-panel streaming library.
#
# Plain CMake (NOT ament). Reusable by ANY Qt application, so it has zero
# dependency on any host application, middleware or rendering library, and none on any other
# library of this project: its seam (seam/, standard library only) is part of it. It DOES depend
# on Qt: that is the point of the library. See docs/ARCHITECTURE.md.
#
# Two targets:
#   qar_qt_streaming_seam  (alias qar_qt_streaming::seam)  the values, the panel and input
#                          interfaces, the naming rule, the timing log; standard library only.
#   qar_qt_streaming       (alias qar_qt_streaming::qar_qt_streaming)  the Qt core over the seam.
# and, when QAR_QT_STREAMING_WITH_QAROS is on, a third one:
#   qar_qt_streaming_qaros (alias qar_qt_streaming::qaros)  the backend over the QarOS C++ SDK
#                          (SDK headers and the seam; no Qt).
#
# The boundary (Qt allowed in the core only, everything else forbidden) is enforced by the layering
# guard: scripts/check_layering.py, CTest test check_layering_qar_qt_streaming.

# The Qt major version comes from the caller (QAR_QT_MAJOR); without one the library builds
# against Qt 5.
if(NOT QAR_QT_MAJOR)
  set(QAR_QT_MAJOR 5)
endif()
find_package(Qt${QAR_QT_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)

add_subdirectory(seam)

# The backend over the QarOS C++ SDK; the top-level project says whether it is built.
if(QAR_QT_STREAMING_WITH_QAROS)
  add_subdirectory(qaros)
endif()

add_library(qar_qt_streaming STATIC
  src/capture_gate.cpp
  src/panel_close_mode.cpp
  src/panel_resize_mode.cpp
  src/offscreen_panel.cpp
  src/offscreen_popup_guard.cpp
  src/panel_geometry.cpp
  src/panel_hit_test.cpp
  src/panel_tombstone.cpp
  src/video_frame.cpp
  src/latest_video_frame.cpp
  src/video_panel_widget.cpp
  src/qt_widget_capture.cpp
  src/scroll_census.cpp
  src/qt_panel_stream.cpp
  src/qt_panel_stream_manager.cpp
  src/ar_readability.cpp
  src/panel_pointer_state.cpp
  src/qt_panel_input_injector.cpp
)
add_library(qar_qt_streaming::qar_qt_streaming ALIAS qar_qt_streaming)

target_compile_features(qar_qt_streaming PUBLIC cxx_std_17)

target_include_directories(qar_qt_streaming PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)

# The seam is PUBLIC: its panel types, the input interface and the timing log appear in this
# library's public headers. Qt is PUBLIC because public headers take QWidget* and
# QtWidgetCapture holds a QImage by value.
target_link_libraries(qar_qt_streaming PUBLIC
  qar_qt_streaming::seam
  Qt${QAR_QT_MAJOR}::Core
  Qt${QAR_QT_MAJOR}::Gui
  Qt${QAR_QT_MAJOR}::Widgets
)

# A static library that is linked into a shared library (a plugin, for example)
# must be position-independent on the platforms where that matters (not Windows).
set_target_properties(qar_qt_streaming PROPERTIES POSITION_INDEPENDENT_CODE ON)

qar_apply_warning_flags(qar_qt_streaming)

# ---------------------------------------------------------------------------
# Header-only test support: recording panel sender test double + manual clock.
# Separate INTERFACE target so tests can reuse it without it shipping.
# ---------------------------------------------------------------------------
add_library(qar_qt_streaming_test_support INTERFACE)
add_library(qar_qt_streaming::test_support ALIAS qar_qt_streaming_test_support)
target_include_directories(qar_qt_streaming_test_support INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test_support/include>"
)
target_link_libraries(qar_qt_streaming_test_support INTERFACE
  qar_qt_streaming
)
