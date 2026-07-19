# FAMP 开发任务进度

- 更新时间：2026-07-19
- 当前稳定版本：`v1.0.0`
- 当前开发分支：`main`
- 最近完成目标：源码工业化分层、CloudCompare 风格内容树和默认内存成果

本文区分“已经正式发布”“已经编码并完成局部验证”和“尚未完成”三种状态。只有经过完整 Release 构建、全量测试、GUI 启动验收、代码复审并上传 GitHub 的功能，才标记为正式完成。

> 路径说明：历史版本章节中的 `src/Foo.cpp` 表示当时的逻辑模块名。当前物理文件已迁入分层目录，准确位置和新增代码归属以 `docs/architecture/SOURCE_LAYOUT.md` 为准。

## 状态总览

| 批次 | 主要目标 | 当前状态 | GitHub 状态 |
| --- | --- | --- | --- |
| v0.1.0 | 可复现构建、版本号、测试和跨平台 CI 基线 | 已完成并发布 | 已发布 `v0.1.0` |
| v0.1.1 | 帮助、最近文件、拖放、旋转和原子文件写入 | 已完成并发布 | 已发布 `v0.1.1` |
| v0.2.0 | 项目文件、CRS、撤销/重做和专业成果导出 | 已完成并发布 | 已发布 `v0.2.0` |
| v0.3.0 | 后台点云加载、二维测量、显示设置和预处理 | 已完成并发布 | 已发布 `v0.3.0` |
| v0.3.1 | SVG/打印、项目 v2、坐标查看和工程属性撤销 | 已完成并发布 | 已发布 `v0.3.1` |
| v0.4.0 | 任务取消、完整测量、裁剪、着色和处理方案 | 已完成并发布 | 已发布 `v0.4.0` |
| v0.5.0 | 考古报告、ICP 配准和 PLY/XYZ 输入 | 已完成并发布 | 已发布 `v0.5.0` |
| v0.5.1 | 配准鲁棒性、双精度 PCD 空间参考和严格校验 | 已完成并发布 | 已发布 `v0.5.1` |
| v0.5.2 | 修复中央三维点云测量交互 | 已完成并发布 | PR #40，已发布 `v0.5.2` |
| v0.6.0 | 补齐项目、图层、属性、坐标和考古记录基础能力 | 已完成并发布 | PR #41，已发布 `v0.6.0` |
| v0.7.0 | DEM、等高线、地形成果和二维画布集成 | 已完成并发布 | PR #42，已发布 `v0.7.0` |
| v0.8.0 | 点云高程剖面分析 | 已完成并发布 | PR #43，已发布 `v0.8.0` |
| v0.9.0 | 挖填方与体积分析 | 已完成并发布 | 已发布 `v0.9.0` |
| v1.0.0 | 源码分层、统一内容树、内存派生成果、项目 schema v4 和 v0.2.0+ 全面验收 | 已完成并发布 | 已发布 `v1.0.0` |

## 早期已发布版本的具体功能模块

本节记录 v0.1.0–v0.5.1 的具体完成内容。“当前主要代码”列中的平铺路径保留发布时的历史称呼，不代表当前物理路径；当前源码已按层次迁移，完整映射见 `docs/architecture/SOURCE_LAYOUT.md`。

### v0.1.0：工程化和跨平台发布基线

| 功能模块 | 具体完成内容 | 当前主要代码或配置 |
| --- | --- | --- |
| CMake 构建 | 建立 C++17/Qt/VTK/PCL 的可复现 Release 构建；根目录入口转发到 `src/` | `CMakeLists.txt`、`src/CMakeLists.txt` |
| vcpkg 依赖 | 使用 manifest 管理 Qt、VTK、PCL、PROJ 等依赖，并区分跨平台 triplet | `vcpkg.json`、`vcpkg-configuration.json`、`triplets/` |
| 统一版本号 | CMake 配置生成 C++ 版本头，窗口标题和 Qt 应用元数据使用同一版本 | `cmake/FampVersion.cmake`、`src/Version.h.in`、`src/main.cpp` |
| 自动化测试 | 接入 GoogleTest 和 CTest，形成后续模块测试入口 | `tests/CMakeLists.txt`、`tests/test_main.cpp` |
| Linux/Windows CI | 自动配置、Release 编译、测试和压缩包上传 | `.github/workflows/ci.yml` |
| 应用图标 | Linux 桌面文件和 Windows 可执行文件使用统一 FAMP 图标 | `resources/`、`src/CMakeLists.txt` |
| 构建文档 | README 提供 Linux/Windows 从零构建、运行和排障命令 | `README.md` |

### v0.1.1：基础交互和文件可靠性

