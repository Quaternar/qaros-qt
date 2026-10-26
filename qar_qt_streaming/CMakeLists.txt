# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# qar_qt_streaming — reusable Qt<->QarOS GUI-panel streaming library.
#
# Plain CMake (NOT ament). Reusable by ANY Qt application, so it has zero
# dependency on rviz_common, rviz_rendering, ament or rclcpp — and, unlike
# qar_ogre_streaming, zero dependency on Ogre. It DOES depend on Qt: that is the
# point of the library. See docs/ARCHITECTURE.md.
#
# The boundary (Qt allowed, everything else forbidden) is enforced by the layering
# guard: scripts/check_layering.py, CTest test check_layering_qar_qt_streaming.

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)

add_library(qar_qt_streaming STATIC
  src/capture_gate.cpp
  src/panel_close_mode.cpp
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

# qar_ar_core is PUBLIC: the panel seam types, the input seam and the timing log
# appear in this library's public headers. Qt is PUBLIC because public headers
# take QWidget* and QtWidgetCapture holds a QImage by value.
target_link_libraries(qar_qt_streaming PUBLIC
  qar_ogre_adapter::qar_ar_core
  Qt5::Core
  Qt5::Gui
  Qt5::Widgets
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
