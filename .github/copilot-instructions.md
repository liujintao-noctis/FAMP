# FAMP — Copilot 指令

## 构建

```bash
# Linux（首次需执行 vcpkg 引导脚本）
./scripts/bootstrap-vcpkg.sh

# 配置并构建
mkdir -p build && cd build
cmake -S ../src -B .
make -j8

# 输出二进制文件：build/bin/FAMP

# Windows PowerShell
.\scripts\bootstrap-vcpkg.ps1
cmake -S src -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

CMake 入口文件为 `src/CMakeLists.txt`。Linux 构建始终使用 `/opt/vcpkg` 作为工具链路径，并使用自定义 triplet `triplets/x64-linux-release.cmake`（仅 release 版本，动态链接）。依赖项（Qt、VTK、PCL）通过 `vcpkg.json` 清单模式管理。

当前未配置测试框架、代码检查工具或 CI 流水线。

## 架构

单窗口 Qt Widgets 桌面应用。`MainWindow`（QMainWindow）是中央调度器——通过直接组合持有所有子系统，并通过 Qt 信号/槽将它们连接在一起。无依赖注入、路由或模块系统。

```
MainWindow（调度器）
├── MyVTK           — 3D 点云渲染（VTK + QVTKOpenGLNativeWidget）
├── MyGraphicsView  — 2D 制图画布（QGraphicsView），又称"米格纸"
├── Cloud           — 点云计算（质心、OBB、去中心化）
├── QDlgClip        — 切割平面对话框（QDialog）
├── QDlgPlotTab     — 出图模板数据录入对话框（QDialog）
├── QStandardItemModel + QTreeView — "DB Tree"面板，管理点云列表
├── QTextEdit       — 控制台消息输出
└── std::vector<MyCloudList> — 已加载点云注册表
```

### 数据流

1. 用户打开 `.las` 文件 → `MainWindow::las2PCD()` 将 LAS 转为 PCL 点云 → 加入 `pointCloudList`
2. 点云同时在 `MyVTK`（3D）和 `MyGraphicsView`（2D 投影）中显示
3. 用户在 VTK 中使用平面控件切割点云 → 切割点投影到 AABB 各面
4. 投影后的 2D 点发送到 `MyGraphicsView` → 以用户选择的比例尺（1:10、1:20、1:50、1:100）渲染为 `MyItem` 图形对象
5. 用户可操作图形项（移动、旋转、组合、图层排序），添加文字、指北针、出图模板
6. 最终 2D 绘图可导出

### 核心算法（位于 MyGraphicsView）

- **KNN Alpha Shape** — 从投影点云中提取边界
- **有序排序** — 将无序投影点重新排序以生成折线
- **Douglas-Peucker（DP）** — 线条简化
- **B 样条** — 通过控制点反求与插值进行曲线拟合

## 关键约定

### 头文件引用风格
- 所有头文件使用 `#pragma once`（不使用传统 include 守卫）
- 项目内头文件使用双引号引用（`"MyVTK.h"`），系统/框架头文件使用尖括号（`<QWidget>`）
- **注意**：现有代码中 `#include` 后的空格不一致——新代码请统一使用 `<QWidget>` 风格（带空格）

### 注释与编码
- 代码注释使用中文
- 含有中文字符串字面量的文件需使用 `#pragma execution_character_set("utf-8")`

### Qt 模式
- 所有 `QObject` 子类均使用 `Q_OBJECT` 宏
- UI 文件存放于 `ui/` 目录（Qt Designer 生成的 `.ui` 文件），由 CMake 的 `AUTOUIC` 自动处理
- 资源文件存放于 `resources/`（`.qrc` 文件），由 `AUTORCC` 自动处理
- MOC 由 `AUTOMOC` 自动处理

### VTK 版本兼容
- `MyVTK.h` 中定义了 `FAMP_QVTK_WIDGET` 宏：VTK ≥9 时为 `QVTKOpenGLNativeWidget`，VTK <9 时为 `QVTKWidget`。引用 VTK 控件基类时请始终使用此宏。

### 数据结构
- 使用轻量级结构体存储纯数据：`MyCloudList`（点云 + actor 指针）、`DPPoint`（DP 算法）、`Point2PointDisIndex`、`MyOrderCloudType`
- 使用 typedef 枚举定义常量：`ProjectType`（XOY、XOZ、YOZ、OLXOY、XOYLine、NONE）、`ScaleType`（OneToTen、OneToTwenty、OneToFifty、OneToHundred）

### 点云处理管线
- 输入：LAS 文件（通过 LAStools 静态库 `lastools_compat`）
- 处理：PCL（`pcl::PointCloud<pcl::PointXYZRGB>::Ptr`）
- 3D 渲染：VTK actor/mapper
- 2D 渲染：QGraphicsItem 子类（`MyItem`、`CompassItem`、`FormTabulationItem`）
- 中间文件以 `.pcd` 格式写入工作目录（保留原有行为，重构中未改动）

### 依赖说明
- LAStools 源码位于 `third_party/lastools/`，通过 CMakeLists 直接编译为静态库（`lastools_compat`）——不由 vcpkg 获取
- Linux 系统需安装 X11/GL 开发库（具体 `apt` 命令见 README）
- 自定义 vcpkg triplet 仅启用 release 构建以减少编译时间；未配置 debug 构建