| 功能模块 | 具体完成内容 | 当前主要代码 |
| --- | --- | --- |
| 离线帮助 | 增加快速入门、快捷键和版本信息对话框 | `src/HelpContent.h`、`src/MainWindow.cpp` |
| 最近文件 | 记录最近 8 个有效点云；过滤不存在、重复或不支持的路径 | `src/RecentFiles.cpp`、`src/RecentFiles.h` |
| 拖放打开 | 支持把 PCD/LAS 文件拖入主窗口并进入统一加载流程 | `src/MainWindow.cpp`、`src/CloudLoader.cpp` |
| 图元旋转 | 菜单、工具栏和快捷键对选中二维图元执行每次 5° 旋转 | `src/GraphicsItemTransform.cpp`、`src/MyGraphicsView.cpp` |
| 原子输出 | BMP 和裁剪 PCD 使用临时文件提交，失败时不破坏原成果 | `src/FileIO.cpp`、`src/QDlgClip.cpp` |
| LAS 解耦 | LAS 读取从主窗口抽离，加入有界预分配和非有限点过滤 | `src/LasLoader.cpp`、`src/LasLoader.h` |
| Unicode 路径 | 改善 PCD、LAS 和导出文件在中文路径下的读取与写入 | `src/PcdLoader.cpp`、`src/LasLoader.cpp`、`src/FileIO.cpp` |
| 输入拒绝 | 无效、空、不存在、不可读或不支持文件不再进入渲染流程 | `src/CloudLoader.cpp`、`src/MainWindow.cpp` |

### v0.2.0：项目、坐标系和专业导出

| 功能模块 | 具体完成内容 | 当前主要代码 |
| --- | --- | --- |
| `.famp` 项目 | 新建、打开、原子保存和另存为；记录点云相对路径、比例尺和项目 CRS | `src/ProjectDocument.cpp`、`src/MainWindow.cpp` |
| 自动备份恢复 | 每 60 秒保存恢复文件；异常退出后可恢复；关闭或切换项目前提示保存 | `src/MainWindow.cpp` |
| CRS 管理 | 验证 EPSG、设置/清除项目 CRS、显示 CRS 状态 | `src/CrsService.cpp`、`src/MainWindow.cpp` |
| 坐标转换器 | 使用 PROJ 在两个有效 CRS 之间转换单个坐标点 | `src/CrsService.cpp`、`src/MainWindow.cpp` |
| 撤销/重做 | 支持最近 100 步图元新增、删除、清空、移动、旋转、层级和字体操作 | `src/GraphicsUndoCommands.cpp`、`src/MyGraphicsView.cpp` |
| 专业成果导出 | 支持 A4/A3、横向/纵向、150/300/600 DPI，以及 PDF/PNG/BMP | `src/GraphicsExport.cpp`、`src/MyGraphicsView.cpp` |
| 比例尺输出 | 可保持当前物理制图比例尺或自动适合页面 | `src/GraphicsExport.cpp`、`src/MetricScale.h` |
| 项目严格校验 | 原子保存、有界数组、严格 JSON 类型和 schema 校验 | `src/ProjectDocument.cpp` |
| 离线 PROJ 数据 | CI 成品携带 `proj.db`，禁止网络时仍可查询和转换 EPSG | `.github/workflows/ci.yml`、`src/CrsService.cpp` |

### v0.3.0：后台点云工作流和二维分析

| 功能模块 | 具体完成内容 | 当前主要代码 |
| --- | --- | --- |
| 后台加载队列 | PCD/LAS 读取、去中心化和项目批量恢复不再阻塞主界面 | `src/CloudLoader.cpp`、`src/MainWindow.cpp` |
| 批量进度 | 拖放多个文件时逐文件显示进度并集中汇总失败原因 | `src/MainWindow.cpp` |
| 二维距离测量 | 按当前物理比例尺把场景长度换算为米等实际单位 | `src/Measurement.cpp`、`src/MeasurementItem.cpp` |
| 二维面积测量 | 计算多边形面积并输出平方米等实际单位 | `src/Measurement.cpp`、`src/MyGraphicsView.cpp` |
| 测量撤销 | 测量图元进入统一撤销/重做历史并随成果导出 | `src/GraphicsUndoCommands.cpp`、`src/MyGraphicsView.cpp` |
| 点云显示设置 | 调整点大小、透明度，切换 RGB 或统一颜色 | `src/CloudDisplaySettings.cpp`、`src/VTKPointCloudManager.cpp` |
| 体素降采样 | 后台执行体素滤波并原子另存为新 PCD | `src/CloudProcessing.cpp` |
| 离群点去噪 | 后台执行统计离群点滤波并加入项目 | `src/CloudProcessing.cpp`、`src/MainWindow.cpp` |

### v0.3.1：完整工程保存和坐标查看

