# Header-only C++17 helpers over qar_streaming.h. The headers are mirrored from
# the Quaternar build; do not edit them here.
add_library(qar-streaming-cpp INTERFACE)

target_include_directories(
  qar-streaming-cpp
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(qar-streaming-cpp INTERFACE qar-streaming-c-headers)
target_compile_features(qar-streaming-cpp INTERFACE cxx_std_17)

# Asio completion tokens: define QAR_STREAMING_CPP_WITH_ASIO (standalone asio)
# or QAR_STREAMING_CPP_WITH_BOOST_ASIO on the consuming target and link asio.
