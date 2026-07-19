# FAMP — Copilot 指令

## 构建

```bash
# Linux（首次需执行 vcpkg 引导脚本）
./scripts/bootstrap-vcpkg.sh
export VCPKG_ROOT=/opt/vcpkg

# 配置、构建、测试
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j8
ctest --test-dir build --output-on-failure

# 单个测试
ctest --test-dir build -R "CloudTest" --output-on-failure

# GUI 启动冒烟测试（退出码 124 表示正常）
QT_QPA_PLATFORM=offscreen timeout 6 ./build/bin/FAMP

# Windows PowerShell
.\scripts\bootstrap-vcpkg.ps1
cmake -S src -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake 入口文件为 `src/CMakeLists.txt`（根目录 `CMakeLists.txt` 只负责转发）。Linux 构建默认使用 `/opt/vcpkg` 作为工具链路径，并指定自定义 triplet `triplets/x64-linux-release.cmake`（仅 release 版本，动态链接）。也可将 vcpkg 放在别处并通过 `-DVCPKG_ROOT=` 指定。依赖项（Qt、VTK、PCL、PROJ）通过 `vcpkg.json` 清单模式管理。

单元测试使用 GoogleTest，由 `ctest` 运行。构建时需 `-DBUILD_TESTING=ON`（CI 默认开启）。CI 在 Ubuntu 22.04 和 Windows 上分别构建、测试并打包可运行制品。

## 架构

### 分层结构（v0.9.0+，已重构）

源码按依赖方向从低到高分为六层。`presentation/app → application → domain/core` 为正向依赖；`infrastructure` 提供 IO 和外部服务实现。详细边界规则见 `docs/architecture/SOURCE_LAYOUT.md`。

```text
src/
├── app/                    # 进程入口、版本模板、VTK/OpenGL 初始化
├── core/{tasks,text}/      # 业务无关基础设施：任务管理、取消、字符串转换
├── domain/                 # 领域模型与纯计算，不依赖 QWidget
│   ├── cloud/              # 点云、属性、坐标、显示设置、图层
│   ├── archaeology/        # 考古元数据、控制点
│   ├── measurement/        # 二维/三维测量
│   └── analysis/{terrain,profile,cutfill}/  # DEM、剖面、挖填方分析
├── application/            # 用例编排，不包含具体窗口布局
│   ├── processing/         # 点云裁剪、投影、预处理、ICP 配准、重投影、处理方案
│   └── workspace/          # 统一工作区实体、存储、快照、渲染/写出注册表
├── infrastructure/         # 外部格式 IO 和系统服务
│   ├── cloud_io/           # PCD/LAS/PLY/XYZ 加载器
│   ├── filesystem/         # 文件读写、最近文件
│   ├── persistence/        # .famp 项目、DEM/剖面/挖填方 IO
│   ├── geospatial/         # PROJ CRS 服务
│   └── reporting/          # 考古报告生成
└── presentation/           # Qt/VTK 界面层
    ├── shell/              # MainWindow、FAMPController（信号/槽调度）
    ├── entity_tree/        # 内容树 Qt Model/View 适配
    ├── dialogs/            # 切割、出图模板、地形、剖面、挖填方等对话框
    ├── viewport3d/         # VTK 3D 渲染、点云管理、投影管理、平面控件
    └── canvas2d/           # 2D 制图画布、图形项、导出、米格纸
