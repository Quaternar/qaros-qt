# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# ===========================================================================
# examples/qt_panel_example - two Qt widgets, streamed to a QarOS Visualizer as AR GUI panels
# ===========================================================================
# A plain Qt Widgets program that joins the QAROS Hub through the QarOS C++ SDK and streams a window
# with a button and a hidden off-screen panel through qar_qt_streaming and its qaros backend. It
# links only the public targets of this repository and the SDK headers behind the backend; there is
# no ROS, no RViz and no Ogre. check_layering.py scans examples/** to keep it that way. The QAROS
# runtime library is loaded at run time: the program expands the function tables of the C header
# (once, in main.cpp) and names the same macros as the backend.

add_executable(qar_qt_panel_example
  main.cpp
  local_app_onboarding.cpp
)

target_compile_features(qar_qt_panel_example PRIVATE cxx_std_17)

# The macros of the C header must be the backend's (the program includes sdk_config_check.hpp, so
# a difference is a link error).
target_compile_definitions(qar_qt_panel_example PRIVATE ${QAR_QT_STREAMING_SDK_DEFINES})

# [qar-snippet:link]
target_link_libraries(qar_qt_panel_example PRIVATE
  qar_qt_streaming::qar_qt_streaming
  qar_qt_streaming::qaros
  Qt${QAR_QT_MAJOR}::Widgets
)
# [qar-snippet-end:link]

if(MSVC)
  target_compile_options(qar_qt_panel_example PRIVATE /W4 /permissive-)
else()
  target_compile_options(qar_qt_panel_example PRIVATE -Wall -Wextra)
endif()
