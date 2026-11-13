# Vendored: the QarOS C++ headers (qar-streaming-cpp)

- What the folder holds: `include/qar_streaming_cpp/`, the header-only C++17 wrapper over the QarOS C header
  (`qar_streaming.h`), and `CMakeLists.txt`, the file of the vendor that defines its INTERFACE target. This repository's
  build does not add that `CMakeLists.txt`; it creates the targets `qar-streaming-cpp` and `qar-streaming-c-headers`
  from the folders itself.
- Origin: repository <https://github.com/Quaternar/qaros>, branch `feature/cpp-api`, folder `qar-streaming-cpp/`,
  revision `1914bc2`. 45 files, copied byte for byte (size and git blob id of each file
  match the vendor's file list).
- Licence: the licence is not stated in the source. The folder carries no licence text and no notice file.
- The C header the wrapper compiles against is `../qar/include/qar_streaming.h`, the header of the installed QAROS
  0.9.0-dev.3008+65dc9aeb (SHA-256 `55E6E3F48D940CA29331F0528FE475C1B7230FB19A59C31D5525C6E90B097F6C`), copied without
  a change; `../qar/LICENSE` is the MIT licence of the public QarOS repository <https://github.com/Quaternar/qaros>,
  which publishes that header in `qar-streaming-c/include`. The wrapper in this folder compiles against that header.

## C++17 and the alternative token `not`

In the default C++17 mode of MSVC the wrapper does not compile because of the alternative token `not`
(`detail/Values.inl`, `detail/Library.inl`, `detail/Native.hpp`). Compile with `/permissive-` (this repository's
build does) or with C++20 or newer.

## Updating it

Replace the files with the same folder of a newer commit of the branch, byte for byte, and change the commit named above.
After a change, build all three layers of `qar_qt_streaming` with every supported Qt version (see
`docs/SDK_COMPATIBILITY.md`).
