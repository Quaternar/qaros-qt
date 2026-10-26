# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Consumer project of the installed qar_qt_streaming package: configured on its own against
# CMAKE_PREFIX_PATH, never as part of the repository build. It is the install smoke test.
cmake_minimum_required(VERSION 3.22)
project(qar_qt_streaming_consumer LANGUAGES CXX)

# [qar-snippet:find]
find_package(qar_qt_streaming 0.1.0 CONFIG REQUIRED)
# [qar-snippet-end:find]

set(consumer_targets qar_qt_streaming::qar_qt_streaming)
list(JOIN consumer_targets " " consumer_target_text)

add_executable(qar_qt_streaming_consumer main.cpp)
target_compile_features(qar_qt_streaming_consumer PRIVATE cxx_std_17)
target_link_libraries(qar_qt_streaming_consumer PRIVATE ${consumer_targets})
target_compile_definitions(qar_qt_streaming_consumer PRIVATE
  "CONSUMER_PACKAGE_VERSION=\"${qar_qt_streaming_VERSION}\""
  "CONSUMER_TARGETS=\"${consumer_target_text}\"")
