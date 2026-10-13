# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
#
# ===========================================================================
# examples/qt_panel_example - two Qt widgets, streamed to a QarOS Visualizer as AR GUI panels
# ===========================================================================
# A plain Qt Widgets program that opens a QarOS session through the render component of the adapter,
# invites a Visualizer and streams a window with a button and a hidden off-screen panel through
# qar_qt_streaming. It links only the public targets of this repository; there is no ROS and no
# RViz. check_layering.py scans examples/** to keep it that way. Nothing links the QarOS SDK: the DLL
# is loaded at run time from --sdk-bin. The render component pulls in Ogre only because the session
# bootstrap lives in it; this program draws no 3D scene and includes no Ogre header.

add_executable(qar_qt_panel_example main.cpp)

target_compile_features(qar_qt_panel_example PRIVATE cxx_std_17)

# [qar-snippet:link]
target_link_libraries(qar_qt_panel_example PRIVATE
  qar_qt_streaming::qar_qt_streaming
  qar_ogre_adapter::render
  qar_ogre_adapter::panels
  Qt5::Widgets
)
# [qar-snippet-end:link]

if(MSVC)
  target_compile_options(qar_qt_panel_example PRIVATE /W4)
else()
  target_compile_options(qar_qt_panel_example PRIVATE -Wall -Wextra)
endif()
