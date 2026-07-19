# FAMP 源码分层与模块边界

本文定义 `src/` 的当前物理结构、CMake 模块和新增代码的归属规则。规范构建入口仍是 `src/CMakeLists.txt`。

## 分层结构

| 目录 | 职责 | 可以依赖 | 不应包含 |
| --- | --- | --- | --- |
| `src/app/` | 进程入口、版本模板、应用启动 | presentation、application | 业务算法和文件格式实现 |
| `src/core/` | 与业务无关的任务、取消、文本等基础设施 | Qt Core、标准库 | VTK/PCL 界面编排 |
| `src/domain/` | 点云、考古、测量、地形/剖面/挖填方领域模型与纯计算 | core、必要的数值库 | 对话框、主窗口、项目文件写入 |
| `src/application/` | 用例编排、点云处理、统一工作区实体和渲染/写出注册表 | domain、core；通过明确接口使用 infrastructure | QWidget 和具体窗口布局 |
| `src/infrastructure/` | 点云/项目/成果 IO、文件系统、PROJ、报告实现 | domain、core、application 的持久化 DTO | 用户交互状态 |
| `src/presentation/` | Qt/VTK 界面、二维画布、三维视口、内容树和对话框 | application、domain、infrastructure | 可脱离界面测试的核心算法 |

当前物理子模块如下：

```text
src/
├── app/
├── core/{tasks,text}/
├── domain/{cloud,archaeology,measurement,analysis/{terrain,profile,cutfill}}/
├── application/{processing,workspace}/
├── infrastructure/{cloud_io,filesystem,persistence,geospatial,reporting}/
└── presentation/{shell,entity_tree,dialogs,viewport3d,canvas2d,common}/
```

依赖方向以 `presentation/app → application → domain/core` 为主；`infrastructure` 提供外部格式和服务实现。旧代码仍使用部分 basename include 和少量跨层服务调用，`FAMP_SOURCE_INCLUDE_DIRS` 暂时提供迁移兼容。新增代码应使用拥有该类型的模块路径，不要重新把源文件放回 `src/` 根目录。

## CMake 模块

`src/CMakeLists.txt` 将源码拆成以下对象模块，并最终聚合为 `famp::runtime`：

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

增加 `.cpp/.h` 时必须同时加入对应目标的 `target_sources` 列表。测试链接 `famp::runtime`，非 GUI 逻辑继续在 `tests/test_*.cpp` 中按功能建聚焦用例。

## 统一工作区模型

`application/workspace/` 是内容列表和项目持久化的稳定边界：

- `WorkspaceEntity` 表达项目、组、点云、DEM、等高线、剖面、挖填方、二维/三维测量和二维图元。
- `WorkspaceStore` 统一维护稳定 ID、父子关系、顺序、名称唯一性、显隐、锁定、脏状态和原子批量修改。
- `RendererRegistry`、`EntityWriterRegistry` 按实体类型分派显示和用户触发的另存操作。
- `WorkspaceSnapshot` 保存树、来源关系和资产引用；大体量 payload 不直接塞入 JSON。
- `presentation/entity_tree/EntityTreeModel` 只负责 Qt Model/View 适配，不复制业务状态。

派生点云和分析成果先成为自包含的内存实体。只有用户明确另存、保存项目或自动恢复需要物化时，才通过 writer 写出资产。这一规则避免处理函数把临时路径变成隐式业务状态。

## 新代码归属检查

提交新功能前依次确认：

1. 算法是否能脱离 QWidget 运行；能则放入 `domain/` 或 `application/`，并先写单元测试。
2. 是否接触文件格式、数据库、PROJ 或报告输出；是则放入 `infrastructure/`。
3. 是否只处理 Qt/VTK 交互和展示；是则放入 `presentation/` 对应子目录。
4. 是否产生用户可管理的成果；是则定义或复用 `EntityKind`，注册 renderer/writer，并补项目往返测试。
5. 是否默认创建文件；除显式导出和项目物化外，处理结果应先驻留内存。

## 验证入口

```bash
export VCPKG_ROOT=/opt/vcpkg
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j8
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen timeout 6 ./build/bin/FAMP
```

内容树、工作区快照和真实点云验收分别集中在 `test_workspace_store.cpp`、`test_entity_tree_model.cpp`、`test_workspace_snapshot.cpp` 和 `test_release_acceptance_real_cloud.cpp`。
