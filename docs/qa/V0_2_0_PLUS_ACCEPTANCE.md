# FAMP v0.2.0 以上版本功能、风险与验收报告

- 验收日期：2026-07-19
- 验收对象：`v1.0.0`（统一内容树、内存成果、源码分层和考古制图链路）
- 平台：Linux x86_64，Release，Qt/VTK/PCL/vcpkg
- 结论：代码审查、全新 Release 构建、无界面 Linux 启动和全量自动验收通过；正式发布以 Linux/Windows GitHub CI 均成功为门禁，真实桌面与纸质输出保留为现场验收

## 验收输入

实测点云使用用户指定文件：

| 项目 | 值 |
| --- | --- |
| 路径 | `build/bin/大墓坑_对齐_sampling_25000.pcd` |
| SHA-256 | `07f0dd8ae84ba21368f41b7b5b88aca7efb4a0cf641df0dfae29995389ebbf0a` |
| 文件大小 | 409,022 字节 |
| 有效点数 | 25,550 |
| PCD 数据 | binary，`x/y/z/_` 字段 |

该文件用于 Unicode 路径加载、坐标去中心化、体素降采样、统计去噪、范围裁剪、平面/剖面投影、DEM、等高线、剖面和挖填方测试。ICP 测试从实测点云均匀抽样，并施加已知 `0.08/-0.05/0.03` 平移和 `0.01 rad` Z 轴旋转，形成可量化真值的仿真目标云，同时构造低重叠和错误初值负例。逐点属性测试为实测点按原索引构造确定性 `uint64 source_index` 通道。

## 总体验收结果

| 门禁 | 命令或范围 | 结果 |
| --- | --- | --- |
| Release 增量构建 | `cmake --build build -- -j8` | 通过 |
| 全量自动测试 | `ctest --test-dir /tmp/famp-v100-release-review-20260719 --output-on-failure` | `313/313` 通过，0 失败，无跳过 |
| 规范入口全新构建 | `VCPKG_ROOT=/opt/vcpkg cmake -S src -B /tmp/famp-v100-release-review-20260719 ...` | 空目录配置、Release 全量编译通过，生成版本为 `1.0.0` |
| 实测/仿真点云 | `ReleaseAcceptanceRealCloudTest.*` | `8/8` 通过，无跳过；包含实测云三向自动绘图四档比例尺 |
| 内容树与工作区 | `WorkspaceStoreTest.*`、`WorkspaceRegistryTest.*`、`WorkspaceSnapshotTest.*`、`EntityTreeModelTest.*` | 全部通过 |
| 二维场景稳定 ID | `GraphicsSceneDocumentTest.*` | `7/7` 通过 |
| 投影与制图工作流 | `CloudProjectionTest.*`、`ProjectionWorkflowTest.*`、`ProjectionPreviewTest.*`、`ProjectionDraftingInputTest.*`、`ProjectionWorkflowUiTest.*`、六步导航测试 | `18/18` 通过；俯视、XOZ、YOZ 均在正式生成前预览并分别确认 0°/90°/180°/270°，四个角度下点云、坐标轴和文字均保持在预览视口内；俯视完成前 XOZ/YOZ 自动绘图禁用并以橙色粗体提示，完成后两项剖面按各自角度生成切割线并进入“二维制图”目录；切割点云可复用 `plane_clip` 中的真实剖切平面；预览正文精简且三枚按钮提供悬停说明，弹窗结束后临时投影清除；三项成果齐全后才解锁第⑥步，清空后重新锁定；选中曲线绘制不再递归触发场景失效 |
| 三向正交排版 | `ProjectionDraftingInputTest.ArrangesThreeViewsAndPreservesTheirLayoutAtEveryScale`、`KeepsOverlookAtItsInitialAnchorWhileProfilesAreAdded`、`KeepsProfileSidesAndSectionAlignmentAtEveryConfirmedRotation`、`ExpandsCanvasForLargeScaleDrawingsWithoutOverlapOrImplicitScaling`、实测云三向测试 | 俯视图固定在画布中心且必须先生成；YOZ 在 0°/90°/180°/270° 下均位于俯视图正上方，XOZ 均位于正右方。XOZ/YOZ 生成时同步增加俯视剖面切割线，剖面成果与对应切割线中心轴在四档比例尺下均保持对齐；碰撞规避和超大成果画布扩展均通过 |
| 初始窗口布局 | `MainWindowLayoutTest.*` | `3/3` 通过；内容树在左、制图在右，VTK/制图宽度差不超过 20 px，六步导航常驻 |
| VTK 方向标记 | `VTKRenderManagerTest.RetainsLabeledOrientationAxesInLowerLeft` | X/Y/Z 彩色方向轴由渲染管理器长期持有，固定在左下角并随相机旋转 |
| A4/A3 物理米格与预览 | `GraphicsExportTest.ExportsA4RasterGridAtExactlyOneMillimeter`、`ExportsA3RasterGridAtExactlyOneMillimeter`、`ConfiguresA3PrintPreviewWithExactPhysicalPage`、`ExportsA4SvgWithPhysicalMillimeterGrid` | A4 PNG 为 `2970×2100`、A3 PNG 为 `4200×2970 @ 254 DPI`，1 mm 固定 10 px；SVG 使用物理 mm；A3 横向预览页严格为 `420×297 mm` |
| GUI 启动 | `QT_QPA_PLATFORM=offscreen timeout 6 /tmp/famp-v100-release-review-20260719/bin/FAMP` | 保持运行至 timeout，退出码为 `124` |
| 静态风险扫描 | clang analyzer/bugprone/performance 聚焦扫描 | 已修复扫描发现的 2D/3D 空指针路径；Qt `QTimer` 报告为库内分析噪声 |
| 补丁格式 | `git diff --check` | 通过 |

