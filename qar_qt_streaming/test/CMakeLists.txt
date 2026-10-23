# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Unit tests for qar_qt_streaming.
#
# GoogleTest comes from the top-level qar_require_googletest(); this directory just uses
# its targets.
#
# Unlike the Ogre tests (which never call a real Ogre function), these DO drive
# the real Qt paint path: QWidget::render into a QImage is the mechanism this
# library exists for, so stubbing it would test nothing. test_main.cpp runs the
# suite under the offscreen platform plugin, so no window is ever created.

add_executable(qar_qt_streaming_tests
  test_main.cpp
  panel_geometry_tests.cpp
  panel_hit_test_tests.cpp
  qt_widget_capture_tests.cpp
  qt_panel_stream_tests.cpp
  frame_brightness_tests.cpp
  scroll_census_tests.cpp
  dialog_fit_tests.cpp
  panel_tombstone_tests.cpp
  panel_close_mode_tests.cpp
  panel_close_tests.cpp
  qt_panel_visibility_tests.cpp
  qt_panel_park_tests.cpp
  offscreen_panel_tests.cpp
  offscreen_popup_guard_tests.cpp
  ar_readability_tests.cpp
  panel_pointer_state_tests.cpp
  qt_panel_input_injector_tests.cpp
  qt_panel_stream_input_tests.cpp
  video_frame_tests.cpp
  latest_video_frame_tests.cpp
  video_panel_widget_tests.cpp
  region_source_tests.cpp
  capture_gate_tests.cpp
  stream_timing_tests.cpp
)

target_link_libraries(qar_qt_streaming_tests PRIVATE
  qar_qt_streaming::qar_qt_streaming
  qar_qt_streaming::test_support
  GTest::gtest
)

target_compile_features(qar_qt_streaming_tests PRIVATE cxx_std_17)

qar_apply_warning_flags(qar_qt_streaming_tests)

# ---------------------------------------------------------------------------
# Stage the Qt runtime next to the test executable.
#
# gtest_discover_tests runs the exe to ENUMERATE tests, and that happens outside
# any per-test ENVIRONMENT property — so a PATH tweak would come too late. Copying
# the DLLs (and the offscreen platform plugin, which Qt looks for in
# <exe_dir>/platforms) makes both discovery and the runs work from a plain shell,
# matching the zero-setup bar the Ogre suite meets via delay loading.
# ---------------------------------------------------------------------------
foreach(_qar_qt_lib Qt5::Core Qt5::Gui Qt5::Widgets)
  if(TARGET ${_qar_qt_lib})
    add_custom_command(TARGET qar_qt_streaming_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${_qar_qt_lib}>" "$<TARGET_FILE_DIR:qar_qt_streaming_tests>"
      VERBATIM
    )
  endif()
endforeach()

if(TARGET Qt5::QOffscreenIntegrationPlugin)
  add_custom_command(TARGET qar_qt_streaming_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:qar_qt_streaming_tests>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt5::QOffscreenIntegrationPlugin>"
      "$<TARGET_FILE_DIR:qar_qt_streaming_tests>/platforms"
    VERBATIM
  )
else()
  message(WARNING
    "Qt5::QOffscreenIntegrationPlugin not found; qar_qt_streaming_tests will need "
    "a usable QT_QPA_PLATFORM on the host to run.")
endif()

# Qt reads and writes JPEG through a plugin, which it looks for in <exe_dir>/imageformats.
# Without it the JPEG test has no encoder and is skipped.
if(TARGET Qt5::QJpegPlugin)
  add_custom_command(TARGET qar_qt_streaming_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:qar_qt_streaming_tests>/imageformats"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt5::QJpegPlugin>"
      "$<TARGET_FILE_DIR:qar_qt_streaming_tests>/imageformats"
    VERBATIM
  )
else()
  message(STATUS "Qt5::QJpegPlugin not found; the JPEG test of qar_qt_streaming_tests is skipped.")
endif()

include(GoogleTest)
gtest_discover_tests(qar_qt_streaming_tests DISCOVERY_TIMEOUT 60)