| 功能模块 | 具体完成内容 | 当前主要代码 |
| --- | --- | --- |
| SVG 导出 | 专业成果输出新增 SVG，保持完整画布和物理比例尺 | `src/GraphicsExport.cpp` |
| 打印预览 | PDF/图像/SVG 保存前可打开打印预览 | `src/GraphicsExport.cpp`、`src/MainWindow.cpp` |
| 自定义纸张 | 除 A4/A3 外支持用户输入纸张尺寸 | `src/GraphicsExport.cpp` |
| 项目 schema v2 | 保存原始坐标、双精度变换、文件元数据、可见性、二维图元、测量、网格和窗口布局 | `src/ProjectDocument.cpp`、`src/GraphicsSceneDocument.cpp` |
| v1 兼容迁移 | 继续读取旧项目并为缺失字段提供安全默认值 | `src/ProjectDocument.cpp` |
| 点云重定位 | 项目源点云移动后可交互选择新路径 | `src/MainWindow.cpp`、`src/ProjectDocument.cpp` |
| 组合/打散撤销 | 图元组合和打散进入撤销栈，并保持子图元场景变换 | `src/GraphicsUndoCommands.cpp` |
| 局部/真实坐标查看 | 使用双精度原点和可逆 4×4 矩阵双向转换点坐标 | `src/CloudCoordinates.cpp`、`src/MainWindow.cpp` |
| 工程属性撤销 | 比例尺和项目 CRS 修改进入中央撤销栈并联动项目脏状态 | `src/GraphicsUndoCommands.cpp`、`src/MainWindow.cpp` |

### v0.4.0：可取消处理、完整测量和处理方案

| 功能模块 | 具体完成内容 | 当前主要代码 |
| --- | --- | --- |
| 加载取消 | 停止当前文件和剩余队列，同时保留此前成功加载的点云 | `src/TaskCancellation.h`、`src/CloudLoader.cpp`、`src/MainWindow.cpp` |
| 预处理取消 | 取消后不集成候选结果、不写入目标文件 | `src/CloudProcessing.cpp` |
| 多段线距离 | 连续选择多个节点后完成并计算累计长度 | `src/Measurement.cpp`、`src/MyGraphicsView.cpp` |
| 面积和周长 | 一个多边形测量同时输出面积和周长 | `src/Measurement.cpp`、`src/MeasurementItem.cpp` |
| 三点夹角 | 通过三个场景点计算夹角并持久化 | `src/Measurement.cpp`、`src/MyGraphicsView.cpp` |
| 高程渐变 | 按局部 Z 自动或手动范围着色，并可切回 RGB/统一颜色 | `src/CloudDisplaySettings.cpp`、`src/VTKPointCloudManager.cpp` |
| 范围裁剪 | 按局部 X/Y/Z 包围范围保留内部或外部点 | `src/CloudCrop.cpp`、`src/QDlgClip.cpp` |
| 裁剪安全 | 后台可取消、过滤非有限点、成功后原子另存 PCD | `src/CloudCrop.cpp`、`src/FileIO.cpp` |
| 处理方案 | 保存/载入版本化 `.famp-process.json`，记录操作参数 | `src/ProcessingRecipe.cpp` |
| 自动旁车 | 成功输出自动保存源文件路径、大小、修改时间和处理参数 | `src/ProcessingRecipe.cpp`、`src/MainWindow.cpp` |

### v0.5.0：报告、配准和更多点云格式

| 功能模块 | 具体完成内容 | 当前主要代码 |
| --- | --- | --- |
| 考古项目报告 | 生成 PDF/HTML，汇总项目 CRS、比例尺、点云、原点、可见性和二维测量 | `src/ArchaeologyReport.cpp`、`src/MainWindow.cpp` |
| 报告原子写入 | PDF/HTML 失败时不覆盖原报告 | `src/ArchaeologyReport.cpp`、`src/FileIO.cpp` |
| ICP 配准 | 选择源/目标点云，设置迭代、距离和收敛参数 | `src/CloudRegistration.cpp`、`src/MainWindow.cpp` |
| 配准输出 | 原子保存新 PCD，输出适应度和 4×4 刚体变换矩阵 | `src/CloudRegistration.cpp` |
| 配准取消 | ICP 长任务可安全取消且不产生部分成果 | `src/CloudRegistration.cpp`、`src/TaskCancellation.h` |
| PLY 输入 | 支持有色或无色顶点、Unicode 路径和非有限点过滤 | `src/AdditionalCloudLoader.cpp` |
| XYZ 输入 | 支持 `x y z`、`x y z r g b` 和空格/逗号/分号分隔 | `src/AdditionalCloudLoader.cpp` |
| 统一加载体验 | PLY/XYZ 支持后台加载、取消、拖放和最近文件 | `src/CloudLoader.cpp`、`src/MainWindow.cpp` |

### v0.5.1：配准和空间参考鲁棒性

| 功能模块 | 具体完成内容 | 当前主要代码 |
| --- | --- | --- |
| ICP 体素加速 | 配准前可选体素降采样，降低大点云耗时和内存 | `src/CloudRegistration.cpp` |
| 伪收敛拒绝 | 无有效重叠、非有限变换或无效输出不会被报告为成功 | `src/CloudRegistration.cpp` |
| PCD 空间注释 | FAMP 输出的局部坐标 PCD 嵌入双精度原点和 4×4 变换 | `src/CloudCoordinates.cpp`、`src/PcdLoader.cpp` |
| 空间参考恢复 | 重新打开预处理、裁剪或配准 PCD 时恢复真实坐标 | `src/PcdLoader.cpp`、`src/CloudLoader.cpp` |
| PCD 非有限过滤 | 加载时过滤 `NaN/Inf`，避免无效点进入渲染和分析 | `src/PcdLoader.cpp` |
| 报告汇总增强 | 增加点云数量、点数和测量汇总，并在生成前校验空间参考 | `src/ArchaeologyReport.cpp` |
| 项目数值校验 | 整数转换前拒绝非有限、非整数和超范围文件元数据 | `src/ProjectDocument.cpp` |