GUI 启动和补丁格式结果在最终工作树上重新执行；若本报告与命令日志不一致，以最终命令结果为准。

## 逐版本功能与风险验收

### v0.2.0：项目、CRS、撤销和专业导出

- 覆盖：`.famp` 原子读写、相对路径、自动恢复基础字段、EPSG/PROJ、二维编辑撤销、PDF/SVG/PNG/BMP 输出和物理比例尺。
- 自动测试：`ProjectDocumentTest`、`CrsServiceTest`、`GraphicsUndoCommandsTest`、`GraphicsExportTest`、`FileIOTest`、`MetricScaleTest`。
- 风险排查：损坏/未知 schema、非法 CRS、非有限数值、Unicode 路径、写入中断和无效比例尺均有拒绝或原子性测试。
- 验收：通过。当前项目 schema 已升级至 v4，并继续覆盖 v1/v2/v3 迁移。

### v0.3.0：后台加载、二维测量、显示和预处理

- 覆盖：PCD/LAS/LAZ/PLY/XYZ 加载、去中心化、取消、二维距离/面积、RGB/统一色/高程/属性着色、体素与统计滤波。
- 自动测试：`CloudLoaderTest`、`AdditionalCloudLoaderTest`、`MeasurementTest`、`CloudDisplaySettingsTest`、`CloudProcessingTest`。
- 风险排查：空点云、NaN/Inf、非法参数、属性长度错配、取消后残留结果、中文路径和输出覆盖。
- 验收：通过。处理核心仍保留显式文件写出 API 供“另存”使用，但主界面默认将结果作为内存点云加入内容树。

### v0.3.1：完整场景、工程保存和坐标查看

- 覆盖：二维图元/测量/等高线场景往返、项目旧版本迁移、局部/真实坐标双向变换、窗口状态和图层空间参考。
- 自动测试：`GraphicsSceneDocumentTest`、`ProjectDocumentTest`、`CloudCoordinatesTest`、`GraphicsItemTransformTest`。
- 风险排查：场景损坏、重复图元 ID、旧文件缺失 ID、奇异矩阵、非有限坐标和重复图层 ID。
- 验收：通过。所有持久图元现在具有稳定 UUID；旧场景自动补 ID，重复 ID 会原子拒绝。

