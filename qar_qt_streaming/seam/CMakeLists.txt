# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# qar_qt_streaming_seam - the part of qar_qt_streaming that every host and every backend shares:
# plain values (poses, sizes, panel states), the interfaces of a panel sender and of a pointer
# input source, the panel naming rule, the attach decision, the panel-state guard, the timing log.
#
# The C++17 standard library only: no Qt, no QarOS header, no middleware, no renderer.
# Enforced by the layering guard (scripts/check_layering.py).

add_library(qar_qt_streaming_seam STATIC
  src/common_name.cpp
  src/panel_attach.cpp
  src/panel_gesture_mapper.cpp
  src/panel_types.cpp
)
add_library(qar_qt_streaming::seam ALIAS qar_qt_streaming_seam)

target_compile_features(qar_qt_streaming_seam PUBLIC cxx_std_17)

# qar_qt_streaming/seam/version.hpp: the version of this build (cmake/QarQtVersion.cmake), generated
# into the build folder and installed with the other headers.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/QarQtVersion.cmake")
set(QAR_QT_STREAMING_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include"
  CACHE INTERNAL "Generated headers of the seam of qar_qt_streaming")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in"
  "${QAR_QT_STREAMING_GENERATED_INCLUDE_DIR}/qar_qt_streaming/seam/version.hpp" @ONLY)

target_include_directories(qar_qt_streaming_seam PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${QAR_QT_STREAMING_GENERATED_INCLUDE_DIR}>"
  "$<INSTALL_INTERFACE:include>"
)

# The timing log runs a writer thread; Windows needs no extra library for std::thread.
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(qar_qt_streaming_seam PUBLIC Threads::Threads)
endif()

# A static library that is linked into a shared library (a plugin, for example)
# must be position-independent on the platforms where that matters (not Windows).
set_target_properties(qar_qt_streaming_seam PROPERTIES POSITION_INDEPENDENT_CODE ON)
# The installed package calls the target qar_qt_streaming::seam, like the alias of the build tree.
set_target_properties(qar_qt_streaming_seam PROPERTIES EXPORT_NAME seam)

qar_apply_warning_flags(qar_qt_streaming_seam)
