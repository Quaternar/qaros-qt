# Installation

## Overview

This document installs everything that you need to build, test and run qar_qt_streaming on a Windows 11 machine. It goes in this order: the machine and its toolchain; the folders and the environment variables that every command uses; the QarOS SDK; a clone of this repository with the Python environment; Qt; and finally this repository, which you configure, build, test and install, and then check with a small consumer program. Every command runs in PowerShell 7 and uses only the environment variables of the layout, so a different layout needs only different values for the variables.

qar_qt_streaming needs Qt (the version 5.15.2 here, or Qt 6.5 and newer; the modules Core, Gui and Widgets) and a C++17 compiler, and it needs no colcon, no ROS 2, no RViz and no Ogre. It carries the headers of the QarOS C++ SDK that its backend `qaros` is compiled against, so QAROS is not needed to build or test: you need it to connect to QarOS.

When you are done, the libraries `qar_qt_streaming_seam`, `qar_qt_streaming` and `qar_qt_streaming_qaros` and their CMake package are installed below `QAR_PREFIX`, and the example `qar_qt_panel_example` is built in the build folder of the repository. The command below shows how much disk space each folder uses; run it after the last step.

```powershell
foreach ($folder in $env:QAR_DEPS_ROOT, $env:QAR_VENV, "$env:QAR_ROOT\src", $env:QAR_PREFIX, $env:QAR_SDK_ROOT) {
    $bytes = (Get-ChildItem -LiteralPath $folder -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum
    '{0}  {1:N2} GiB' -f $folder, ($bytes / 1GB)
}
```
```text
C:\qar\deps  1.82 GiB
C:\qar\venv  0.24 GiB
C:\qar\src  0.32 GiB
C:\qar\install  0.01 GiB
C:\Program Files\Quaternar\QAROS  0.64 GiB
```

The sizes were measured on the machine of the documented runs, where the dependency folder and the Python environment held more than this repository needs; your folders are smaller.

## Machine prerequisites

The installation was developed and run on Windows 11 Pro, 64-bit. Check the machine before you install anything: the commands below only read the system.

```powershell
$os = Get-CimInstance Win32_OperatingSystem
'{0}, version {1}, 64-bit: {2}' -f $os.Caption, $os.Version, [Environment]::Is64BitOperatingSystem
```
```text
Microsoft Windows 11 Pro, version 10.0.26200, 64-bit: True
```

| Requirement | What this stack needs | Why |
|---|---|---|
| Operating system | Windows 11, 64-bit. Other Windows versions and Linux are not built or tested. | The toolchain and the Qt kit of this documentation are the Windows 64-bit builds. |
| Desktop session | An interactive desktop on the local console of the machine. Remote Desktop and services are not covered. | The Qt example shows its windows on the desktop, the QarOS Visualizer that you open for it is an OpenGL window, and the documented runs were made from the local console. |
| Graphics | A GPU with a current OpenGL driver; the reference machine has an NVIDIA GPU. This repository states no minimum GPU or driver version. | The QarOS Visualizer renders with OpenGL, and QarOS encodes the frames on the GPU. |
| Administrator rights | Only for the installers of the toolchain. The scripts of this stack and the commands of this documentation run in a normal, non-elevated PowerShell. | The scripts write below the folders you pass them. |
| Paths | A short root folder: this documentation uses `C:\qar`. | Windows limits a path to 260 characters unless long paths are enabled, and a deep clone path overflows that limit. |

Check the two Windows settings that matter for the layout. The first value is the long-path policy: `0` means long paths are off, which is the Windows default. This stack does not change it and does not need it, because the layout above is short.

```powershell
(Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled).LongPathsEnabled
```
```text
0
```

The second command lists the graphics adapters Windows sees. On a laptop with an integrated and a discrete adapter, Windows may run an application on the integrated one, while QarOS encodes on the NVIDIA GPU; frames then cross between the adapters.

```powershell
Get-CimInstance Win32_VideoController | ForEach-Object { '{0}, driver {1}' -f $_.Name, $_.DriverVersion }
```
```text
Intel(R) UHD Graphics, driver 31.0.101.5187
NVIDIA GeForce RTX 4080 Laptop GPU, driver 32.0.15.7657
```


