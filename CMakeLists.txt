# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
#
# qar_qt_streaming: Qt widgets as QarOS AR GUI panels - the library qar_qt_streaming and the example
# qt_panel_example. The library needs Qt and the component qar_ar_core of the installed package
# qar_ogre_adapter. The example also links the components render and panels, and render is built against
# the Ogre of the ROS 2 distribution, so only the example needs that distribution.
cmake_minimum_required(VERSION 3.22)
project(qar_qt_streaming VERSION 0.1.0
  DESCRIPTION "Qt widgets as QarOS AR GUI panels"
  LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/QarBuildSupport.cmake")

option(QAR_BUILD_TESTS "Build the unit tests and the layering guard tests" ON)
option(QAR_BUILD_EXAMPLES "Build the examples under examples/" ON)
option(QAR_BUILD_DOC_SNIPPETS "Build the compile-checked documentation snippets" ON)
option(QAR_INSTALL "Generate the install rules and the CMake package" ON)

qar_set_default_build_type()
qar_set_default_install_prefix()

set(_qar_upstream_components qar_ar_core)
if(QAR_BUILD_EXAMPLES)
  qar_use_ros2_distro()
  list(APPEND _qar_upstream_components render panels)
endif()
qar_use_qt5(COMPONENTS Core Gui Widgets)
qar_find_upstream(qar_ogre_adapter VERSION 0.1.0 COMPONENTS ${_qar_upstream_components})

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
  add_subdirectory(qar_qt_streaming/test)
  qar_add_layering_guard_tests(MODULES qar_qt_streaming examples)
endif()

qar_install_enabled(_qar_install)
if(_qar_install)
  install(TARGETS qar_qt_streaming EXPORT qar_qt_streamingTargets
    ARCHIVE DESTINATION lib
    INCLUDES DESTINATION include)
  install(DIRECTORY qar_qt_streaming/include/ DESTINATION include)
  install(FILES LICENSE DESTINATION share/qar_qt_streaming OPTIONAL)
  qar_install_package(qar_qt_streaming EXPORT qar_qt_streamingTargets NAMESPACE qar_qt_streaming::)
endif()