## 已完成并发布的功能

### v0.5.2：三维点云测量交互修复

- “距离”“面积”“角度”工具已经连接中央 VTK 点云视图，不再只对二维画布生效。
- 支持在可见点云上进行真实点拾取。
- 拾取过程中实时显示节点、连线和结果标签。
- 右键可完成当前测量，Esc 可取消。
- 距离、面积、周长和角度结果会写入状态栏及控制台。
- 三维测量关联来源图层与 CRS，可保存到项目和考古报告。
- 测量新增、清除支持撤销和重做；隐藏点云只隐藏标注，不删除记录。

当前代码模块映射：

| 子模块 | 模块职责 | 当前主要代码 |
| --- | --- | --- |
| VTK 测量交互 | 点拾取、悬停预览、节点/连线/标签、右键完成和 Esc 取消 | `src/MyVTK.cpp`、`src/MyVTK.h` |
| 三维测量计算 | 三维折线长度、任意朝向多边形面积/周长、三点夹角和格式化 | `src/Measurement.cpp`、`src/Measurement.h` |
| 主窗口编排 | 工具菜单状态、测量模式切换、结果写入状态栏和控制台 | `src/MainWindow.cpp`、`src/MainWindow.h` |
| 项目持久化 | 保存测量 ID、类型、点、来源图层和 CRS | `src/ProjectDocument.cpp` |
| 报告输出 | 将三维距离、面积、周长和角度写入 HTML/PDF | `src/ArchaeologyReport.cpp` |
| 回归测试 | 测量数值、记录校验、项目往返和报告内容 | `tests/test_measurement.cpp`、`tests/test_project_document.cpp`、`tests/test_archaeology_report.cpp` |

### v0.6.0：项目基础能力补全

- 建立统一的 `CloudLayer` 图层模型和稳定图层 ID。
- 建立类型安全的逐点属性容器，支持浮点、有符号整数和无符号整数属性。
- 支持 LAS/LAZ 强度、分类、回波、扫描角、用户数据、点源 ID 和 GPS 时间属性。
- 支持逐点属性渐变着色及自动、手动色带范围。
- 支持 PCD 自定义属性、Unicode 名称和单位的精确保存与读取。
- 支持所选点云整云 CRS 重投影，使用双精度真实坐标并原子另存 PCD。
- 支持考古图层属性、控制点管理和 3D 刚体空间配准。
- 支持逐点残差、RMSE、平均残差、最大残差等配准质量指标。
- 项目格式升级到 schema v3，保存图层、显示、属性、控制点、测量和空间参考。
- HTML/PDF 考古报告可以输出图层属性、控制点和三维测量成果。
- 引入可取消任务状态机，点云加载和长任务可以安全取消。

当前代码模块映射：

| 子模块 | 模块职责 | 当前主要代码 |
| --- | --- | --- |
| 图层模型 | 稳定 ID、名称、CRS、空间参考、锁定/可见状态和显示配置 | `src/CloudLayer.cpp`、`src/CloudLayer.h` |
| 逐点属性 | 类型安全存储、大小校验、范围摘要、Unicode 名称和单位 | `src/CloudAttributes.cpp`、`src/CloudAttributes.h` |
| LAS/LAZ 属性读取 | 解压 LAZ 并读取强度、分类、回波、扫描角、用户数据、点源和 GPS 时间 | `src/LasLoader.cpp`、`third_party/lastools/` |
| PCD 属性往返 | 保存/读取版本化 FAMP 注释、类型信息、原点、变换和逐点属性 | `src/PcdLoader.cpp`、`src/CloudCoordinates.cpp` |
| 属性着色 | 仅为当前选中属性创建 VTK 标量数组，支持自动/手动色带 | `src/CloudDisplaySettings.cpp`、`src/VTKPointCloudManager.cpp` |
| CRS 重投影 | PROJ 逐点转换、重新中心化、属性保留、原子 PCD 输出 | `src/CloudReprojection.cpp`、`src/CrsService.cpp` |
| 考古属性 | 标准字段、自定义字段、重复/长度校验和编辑对话框 | `src/ArchaeologyMetadata.cpp`、`src/ArchaeologyMetadataDialog.cpp` |
| 控制点配准 | 控制点启用、非共线检查、刚体解算和逐点残差统计 | `src/ControlPoints.cpp`、`src/ControlPointDialog.cpp` |
| 任务状态机 | 创建、进度、成功、失败、取消和跨线程状态同步 | `src/TaskManager.cpp`、`src/TaskCancellation.h` |
| 项目 schema v3 | 图层、属性、考古记录、控制点、三维测量和迁移兼容 | `src/ProjectDocument.cpp` |
| 报告集成 | 按图层输出考古属性、控制点质量和三维测量 | `src/ArchaeologyReport.cpp` |
| 回归测试 | 图层、属性、加载、显示、重投影、控制点、任务、项目和报告 | `tests/test_cloud_layer.cpp`、`tests/test_cloud_attributes.cpp`、`tests/test_cloud_reprojection.cpp`、`tests/test_control_points.cpp`、`tests/test_task_manager.cpp` |

