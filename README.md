# FAMP

FAMP 是一款基于 C++/Qt 的田野考古制图程序，用于基于点云的绘图工作流。当前代码已经从原始 Visual Studio 工程整理为可提交到 GitHub 的 CMake/vcpkg 源码结构。

本仓库不提交本地构建产物、预编译 DLL、安装包输出或项目内 vcpkg 目录。第三方依赖通过项目外部的 vcpkg 安装树提供。

## 目录结构

- `src/`：主程序源码和标准 CMake 入口。
- `ui/`：Qt Designer `.ui` 文件。
- `resources/`：Qt `.qrc` 资源文件和图片资源。
- `third_party/lastools/`：原始程序依赖的 LAStools/LASlib 源码，作为 `lastools_compat` 静态库编译。
- `samples/`：用于本地冒烟测试的小型输入文件。
- `triplets/`：Linux vcpkg 覆盖 triplet。
- `scripts/`：环境准备脚本。

## 依赖项

- CMake 3.22 或更新版本
- C++17 编译器
- Git
- Linux 上 vcpkg 安装于 `/opt/vcpkg`

Qt/VTK 桌面渲染所需的 Linux 系统包：

```bash
sudo apt-get install -y '^libxcb.*-dev' libx11-xcb-dev libglu1-mesa-dev libxrender-dev libxi-dev libxkbcommon-dev libxkbcommon-x11-dev libegl1-mesa-dev libxt-dev
```

`vcpkg.json` 会安装 Qt、VTK 和 PCL：

- `qtbase`：启用 `gui`、`widgets`、`opengl`、`xcb`、`png`、`jpeg` 等 GUI 运行所需特性。
- `vtk`：启用 `opengl` 和 `qt`。
- `pcl`：启用 `visualization` 和 `qt`。

Linux 首次准备 vcpkg：

```bash
cd /data/专利/田野考古制图系统/FAMP
./scripts/bootstrap-vcpkg.sh
```

该脚本会检查 `/opt/vcpkg`，不存在时从 GitHub 拉取并执行 `/opt/vcpkg/bootstrap-vcpkg.sh`。项目不在当前目录保存 vcpkg 副本。

## 配置与构建

Linux 标准构建流程：

```bash
cd /data/专利/田野考古制图系统/FAMP
mkdir -p build
cd build
cmake -S ../src -B .
make -j8
```

Linux CMake 主入口为 `src/CMakeLists.txt`，默认使用：

```bash
/opt/vcpkg/scripts/buildsystems/vcpkg.cmake
```

依赖安装树在项目外部：

```bash
/opt/vcpkg/installed
```

Linux 构建使用覆盖 triplet `triplets/x64-linux-release.cmake`，使 vcpkg 跳过未使用的 debug 变体以节省构建时间。该 triplet 保持动态库链接，与 vcpkg 在 Linux 上处理大型 GUI/渲染依赖的常规行为一致。

构建成功后，可执行文件生成在：

```bash
build/bin/FAMP
```

安装命令会安装到指定前缀下的 `bin` 目录：

```bash
cmake --install . --prefix ../install
```

安装后路径为：

```bash
../install/bin/FAMP
```

本项目当前推荐使用 `build/bin/FAMP` 进行本地开发和验证。

## 运行验证

在图形桌面环境中运行：

```bash
cd /data/专利/田野考古制图系统/FAMP
./build/bin/FAMP
```

也可以用 `timeout` 做启动冒烟测试：

```bash
timeout 6 ./build/bin/FAMP
```

返回码 `124` 表示程序正常保持运行直到被 `timeout` 结束。若出现 Qt 平台插件错误，优先确认系统 XCB 开发包已安装，并确认 `/opt/vcpkg/installed/x64-linux-release/Qt6/plugins/platforms/libqxcb.so` 存在。

## vcpkg 缓存清理

依赖成功编译后，可以删除 vcpkg 的临时构建目录和下载缓存，保留已安装依赖：

