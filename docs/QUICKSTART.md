# Quick start

## Steps

These ten steps take a Windows 11 machine that has the toolchain of [Installation](INSTALL.md#toolchain) installed to a checked connection to QarOS. Every command is explained, with its output, in the document that the step names. Run the steps in one PowerShell 7 window, in this order.

1. Create the folders and set the seven variables that all commands use ([Directory layout and environment variables](INSTALL.md#directory-layout-and-environment-variables)).

```powershell
$layout = [ordered]@{
    QAR_ROOT          = 'C:\qar'
    QAR_DEPS_ROOT     = 'C:\qar\deps'
    ROS2_INSTALL_ROOT = 'C:\qar\deps\ros2_humble\ros2-windows'
    QAR_QT_ROOT       = 'C:\qar\deps\Qt\5.15.2\msvc2019_64'
    QAR_VENV          = 'C:\qar\venv'
    QAR_PREFIX        = 'C:\qar\install'
    QAR_SDK_ROOT      = 'C:\Program Files\Quaternar\QAROS'
}
foreach ($name in $layout.Keys) {
    [Environment]::SetEnvironmentVariable($name, $layout[$name], 'User')
    Set-Item -Path "Env:$name" -Value $layout[$name]
}
New-Item -ItemType Directory -Force -Path $layout.QAR_ROOT, $layout.QAR_DEPS_ROOT, $layout.QAR_PREFIX | Out-Null
```

> Success: the block prints nothing and ends without an error. Afterwards the seven variables are set for your Windows user and in the current window, and these folders exist: `C:\qar`, `C:\qar\deps` and `C:\qar\install`. The folder of QAROS is made by its setup program, not by this block.
>
> This command changes your machine, so no output is shown here.

2. Install QAROS with the setup program that you received from QuaternAR, then check the installation. The command prints the version, the install folder and the hash of the C header ([The QarOS SDK](INSTALL.md#the-qaros-sdk)).

```powershell
$qaros = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Quaternar\QAROS'
'Version        {0}' -f $qaros.Version
'InstallFolder  {0}' -f $qaros.InstallFolder
foreach ($file in 'bin\qar-streaming-c.dll', 'bin\qar-runtime-launcher.exe', 'bin\qar-streaming-viz.exe', 'include\qar_streaming.h') {
    '{0,-30} {1}' -f $file, (Test-Path -LiteralPath (Join-Path $qaros.InstallFolder $file))
}
(Get-FileHash -LiteralPath (Join-Path $qaros.InstallFolder 'include\qar_streaming.h') -Algorithm SHA256).Hash
```
```text
Version        0.9.0-dev.3008+65dc9aeb
InstallFolder  C:\Program Files\Quaternar\QAROS
bin\qar-streaming-c.dll        True
bin\qar-runtime-launcher.exe   True
bin\qar-streaming-viz.exe      True
include\qar_streaming.h        True
55E6E3F48D940CA29331F0528FE475C1B7230FB19A59C31D5525C6E90B097F6C
```

3. Clone this repository and go to the folder of the clone; the next steps run there until a step names another folder ([Python environment](INSTALL.md#python-environment)).

```powershell
git clone https://github.com/Quaternar/qaros-qt "$env:QAR_ROOT\src\qar_qt_streaming"
```
```text
Cloning into 'C:\qar\src\qar_qt_streaming'...
done.
```

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

4. Create the Python environment, which holds the installer of Qt, and fill it from the pinned list of the repository.

```powershell
& "$env:ProgramFiles\Python310\python.exe" -m venv $env:QAR_VENV
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m pip install --disable-pip-version-check -r requirements.txt
```
```text
...
Successfully installed PyYAML-6.0.3 Pygments-2.20.0 aqtinstall-3.3.0 backports.zstd-1.6.0 beautifulsoup4-4.15.0 brotli-1.2.0 bs4-0.0.2 catkin-pkg-1.1.0 certifi-2026.6.17 charset-normalizer-3.4.9 colcon-cmake-0.2.30 colcon-common-extensions-0.3.0 colcon-core-0.21.0 colcon-defaults-0.2.9 colcon-devtools-0.3.0 colcon-library-path-0.2.1 colcon-metadata-0.2.5 colcon-notification-0.3.1 colcon-output-0.2.14 colcon-package-information-0.4.1 colcon-package-selection-0.2.10 colcon-parallel-executor-0.4.0 colcon-pkg-config-0.1.0 colcon-powershell-0.5.0 colcon-python-setup-py-0.2.9 colcon-recursive-crawl-0.2.3 colcon-ros-0.5.0 colcon-test-result-0.3.8 colorama-0.4.6 coloredlogs-15.0.1 coverage-7.15.0 defusedxml-0.7.1 distlib-0.4.3 docutils-0.23 empy-4.2.1 exceptiongroup-1.3.1 humanfriendly-10.0 humanize-4.16.0 idna-3.18 inflate64-1.0.4 iniconfig-2.3.0 lark-1.3.1 multivolumefile-0.2.3 netifaces-0.11.0 numpy-2.2.6 opencv-python-5.0.0.93 packaging-26.2 patch-ng-1.19.1 pluggy-1.6.0 psutil-7.2.2 py7zr-1.1.3 pybcj-1.0.7 pycryptodomex-3.23.0 pyparsing-3.3.2 pyppmd-1.3.1 pyreadline3-3.5.6 pytest-9.1.1 pytest-cov-7.1.0 pytest-repeat-0.9.4 pytest-rerunfailures-16.4 python-dateutil-2.9.0.post0 pywin32-312 requests-2.34.2 semantic-version-2.10.0 six-1.17.0 soupsieve-2.8.4 texttable-1.7.0 tomli-2.4.1 typing_extensions-4.16.0 urllib3-2.7.0 vcstool-0.3.0
```

5. Download and unpack the ROS 2 Humble distribution, which provides the Ogre that the upstream repository links, and install Qt ([ROS 2 Humble for Windows](INSTALL.md#ros-2-humble-for-windows), [Qt 5.15.2](INSTALL.md#qt-5152)). This repository needs none of the repairs that the reminder at the end of the output mentions: the repair scripts in the folder `scripts` serve qar_rviz_plugin.

```powershell
.\scripts\fetch_dependencies.ps1
```
```text
=== ROS 2 Humble Windows binary distro ===
Downloading https://github.com/ros2/ros2/releases/download/release-humble-20260220/ros2-humble-20260220-windows-release-amd64.zip
Downloaded and verified: C:\qar\deps\downloads\ros2-humble-20260220-windows-release-amd64.zip
Extracting to C:\qar\deps\ros2_humble

=== ROS 2 source manifest repo (reference only) ===
Downloading https://github.com/ros2/ros2/archive/refs/tags/release-humble-20260220.zip
Downloaded and verified: C:\qar\deps\downloads\ros2-release-humble-20260220-src.zip
Extracting to C:\qar\deps\ros2_src_release-humble-20260220

Done. Binary distro: C:\qar\deps\ros2_humble\ros2-windows
Reminder: the binary distro has packaging gaps - run the repair scripts next:
fix_missing_spdlog_dll.ps1, fix_missing_vendor_dlls.ps1, fix_missing_openssl_dll.ps1, fix_qt_conda_imports.ps1.
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 --outputdir "$env:QAR_DEPS_ROOT\Qt"
```
```text
INFO    : aqtinstall(aqt) v3.3.0 on Python 3.10.11 [CPython MSC v.1929 64 bit (AMD64)]
...
INFO    : Patching C:\qar\deps\Qt\5.15.2\msvc2019_64\bin\qmake.exe
...
INFO    : Finished installation
INFO    : Time elapsed: 15.21180160 second
```

6. Clone the upstream repository `qar_ogre_adapter`, go to the folder of its clone and configure it ([Upstream repositories](INSTALL.md#upstream-repositories)).

```powershell
git clone https://github.com/Quaternar/qaros-ogre "$env:QAR_ROOT\src\qar_ogre_adapter"
```
```text
Cloning into 'C:\qar\src\qar_ogre_adapter'...
done.
```

```powershell
Set-Location "$env:QAR_ROOT\src\qar_ogre_adapter"
```

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_ROS2_INSTALL_ROOT="C:\qar\deps\ros2_humble\ros2-windows"
  QAR_SDK_ROOT="C:\Program Files\Quaternar\QAROS"

...
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- ROS 2 distribution: C:/qar/deps/ros2_humble/ros2-windows
-- Found rviz_ogre_vendor: 11.2.25 (C:/qar/deps/ros2_humble/ros2-windows/share/rviz_ogre_vendor/cmake)
...
-- qar_qar_adapter: QarOS SDK backend, DYNAMIC loading of C:\Program Files\Quaternar\QAROS/bin/qar-streaming-c.dll (no import lib linked)
...
-- Configuring done (10.4s)
-- Generating done (0.2s)
-- Build files have been written to: C:/qar/src/qar_ogre_adapter/build/vs2019-x64
```

7. Build the upstream repository and install it into `QAR_PREFIX`.

```powershell
cmake --build --preset vs2019-x64
```
```text
  qar_ar_core.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_ar_core\RelWithDebInfo\qar_ar_core.lib
...
  qar_qar_adapter_session.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_qar_adapter\RelWithDebInfo\qar_qar_adapter_session.lib
...
  qar_ogre_streaming.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_ogre_streaming\RelWithDebInfo\qar_ogre_streaming.lib
...
  qar_qar_adapter_render.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_qar_adapter\RelWithDebInfo\qar_qar_adapter_render.lib
...
  qar_ogre_standalone_streaming_sample.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\examples\ogre_standalone_streaming_sample\RelWithDebInfo\qar_ogre_standalone_streaming_sample.exe
...
  qar_ogre_live_stream_example.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\examples\qaros_live_stream\RelWithDebInfo\qar_ogre_live_stream_example.exe
```

```powershell
cmake --install build\vs2019-x64 --config RelWithDebInfo --prefix $env:QAR_PREFIX
```
```text
-- Installing: C:\qar\install/lib/qar_ar_core.lib
-- Installing: C:\qar\install/lib/qar_ogre_streaming.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_capi.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_session.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_render.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_panels.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_url.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_gestures.lib
-- Installing: C:\qar\install/include
...
-- Installing: C:\qar\install/include/qar_streaming.h
-- Installing: C:\qar\install/share/qar_ogre_adapter/LICENSE.qar_streaming_header
-- Installing: C:\qar\install/share/qar_ogre_adapter/LICENSE
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterTargets.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterTargets-relwithdebinfo.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterConfig.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterConfigVersion.cmake
```

8. Go back to the folder of the clone of this repository, then configure and build this repository, which finds the upstream package in `QAR_PREFIX` ([Build and install qar_qt_streaming](INSTALL.md#build-and-install-qar_qt_streaming)).

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_PREFIX="C:\qar\install"
  QAR_QT_ROOT="C:\qar\deps\Qt\5.15.2\msvc2019_64"

...
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- ROS 2 distribution: C:/qar/deps/ros2_humble/ros2-windows
...
-- qar_ogre_adapter 0.1.0: C:/qar/install/share/qar_ogre_adapter/cmake
...
-- Configuring done (10.9s)
-- Generating done (0.2s)
-- Build files have been written to: C:/qar/src/qar_qt_streaming/build/vs2019-x64
```

```powershell
cmake --build --preset vs2019-x64
```
```text
  qar_qt_streaming.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\RelWithDebInfo\qar_qt_streaming.lib
...
  qar_qt_panel_example.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe
...
  qar_qt_streaming_doc_snippets.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\docs\snippets\RelWithDebInfo\qar_qt_streaming_doc_snippets.exe
...
  qar_qt_streaming_tests.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\test\RelWithDebInfo\qar_qt_streaming_tests.exe
```

9. Run the tests ([Testing](TESTING.md)).

```powershell
ctest --preset vs2019-x64
```
```text
Test project C:/qar/src/qar_qt_streaming/build/vs2019-x64
        Start   1: check_layering_qar_qt_streaming
  1/681 Test   #1: check_layering_qar_qt_streaming ................................................................   Passed    0.33 sec
        Start   2: check_layering_examples
  2/681 Test   #2: check_layering_examples ........................................................................   Passed    0.06 sec
        Start   3: check_layering_selftest
  3/681 Test   #3: check_layering_selftest ........................................................................   Passed    0.07 sec
        Start   4: AlignUp.RoundsUpToTheNextMultiple
...
100% tests passed, 0 tests failed out of 681

Total Test time (real) =  65.41 sec
```

10. Check the connection to QAROS. The example joins the QAROS Hub of this PC as a source app: the Hub asks you once to allow the program, so click Allow in the QAROS window. The example prints the runtime, the mode, its peer and the targets, then exits; it opens no window ([Check the connection to QarOS without a window](GETTING_STARTED.md#check-the-connection-to-qaros-without-a-window)). The command starts the Hub when it is not running, so it is not run for this document and its output is not shown.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
& .\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe --local --connect-only
```

[Getting started](GETTING_STARTED.md) goes on from here: it shows how AR input and closing work and lists what to check when you put the library into your own Qt application. The live example that streams a window and a hidden panel to a Visualizer is [Stream one widget into AR](GETTING_STARTED.md#stream-one-widget-into-ar); it needs the installed QAROS, which holds the Hub and the Visualizer, and a receiver that you add in the Visualizer.