### v0.7.0：DEM 与等高线

- 从所选点云的双精度真实坐标生成 DEM。
- 支持自动分辨率和手动分辨率。
- 支持最低值、最高值、平均值和中位数四种单元高程统计。
- 支持填补不超过 3 个连通单元的封闭 NoData 小空洞。
- 支持自动或手动等高距和基准高程。
- 支持 0–3 次等高线平滑。
- 地理经纬度 CRS 会被明确拒绝，必须先重投影到投影坐标系。
- 未声明 CRS 时必须由用户确认坐标是本地米制平面坐标。
- 支持版本化 `.famp-dem` 边车。
- 支持原子导出 ESRI ASCII Grid、DEM CSV、等高线 CSV 和 SVG。
- 等高线可以作为可移动、可撤销图元加入二维画布并保存到项目。
- 地形分析、等高线生成和长文件写入均支持取消。

当前代码模块映射：

| 子模块 | 模块职责 | 当前主要代码 |
| --- | --- | --- |
| DEM 网格分析 | 分辨率估算、单元聚合、NoData 小空洞填补和双精度坐标转换 | `src/TerrainAnalysis.cpp`、`src/TerrainAnalysis.h` |
| 等高线生成 | 网格三角剖分、层级求交、线段拼接、安全上限和 Chaikin 平滑 | `src/TerrainAnalysis.cpp` |
| 地形参数界面 | 分辨率、统计、空洞、等高距、基准、平滑和输出选择 | `src/TerrainDialog.cpp`、`src/TerrainDialog.h` |
| 地形成果 IO | `.famp-dem` schema v1、ASC、网格 CSV、等高线 CSV/SVG 原子读写 | `src/TerrainIO.cpp`、`src/TerrainIO.h` |
| 二维等高线图元 | 相对坐标绘制、真实坐标元数据、比例尺重绘和数据校验 | `src/ContourItem.cpp`、`src/ContourItem.h` |
| 场景持久化 | 二维场景 schema v2 保存/读取等高线图元并兼容 schema v1 | `src/GraphicsSceneDocument.cpp` |
| 主窗口工作流 | CRS/单位检查、后台分析、进度/取消、覆盖确认、画布加入和结果汇总 | `src/MainWindow.cpp` |
| 回归测试 | 网格统计、空洞、等高线、对话框、边车、导出和图元往返 | `tests/test_terrain_analysis.cpp`、`tests/test_terrain_dialog.cpp`、`tests/test_terrain_io.cpp`、`tests/test_contour_item.cpp` |

### v0.8.0：点云高程剖面

- 在中央三维视图中交互拾取剖面起点和终点。
- 只允许在当前所选可见点云上拾取，避免跨图层错误。
- 实时显示橙色基线预览，Esc 或右键可取消。
- 通过 VTK 点 ID 恢复点云局部坐标，再转换为双精度真实坐标。
- 拾取期间会监控点云数据、空间参考和 CRS；状态发生变化时自动取消。
- 支持自定义完整走廊宽度、采样间隔和每段最少点数。
- 支持最低值、最高值、平均值和中位数代表高程。
- 保存每段点数、最低/最高包络、沿线里程和带符号横向偏距。
- 空白采样段保持断开，不跨数据缺口伪造连线。
- 支持版本化 `.famp-profile` 边车。
- 支持原子导出采样段 CSV、原始走廊点 CSV 和 SVG。
- 程序内提供可处理大量采样段的降采样剖面预览。
- 设置 25 万采样段和 200 万走廊点安全上限。
- 分析、导出和长文件写入均支持取消且不会破坏已有文件。
- 发布前已完成干净 Release 构建、211/211 全量测试和 GUI 启动冒烟。

当前代码模块映射：

| 子模块 | 模块职责 | 当前主要代码 |
| --- | --- | --- |
| 剖面点拾取 | 目标图层约束、VTK 点 ID、橙色基线预览、完成/取消和状态失效检查 | `src/MyVTK.cpp`、`src/MyVTK.h` |
| 剖面走廊分析 | 双精度坐标、里程/偏距、分段、四种统计、包络、空白段和安全上限 | `src/ProfileAnalysis.cpp`、`src/ProfileAnalysis.h` |
| 参数与结果界面 | 走廊宽度、间隔、最少点数、统计、输出选项和降采样剖面图 | `src/ProfileDialog.cpp`、`src/ProfileDialog.h` |
| 剖面成果 IO | `.famp-profile` schema v1、采样段 CSV、原始点 CSV 和 SVG 原子读写 | `src/ProfileIO.cpp`、`src/ProfileIO.h` |
| 主窗口工作流 | CRS/单位检查、拾取状态快照、覆盖确认、后台任务、取消和结果汇总 | `src/MainWindow.cpp`、`src/MainWindow.h` |
| 回归测试 | 走廊几何、统计、空白段、上限、取消、对话框、边车损坏和导出 | `tests/test_profile_analysis.cpp`、`tests/test_profile_dialog.cpp`、`tests/test_profile_io.cpp` |

