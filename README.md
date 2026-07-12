# FAMP

[![CI](https://github.com/liujintao-noctis/FAMP/actions/workflows/ci.yml/badge.svg)](https://github.com/liujintao-noctis/FAMP/actions/workflows/ci.yml)

FAMP（Field Archaeology Mapping Program）是一款基于 C++17、Qt、VTK 和 PCL 的田野考古制图桌面程序，支持 PCD/LAS 点云查看、平面切割、投影、二维绘图和成果图片保存。

项目使用 CMake 和 vcpkg 管理跨平台构建。规范的本地 CMake 入口是 `src/CMakeLists.txt`，构建产物统一放在仓库根目录的 `build/`。根目录 `CMakeLists.txt` 仍可使用，但它只负责设置工程并转发到 `src/`。

当前应用版本为 `0.3.0`。`cmake/FampVersion.cmake` 是 CMake 和 C++ 程序使用的版本号来源；配置时会生成 `Version.h`，程序会把版本写入 Qt 应用元数据并在主窗口标题栏显示为 `FAMP 0.3.0`。发布新版本时，还需要同步更新 `vcpkg.json` 中的 `version-string`。

## 当前功能

- 在后台串行读取、校验并打开 PCD/LAS 点云，加载期间界面仍可响应；随后可进行平面切割、投影和二维绘图。
- 为选中点云调整点大小、透明度及 RGB/统一颜色显示，并在后台执行体素降采样或统计离群点去噪；结果原子保存为新的 PCD，原始文件不被修改。
- 新建、打开和原子保存 `.famp` 项目，并每 60 秒自动备份未保存更改；项目格式 v2 保存点云原始坐标/双精度变换、可见性、二维图元、测量成果、比例尺和窗口布局，兼容读取 v1 项目，源点云移动后可交互重新定位。
- 为项目记录经 PROJ 验证的 EPSG 坐标系，并使用单点坐标转换器核对坐标。
- 将 PCD/LAS 文件拖入主窗口，或从“文件 → 最近打开”恢复最近 8 个有效文件。
- 使用 `Ctrl+Shift+Left/Right` 将选中的二维图元绕中心每次旋转 5°。
- 使用 `Ctrl+Z` 和 `Ctrl+Shift+Z`/`Ctrl+Y` 撤销或重做最近 100 步二维图元编辑。
- 在二维画布按当前制图比例尺测量距离和多边形面积，结果可随成果一起导出，并支持撤销、重做和集中清除。
- 通过“帮助”菜单查看离线快速入门、快捷键和 Qt/VTK/PCL 版本。
- 将完整二维画布按 A4/A3/自定义纸张、横向/纵向和 150/300/600 DPI 原子导出为 PDF、SVG、PNG 或 BMP，可保持当前制图比例尺或自动适合页面，并可在保存前打开打印预览。

完整版本变更见 [`CHANGELOG.md`](CHANGELOG.md)。

## 已验证平台

| 平台 | 构建配置 | 可执行文件 |
| --- | --- | --- |
| Ubuntu 22.04 x86_64 | Release，GCC/CMake/vcpkg | `build/bin/FAMP` |
| Windows x64 | Visual Studio 2022，Release-only vcpkg triplet | `build\bin\Release\FAMP.exe` |

GitHub Actions 会在 Ubuntu 22.04 和 `windows-latest` 上分别完成配置、Release 编译和测试，并上传可运行压缩包。Windows CI 还会重新解压压缩包，确认依赖文件完整，并验证程序启动后持续运行 15 秒。

## Linux：从零构建并运行

以下命令适用于 Ubuntu 22.04。先安装编译工具及 Qt/VTK/PCL 所需的系统库：

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git curl zip unzip tar \
  autoconf autoconf-archive automake libtool pkg-config ninja-build \
  libgl1-mesa-dev libglu1-mesa-dev libegl1-mesa-dev libglew-dev \
  libx11-dev libxext-dev libxt-dev libxtst-dev libx11-xcb-dev \
  libxcb1-dev libxcb-cursor-dev libxcb-glx0-dev libxcb-render0-dev \
  libxcb-shm0-dev libxcb-sync-dev libxcb-util-dev libxcb-xfixes0-dev \
  libxcb-xinerama0-dev libxcb-icccm4-dev libxcb-image0-dev \
  libxcb-keysyms1-dev libxcb-randr0-dev libxcb-render-util0-dev \
  libxcb-shape0-dev libxcb-xinput-dev libxcb-xkb-dev \
  libice-dev libsm-dev libxi-dev libxrender-dev \
  libxkbcommon-dev libxkbcommon-x11-dev
```

克隆仓库并准备 vcpkg：

```bash
git clone https://github.com/liujintao-noctis/FAMP.git
cd FAMP

./scripts/bootstrap-vcpkg.sh
export VCPKG_ROOT=/opt/vcpkg
```

`bootstrap-vcpkg.sh` 会在需要时创建 `/opt/vcpkg`、克隆 vcpkg 并执行 bootstrap。首次创建 `/opt/vcpkg` 可能要求输入 `sudo` 密码。

然后在仓库根目录执行规范构建命令：

```bash
cmake -S src -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_ROOT=/opt/vcpkg \
  -DBUILD_TESTING=ON

cmake --build build -- -j8
ctest --test-dir build --output-on-failure
```

在图形桌面中启动：

```bash
./build/bin/FAMP
```

也可以执行六秒启动冒烟测试：

```bash
timeout 6 ./build/bin/FAMP
```

退出码 `124` 表示 FAMP 正常保持运行，随后被 `timeout` 主动结束；它不表示程序崩溃。

首次配置需要从网络下载并编译 Qt、VTK、PCL、PROJ 以及 GoogleTest，耗时会明显长于后续构建。不要因为 vcpkg 长时间编译依赖而重复中断配置。

### 不使用 `/opt/vcpkg`

如果当前用户不能写入 `/opt`，可以把 vcpkg 放在其他仓库外目录：

```bash
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
export VCPKG_ROOT="$HOME/vcpkg"

cmake -S src -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_ROOT="$VCPKG_ROOT" \
  -DBUILD_TESTING=ON
cmake --build build -- -j8
ctest --test-dir build --output-on-failure
```

不要把 vcpkg checkout 放入 FAMP 仓库。

## Windows：从零构建并运行

需要预先安装：

- Visual Studio 2022，并勾选“使用 C++ 的桌面开发”和 Windows SDK。
- CMake 3.22 或更新版本。
- Git。
- 64 位 PowerShell。

Qt 和 VTK 会生成较深的文件路径。建议把仓库放在短的纯英文路径，例如 `C:\src\FAMP`，并把 vcpkg 放在 `C:\vcpkg`。

在 PowerShell 中执行：

```powershell
New-Item -ItemType Directory -Force C:\src | Out-Null
Set-Location C:\src
git clone https://github.com/liujintao-noctis/FAMP.git
Set-Location FAMP

$env:VCPKG_ROOT = "C:\vcpkg"
.\scripts\bootstrap-vcpkg.ps1

cmake -S src -B build `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DVCPKG_ROOT="$env:VCPKG_ROOT" `
  -DVCPKG_TARGET_TRIPLET=x64-win-rel `
  -DVCPKG_HOST_TRIPLET=x64-win-rel `
  -DBUILD_TESTING=ON

cmake --build build --config Release --parallel

$env:PATH = "$env:VCPKG_ROOT\installed\x64-win-rel\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$env:VCPKG_ROOT\installed\x64-win-rel\Qt6\plugins"

ctest --test-dir build -C Release --output-on-failure
.\build\bin\Release\FAMP.exe
```

`x64-win-rel` 是仓库提供的 Release-only triplet，只生成 Release 依赖，不创建 vcpkg Debug 库。短 triplet 名称也能降低 Qt 生成文件触及 Windows `MAX_PATH` 的风险。

`PATH` 用于查找 Qt、VTK、PCL、PROJ 等 DLL，`QT_PLUGIN_PATH` 用于查找 `qwindows.dll`。如果只想使用程序而不开发，优先下载 CI 生成的 Windows 压缩包，不需要自行配置这些变量。

## CMake 入口与缓存

本地开发统一使用：

```bash
cmake -S src -B build
```

根目录入口 `cmake -S . -B build` 仍然有效，GitHub Actions 当前也使用该入口。但是，同一个 `build/` 不能在 `-S src` 和 `-S .` 之间切换，否则 CMake 会报告源码目录与缓存不一致。

发生该问题时，只删除生成目录后重新配置；不要删除源码：

```bash
rm -rf build
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DVCPKG_ROOT=/opt/vcpkg
```

Windows PowerShell：

```powershell
Remove-Item -Recurse -Force build
cmake -S src -B build `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DVCPKG_ROOT="$env:VCPKG_ROOT" `
  -DVCPKG_TARGET_TRIPLET=x64-win-rel `
  -DVCPKG_HOST_TRIPLET=x64-win-rel
```

## 使用 GitHub Actions 成品

每次 CI 会上传两个 artifact，内部压缩包名称分别类似：

- `FAMP-linux-<commit>.tar.gz`
- `FAMP-windows-<commit>.zip`

### Linux 成品

```bash
tar -xzf FAMP-linux-<commit>.tar.gz
cd FAMP-linux
./run-famp.sh
```

`run-famp.sh` 会自动设置随包携带的共享库和 Qt 插件路径。压缩包以 Ubuntu 22.04 为目标系统，仍需要图形桌面、OpenGL/X11 和本 README 前面列出的系统运行库。

包内还包含：

- `share/applications/famp.desktop`
- `share/icons/hicolor/256x256/apps/famp.png`

### Windows 成品

必须先完整解压 zip，不能直接在压缩包预览窗口中运行程序。解压后双击：

- `run-famp.bat`：正常启动，使用包内 Mesa3D 软件 OpenGL 回退。
- `run-famp-debug.bat`：输出 Qt 插件诊断信息，并生成 `famp-run.log`。
- `FAMP.exe`：主程序。

不要只复制 `FAMP.exe`；程序还需要同目录 DLL、`platforms/qwindows.dll` 和其他 Qt 插件。

## 测试

Linux：

```bash
ctest --test-dir build --output-on-failure
```

Windows 多配置生成器必须指定 Release：

```powershell
ctest --test-dir build -C Release --output-on-failure
```

当前 GoogleTest 用例覆盖点云质心与去中心化、原始坐标、OBB、统一 PCD/LAS 加载服务、非 ASCII 点云路径、项目文件 v1/v2、二维场景完整往返、坐标系、撤销/重做、专业导出、字符串转换和物理比例尺计算。首次配置可能需要从 GitHub 下载 GoogleTest。

## 安装

本地开发通常直接运行 `build/bin/FAMP`。如需验证 CMake 安装规则，可以安装到仓库内的独立目录：

```bash
cmake --install build --prefix "$PWD/install"
```

安装结果包括：

- `install/bin/FAMP`
- `install/share/applications/famp.desktop`
- `install/share/icons/hicolor/256x256/apps/famp.png`

`cmake --install` 不会复制 vcpkg 的 Qt、VTK、PCL 动态库，因此上述目录不是可直接分发的独立软件包。在本机构建环境中运行安装后的程序，需要继续提供 vcpkg 库和 Qt 插件路径：

```bash
export VCPKG_ROOT=/opt/vcpkg
export LD_LIBRARY_PATH="$VCPKG_ROOT/installed/x64-linux-release/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$VCPKG_ROOT/installed/x64-linux-release/Qt6/plugins"
./install/bin/FAMP
```

不要直接把这一安装结果复制到其他电脑，也不要在未配置系统动态库搜索路径时安装到 `/usr/local`。需要分发时，应使用 GitHub Actions 生成的 Linux tar.gz 或已完成启动验证的 Windows zip。

## vcpkg 目录说明

`VCPKG_ROOT` 指向仓库外部的 vcpkg 工具目录。使用本 README 的本地命令时，依赖通常安装在：

- Linux：`/opt/vcpkg/installed/x64-linux-release`
- Windows：`C:\vcpkg\installed\x64-win-rel`

如果像 CI 一样直接传入 `CMAKE_TOOLCHAIN_FILE` 而不传 `VCPKG_ROOT`，vcpkg manifest 默认可能把依赖放到 `build/vcpkg_installed/`。两种布局都受支持。

依赖成功构建后，可以删除下载包和临时编译目录以释放空间，但这些目录可能被其他 vcpkg 项目共享：

```bash
rm -rf /opt/vcpkg/buildtrees \
       /opt/vcpkg/packages \
       /opt/vcpkg/downloads \
       "$HOME/.cache/vcpkg"
```

不要删除 `/opt/vcpkg/installed`，否则 Qt、VTK、PCL 需要重新编译。

## 项目结构

- `CMakeLists.txt`：兼容入口，设置工程后转发到 `src/`。
- `cmake/FampVersion.cmake`：应用版本号的 CMake 单一来源。
- `src/`：主程序源码和规范的 CMake 入口。
- `src/Version.h.in`：由 CMake 配置为供 C++ 使用的版本头。
- `ui/`：Qt Designer `.ui` 表单。
- `resources/`：Qt 资源、应用图标和 Linux desktop 文件。
- `tests/`：GoogleTest 单元测试。
- `third_party/lastools/`：随源码编译的 LAStools/LASlib 兼容代码。
- `samples/`：本地冒烟测试用的小型输入文件。
- `triplets/`：Linux 和 Windows 的 Release-only vcpkg triplet。
- `scripts/`：Linux/Windows vcpkg bootstrap 脚本。

本仓库不提交 `build/`、`install/`、vcpkg 缓存、预编译 DLL、CI 打包结果或 IDE 产物。

## Qt 资源约定

按钮图标和窗口图标必须使用 Qt 资源路径，不要依赖程序工作目录。

正确示例：

```xml
<normaloff>:/images/images/ccOpen.png</normaloff>
```

`resources/res.qrc` 使用前缀 `/images`，文件条目形如 `images/ccOpen.png`，所以代码和 `.ui` 中的完整路径是 `:/images/images/ccOpen.png`。新增图片时，需要把文件放入 `resources/images/`，同时加入 `resources/res.qrc`。

生产版本不会自动把投影过程中的中间 PCD 文件写入工作目录；只有用户主动保存切割点云时才会生成 PCD 文件。

## 常见问题

### 配置时显示 `VCPKG_ROOT not set; using system packages`

说明 CMake 没有收到 vcpkg 路径。清理 `build/` 后重新设置并显式传入：

```bash
export VCPKG_ROOT=/opt/vcpkg
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DVCPKG_ROOT="$VCPKG_ROOT"
```

### Linux 报 `libqxcb.so` 或 Qt platform plugin 错误

先安装 Linux 依赖，然后确认插件存在：

```bash
find "$VCPKG_ROOT/installed/x64-linux-release/Qt6/plugins" \
  -name libqxcb.so -print
```

需要诊断时运行：

```bash
QT_DEBUG_PLUGINS=1 ./build/bin/FAMP
```

### Linux 没有图形桌面

FAMP 是 GUI 程序，需要有效的 X11/Wayland 桌面会话。SSH 或无头环境中没有 `DISPLAY` 时，不能把普通启动失败当成编译失败；测试仍可通过 `ctest` 单独运行。

### Windows 启动时提示缺少 DLL 或 `qwindows.dll`

本地开发请重新执行 Windows 构建章节中的 `PATH` 和 `QT_PLUGIN_PATH` 设置。普通用户应使用完整 CI zip，并通过 `run-famp-debug.bat` 收集 `famp-run.log`，不要只复制 `FAMP.exe`。

### Windows 图标在 Linux 中显示为齿轮

这是 Linux 文件管理器把 `.exe` 当作普通 Windows 可执行文件显示的通用图标，不代表 `FAMP.exe` 没有嵌入图标。请在 Windows 资源管理器中查看；如果资源管理器缓存旧图标，可以换一个目录解压或重启资源管理器。

### Windows 配置时报路径过长

把仓库和 vcpkg 移到 `C:\src\FAMP`、`C:\vcpkg` 这类短英文路径，删除旧 `build` 后重新配置，并继续使用 `x64-win-rel`。

### VTK/OpenGL 启动异常

程序在 `main.cpp` 中为 VTK 9 设置 `QVTKOpenGLNativeWidget::defaultFormat()`，并使用 `vtkGenericOpenGLRenderWindow`。Windows CI 成品携带 Mesa3D 回退；Linux 应确认 Mesa/OpenGL 和图形驱动可用。

## 维护约定

- 规范本地配置命令是 `cmake -S src -B build -DCMAKE_BUILD_TYPE=Release`。
- Linux 开发输出是 `build/bin/FAMP`。
- Windows Release 输出是 `build\bin\Release\FAMP.exe`。
- 非 GUI 逻辑应补充 `tests/test_*.cpp`；GUI 修改至少完成构建和启动冒烟测试。
- 不要提交生成的二进制、依赖安装树或本地构建缓存。