### v0.4.0：取消、完整测量、范围裁剪和处理方案

- 覆盖：加载/处理取消、多段线/面积周长/角度、范围内外裁剪、处理方案往返和高程着色。
- 自动测试：`TaskManagerTest`、`CloudCropTest`、`CloudProcessingTest`、`ProcessingRecipeTest`、`MeasurementTest`。
- 风险排查：锁定状态、空裁剪、边界参数、非有限点、取消后文件/实体残留、方案损坏和未知版本。
- 验收：通过。裁剪结果保留来源索引和逐点属性映射，默认不创建 PCD 或自动旁车。

### v0.5.0：考古报告、ICP 和扩展格式

- 覆盖：HTML/PDF 报告、ICP 参数/取消/原子输出、PLY/XYZ 输入和 Unicode 文件名。
- 自动测试：`ArchaeologyReportTest`、`CloudRegistrationTest`、`AdditionalCloudLoaderTest`、`CloudLoaderTest`。
- 风险排查：无重叠伪收敛、非有限变换、错误点数、格式截断、HTML 转义和报告写入失败。
- 验收：通过。ICP 现在显式在目标点云局部坐标框架中输出，派生云以目标空间参考加入树中。

### v0.5.1：配准与空间参考鲁棒性

- 覆盖：ICP 体素加速、目标局部框架、PCD 双精度空间注释、非有限过滤、项目严格数值校验和报告空间参考。
- 自动测试：`CloudRegistrationTest`、`PcdLoaderTest`、`CloudCoordinatesTest`、`ProjectDocumentTest`、`ArchaeologyReportTest`。
- 风险排查：大坐标精度、奇异变换、局部/真实坐标混用、伪收敛和不安全整数转换。
- 验收：通过。新增已知刚体真值实测云仿真，点对点 RMSE 阈值 `< 0.02`，颜色和来源索引保持。

### v0.5.2：三维点云测量

- 覆盖：三维折线距离、任意平面面积/周长、三点夹角、稳定记录 ID、来源图层/CRS 和报告/项目往返。
- 自动测试：`Measurement3DTest`、`ProjectDocumentTest`、`ArchaeologyReportTest`。
- 风险排查：非有限坐标、陈旧 CRS、未知来源图层、隐藏图层后标注状态和删除同步。
- 验收：通过。三维测量现在作为独立树实体控制显隐和删除，而不是隐含在点云节点内。

### v0.6.0：图层、属性、重投影和考古记录

- 覆盖：稳定图层 ID、类型安全属性、LAS/LAZ/PCD 属性、属性着色、CRS 重投影、考古元数据、控制点配准、任务状态机和报告。
- 自动测试：`CloudLayerTest`、`CloudAttributesTest`、`LasLoaderTest`、`PcdLoaderTest`、`CloudReprojectionTest`、`ArchaeologyMetadataTest`、`ControlPointsTest`、`TaskManagerTest`。
- 风险排查：属性数量/类型错配、Unicode 属性名、`float64/int64/uint64` 精度、经纬度误用、控制点重复/共线和任务非法状态转换。
- 验收：通过。预处理、裁剪、ICP 和重投影派生云均保持可映射属性；原图层不被隐式替换。

### v0.7.0：DEM 与等高线

- 覆盖：自动/手动分辨率、四种网格统计、NoData 小洞、等高线拓扑/平滑、`.famp-dem`、ASC/CSV/SVG 和二维等高线图元。
- 自动测试：`TerrainAnalysisTest`、`TerrainDialogTest`、`TerrainIOTest`、`ContourItemTest`、`GraphicsSceneDocumentTest`。
- 风险排查：地理 CRS、非米制换算、超大网格、损坏/截断边车、取消和原子写入。
- 验收：通过。DEM 和等高线默认成为内存类型实体；用户另存或保存项目时才物化资产。

