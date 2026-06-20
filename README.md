# FAMP

[![CI](https://github.com/liujintao-noctis/FAMP/actions/workflows/ci.yml/badge.svg)](https://github.com/liujintao-noctis/FAMP/actions/workflows/ci.yml)

FAMP 是一款基于 C++17、Qt、VTK 和 PCL 的田野考古制图程序，用于点云查看、投影、绘图和相关制图工作流。当前工程已经整理为 CMake/vcpkg 源码结构，构建产物固定放在项目根目录下的 `build/`。

本仓库不提交本地构建产物、预编译 DLL、安装包输出或项目内 vcpkg 目录。第三方依赖通过仓库外部的 vcpkg 安装树提供，Linux 默认使用 `/opt/vcpkg`。

## 目录结构

- `CMakeLists.txt`：根目录 CMake 入口，负责在 `project()` 前设置 vcpkg toolchain，然后进入 `src/`。
- `src/`：主程序源码和主要 CMake 配置。
- `ui/`：Qt Designer `.ui` 表单。
- `resources/`：Qt `.qrc` 资源文件和图片资源。
- `tests/`：GoogleTest 单元测试。
- `third_party/lastools/`：随源码编译的 LAStools/LASlib 兼容库。
- `samples/`：本地冒烟测试用的小型输入文件。
- `triplets/`：vcpkg 覆盖 triplet，Linux 使用 `x64-linux-release`。
- `scripts/`：vcpkg 环境准备脚本。

## 依赖项

基础工具：

- CMake 3.22 或更新版本
- C++17 编译器
- Git
- vcpkg，Linux 推荐路径为 `/opt/vcpkg`

Linux 桌面渲染依赖：

```bash
sudo apt-get install -y '^libxcb.*-dev' libx11-xcb-dev libglu1-mesa-dev \
  libxrender-dev libxi-dev libxkbcommon-dev libxkbcommon-x11-dev \
  libegl1-mesa-dev libxt-dev
```

`vcpkg.json` 会安装 Qt、VTK 和 PCL。Linux 下首次准备 vcpkg：

```bash
cd /FAMP
./scripts/bootstrap-vcpkg.sh
```

该脚本会检查 `/opt/vcpkg`，不存在时从 GitHub 拉取并执行 `/opt/vcpkg/bootstrap-vcpkg.sh`。

## 配置与构建

所有本地构建都使用项目根目录下的 `build/`。推荐先创建并进入该目录，再从 `build/` 内配置和编译。不要在 `src/` 内建目录，也不要使用 `/tmp/famp-build` 作为常规构建目录。

### Linux

```bash
cd /FAMP
export VCPKG_ROOT=/opt/vcpkg
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -- -j$(nproc)
```

说明：

- `mkdir -p build && cd build` 创建并进入项目根目录下的 `build/`。
- `cmake ..` 从 `build/` 内配置仓库根目录工程。
- 根目录 `CMakeLists.txt` 会在第一次 `project()` 前读取 `VCPKG_ROOT` 并设置 vcpkg toolchain。
- Linux 默认使用 `triplets/x64-linux-release.cmake`，依赖安装在 `/opt/vcpkg/installed/x64-linux-release`。
- 构建成功后，可执行文件位于 `build/bin/FAMP`。

如需显式覆盖 vcpkg 路径：

```bash
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_ROOT=/opt/vcpkg
```

### Windows

PowerShell 示例：

```powershell
cd /FAMP
.\scripts\bootstrap-vcpkg.ps1
mkdir build
cd build
cmake .. `
  -DCMAKE_BUILD_TYPE=Release `
  -DVCPKG_ROOT=C:\vcpkg `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DVCPKG_HOST_TRIPLET=x64-windows
cmake --build . --config Release
```

Windows 输出通常位于 `build\bin\Release\FAMP.exe`。MSVC 编译选项 `_CRT_SECURE_NO_WARNINGS`、`NOMINMAX` 和 `/utf-8` 由 CMake 自动设置。

## 测试

测试目标默认随工程配置。构建后运行：

```bash
ctest --test-dir build/tests --output-on-failure
```

当前测试使用 GoogleTest，首次配置可能会下载 GoogleTest。新增非 GUI 逻辑时，优先在 `tests/test_*.cpp` 中补充单元测试。

## 运行验证

在图形桌面环境中运行：

```bash
cd /FAMP
./build/bin/FAMP
```

也可以做启动冒烟测试：

```bash
timeout 6 ./build/bin/FAMP
```

返回码 `124` 表示程序正常保持运行直到被 `timeout` 结束。若出现 Qt platform plugin 或 `libqxcb.so` 错误，先确认 Linux XCB 相关系统包已安装，并检查：

```bash
/opt/vcpkg/installed/x64-linux-release/Qt6/plugins/platforms/libqxcb.so
```

## 安装

本地开发推荐直接使用 `build/bin/FAMP`。如需安装到单独目录：

```bash
cmake --install build --prefix install
```

安装后可执行文件位于 `install/bin/FAMP`。

## Qt 资源说明

按钮图标和窗口图标必须使用 Qt 资源路径，不要在 `.ui` 文件中写运行目录相关的相对路径。

正确示例：

```xml
<normaloff>:/images/images/ccOpen.png</normaloff>
```

`resources/res.qrc` 使用前缀 `/images`，文件条目形如 `images/ccOpen.png`，因此代码和 `.ui` 中的访问路径为 `:/images/images/ccOpen.png`。新增图片时，需要同时把图片放入 `resources/images/` 并加入 `resources/res.qrc`。

## vcpkg 缓存清理

依赖成功编译后，可以清理临时构建目录和下载缓存：

```bash
rm -rf /opt/vcpkg/buildtrees /opt/vcpkg/packages /opt/vcpkg/downloads ~/.cache/vcpkg
```

不要删除：

```bash
/opt/vcpkg/installed
```

该目录保存当前项目编译和运行需要的 Qt、VTK、PCL 等库。

## 维护约定

- 不提交 `build/`、`install/`、vcpkg 下载缓存或本地 IDE 产物。
- 标准配置命令是在项目根目录执行 `mkdir -p build && cd build`，然后运行 `cmake .. -DCMAKE_BUILD_TYPE=Release`。
- 标准构建目录是项目根目录下的 `build/`。
- `build/bin/FAMP` 是 Linux 本地开发和验证用输出。
- `install(TARGETS FAMP RUNTIME DESTINATION bin)` 控制安装输出。
- 程序仍会将中间 `.pcd` 文件写入工作目录，与原有行为保持一致。

## 常见问题

### `libqxcb.so` 或 Qt platform plugin 报错

安装 Linux XCB 相关系统包，并确认 vcpkg Qt 编译时启用了 `xcb`、`xcb-sm`、`xcb-xlib`、`xrender` 等特性。

### 按钮图标不显示

检查 `.ui` 中是否仍有 `images/...` 相对路径。应改为 `:/images/images/...`，并确认对应图片已经加入 `resources/res.qrc`。

### VTK/OpenGL 启动崩溃

程序在 `main.cpp` 中为 VTK 9 设置了 `QVTKOpenGLNativeWidget::defaultFormat()`，并在 `MyVTK.cpp` 中使用 `vtkGenericOpenGLRenderWindow`。升级 Qt/VTK 时，需要保持这套初始化方式。