## v0.9.0：挖填方与体积分析

- 在“工具”菜单增加“挖填方与体积…”，仅在选中未锁定点云且没有加载任务时启用。
- 支持当前地表与固定设计高程比较，也支持与已有 `.famp-dem` 参考地表比较。
- 高差统一定义为“当前地表 - 参考地表”：正值为挖方、负值为填方；挖方和填方分别以正体积报告，净体积为“挖方 - 填方”。
- 固定高程模式支持自动或手动分辨率；参考 DEM 模式在后台读取参考文件，并强制使用参考网格分辨率生成当前 DEM。
- 支持最低、最高、平均和中位数四种单元高程统计，以及最多 3 格封闭 NoData 小空洞填补。
- 严格校验当前/参考 CRS、坐标单位、分辨率、整数网格原点对齐和重叠范围；参考 NoData 或范围外单元单独统计，不进行隐式插值。
- 分别统计挖方、填方和平衡区的单元数与面积，计算挖方、填方、净体积及最小/最大高差。
- 经度/纬度 CRS 会被拒绝；未声明 CRS 时必须确认真实 X/Y/Z 使用同一本地米制单位。
- 后台任务支持进度和取消，必需主成果失败时任务失败，可选交换文件失败时保留主成果并集中提示。
- 程序内结果窗口显示汇总数值和红色挖方、蓝色填方、灰色平衡区概览；大网格自动降采样。
- 支持版本化 `.famp-volume` schema v1，以及汇总 CSV、逐格 CSV 和 SVG 原子导出。

当前代码模块映射：

| 子模块 | 模块职责 | 当前主要代码 |
| --- | --- | --- |
| 点云到 DEM 复用 | 应用双精度原点和完整 4×4 变换，生成可取消、原子提交的当前地表网格 | `src/TerrainAnalysis.cpp`、`src/TerrainAnalysis.h` |
| 挖填核心计算 | 固定高程/参考 DEM、分类、面积体积、单位换算、上限、取消和一致性复算 | `src/CutFillAnalysis.cpp`、`src/CutFillAnalysis.h` |
| 参数与结果界面 | 参考方式、网格参数、容差、输出选择、结果汇总和降采样分类图 | `src/CutFillDialog.cpp`、`src/CutFillDialog.h` |
| 成果持久化 | `.famp-volume` schema v1、严格读取、汇总/逐格 CSV 和降采样 SVG | `src/CutFillIO.cpp`、`src/CutFillIO.h` |
| 主窗口工作流 | 工具状态、CRS/单位检查、覆盖确认、后台任务、取消、保存和结果提示 | `src/MainWindow.cpp`、`src/MainWindow.h` |
| 用户文档 | 操作步骤、输入约束、正负号、输出格式、版本说明和离线帮助 | `README.md`、`CHANGELOG.md`、`src/HelpContent.h` |
| 回归测试 | 计算、单位、对齐、NoData、篡改、取消、界面、IO、降采样和端到端往返 | `tests/test_cut_fill_*.cpp`、`tests/test_terrain_analysis.cpp` |

### v0.9.0 可靠性与边界

- 默认最多计算 1000 万个网格，内部硬上限为 1 亿；超过上限时要求增大分辨率。
- 面积和体积使用 `long double` 累加，转为 `double` 前检查非有限值和溢出。
- 成果读取会复算有效/NoData/挖/填/平衡计数、单元面积、分类面积、挖填体积、净体积和高差范围。
- 固定高程成果会逐格验证“当前高程 - 高差 = 固定参考高程”；参考 DEM 成果会验证参考 CRS 元数据与当前网格一致。
- 单元面积必须等于“分辨率² × 坐标单位到米换算²”，不能通过同时篡改面积和体积字段绕过检查。
- 边车读取限制文本和数组尺寸，拒绝错误标识、未知版本、非法 UTF-8、截断、额外尾部数据和内部不一致。
- 所有成果通过 `QSaveFile` 原子提交，并在最终提交前再次检查取消；取消不会用半成品覆盖已有文件。
- 程序内概览最多 24 万个采样像素，SVG 最多约 25 万个分块，避免超大网格导致界面或矢量文件失控。

### v0.9.0 最终验收记录

验收目录：`/tmp/famp-v090-final-release-localgtest-build`。

```bash
cmake -S src -B /tmp/famp-v090-final-release-localgtest-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_ROOT=/opt/vcpkg \
  -DBUILD_TESTING=ON \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/tmp/famp-v080-release-build/_deps/googletest-src
cmake --build /tmp/famp-v090-final-release-localgtest-build -- -j8
ctest --test-dir /tmp/famp-v090-final-release-localgtest-build --output-on-failure
QT_QPA_PLATFORM=offscreen timeout 6 /tmp/famp-v090-final-release-localgtest-build/bin/FAMP
git diff --check
```