### v0.8.0：点云高程剖面

- 覆盖：走廊几何、里程/偏距、四种统计、空白段、上下包络、数量上限、`.famp-profile`、CSV/SVG 和结果对话框。
- 自动测试：`ProfileAnalysisTest`、`ProfileDialogTest`、`ProfileIOTest`。
- 风险排查：零长度剖面、点数上限、缺口伪连线、取消、损坏文件、真实坐标变换和大预览降采样。
- 验收：通过。剖面结果默认进入内容树，导出路径为空时不会创建文件。

### v0.9.0：挖填方与体积

- 覆盖：固定高程/参考 DEM、网格对齐、NoData、单位换算、面积体积复算、`.famp-volume`、CSV/SVG、对话框和端到端工作流。
- 自动测试：`CutFillAnalysisTest`、`CutFillDialogTest`、`CutFillIOTest`、`CutFillWorkflowTest`、`TerrainAnalysisTest`。
- 风险排查：符号口径、参考网格错位、非米制单位、统计篡改、溢出、超大网格、取消和必需/可选输出失败。
- 验收：通过。结果默认成为内存实体，固定高程和实测点云路径已执行无文件端到端测试。

### v1.0.0：CloudCompare 风格工作区与考古制图链路

- 覆盖：统一类型树、扩展选择、拖放、同级排序、分组、重命名、显隐、锁定、删除、定位、属性面板、按类型另存、来源关系、项目恢复，以及六步考古制图导航、选择驱动投影、会话内弹窗位置记忆、单项当前预览与俯视/XOZ/YOZ 三项实际自动绘图门禁。
- 自动测试：`WorkspaceStoreTest`、`WorkspaceSnapshotTest`、`EntityTreeModelTest`、`ProjectDocumentTest`、`GraphicsSceneDocumentTest`、`CloudProjectionTest`、`ProjectionWorkflowTest`、`ProjectionPreviewTest`、`ProjectionDraftingInputTest`、`ProjectionWorkflowUiTest`、`MainWindowLayoutTest`、`ReleaseAcceptanceRealCloudTest`。
- 风险排查：多选拖放部分提交、同级 Qt drop row 偏移、循环父子关系、锁定子树删除、重名、无效路径、后端对象先删而模型拒绝、图元 ID 漂移、恢复资产悬空、投影自动污染树、隐式复用旧裁切结果、投影方向猜错和自动绘图改写预览。
- 验收：通过。批量移动/删除采用先完整校验后提交；常规派生点云是源云同级节点且自包含；投影只预览，只有明确确认才入树，预览不累计；自动绘图进入“二维制图”分组。俯视、XOZ、YOZ 三项自动绘图只有同时存在于右侧画布和左侧“二维制图”目录时才解锁第⑥步、编辑和 A4 导出，删除或清空任一成果后重新锁定。

## 本轮发现并关闭的高风险问题