Plan the disk space before you start. The layout of this documentation puts the dependencies, the virtual environment, the clones, the install prefix and the SDK on one drive; the [Overview](#overview) has a command that measures them, so run that command after the last step of the chain to see what your machine used.

## Toolchain

The stack builds with five tools. Install them in the order of the table; every tool has an install command and a verify command, and the verify commands are the ones this documentation was run with.

| Tool | Version | Used for |
|---|---|---|
| Visual Studio 2019 Build Tools | MSVC v142 (compiler 19.29.30151.0) with a Windows 10 SDK | compiling and linking every C++ module |
| CMake | 3.22 or newer | configuring and building; the presets of every repository need it |
| Git | any recent version | cloning the repositories |
| PowerShell 7 | 7.x (`pwsh`) | running every command and script of this documentation |
| Python | 3.10.11 (any 3.10.x) | the virtual environment, `aqt` and the layering guard |

All install commands use `winget`, the Windows package manager that ships with Windows 11 as part of the App Installer. They change the machine (they install software system-wide), so run them in an elevated PowerShell 7 and open a new window afterwards, so that the new `PATH` is read.

### Why Visual Studio 2019 and the v142 toolset

C++ classes cross DLL boundaries between your code and Qt, and the compiled layout of those classes and the C++ runtime they use must match the toolset that built the Qt binaries. The Qt 5.15.2 kit `msvc2019_64` of this documentation is compiled with the Visual Studio 2019 toolset (v142), and the preset `vs2019-x64` selects the generator Visual Studio 16 2019 to build with the same toolset. The preset `vs2022-x64` selects the generator Visual Studio 17 2022 for a Qt 6 kit. The combinations that were built and tested are Qt 5.15.2 and Qt 6.5.3 with the toolset v142 of Visual Studio 2019, and Qt 6.8.3 and Qt 6.10.3 with the toolset v143 of Visual Studio 2022 ([Testing](TESTING.md#tested-configurations)). Always build `RelWithDebInfo` or `Release`: the prebuilt Qt binaries are release builds, and a debug C runtime in the same process crashes on load.


### Visual Studio 2019 Build Tools

Install the workload `Microsoft.VisualStudio.Workload.VCTools` (the C++ build tools) together with the components `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` (the MSVC v142 compiler and linker) and `Microsoft.VisualStudio.Component.Windows10SDK.19041` (the Windows 10 SDK 10.0.19041). The full Visual Studio 2019 IDE with the same workload works as well.

```powershell
winget install --id Microsoft.VisualStudio.2019.BuildTools --source winget --accept-package-agreements --accept-source-agreements --override "--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows10SDK.19041"
```

> Success: `winget` finishes with exit code 0 once the installer has added the C++ build tools workload, the MSVC compiler and linker and the Windows 10 SDK. The options of the command make the installer wait until it is done, work without a window and not restart the machine. The verify command below shows the installation.
>
> This command changes your machine, so no output is shown here.

The verify command below finds a Visual Studio installation with `vswhere`, a tool that the Visual Studio installer puts in a fixed folder, then reads the toolset folder and the compiler version from that installation. The toolset folder starts with `14.2` for v142.

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -version '[16.0,17.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -latest
$vs
$toolset = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Name | Select-Object -Last 1
$toolset
(Get-Item (Join-Path $vs "VC\Tools\MSVC\$toolset\bin\Hostx64\x64\cl.exe")).VersionInfo.FileVersion
```
```text
C:\Program Files (x86)\Microsoft Visual Studio\2019\Community
14.29.30133
19.29.30151.0 built by: cloudtest
```

The first line is the installation folder (its last part names the edition), the second the toolset folder, the third the compiler version followed by a build annotation. The reference build used the compiler 19.29.30151.0; a later 16.11 servicing release of the same toolset is the same toolset family. If the first command prints nothing, the C++ x64 tools are not installed, and the scripts stop with an error that points here.

### CMake

The presets require CMake 3.22 or newer.

The runs that this documentation shows used CMake 3.29.5 as Visual Studio 2022 version 17.11 bundles it, which prints `cmake version 3.29.5-msvc4`; the compiler of those runs was still the toolset v142 of Visual Studio 2019. Every output of this documentation that shows a version of CMake comes from that build. No run used the build of Kitware that the install command below fetches, and none used a CMake 4 release, so nobody has checked whether a CMake 4 release configures this stack. The command therefore installs the Kitware build of the version number of the recorded runs, 3.29.5, which `winget` lists. A Kitware build and a Microsoft build of one version number are two builds, and this documentation does not claim that they behave alike.

```powershell
winget install --id Kitware.CMake --version 3.29.5 --source winget --accept-package-agreements --accept-source-agreements
```

> Success: `winget` finishes with exit code 0 and CMake 3.29.5 is installed. In a new window the verify command below prints the version of the CMake that comes first on `PATH`.
>
> This command changes your machine, so no output is shown here.

```powershell
cmake --version
```
```text
cmake version 3.29.5-msvc4
...
```

### Git

Git clones the repositories and is the only tool of this chain that talks to a Git host. The CMake configure step of every repository downloads GoogleTest as an archive and does not need Git for it.

```powershell
winget install --id Git.Git --source winget --accept-package-agreements --accept-source-agreements
```

> Success: `winget` finishes with exit code 0 and Git is installed. In a new window the verify command below prints the version of Git.
>
> This command changes your machine, so no output is shown here.

```powershell
git --version
```
```text
git version 2.39.2.windows.1
```

### PowerShell 7

Every command of this documentation was run in PowerShell 7 (`pwsh`), started without a profile, and that is the shell the commands are written for. Windows PowerShell 5.1 (`powershell.exe`) is not used by the documented commands.

```powershell
winget install --id Microsoft.PowerShell --source winget --accept-package-agreements --accept-source-agreements
```

> Success: `winget` finishes with exit code 0 and PowerShell 7 is installed. In a new PowerShell 7 window (`pwsh`) the verify command below prints a version that starts with 7.
>
> This command changes your machine, so no output is shown here.

```powershell
$PSVersionTable.PSVersion.ToString()
```
```text
7.4.4
```

### Python 3.10

The pinned list of the virtual environment, which installs the Qt installer `aqt`, is written for Python 3.10. Install the machine-wide interpreter; it lands in the folder Python310 below Program Files.

```powershell
winget install Python.Python.3.10 --scope machine --source winget --accept-package-agreements --accept-source-agreements
```

> Success: `winget` finishes with exit code 0 and the machine-wide Python 3.10 is installed in the folder Python310 below Program Files. The verify command below calls that interpreter by its full path and prints its version, which starts with 3.10.
>
> This command changes your machine, so no output is shown here.

The verify command calls the interpreter by its full path, because other Python versions may come first on `PATH`. The next section creates the virtual environment from this interpreter.

```powershell
& "$env:ProgramFiles\Python310\python.exe" --version
```
```text
Python 3.10.11
```


### Execution policy and downloaded files

The scripts of this stack are not signed. If the execution policy of your machine blocks local scripts, start the script through `pwsh` and set its `-ExecutionPolicy` option to `Bypass`, which applies to that one process only. Windows marks files that were downloaded from the internet (for example a repository or the SDK saved as a zip file by a browser); the policy `RemoteSigned` refuses an unsigned script that carries the mark, and `Unblock-File` removes it.

The command prints the policy stored for your user and for the machine; `Undefined` means that scope sets nothing.

```powershell
foreach ($scope in 'CurrentUser', 'LocalMachine') { '{0,-13} {1}' -f $scope, (Get-ExecutionPolicy -Scope $scope) }
```
```text
CurrentUser   Undefined
LocalMachine  RemoteSigned
```

## Directory layout and environment variables

This documentation uses one short folder layout and refers to every folder through an environment variable. A different layout needs only different values for the variables, never different commands.

```text
C:\qar                                  QAR_ROOT, the root of the layout
C:\qar\deps                             QAR_DEPS_ROOT: dependencies (the Qt kit)
C:\qar\deps\Qt\5.15.2\msvc2019_64       QAR_QT_ROOT: Qt 5.15.2
C:\qar\venv                             QAR_VENV: the Python virtual environment
C:\qar\src\<repository>                 the clones of the repositories
C:\qar\install                          QAR_PREFIX: the install prefix of the repositories
C:\Program Files\Quaternar\QAROS   QAR_SDK_ROOT: the install folder of QAROS (made by its setup program, outside the layout root)
```

| Variable | Meaning | Value in this documentation |
|---|---|---|
| `QAR_ROOT` | Root of the layout. Documented commands use it to name the clone and working folders; no script and no CMake file reads it. | `C:\qar` |
| `QAR_DEPS_ROOT` | Dependency root: the folder of the dependencies, such as the Qt kit. | `C:\qar\deps` |
| `QAR_QT_ROOT` | The Qt kit that the build uses, the folder that holds `bin` and `lib\cmake`: the Qt 5.15.2 kit `msvc2019_64` here, or a Qt 6 kit. The presets pass it on to CMake as the cache variable `QAR_QT_ROOT`. | `C:\qar\deps\Qt\5.15.2\msvc2019_64` |
| `QAR_VENV` | The Python 3.10 virtual environment, the folder that holds `Scripts\python.exe`. | `C:\qar\venv` |
| `QAR_PREFIX` | The install prefix: the documented install command writes the library and its CMake package into it, and the consumer project finds them there. | `C:\qar\install` |
| `QAR_SDK_ROOT` | The install folder of QAROS, the folder that holds `bin` and `include`; set it to the `InstallFolder` that the registry names. The configure step uses its `include` folder for the QarOS headers when that folder holds both `qar_streaming_cpp\Config.hpp` and `qar_streaming.h`, and the copies in `third_party` otherwise. | `C:\Program Files\Quaternar\QAROS` |

The outputs shown in this documentation were recorded in another folder layout on a machine of the same kind; the folder names in them were replaced by the names of the layout above.

CMake takes a location from the option `-D<NAME>=<folder>` first and from the environment variable of the same name second, and the presets of this repository pass `QAR_QT_ROOT` and `QAR_SDK_ROOT` on from the environment. A folder that does not exist, or that lacks the file that identifies it, stops the configure step with an error that names the variable.


### Create the folders and set the variables

The block below creates the folders and sets the six variables for your Windows user, so that every new shell has them, and for the current shell, so that you can continue at once. It changes your user environment; [Removing everything again](#removing-everything-again) shows how to undo it. If you chose other folders, change the values at the top.

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

> Success: the block prints nothing and ends without an error. Afterwards the six variables are set for your Windows user and in the current window, and these folders exist: `C:\qar`, `C:\qar\deps` and `C:\qar\install`. The folder of QAROS is made by its setup program, not by this block. The check below lists the variables.
>
> This command changes your machine, so no output is shown here.

A variable that is set for your user is visible to programs you start afterwards, not to windows that were already open: open a new PowerShell 7 window, or set the variable in the current window as the block does.

Check the result. Every variable must be set, and the folders you created must exist; the folders of the Qt kit and of the clones appear later in the chain.

```powershell
foreach ($name in 'QAR_ROOT', 'QAR_DEPS_ROOT', 'QAR_QT_ROOT', 'QAR_VENV', 'QAR_PREFIX', 'QAR_SDK_ROOT') {
    '{0,-14} {1}' -f $name, [Environment]::GetEnvironmentVariable($name)
}
```
```text
QAR_ROOT       C:\qar
QAR_DEPS_ROOT  C:\qar\deps
QAR_QT_ROOT    C:\qar\deps\Qt\5.15.2\msvc2019_64
QAR_VENV       C:\qar\venv
QAR_PREFIX     C:\qar\install
QAR_SDK_ROOT   C:\Program Files\Quaternar\QAROS
```


Every command of this documentation that clones, builds or runs something uses only these variables, and a shell that has them needs nothing else.

## The QarOS SDK

QAROS is QuaternAR's proprietary runtime and is not part of this stack. You obtain its setup program from QuaternAR and install it once on the PC, for all users. The setup program records the installed version and the install folder in the registry value `Version` and the registry value `InstallFolder` of the key `HKLM\SOFTWARE\Quaternar\QAROS`. The QAROS version that this stack was verified against is in [SDK compatibility](SDK_COMPATIBILITY.md).

You do not copy any part of QAROS into a layout folder or into a repository. A program that uses QAROS loads the installed library `bin\qar-streaming-c.dll` from the install folder, and the runtime finds its launcher and its other programs next to that library.

The first command reads the version and the install folder from the registry, checks that the folder holds the C library, the runtime launcher and the Visualizer, and prints the SHA256 of the C header `include\qar_streaming.h` of the installation. [Identifying the QAROS version](SDK_COMPATIBILITY.md#identifying-the-qaros-version) explains what to compare the hash with. The command changes nothing.

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

### How the stack finds QAROS

A program calls `qar_library_load` with no path. The C header documents the order in which that call looks for the library and takes the first hit: a library that is already loaded in the process; `qar-streaming-c.dll` beside the module that calls it; the folder that the environment variable `QAROS_RUNTIME_DIR` names; the install folder of the installed QAROS; a folder on `PATH`. A library found beside the program is loaded before the installed one, which is why a program must not ship its own copy. Each candidate is loaded with its dependencies taken from its own folder and from `System32` only.

The runtime initialisation leaves the field `runtime_binaries_folder_path` empty: the runtime then uses the folder that the loaded library came from, which is the `bin` folder of the installation and holds the launcher.

Nothing in this repository links against QAROS. The library `qaros` is compiled against the headers of the QarOS C++ SDK, and a program that uses it loads `qar-streaming-c.dll` at run time. The configure step needs the headers only, not the DLL: it uses the `include` folder of `QAR_SDK_ROOT` when that folder holds both `qar_streaming_cpp\Config.hpp` and `qar_streaming.h`, and the copies in `third_party` of the repository otherwise. A build and the whole test suite therefore work without QAROS; only the programs that connect to QAROS need the installation at run time.

How a program joins the QAROS Hub is described by QuaternAR: [Onboarding and sessions](https://docs.quaternar.com/docs/next/developer-guide/onboarding-and-sessions/) and [Source applications](https://docs.quaternar.com/docs/next/operator-guide/source-applications/). A program that runs on the PC of the Hub joins as a local source app, and the Hub asks its operator once to allow it; a Hub on another PC needs a pairing code.

## Python environment

### Clone this repository

The next steps use the `requirements.txt` of this repository, so clone it first.

```powershell
git clone https://github.com/Quaternar/qaros-qt "$env:QAR_ROOT\src\qar_qt_streaming"
```
```text
Cloning into 'C:\qar\src\qar_qt_streaming'...
done.
```

Go to the folder of the clone; the next steps run there:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

### Create the virtual environment

The virtual environment holds the Python tools of the build in one folder that you can delete. It is created from the Python 3.10 interpreter of the toolchain section and filled from the pinned list `requirements.txt` of the repository.
The list contains, among other packages, `aqtinstall`, which installs Qt.
This repository uses `aqtinstall`, to install Qt in a later section, and no other tool of the list; the environment is optional when you install Qt in another way, and the layering guard that the tests run needs only the Python standard library.

```powershell
& "$env:ProgramFiles\Python310\python.exe" -m venv $env:QAR_VENV
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m pip install --disable-pip-version-check -r requirements.txt
```
```text
Successfully installed Pygments-2.20.0 aqtinstall-3.3.0 backports.zstd-1.6.0 beautifulsoup4-4.15.0 brotli-1.2.0 bs4-0.0.2 certifi-2026.6.17 charset-normalizer-3.4.9 colorama-0.4.6 coverage-7.15.0 defusedxml-0.7.1 exceptiongroup-1.3.1 humanize-4.16.0 idna-3.18 inflate64-1.0.4 iniconfig-2.3.0 multivolumefile-0.2.3 packaging-26.2 patch-ng-1.19.1 pluggy-1.6.0 psutil-7.2.2 py7zr-1.1.3 pybcj-1.0.7 pycryptodomex-3.23.0 pyppmd-1.3.1 pytest-9.1.1 pytest-cov-7.1.0 pytest-repeat-0.9.4 pytest-rerunfailures-16.4 requests-2.34.2 semantic-version-2.10.0 soupsieve-2.8.4 texttable-1.7.0 tomli-2.4.1 typing_extensions-4.16.0 urllib3-2.7.0
```

Check that the tool you will use, `aqtinstall`, is installed at the pinned version.

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m pip list --disable-pip-version-check --format=freeze | Select-String -Pattern '^aqtinstall=='
```
```text

aqtinstall==3.3.0
```

## Qt 5.15.2

Qt is the one large dependency of this repository. The documentation uses the Qt 5.15.2 kit for MSVC 2019 (`win64_msvc2019_64`), the Qt build that matches the v142 toolset. A Qt 6 kit of version 6.5 or newer works as well: the build takes the major version from the kit that `QAR_QT_ROOT` names. The repository uses the Qt modules Core, Gui and Widgets, because it turns Qt widgets into AR panels.

The installer is `aqt` (aqtinstall), a command-line installer for Qt that needs no Qt account; it is in the virtual environment you created. The command installs the kit below `$env:QAR_DEPS_ROOT\Qt`, which makes the Qt folder `$env:QAR_DEPS_ROOT\Qt\5.15.2\msvc2019_64`, the value of `QAR_QT_ROOT`.

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

Check the version and that the CMake packages of the three modules of the repository are present.

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

To use a Qt 6 kit instead, install it with the Qt online installer or with `aqt` (its sub-command list-qt lists the architectures of a version), and set `QAR_QT_ROOT` to its folder, which holds `lib\cmake\Qt6Core`. The preset `vs2019-x64` is for Qt 5.15 or Qt 6.5 built for MSVC 2019, and the preset `vs2022-x64` for Qt 6.5 or newer built for MSVC 2019 or MSVC 2022.

## Build and install qar_qt_streaming

This repository is the one you cloned in [Python environment](#python-environment). Go back to the folder of its clone; the commands below build it from there:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

Configure with the preset of the repository. The preset selects the generator Visual Studio 16 2019 for x64, the build folder `build\vs2019-x64` inside the clone and the configuration `RelWithDebInfo`, and it reads the location variables of the layout. For a Qt 6 kit use the preset `vs2022-x64` (the generator Visual Studio 17 2022, the build folder `build\vs2022-x64`) in this and the following commands; the major version of Qt follows the kit that `QAR_QT_ROOT` names. Configuring downloads GoogleTest 1.14.0 as an archive from `github.com` and checks its SHA256, so this step needs network access; without it, pass `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST` with the path of an extracted GoogleTest 1.14.0 folder.

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_QT_ROOT="C:\qar\deps\Qt\5.15.2\msvc2019_64"
  QAR_SDK_ROOT="C:\Program Files\Quaternar\QAROS"
...
-- Qt 5.15.2: C:/qar/deps/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5
-- QarOS SDK headers: third_party/
...
-- Configuring done (12.5s)
-- Generating done (0.3s)
-- Build files have been written to: C:/qar/src/qar_qt_streaming/build/vs2019-x64
```

Build it. The build writes into the build folder of the repository only.

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

Run the tests of the repository. The tests need no QarOS SDK and open no window; [Testing](TESTING.md) explains what they cover.

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

Install it into the prefix. The install puts the libraries `qar_qt_streaming_seam`, `qar_qt_streaming` and `qar_qt_streaming_qaros` below `$env:QAR_PREFIX`, together with their headers, the QarOS headers of `third_party` when the build used them, and the CMake package that your own project finds there.

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

## Install smoke test

The folder `docs/snippets/consumer` of the repository holds a small project that uses the installed package the way your own project does. It is not part of the repository build; it configures on its own against the install prefix. It asks for the version 0.2.0 of the package and links its one target. The installed headers are UTF-8 text without a byte order mark, and some comments contain characters outside ASCII; compile your own code with `/utf-8`, as this repository does.

```cmake
find_package(qar_qt_streaming 0.2.0 CONFIG REQUIRED)
```

The package needs Qt as well, because the public headers of the library take `QWidget` pointers and the library links the Qt modules publicly. The configure command therefore lists the Qt folder next to `QAR_PREFIX` in `CMAKE_PREFIX_PATH`, so that the package finds the Qt modules.

Its program prints the version of the package that `find_package` found and the target that it was linked with, and it calls three functions of the library that use neither a widget nor QarOS: the readability preset for a viewing distance of 0.65 m, the geometry of a panel and the parser of the close mode. The three functions are free of Qt, so the program starts without the Qt folder on `PATH`.

Configure the project against the install prefix, build it, and run it.

```powershell
cmake -S docs\snippets\consumer -B build\consumer -G "Visual Studio 16 2019" -A x64 "-DCMAKE_PREFIX_PATH=$env:QAR_PREFIX;$env:QAR_QT_ROOT"
```
```text
-- Selecting Windows SDK version 10.0.22621.0 to target Windows 10.0.26200.
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- Found Threads: TRUE
-- Configuring done (3.9s)
-- Generating done (0.0s)
```

```powershell
cmake --build build\consumer --config RelWithDebInfo
```
```text
  Building Custom Rule C:/qar/src/qar_qt_streaming/docs/snippets/consumer/CMakeLists.txt
```

```powershell
.\build\consumer\RelWithDebInfo\qar_qt_streaming_consumer.exe
```
```text
qar_qt_streaming 0.2.0
targets: qar_qt_streaming::qar_qt_streaming
arm's-length preset: 0.50 mm per logical pixel, render scale 2.0
a 420 x 200 widget: buffer 420 x 200 px, panel 0.504 x 0.309 m
close mode of "notice": notice
```

The first line is the version of the package, the second the target that the program was linked with, and the next three lines are the results of the three calls. If `find_package` reports that it cannot find the package, `QAR_PREFIX` does not point to the folder that the install step wrote to, or the version of the installed package differs from the requested one; if it cannot find Qt, `QAR_QT_ROOT` is not set in the shell. [Troubleshooting](TROUBLESHOOTING.md#installation-and-build-problems) lists the installation problems.

## Removing everything again

The installation changes your machine in a few places. This section lists every one of them and how to undo it. The scripts and commands that this documentation tells you to run write only to the folders and registry values listed here, to the build folders inside the clones, and to the files they report. The programs that they call keep caches and logs of their own outside these folders, such as the download cache of pip; this section does not list them.

| Change | Made by | Where it lives | How to undo it |
|---|---|---|---|
| The toolchain: Visual Studio 2019 Build Tools, CMake, Git, PowerShell 7, Python 3.10 | the installers of [Toolchain](#toolchain) | the Windows installation and `C:\Program Files` | Uninstall each program in Windows Settings, or with `winget uninstall` and the package id used to install it. |
| The environment variables of the layout, set for your user | the block of [Directory layout and environment variables](#directory-layout-and-environment-variables) | the environment of your Windows account | The second block below removes them. |
| The layout folders: the dependencies, the virtual environment, the clones, the install prefix | the steps of this document | `C:\qar` | Delete the folder; the second block below does it. The Qt kit and the downloads are inside it. |
| The QarOS SDK | [The QarOS SDK](#the-qaros-sdk) | the installed QAROS, whose folder `QAR_SDK_ROOT` names when you set it | No block below removes it; the paragraph below the table says how to remove it. |
| The logs of the QarOS library | QarOS, when an application connects to it | the folder `quaternar` in `%TEMP%` | Delete the folder. It can grow to gigabytes when trace logging is on. |

QAROS is a product of QuaternAR that you may use for other work, so no block of this document removes or uninstalls it. Uninstall it with its own uninstaller from the install folder when you have no use for it, and remove the variable `QAR_SDK_ROOT` with the blocks below.

Close every program that uses the layout first, and go back to your home folder, so that the window does not stand in a folder that the blocks remove:

```powershell
Set-Location "$env:USERPROFILE"
```

The first block lists what the second one deletes and changes nothing: it prints the folder and runs the removal with `-WhatIf`, which only reports what it would do. Read the folder before you go on, because the second block deletes without asking. Both blocks stop with an error when `QAR_ROOT` is empty, does not start with a drive letter, a colon and a backslash (so a network path and every path that starts with two backslashes are refused), is the root of a drive, or is your user profile folder or a folder above it.

```powershell
$folder = $env:QAR_ROOT
if (-not $folder -or $folder -notmatch '^[A-Za-z]:\\') {
    throw 'QAR_ROOT is empty or does not start with a drive letter and a backslash: nothing is removed.'
}
$full = [System.IO.Path]::GetFullPath($folder).TrimEnd('\')
$profileFolder = [System.IO.Path]::GetFullPath($env:USERPROFILE).TrimEnd('\')
if ($full -eq [System.IO.Path]::GetPathRoot($full).TrimEnd('\') -or
    $full -eq $profileFolder -or
    $profileFolder.StartsWith("$full\", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove '$full': it is a drive, your user profile folder or a folder above it."
}
"Folder to remove: $full"
foreach ($name in 'QAR_DEPS_ROOT', 'ROS2_INSTALL_ROOT', 'QAR_QT_ROOT', 'QAR_VENV', 'QAR_PREFIX') {
    $value = [Environment]::GetEnvironmentVariable($name)
    if ($value -and -not "$value\".StartsWith("$full\", [System.StringComparison]::OrdinalIgnoreCase)) {
        "Not removed, outside that folder: $name = $value"
    }
}
Remove-Item -LiteralPath $full -Recurse -Force -WhatIf
```

> Success: the block prints one line that starts with "Folder to remove:" and gives the full path of the folder that `QAR_ROOT` names, one line that starts with "Not removed, outside that folder:" for each of the other folder variables that points outside it, and a line from `-WhatIf` that starts with "What if: Performing the operation" and names the folder. It deletes nothing. When `QAR_ROOT` is not a safe folder, it ends with an error that says so.
>
> This block changes nothing and prints your own folders, so no output is shown here.

The second block deletes the folder that `QAR_ROOT` names and then the variables of the layout, and nothing else. It leaves the QarOS SDK, the other items of the table and the programs of the toolchain alone. If you gave `QAR_DEPS_ROOT`, `ROS2_INSTALL_ROOT`, `QAR_QT_ROOT`, `QAR_VENV` or `QAR_PREFIX` a value outside the folder of `QAR_ROOT`, delete those folders yourself: the first block prints them. Run the second block when the first one shows the folder that you want to remove:

```powershell
$folder = $env:QAR_ROOT
if (-not $folder -or $folder -notmatch '^[A-Za-z]:\\') {
    throw 'QAR_ROOT is empty or does not start with a drive letter and a backslash: nothing is removed.'
}
$full = [System.IO.Path]::GetFullPath($folder).TrimEnd('\')
$profileFolder = [System.IO.Path]::GetFullPath($env:USERPROFILE).TrimEnd('\')
if ($full -eq [System.IO.Path]::GetPathRoot($full).TrimEnd('\') -or
    $full -eq $profileFolder -or
    $profileFolder.StartsWith("$full\", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove '$full': it is a drive, your user profile folder or a folder above it."
}
Remove-Item -LiteralPath $full -Recurse -Force
foreach ($name in 'QAR_ROOT', 'QAR_DEPS_ROOT', 'ROS2_INSTALL_ROOT', 'QAR_QT_ROOT', 'QAR_VENV', 'QAR_PREFIX', 'QAR_SDK_ROOT') {
    [Environment]::SetEnvironmentVariable($name, $null, 'User')
}
```

> Success: the command prints nothing and ends without an error. Afterwards the folder `C:\qar` is gone and the variables of the layout are deleted from the environment of your Windows user: a new PowerShell 7 window shows them as empty, while a window that was already open keeps the values it had. The QarOS SDK is still there.
>
> This command changes your machine, so no output is shown here.

## Next steps

Continue with [Getting started](GETTING_STARTED.md): it checks the connection to QarOS, streams a window and a hidden panel to a Visualizer and shows how to put the library into your own Qt application. [Testing](TESTING.md) explains what the tests cover, [Configuration](CONFIGURATION.md) lists every build option and environment variable, and [Troubleshooting](TROUBLESHOOTING.md) helps when a step fails.