- 全新 Release 配置和完整构建成功。
- `233/233` 项 GoogleTest/CTest 全部通过，失败数为 0。
- 其中 `21/21` 项 `CutFill*` 聚焦测试通过。
- GUI 在当前无可用 X11 显示的环境中使用 Qt offscreen 启动并保持运行，`timeout` 退出码为 `124`。
- 生成的 `Version.h`、CMake 版本和 vcpkg manifest 均为 `0.9.0`。
- 已复审大坐标精度、三轴单位换算、NoData、取消、原子写入、损坏文件、超大网格和结果字符串占位符。

## v1.0.0：统一工作区与内存成果

- `src/` 已从平铺文件迁移到 `app/core/domain/application/infrastructure/presentation`，CMake 使用模块对象库并聚合为 `famp::runtime`。
- 左侧内容列表改为统一类型树，支持项目根、组、点云、DEM、等高线、剖面、挖填方、二维/三维测量和二维图元；中间 VTK 与右侧考古制图画布默认等宽。
- VTK 视口左下角新增长期持有的方向控件，明确显示红 X、绿 Y、蓝 Z 三轴，并随相机旋转；控件使用固定视口比例，不受点云包围盒或缩放影响。
- 主窗口新增常驻六步考古制图导航，按“选择点云 → 点云准备 → 选择制图类型 → 当前投影预览 → 三项自动绘图 → 编辑与 A4 出图”显示当前来源、完成状态和下一步操作。投影预览不累计；俯视、XOZ、YOZ 三项自动绘图必须同时真实存在于右侧画布和左侧“二维制图”目录才解锁第⑥步及二维编辑/A4 导出，删除或清空任一成果会重新锁定；XOY 为可选辅助投影。
- 内容树支持扩展选择、拖拽层级/排序、重命名、递归显隐、锁定、原子删除、定位、属性面板和按类型另存。
- 屏幕米格纸按窗口所在显示器的物理 DPI 自动重算；专业成果导出默认生成最小间隔 1 mm 的物理米格纸，A4/A3 使用精确毫米尺寸，PNG/BMP 米格输出采用 254/508 DPI 的整数像素间隔，PDF/SVG 使用矢量毫米坐标。
- 三向二维成果以俯视图为必须首项和固定锚点自动排版：YOZ 在所有确认方向下始终位于俯视图正上方，XOZ 始终位于正右方。生成 XOZ/YOZ 时从当前选中的原始或切割点云计算剖切位置；若切割点云带有 `plane_clip` 来源信息，则优先使用真实剖切平面原点生成俯视剖面切割线，并对齐剖面成果中心轴。按 1:10、1:20、1:50、1:100 重绘时五项图元关系不变，使用物理 10 mm 间距规避碰撞，超出默认场景时只扩展画布、不改变比例尺。
- 专业成果对话框明确标注“实尺，不缩放”和“自动适合页面（会缩放成果）”，可按 A4/A3/自定义纸张预览；打印路径按设备实际接受的 DPI 计算物理毫米，纸质输出仍要求打印驱动选择“实际大小/100%”。
- 预处理、范围裁剪、平面裁切、ICP 和重投影默认生成内存点云，并作为源点云同级实体紧跟在树中；不再要求先选择输出路径。
- 投影可从内容列表当前选中的任意点云开始，显式区分 XOY、XOZ、YOZ 和俯视类型。投影默认只生成瞬时预览；用户在决策弹窗中选择“加入内容列表”才创建同级点云，选择“自动绘图”则直接生成“二维制图”分组下的图元。
- 投影决策窗为非模态，预览在弹窗显示前完成 VTK 刷新；首次弹出位于主窗口中心，同一会话后续弹出沿用用户上次位置，重启后恢复居中。正文只保留来源、预览类型和点数，“自动绘图”“加入内容列表”“关闭预览”的操作后果分别放入按钮悬停提示。弹窗以按钮、Esc 或标题栏关闭后，VTK 临时投影和自动绘图临时输入立即清除；明确加入内容列表的实体不受影响。裁剪/投影预览使用高对比颜色和更大点径；六步导航使用固定字体与几何尺寸，裁切平面手柄和法向旋转轴按点云平面长宽比自适应。
- 俯视、XOZ、YOZ 每次生成正式二维成果前均提供 0°、顺时针 90°、180°、270° 实时方向预览和确认。四个角度共用全部点云、坐标轴和文字旋转联合边界计算出的中心与缩放比例，并在窗口显示或改变尺寸时重新适配；俯视角度只控制俯视图及其剖切线，XOZ/YOZ 各自保存确认角度，取消确认不生成成果。交互强制“俯视先生成，XOZ/YOZ 后生成”，并以橙色粗体在弹窗和常驻流程栏突出俯视优先；剖面确认后再自动对齐，且不改变 YOZ 在上、XOZ 在右的固定方位。
- 修复选中二维制图线拖拽或旋转时的拖影：去掉投影线、指北针和制图信息绘制过程中的递归 `update()`，禁用画布缓存并将交互刷新设为完整视口更新，扩大包围盒以覆盖选中虚线、线宽和抗锯齿像素。
- 发布前审查进一步修复了切割对话框空指针与重复实例、范围裁剪误隐藏源点云、关闭窗口时 VTK 引用未释放、Windows 耗时文本类型不匹配和异常米格范围可能卡死的问题，并为后两类交互/边界路径新增回归测试。
- 内容列表选择反馈改为单实体联动：点云包围盒默认关闭，用户点击可见点云后只显示当前点云包围盒；点击二维图元会清除全部点云包围盒并在右侧画布显示统一橙色高亮框；三维测量使用增强颜色、线宽和标签高亮。包围盒动作文字保持固定，避免工具栏宽度随状态变化。
- 派生点云保留来源点索引、逐点属性、空间参考、参数、变换和质量指标；ICP 输出使用目标点云的局部坐标框架。
- DEM、等高线、剖面和挖填方默认生成类型化内存成果；只有勾选“立即另存”、从内容树另存、保存项目或写自动恢复时才物化文件。
- 项目格式升级至 schema v4，工作区快照升级至 v2；保存树顺序、稳定 ID、来源关系和相对资产，兼容 v1/v2/v3 项目。
- 二维持久图元使用稳定 UUID；三维测量成为独立内容树实体并可单独显隐、删除和恢复。

