# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
#
# ===========================================================================
# examples/ - the programs that use the libraries of this repository
# ===========================================================================
# qt_panel_example streams Qt widgets to a QarOS Visualizer as AR GUI panels, so it needs Qt,
# qar_qt_streaming and the panels component of the adapter, and the render component, which holds the
# session bootstrap and brings Ogre from the ROS 2 distribution; it uses no ROS and no RViz. It is
# added only when everything it links is built (the check_layering_examples CTest runs
# scripts/check_layering.py on examples/**).

if(TARGET qar_qt_streaming AND TARGET qar_ogre_adapter::render AND TARGET qar_ogre_adapter::panels)
  add_subdirectory(qt_panel_example)
endif()
