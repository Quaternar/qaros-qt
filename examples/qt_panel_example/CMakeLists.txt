# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# ===========================================================================
# examples/qt_panel_example - two Qt widgets, streamed to a QarOS Visualizer as AR GUI panels
# ===========================================================================
# A plain Qt Widgets program that joins the QAROS Hub through the session component of the adapter
# (QarSessionOwner) and streams a window with a button and a hidden off-screen panel through
# qar_qt_streaming. It links only the public targets of this repository; there is no ROS and no
# RViz. check_layering.py scans examples/** to keep it that way. Nothing links the QAROS SDK: the
# installed QAROS library is loaded at run time. The program draws no 3D scene, so it links neither
# the render component nor Ogre.

add_executable(qar_qt_panel_example main.cpp)

target_compile_features(qar_qt_panel_example PRIVATE cxx_std_17)

# [qar-snippet:link]
target_link_libraries(qar_qt_panel_example PRIVATE
  qar_qt_streaming::qar_qt_streaming
  qar_ogre_adapter::session
  qar_ogre_adapter::panels
  Qt5::Widgets
)
# [qar-snippet-end:link]

if(MSVC)
  target_compile_options(qar_qt_panel_example PRIVATE /W4)
else()
  target_compile_options(qar_qt_panel_example PRIVATE -Wall -Wextra)
endif()
