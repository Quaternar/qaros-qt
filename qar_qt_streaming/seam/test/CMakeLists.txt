# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Unit tests of the seam of qar_qt_streaming: standard library and GoogleTest only (no Qt, no
# QApplication). GoogleTest comes from the top-level qar_require_googletest().

add_executable(qar_qt_streaming_seam_tests
  common_name_tests.cpp
  panel_attach_tests.cpp
  panel_gesture_mapper_tests.cpp
  panel_seam_tests.cpp
  panel_state_guard_tests.cpp
  pointer_input_tests.cpp
  timing_log_tests.cpp
  values_tests.cpp
  # The version of the build against the file VERSION.
  version_tests.cpp
)

target_link_libraries(qar_qt_streaming_seam_tests PRIVATE
  qar_qt_streaming::seam
  GTest::gtest_main
)

target_compile_features(qar_qt_streaming_seam_tests PRIVATE cxx_std_17)
target_compile_definitions(qar_qt_streaming_seam_tests PRIVATE
  QAR_TEST_VERSION_FILE="${PROJECT_SOURCE_DIR}/VERSION")

qar_apply_warning_flags(qar_qt_streaming_seam_tests)

include(GoogleTest)
gtest_discover_tests(qar_qt_streaming_seam_tests DISCOVERY_TIMEOUT 30)
