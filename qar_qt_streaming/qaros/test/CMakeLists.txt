# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Unit tests of the qaros backend against a fake of the QarOS function tables: no library is
# loaded, no Hub is started, and Qt is not used. GoogleTest comes from the top-level
# qar_require_googletest().
#
# The tests need the C header's dynamic loading: only then do the calls go through function tables
# that the fake can fill.

if(NOT "QAR_ENABLE_DYNAMIC_LOADING" IN_LIST QAR_QT_STREAMING_SDK_DEFINES)
  message(STATUS
    "qaros tests skipped: QAR_QT_STREAMING_SDK_DEFINES has no QAR_ENABLE_DYNAMIC_LOADING")
  return()
endif()

add_executable(qar_qt_streaming_qaros_tests
  backend_tests.cpp
  fake_c_table.cpp
)

target_link_libraries(qar_qt_streaming_qaros_tests PRIVATE
  qar_qt_streaming::qaros
  GTest::gtest_main
)

# The tests include the backend's private header (the executor and the message queue) by its path.
target_include_directories(qar_qt_streaming_qaros_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# The program names the same macros of the C header as the backend.
target_compile_definitions(qar_qt_streaming_qaros_tests PRIVATE ${QAR_QT_STREAMING_SDK_DEFINES})

target_compile_features(qar_qt_streaming_qaros_tests PRIVATE cxx_std_17)

qar_apply_warning_flags(qar_qt_streaming_qaros_tests)

include(GoogleTest)
gtest_discover_tests(qar_qt_streaming_qaros_tests DISCOVERY_TIMEOUT 60)
