# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Unit tests for qar_qt_streaming.
#
# GoogleTest comes from the top-level qar_require_googletest(); this directory just uses
# its targets.
#
# These tests DO drive
# the real Qt paint path: QWidget::render into a QImage is the mechanism this
# library exists for, so stubbing it would test nothing. test_main.cpp runs the
# suite under the offscreen platform plugin, so no window is ever created.
#
# The tests build against Qt 5 or Qt 6: the Qt major version is QAR_QT_MAJOR (set by qar_use_qt
# in the top-level directory; Qt 5 when the caller sets none).

if(NOT QAR_QT_MAJOR)
  set(QAR_QT_MAJOR 5)
endif()

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
  panel_swap_tests.cpp
  panel_async_tests.cpp
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
  fractional_dpr_tests.cpp
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
# <exe_dir>/platforms) makes both discovery and the runs work from a plain shell
# without any setup.
# ---------------------------------------------------------------------------
foreach(_qar_qt_lib Qt${QAR_QT_MAJOR}::Core Qt${QAR_QT_MAJOR}::Gui Qt${QAR_QT_MAJOR}::Widgets)
  if(TARGET ${_qar_qt_lib})
    add_custom_command(TARGET qar_qt_streaming_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${_qar_qt_lib}>" "$<TARGET_FILE_DIR:qar_qt_streaming_tests>"
      VERBATIM
    )
  endif()
endforeach()

if(TARGET Qt${QAR_QT_MAJOR}::QOffscreenIntegrationPlugin)
  add_custom_command(TARGET qar_qt_streaming_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:qar_qt_streaming_tests>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt${QAR_QT_MAJOR}::QOffscreenIntegrationPlugin>"
      "$<TARGET_FILE_DIR:qar_qt_streaming_tests>/platforms"
    VERBATIM
  )
else()
  message(WARNING
    "Qt${QAR_QT_MAJOR}::QOffscreenIntegrationPlugin not found; qar_qt_streaming_tests will need "
    "a usable QT_QPA_PLATFORM on the host to run.")
endif()

# Qt reads and writes JPEG through a plugin, which it looks for in <exe_dir>/imageformats.
# Without it the JPEG test has no encoder and is skipped.
if(TARGET Qt${QAR_QT_MAJOR}::QJpegPlugin)
  add_custom_command(TARGET qar_qt_streaming_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:qar_qt_streaming_tests>/imageformats"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt${QAR_QT_MAJOR}::QJpegPlugin>"
      "$<TARGET_FILE_DIR:qar_qt_streaming_tests>/imageformats"
    VERBATIM
  )
else()
  message(STATUS
    "Qt${QAR_QT_MAJOR}::QJpegPlugin not found; the JPEG test of qar_qt_streaming_tests is skipped.")
endif()

# From Qt 6.7 the windows11 style is a plugin, which Qt looks for in <exe_dir>/styles. Without it
# the two tests that run with that style find no such style and are skipped.
if(TARGET Qt${QAR_QT_MAJOR}::QModernWindowsStylePlugin)
  add_custom_command(TARGET qar_qt_streaming_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:qar_qt_streaming_tests>/styles"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt${QAR_QT_MAJOR}::QModernWindowsStylePlugin>"
      "$<TARGET_FILE_DIR:qar_qt_streaming_tests>/styles"
    VERBATIM
  )
endif()

include(GoogleTest)
gtest_discover_tests(qar_qt_streaming_tests DISCOVERY_TIMEOUT 60)

# The same executable at a fractional device pixel ratio: QT_SCALE_FACTOR sets the ratio of the
# offscreen screen, and QAR_QT_TESTS_FRACTIONAL_DPR makes test_main.cpp let the fraction through
# (rounding policy PassThrough; in Qt 5 also AA_EnableHighDpiScaling). QT_FONT_DPI=96 keeps the
# logical DPI of the offscreen screen from adding a factor of its own: without it Qt 5.15 reports
# a ratio of 1.3021 for a scale factor of 1.25.
foreach(_qar_dpr 1.25 1.5)
  add_test(
    NAME qar_qt_streaming_fractional_dpr_${_qar_dpr}
    COMMAND qar_qt_streaming_tests --gtest_filter=FractionalDpr*
  )
  set_tests_properties(qar_qt_streaming_fractional_dpr_${_qar_dpr} PROPERTIES
    ENVIRONMENT "QT_SCALE_FACTOR=${_qar_dpr};QT_FONT_DPI=96;QAR_QT_TESTS_FRACTIONAL_DPR=1"
  )
endforeach()
