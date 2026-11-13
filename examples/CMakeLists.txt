# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# ===========================================================================
# examples/ - the programs that use the libraries of this repository
# ===========================================================================
# qt_panel_example streams Qt widgets to a QarOS Visualizer as AR GUI panels. It needs Qt, the
# libraries qar_qt_streaming and qaros of this repository and the SDK headers behind the qaros
# backend; it uses no ROS, no RViz and no Ogre. It is added only when the qaros backend is built
# (the check_layering_examples CTest runs scripts/check_layering.py on examples/**).

if(TARGET qar_qt_streaming AND TARGET qar_qt_streaming_qaros)
  add_subdirectory(qt_panel_example)
endif()