| 风险 | 影响 | 修复与验证 |
| --- | --- | --- |
| 处理结果默认落盘 | 临时文件泛滥，项目状态依赖路径 | 主界面改为内存实体；显式另存/项目保存才写盘；真实云测试验证目录无新增文件 |
| ICP 混用源/目标局部框架 | 大坐标或不同原点时结果位置错误 | 输出转到目标局部框架；单元测试和已知刚体实测云仿真通过 |
| 滤波/裁剪丢失逐点属性 | 分类、强度等与点错位 | 返回来源索引并按索引选取全部属性；确定性索引通道逐点核对 |
| 多选拖拽逐个提交 | 后一项失败后树只移动一部分 | `moveEntities` 一次校验/一次 reset；锁定混合选择测试验证零移动 |
| 删除先动渲染后动模型 | 锁定子树拒绝删除但画面对象已消失 | 先让 store 原子接受，再删除 2D/3D 后端对象 |
| 二维图元缺少稳定身份 | 重开项目后树节点和操作对象失配 | 序列化 UUID，兼容旧文件补 ID，重复/非法 ID 原子拒绝 |
| 自动恢复引用临时成果 | 崩溃后派生结果无法恢复 | schema v4/snapshot v2 物化恢复资产并使用相对路径 |
| 预览对象生命周期空指针 | 特定缩放/析构路径可能崩溃 | 使用图元所属场景删除并保护空 renderer；clang analyzer 复核 |
| 投影自动创建内容树节点 | 反复试方向产生大量临时点云，工作区被污染 | 投影改为弹窗生命周期内的瞬时预览，决策弹窗正文仅保留来源/类型/点数，自动绘图、加入内容列表和关闭预览分别通过按钮悬停说明；GUI 测试验证确认前节点数不变且弹窗结束后临时投影消失 |
| 投影依赖旧裁切缓存或几何猜测 | 切换点云后可能投影错误来源，XOY 与俯视几何相同而制图类型误判 | 每次从当前树选中点云读取数据，并显式传递 XOY/XOZ/YOZ/俯视枚举；来源指针变化会使旧预览失效 |
| 自动绘图修改投影点云 | 降采样后再次预览或入树的数据被意外改变 | 二维绘图只处理预览的深拷贝；单元测试核对输入类型和实际图元生成 |
| 投影决策框阻塞渲染与交互 | 用户无法检查预览，且投影演员要等关闭窗口后才显示 | 在发出预览信号前同步完成 VTK Render；决策框改为非模态并取消强制置顶，界面测试验证主窗口和 VTK 保持 enabled |
| 投影决策框反复回到默认位置 | 用户为查看 VTK 移开弹窗后，每次刷新仍需重复拖动 | 首次按主窗口居中；会话内保存并复用最后位置，同时限制在可用屏幕范围内；GUI 测试核对位置 |
| 单向成果误解锁最终出图 | 仅有一个投影轮廓也能进入编辑/A4，缺少正交剖面 | 按源点云分别累计俯视、XOZ、YOZ 预览和自动绘图；缺失提示、步骤⑥及导出门禁由 GUI 测试覆盖 |
| 二维画布先于工作区销毁 | 内容树仍持有已删除图元句柄，关闭窗口可能崩溃 | 画布清空/恢复/析构前统一失效外部句柄，主窗口在子对象析构前主动断开并清理；含多个自动绘图成果的窗口销毁测试通过 |
| 预览与源云难以区分 | 同色同点径时无法判断裁剪或投影结果 | 预览强制关闭标量着色，使用琥珀/青/绿/洋红/紫高对比色，点径始终大于当前来源点径；确认后的裁剪/投影云使用独立显示样式 |
| 导航与裁切控件造成视觉跳动 | 状态字体粗细和换行改变布局，VTK 默认手柄/法向轴遮挡点云 | 导航按钮与状态行固定尺寸；裁切球、锥体和旋转轴按点云平面长宽比缩放并降低线宽 |
| 三向成果依赖固定偏移 | XOZ/YOZ 可能方向放反、互相重叠或落在固定画布外，切换比例尺后相对位置漂移 | 强制俯视先生成；每次生成、比例尺或屏幕 DPI 变化后重算包围盒，固定 YOZ 在上、XOZ 在右，并以俯视剖面切割线中心轴对齐；检测碰撞并按物理间距推开，超界时扩展场景而不缩放 |
| 俯视前生成剖面导致无对齐基准 | XOZ/YOZ 先生成时无法确定俯视剖面线与相对位置 | 剖面预览允许查看，但在俯视成果存在前禁用自动绘图；GUI 测试验证顺序门禁、两条切割线入树与三向完成解锁 |
| 各成果方向调整破坏三视图关系 | 用户分别旋转俯视、XOZ、YOZ 后出现错边、脱轴、带动俯视锚点或比例尺重绘回跳 | 三项成果生成前都提供 0°/90°/180°/270° 实时预览确认；俯视角度仅控制俯视和剖切线，XOZ/YOZ 角度分别持久化，确认后才按对应切割线中心轴排版。独立角度、统一角度兼容路径、四档比例尺及实测点云共同验证固定俯视锚点、无重叠和切割线对齐 |
| 二维图元拖拽/旋转拖影 | 投影线、指北针或制图信息在 `paint()` 内再次 `update()` 形成持续脏区，选中前景框又跨出局部失效区，旧像素无法完整擦除 | 所有图元去掉绘制阶段递归更新，画布禁用缓存并采用完整视口更新，包围盒额外覆盖线宽和抗锯齿像素；`SelectedCurvePaintDoesNotScheduleRecursiveSceneInvalidation` 同时验证三类图元不再触发新场景失效 |
| 方向预览旋转后越界或跳变 | 图元组旋转后返回空边界，旧缩放将长边放大到预览框外；逐角度自动适配还会造成显示大小变化 | 四个角度共用场景全部图元旋转联合边界计算出的中心和缩放比例；极端长宽比数据验证四个角度全部可见且缩放一致 |
| 内容树选择与视图对象脱节 | 树行被选中但用户无法判断 VTK/二维画布中对应对象，多个点云包围盒残留造成误判 | 包围盒默认关闭；每次点击先清理旧高亮，再对点云显示单一包围盒、对二维图元绘制通用前景高亮、对三维测量增强样式，GUI 测试覆盖点云与二维成果切换 |
| 打印机驱动采用不同 DPI | 预览正确但纸质 1 mm 米格可能按请求像素数落到另一设备分辨率 | `printScene` 在页面配置后读取设备实际分辨率，并用该 DPI 重新计算毫米网格和实尺内容；界面继续要求关闭驱动的适页缩放 |
| 切割对话框空指针或重复实例 | 无活动切割时的可见性信号可崩溃，多对话框关闭会清除错误状态 | 槽函数先校验指针，重复请求复用唯一对话框；`ReusesDialogAndIgnoresVisibilityWithoutOne` 通过 |
| 异常场景范围导致米格爆炸 | 非法浮点到整数转换、计数溢出或过多路径可卡死/耗尽内存 | 绘制前校验可表示索引与单轴线数上限，循环终止不依赖溢出；极端范围回归通过 |