```bash
rm -rf /opt/vcpkg/buildtrees /opt/vcpkg/packages /opt/vcpkg/downloads ~/.cache/vcpkg
```

不要删除：

```bash
/opt/vcpkg/installed
```

该目录保存当前项目编译和运行需要的 Qt、VTK、PCL 等库。

## Qt 资源说明

按钮图标和窗口图标必须使用 Qt 资源路径，不要在 `.ui` 文件中使用运行目录相关的相对路径。

正确示例：

```xml
<normaloff>:/images/images/ccOpen.png</normaloff>
```

资源文件位置：

```bash
resources/res.qrc
```

`res.qrc` 使用前缀 `/images`，其中的文件条目形如 `images/ccOpen.png`，因此代码和 `.ui` 中的访问路径为 `:/images/images/ccOpen.png`。新增按钮图片时，需要同时把图片放入 `resources/images/` 并加入 `resources/res.qrc`。

`src/CMakeLists.txt` 已启用 `CMAKE_AUTORCC`，资源会随程序一起编译。

## Windows 构建说明

Windows 可使用本地 vcpkg，例如 `C:\vcpkg`。由于 Linux 默认配置指向 `/opt/vcpkg`，Windows 配置时应显式覆盖 vcpkg 相关路径：

```powershell
$VcpkgRoot = "C:\vcpkg"
$ManifestDir = (Get-Location).Path
.\scripts\bootstrap-vcpkg.ps1
cmake -S src -B build `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_INSTALLED_DIR="$VcpkgRoot\installed" `
  -DVCPKG_MANIFEST_DIR="$ManifestDir" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DVCPKG_HOST_TRIPLET=x64-windows
cmake --build build --config Release
```

Windows 输出通常位于：

```powershell
build\bin\Release\FAMP.exe
```

Windows 命令为跨平台配置参考；当前仓库本轮已在 Linux `/opt/vcpkg` 环境下完成编译和启动验证。

## 自定义 Linux 构建目录

如需使用其他 Linux 构建目录，可手动配置，但需保持 `/opt/vcpkg` 工具链和安装树路径：

```bash
cmake -S src -B /tmp/famp-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_INSTALLED_DIR=/opt/vcpkg/installed \
  -DVCPKG_MANIFEST_DIR="$PWD" \
  -DVCPKG_OVERLAY_TRIPLETS="$PWD/triplets" \
  -DVCPKG_TARGET_TRIPLET=x64-linux-release \
  -DVCPKG_HOST_TRIPLET=x64-linux-release
cmake --build /tmp/famp-build -- -j8
```

## 维护约定

- 不提交 `build/`、`install/`、vcpkg 下载缓存或本地 IDE 产物。
- Linux 标准流程使用 `cmake -S ../src -B .`，主 CMake 文件为 `src/CMakeLists.txt`。
- `build/bin/FAMP` 是本地构建输出；`install(TARGETS FAMP RUNTIME DESTINATION bin)` 控制安装输出。
- CMake 文件有意不使用项目本地的 vcpkg 目录。Linux 构建使用 `/opt/vcpkg` 工具链路径。
- 程序仍会将中间 `.pcd` 文件写入工作目录，与原有行为保持一致。
- 首次重构的重点是实现可复现的跨平台构建，而非算法或 UI 重新设计。

## 常见问题

### `libqxcb.so` 或 Qt platform plugin 报错

安装 Linux XCB 相关系统包，并确认 vcpkg Qt 编译时启用了 `xcb`、`xcb-sm`、`xcb-xlib`、`xrender` 特性。

### 按钮图标不显示

检查 `.ui` 中是否仍有 `images/...` 相对路径。应改为 `:/images/images/...`，并确认对应图片已经加入 `resources/res.qrc`。

### VTK/OpenGL 启动崩溃

当前程序在 `main.cpp` 中为 VTK 9 设置了 `QVTKOpenGLNativeWidget::defaultFormat()`，并在 `MyVTK.cpp` 中使用 `vtkGenericOpenGLRenderWindow`。如果后续升级 VTK/Qt，需要保持这套初始化方式。