### v1.0.0 验收记录

```bash
VCPKG_ROOT=/opt/vcpkg cmake -S src -B /tmp/famp-v100-release-review-20260719 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build /tmp/famp-v100-release-review-20260719 -- -j8
FAMP_ACCEPTANCE_CLOUD="$PWD/build/bin/大墓坑_对齐_sampling_25000.pcd" QT_QPA_PLATFORM=offscreen ctest --test-dir /tmp/famp-v100-release-review-20260719 --output-on-failure
QT_QPA_PLATFORM=offscreen timeout 6 /tmp/famp-v100-release-review-20260719/bin/FAMP
git diff --check
```

- 使用规范入口 `VCPKG_ROOT=/opt/vcpkg cmake -S src` 在空目录 `/tmp/famp-v100-release-review-20260719` 全新配置，Release 全量编译成功，生成版本为 `1.0.0`。
- `313/313` 项 GoogleTest/CTest 全部通过，失败数为 0；通过 `FAMP_ACCEPTANCE_CLOUD` 指向实测云，无测试跳过。
- 用户指定的 `build/bin/大墓坑_对齐_sampling_25000.pcd` 含 25,550 点；`8/8` 项实测/仿真端到端验收全部执行并通过，其中新增实测云三向自动绘图四档比例尺验收。
- 投影计算、状态失效、显式制图类型、弹窗位置记忆、按钮悬停说明、四角度预览边界、三成果独立方向持久化、俯视优先门禁、两条剖面切割线、精确剖切平面复用、固定 YOZ 在上/XOZ 在右对齐、四档比例尺、拖影回归和六步导航共 `18/18` 项聚焦测试通过；新增 VTK 左下角带标签方向轴生命周期与位置测试；`15/15` 项专业导出测试及内容树/工作区各套件通过，二维场景稳定 ID `7/7` 项测试通过。交互测试同时验证点云点击显示包围盒、二维成果点击高亮、三个方向确认框、剖面线入树，以及三项成果齐全后解锁、清空后重新锁定。
- GUI 使用 Qt offscreen 启动并保持 6 秒，`timeout` 退出码为 `124`。
- 逐版本覆盖、已关闭风险和残余人工门禁见 `docs/qa/V0_2_0_PLUS_ACCEPTANCE.md`。

`v1.0.0` 按上述本地门禁提交发布；GitHub 上的 Linux/Windows Release 构建、测试、打包与成品启动状态为最终跨平台依据。

## 发布门禁和后续候选

`v1.0.0` 既定功能范围内没有未完成项。有显示器和实际打印机环境下的 VTK 操作手感、高 DPI 排版和纸张 100 mm 标距属于现场硬件验收，不能由无头自动测试代替。以下属于后续功能候选：

1. 把挖填方色斑作为可开关的三维/二维图层叠加到主场景，而不只在成果窗口和 SVG 中查看。
2. 支持用户绘制或选择多边形边界，只统计指定探方或施工分区，并按分区生成报表。
3. 支持批量对比多个时期 DEM，形成分期土方变化表和时间序列。
4. 增加 GeoTIFF/Cloud Optimized GeoTIFF 交换格式；需要先确定 GDAL 依赖、NoData 和垂直单位策略。
5. 增加可配置的挖填单价、松散/压实系数和工程量清单；需要明确业务口径后再进入实现。

CI 验证策略：Linux 与 Windows 两个 Release 任务均为发布阻塞条件；Windows 还必须完成压缩包重新解压、关键 DLL/PROJ 数据校验，并证明成品程序启动后持续运行 15 秒。
