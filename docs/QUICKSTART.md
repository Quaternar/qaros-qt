# Quick start

## Steps

These eight steps take a Windows 11 machine that has the toolchain of [Installation](INSTALL.md#toolchain) installed to a checked connection to QarOS. Every command is explained, with its output, in the document that the step names. Run the steps in one PowerShell 7 window, in this order.

1. Create the folders and set the six variables that all commands use ([Directory layout and environment variables](INSTALL.md#directory-layout-and-environment-variables)).

```powershell
$layout = [ordered]@{
    QAR_ROOT      = 'C:\qar'
    QAR_DEPS_ROOT = 'C:\qar\deps'
    QAR_QT_ROOT   = 'C:\qar\deps\Qt\5.15.2\msvc2019_64'
    QAR_VENV      = 'C:\qar\venv'
    QAR_PREFIX    = 'C:\qar\install'
    QAR_SDK_ROOT  = 'C:\Program Files\Quaternar\QAROS'
}
foreach ($name in $layout.Keys) {
    [Environment]::SetEnvironmentVariable($name, $layout[$name], 'User')
    Set-Item -Path "Env:$name" -Value $layout[$name]
}
New-Item -ItemType Directory -Force -Path $layout.QAR_ROOT, $layout.QAR_DEPS_ROOT, $layout.QAR_PREFIX | Out-Null
```

> Success: the block prints nothing and ends without an error. Afterwards the six variables are set for your Windows user and in the current window, and these folders exist: `C:\qar`, `C:\qar\deps` and `C:\qar\install`. The folder of QAROS is made by its setup program, not by this block.
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

3. Clone this repository and go to the folder of the clone; the next steps run there until a step names another folder ([Clone this repository](INSTALL.md#clone-this-repository)).

```powershell
git clone --recurse-submodules https://github.com/Quaternar/qaros-qt "$env:QAR_ROOT\src\qar_qt_streaming"
```
```text
Cloning into 'C:\qar\src\qar_qt_streaming'...
done.
```

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

4. Create the Python environment, which holds the installer of Qt, and fill it from the pinned list of the repository. Skip this step and the installation of Qt in the next one when you install Qt in another way.

```powershell
& "$env:ProgramFiles\Python310\python.exe" -m venv $env:QAR_VENV
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m pip install --disable-pip-version-check -r requirements.txt
```
```text
Successfully installed Pygments-2.20.0 aqtinstall-3.3.0 backports.zstd-1.6.0 beautifulsoup4-4.15.0 brotli-1.2.0 bs4-0.0.2 certifi-2026.6.17 charset-normalizer-3.4.9 colorama-0.4.6 coverage-7.15.0 defusedxml-0.7.1 exceptiongroup-1.3.1 humanize-4.16.0 idna-3.18 inflate64-1.0.4 iniconfig-2.3.0 multivolumefile-0.2.3 packaging-26.2 patch-ng-1.19.1 pluggy-1.6.0 psutil-7.2.2 py7zr-1.1.3 pybcj-1.0.7 pycryptodomex-3.23.0 pyppmd-1.3.1 pytest-9.1.1 pytest-cov-7.1.0 pytest-repeat-0.9.4 pytest-rerunfailures-16.4 requests-2.34.2 semantic-version-2.10.0 soupsieve-2.8.4 texttable-1.7.0 tomli-2.4.1 typing_extensions-4.16.0 urllib3-2.7.0
```

5. Install Qt 5.15.2 and check it ([Qt 5.15.2](INSTALL.md#qt-5152)). A Qt 6 kit works as well: set `QAR_QT_ROOT` to its folder and use the preset `vs2022-x64` in the steps below.

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

```powershell
& "$env:QAR_QT_ROOT\bin\qmake.exe" -v
foreach ($module in 'Core', 'Gui', 'Widgets') {
    '{0,-11} {1}' -f $module, (Test-Path -LiteralPath (Join-Path $env:QAR_QT_ROOT "lib\cmake\Qt5$module"))
}
```
```text
QMake version 3.1
Using Qt version 5.15.2 in C:/qar/deps/Qt/5.15.2/msvc2019_64/lib
Core        True
Gui         True
Widgets     True
```

6. Go back to the folder of the clone, then configure and build this repository ([Build and install qar_qt_streaming](INSTALL.md#build-and-install-qar_qt_streaming)).

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_QT_ROOT="C:\qar\deps\Qt\5.15.2\msvc2019_64"
...
-- Qt 5.15.2: C:/qar/deps/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5
-- QarOS SDK headers: QarOS repository C:/qar/src/qar_qt_streaming/external/qaros
...
-- Configuring done (12.5s)
-- Generating done (0.3s)
-- Build files have been written to: C:/qar/src/qar_qt_streaming/build/vs2019-x64
```

```powershell
cmake --build --preset vs2019-x64
```
```text
  qar_qt_streaming_seam.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\seam\RelWithDebInfo\qar_qt_streaming_seam.lib
...
  qar_qt_streaming_qaros.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\qaros\RelWithDebInfo\qar_qt_streaming_qaros.lib
...
  qar_qt_streaming_seam_tests.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\seam\test\RelWithDebInfo\qar_qt_streaming_seam_tests.exe
  qar_qt_streaming_qaros_tests.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\qaros\test\RelWithDebInfo\qar_qt_streaming_qaros_tests.exe
...
  qar_qt_streaming.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\RelWithDebInfo\qar_qt_streaming.lib
...
  qar_qt_panel_example.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe
...
  qar_qt_streaming_doc_snippets.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\docs\snippets\RelWithDebInfo\qar_qt_streaming_doc_snippets.exe
...
  qar_qt_streaming_tests.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\test\RelWithDebInfo\qar_qt_streaming_tests.exe
```

7. Run the tests ([Testing](TESTING.md)), then install the libraries into `QAR_PREFIX`.

```powershell
ctest --preset vs2019-x64
```
```text
Test project C:/qar/src/qar_qt_streaming/build/vs2019-x64
        Start   1: check_layering_qar_qt_streaming
  1/832 Test   #1: check_layering_qar_qt_streaming ................................................................   Passed    0.33 sec
        Start   2: check_layering_examples
  2/832 Test   #2: check_layering_examples ........................................................................   Passed    0.07 sec
        Start   3: check_layering_selftest
  3/832 Test   #3: check_layering_selftest ........................................................................   Passed    0.10 sec
        Start   4: CommonName.SanitizeKeepsLowerCaseLettersDigitsAndDashesAndMapsTheRestToDash
...
100% tests passed, 0 tests failed out of 832

Total Test time (real) =  73.75 sec

The following tests did not run:
	626 - OffscreenPopupGuard.TheWindows11StyleComboListReachesThePanelImage (Skipped)
	627 - OffscreenPopupGuard.TheWindows11StyleListOfAVisiblePanelReachesThePanelImage (Skipped)
```

```powershell
cmake --install build\vs2019-x64 --config RelWithDebInfo --prefix $env:QAR_PREFIX
```
```text
-- Installing: C:\qar\install/lib/qar_qt_streaming_seam.lib
-- Installing: C:\qar\install/lib/qar_qt_streaming.lib
-- Installing: C:\qar\install/lib/qar_qt_streaming_qaros.lib
-- Installing: C:\qar\install/include
-- Installing: C:\qar\install/include/qar_qt_streaming
-- Installing: C:\qar\install/include/qar_qt_streaming/seam
...
-- Installing: C:\qar\install/share/qar_qt_streaming/sdk/LICENSE
-- Installing: C:\qar\install/share/qar_qt_streaming/LICENSE
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingTargets.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingTargets-relwithdebinfo.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingConfig.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingConfigVersion.cmake
```

8. Check the connection to QAROS. The example joins the QAROS Hub of this PC as a source app: the Hub asks you once to allow the program, so click Allow in the QAROS window. The example prints a description of the session and its id, then exits; it opens no window. The command starts the Hub of the installed QAROS when it is not running, so it is not run for this document and no output is shown ([Getting started](GETTING_STARTED.md#check-the-connection-to-qaros-without-a-window)).

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
& .\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe --local --connect-only
```

[Getting started](GETTING_STARTED.md) goes on from here: it shows how AR input and closing work and lists what to check when you put the library into your own Qt application. The live example that streams a window and a hidden panel to a Visualizer is described there too. [Installation](INSTALL.md#install-smoke-test) shows the consumer project that checks the installed package.