```

### CMake 模块

每个子目录编译为一个 OBJECT 库，最终聚合为 `famp::runtime` 静态库，由 `FAMP` 可执行文件和测试链接：

| CMake 目标 | 主要目录 |
| --- | --- |
| `famp::core` | `core/` |
| `famp::domain` | `domain/` |
| `famp::processing` | `application/processing/` |
| `famp::workspace` | `application/workspace/` |
| `famp::cloud_io` | `infrastructure/cloud_io/` |
| `famp::filesystem` | `infrastructure/filesystem/` |
| `famp::persistence` | `infrastructure/persistence/` |
| `famp::geospatial` | `infrastructure/geospatial/` |
| `famp::reporting` | `infrastructure/reporting/` |
| `famp::canvas2d` | `presentation/canvas2d/`、`presentation/common/` |
| `famp::viewport3d` | `presentation/viewport3d/` |
| `famp::dialogs` | `presentation/dialogs/` |
| `famp::entity_tree` | `presentation/entity_tree/` |
| `famp::shell` | `presentation/shell/` |

**新增 `.cpp/.h` 文件时必须同步加入 `src/CMakeLists.txt` 对应目标的 `target_sources` 列表。**

### 统一工作区模型

`application/workspace/` 是内容列表和项目持久化的稳定边界：

- **WorkspaceEntity** — 表达项目、组、点云、DEM、等高线、剖面、挖填方、测量和图元。每种类型有固定 `EntityKind`。
- **WorkspaceStore** — 统一维护实体 ID、父子关系、顺序、名称唯一性、显隐、锁定、脏状态和原子批量修改。
- **RendererRegistry / EntityWriterRegistry** — 按实体类型分派 3D 渲染和用户触发的另存操作。
- **WorkspaceSnapshot** — 保存树结构、来源关系和资产引用；大体量 payload 不直接塞入 JSON。
- **EntityTreeModel**（`presentation/entity_tree/`）— 只做 Qt Model/View 适配，不复制业务状态。

**关键规则**：派生点云和分析成果先成为自包含的内存实体（不写文件）。只有用户明确另存、保存项目或自动恢复时才物化。这一规则避免处理函数把临时路径变成隐式业务状态。

### 数据流

1. 用户打开点云文件 → `CloudLoader`/`PcdLoader`/`LasLoader` 读入 PCL 点云 → 加入 `WorkspaceStore`
2. 点云同时在 3D 视口（`VTKRenderManager`/`VTKPointCloudManager`）和 2D 画布投影中显示
3. 平面切割/预处理/裁剪/ICP/重投影 → 生成内存派生点云 → 作为同级实体加入树
4. 制图投影（XOY/XOZ/YOZ/俯视）→ 临时 VTK 预览 → 用户确认后自动绘图生成 2D 图元
5. 2D 绘图以指定比例尺渲染为 `MyItem` 图形对象，支持移动、旋转、组合、图层排序
6. 最终通过 `GraphicsExport` 导出为 PDF/SVG/PNG/BMP

### 核心算法

- **Alpha Shape** — 从投影点云中提取边界
- **有序排序** — 将无序投影点重新排序以生成折线
- **Douglas-Peucker（DP）** — 线条简化
- **B 样条** — 通过控制点反求与插值进行曲线拟合

## 关键约定

### 新代码归属检查

1. 算法是否能脱离 QWidget 运行？→ 放入 `domain/` 或 `application/`，先写单元测试
2. 是否接触文件格式、PROJ 或报告输出？→ 放入 `infrastructure/`
3. 是否只处理 Qt/VTK 交互和展示？→ 放入 `presentation/` 对应子目录
4. 是否产生用户可管理的成果？→ 定义 `EntityKind`，注册 renderer/writer，补项目往返测试
5. 是否默认创建文件？→ 除显式导出和项目物化外，处理结果应先驻留内存

### 头文件引用风格
- 所有头文件使用 `#pragma once`（不使用传统 include 守卫）
- 项目内头文件使用双引号（`"MyVTK.h"`），系统/框架头文件使用尖括号（`<QWidget>`）
- 新代码统一使用 `<QWidget>` 风格（`#include` 后带空格）

### 注释与编码
- 代码注释使用中文
- 含有中文字符串字面量的文件需使用 `#pragma execution_character_set("utf-8")`
- 文件编码为 UTF-8

### Qt 模式
- 所有 `QObject` 子类均使用 `Q_OBJECT` 宏
- UI 文件存放于 `ui/`（Qt Designer 生成的 `.ui`），由 `AUTOUIC` 自动处理
- 资源文件存放于 `resources/`（`.qrc`），由 `AUTORCC` 自动处理；新增图片时同时加入 `resources/images/` 和 `resources/res.qrc`
- MOC 由 `AUTOMOC` 自动处理
- Qt 资源路径格式：`:/images/images/name.png`

### VTK 版本兼容
- `MyVTK.h` 中定义了 `FAMP_QVTK_WIDGET` 宏：VTK ≥9 时为 `QVTKOpenGLNativeWidget`，<9 时为 `QVTKWidget`。引用 VTK 控件基类时请始终使用此宏。

### 编码风格
- 4 空格缩进，函数左花括号单独成行
- 类名 PascalCase（`MainWindow`、`VTKRenderManager`），方法和变量 camelCase
- 测试命名：`SuiteName, BehaviorName`（如 `CloudTest, CentroidOfUnitCube`）
- 浮点几何断言使用 `EXPECT_NEAR`
- 提交使用祈使句标题（如 `Add cloud centroid tests`）

### 数据结构
- 使用轻量级结构体存储纯数据，typedef 枚举定义常量
- `ProjectType`：XOY、XOZ、YOZ、OLXOY、XOYLine、NONE
- `ScaleType`：OneToTen、OneToTwenty、OneToFifty、OneToHundred

### 点云处理管线
- 输入：LAS/LAZ（通过 `lastools_compat` 静态库）、PCD、PLY、XYZ
- 处理：PCL（`pcl::PointCloud<pcl::PointXYZRGB>::Ptr`）
- 3D 渲染：VTK actor/mapper
- 2D 渲染：QGraphicsItem 子类（`MyItem`、`CompassItem`、`FormTabulationItem`、`ContourItem`、`MeasurementItem`）
- 项目文件：`.famp`（JSON，当前 schema v4）

### 依赖说明
- LAStools 源码位于 `third_party/lastools/`，由 CMake 直接编译为静态库 `lastools_compat`（C++14），不由 vcpkg 获取
- 仅启用 release 构建（自定义 triplet 无 debug 配置）
- 版本号在 `cmake/FampVersion.cmake` 中定义，配置时生成 `Version.h`；发布时同步更新 `vcpkg.json` 中的 `version-string`
- 不要提交 `build/`、`install/`、vcpkg 缓存或 IDE 产物
