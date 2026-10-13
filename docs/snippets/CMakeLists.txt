# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
#
# Compile-checked documentation snippets of qar_qt_streaming. Every C++ and CMake block of the documentation
# is a region of a file in this folder (or of an example), so the code shown compiles and links.
# The target is built and never run; it registers no test.
file(GLOB _qar_snippet_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(qar_qt_streaming_doc_snippets ${_qar_snippet_sources})
target_compile_features(qar_qt_streaming_doc_snippets PRIVATE cxx_std_17)
# The library links Qt Core, Gui and Widgets and qar_ar_core publicly, so this one target is all the snippets need.
# [qar-snippet:link]
target_link_libraries(qar_qt_streaming_doc_snippets PRIVATE
  qar_qt_streaming::qar_qt_streaming
)
# [qar-snippet-end:link]
qar_apply_warning_flags(qar_qt_streaming_doc_snippets)