## 尚需现场人工验收的风险

以下不能用无头自动测试替代：

1. `offscreen` 平台不能验收真实 OpenGL 像素、VTK 点拾取手感、拖放指示器和高 DPI 排版；需在有显示器的 Linux/Windows 各做一次交互走查。
2. Windows CI 会对上传的成品压缩包重新解压，校验 Qt/PROJ/MSVC/Mesa 关键依赖并启动 15 秒；这仍不能代替目标现场 Windows 显卡与驱动的长时间交互验收。
3. 实测 PCD 没有声明 CRS，也没有分类/强度字段；CRS、LAS/LAZ 类型属性和控制点精度继续由确定性合成夹具覆盖。
4. 真实 ICP 仿真已覆盖正确小角度初值、低重叠和错误初值质量门禁；未提供粗配准的大角度场景仍应视为需要人工初始变换的使用约束。
5. 屏幕米格能随显示器切换自动读取物理 DPI，但虚拟机、远程桌面或错误 EDID 可能上报错误尺寸；纸质成果还会受到打印机机械误差和驱动缩放影响，验收时必须选择“实际大小/100%”并用直尺复核 100 mm 标距。

## 复现命令

```bash
export VCPKG_ROOT=/opt/vcpkg
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j8
ctest --test-dir build --output-on-failure
ctest --test-dir build -R ReleaseAcceptanceRealCloudTest --output-on-failure
QT_QPA_PLATFORM=offscreen timeout 6 ./build/bin/FAMP
git diff --check
```

可用环境变量覆盖实测云位置：

```bash
FAMP_ACCEPTANCE_CLOUD=/absolute/path/to/cloud.pcd \
ctest --test-dir build -R ReleaseAcceptanceRealCloudTest --output-on-failure
```
