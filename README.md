# FAMP

[![CI](https://github.com/liujintao-noctis/FAMP/actions/workflows/ci.yml/badge.svg)](https://github.com/liujintao-noctis/FAMP/actions/workflows/ci.yml)

FAMP（Field Archaeology Mapping Program）是一款基于 C++17、Qt、VTK 和 PCL 的田野考古制图桌面程序，支持 PCD/LAS/LAZ/PLY/XYZ 点云查看、平面切割、投影、二维绘图和成果图片保存。

项目使用 CMake 和 vcpkg 管理跨平台构建。规范的本地 CMake 入口是 `src/CMakeLists.txt`，构建产物统一放在仓库根目录的 `build/`。根目录 `CMakeLists.txt` 仍可使用，但它只负责设置工程并转发到 `src/`。

当前应用版本为 `1.0.0`。`cmake/FampVersion.cmake` 是 CMake 和 C++ 程序使用的版本号来源；配置时会生成 `Version.h`，程序会把版本写入 Qt 应用元数据并在主窗口标题栏显示为 `FAMP 1.0.0`。发布新版本时，还需要同步更新 `vcpkg.json` 中的 `version-string`。

## 当前功能

- 左侧内容列表使用统一的项目树管理点云、派生点云、DEM、等高线、剖面、挖填方、二维/三维测量和二维图元；中间为 VTK 点云视口，右侧为考古制图画布，两个工作区默认等宽。VTK 视口左下角常驻带 X/Y/Z 标签的彩色方向轴，并随相机同步旋转。内容树支持分组、扩展选择、拖拽排序、重命名、显隐、锁定、删除、定位、属性查看和按类型另存，交互方式接近 CloudCompare 的 DB Tree。点云包围盒默认关闭；点击一个可见点云时只显示该点云包围盒，点击二维成果或三维测量时会清除点云包围盒并在对应视图中高亮所选成果。
- 主界面顶部持续显示六步考古制图导航：选择点云、可选点云准备、制图类型、当前投影预览、三项自动绘图、编辑与 A4 出图。投影预览不累计；只有俯视（沿 Z 轴垂直投影）、XOZ、YOZ 三项自动绘图同时真实存在于右侧画布和左侧“二维制图”目录时，第⑥步及其编辑/A4 导出动作才会解锁。删除或清空任一必做成果会重新锁定。
- XOY、XOZ、YOZ 和俯视投影可直接作用于内容列表当前选中的任意点云（包括切割、预处理、ICP 和重投影成果）。投影默认只在 VTK 中临时预览，不落盘也不自动入树；预览后可选自动绘图、显式加入内容列表或关闭预览。投影决策窗首次在主窗口中心显示，同一软件会话内再次投影时沿用用户上次移动后的位置，软件重启后重新居中；窗口结束后临时投影演员和自动绘图输入会自动清除。
- 三向二维成果以俯视图为必须首项和固定锚点：YOZ 始终放在俯视图正上方，XOZ 始终放在俯视图正右方。生成 XOZ/YOZ 时会从当前选中的原始或切割点云计算剖切位置；切割点云带有 `plane_clip` 来源信息时直接复用真实剖切平面，在俯视图上自动加入对应剖面切割线，并使剖面成果与切割线中心轴对齐。四个方向和 1:10、1:20、1:50、1:100 比例尺下固定方位与对齐关系不变；程序使用物理 10 mm 图间距规避重叠，超出默认画布时只扩展画布，不改变制图比例尺。
- 在后台串行读取、校验并打开 PCD/LAS/LAZ/PLY/XYZ 点云，加载期间界面仍可响应，也可安全取消当前文件和剩余队列；LAS/LAZ 会读取强度、分类、回波、扫描角、用户数据、点源 ID 和可用的 GPS 时间，PLY 支持有色或无色顶点，XYZ 支持 `x y z` 或 `x y z r g b` 及空格/逗号/分号分隔。
- 为选中点云调整点大小和透明度，并使用 RGB、统一颜色、局部高程 Z 或 LAS/LAZ 逐点属性渐变着色；色带支持自动数据范围和手动范围，对话框同时显示属性类型、值数和范围。在后台执行可取消的体素降采样或统计离群点去噪，成功后生成自包含的内存点云并加入内容树，原始文件不被修改。
- LAS/LAZ 逐点属性用于图层显示，项目会保存属性摘要和着色选择并在重新打开时从源文件恢复。FAMP 可在 ASCII PCD 自定义字段中无损保存 `float64`、`int64` 和 `uint64` 属性及其 Unicode 名称/单位；预处理、裁剪、ICP 和重投影通过来源索引保持逐点属性对应关系。
- 按当前点云的局部 X/Y/Z 包围范围保留内部或外部点；裁剪任务可取消，成功结果默认作为内存点云加入内容树，需要时再从树中另存 PCD。
- 点云预处理和范围裁剪参数可显式保存、载入为 `.famp-process.json` 方案；每个内存成果同时记录来源实体、参数、时间、变换和质量指标，不会自动创建方案旁车文件。
- 支持在后台执行 ICP 刚性配准，将源点云换算并对齐到目标点云的局部坐标框架；可使用体素降采样加速大点云配准，无有效重叠时不会生成伪结果，操作可安全取消。成果默认作为目标空间参考下的内存点云加入树中，需要时再另存 PCD。
- 预处理、裁剪、ICP 和重投影成果另存为 PCD 时会嵌入 FAMP 双精度空间参考及可用逐点属性；文件仍兼容标准 PCD 工具，重新载入 FAMP 时可恢复原点和 4×4 变换。
- 可从“工具 → 考古图层属性”给所选点云填写遗址、发掘区、探方/探沟、地层、遗迹编号、年代、日期、记录人、说明及自定义字段；修改可撤销/重做并保存到项目。考古项目报告会按图层输出这些记录，同时汇总点云坐标以及二维、三维测量成果。
- 可从“工具 → 控制点与空间配准”维护点云局部坐标和实测目标坐标；至少 3 个启用且不共线的控制点可解算保距 3D 刚体变换，并显示逐点 XYZ/总残差、RMSE、平均和最大残差。控制点、解算后的双精度空间参考及已有三维测量的同步旋转均可撤销/重做，报告会按图层输出完整质量记录。
- 新建、打开和原子保存 `.famp` 项目，并每 60 秒自动备份未保存更改；项目格式 v4 保存统一内容树、稳定实体/图层/图元 ID、来源关系、相对资产路径、图层 CRS、锁定/可见状态、显示参数、点属性摘要、考古字段、控制点、双精度空间变换、二维/三维测量、比例尺和窗口布局，兼容读取 v1/v2/v3 项目，源资产移动后可交互重新定位。
- 为项目记录经 PROJ 验证的 EPSG 坐标系，并使用通用坐标转换器核对 CRS；选择点云后可使用“点云局部/真实坐标”按保存的双精度原点和变换矩阵双向换算坐标。
- 可使用“工具 → 重投影所选点云”在后台逐点执行 CRS 转换；结果重新中心化并作为保留全部逐点属性的派生内存点云加入内容树，原图层不被替换，需要时可另存为带双精度空间参考的 PCD。
- 可使用“工具 → DEM 与等高线”从所选点云的双精度真实坐标生成地形网格：自动分辨率取水平中位最近邻间距的两倍且不小于 0.01 米，也可手动设置；单元高程支持最低值、最高值、平均值或默认中位数，并可填补不超过 3 格的封闭 NoData 小空洞。等高距和基准高程可自动或手动设置，折线支持 0–3 次平滑。任务在后台运行并可安全取消，原始点云不被修改。
- DEM 与等高线默认作为类型化内存成果加入内容树；可选立即另存版本化 `.famp-dem`、ESRI ASCII Grid、DEM CSV、等高线 CSV 和 SVG。等高线也可作为可移动、可撤销的二维图元加入制图画布，会随比例尺重绘并保存到 `.famp` 项目；真实投影坐标和来源 CRS/图层仍保留在图元元数据中。
- 可使用“工具 → 挖填方与体积”把所选点云生成当前地表 DEM，再与固定设计高程或已有 `.famp-dem` 参考地表比较。参考 DEM 模式会在后台读取并强制采用其分辨率，严格检查 CRS、单位、网格原点对齐和重叠范围，不会隐式插值或忽略 NoData。
- 挖填方结果分别报告挖方/填方面积和体积、平衡区、缺少参考的网格以及“挖方 - 填方”净体积，并显示红/蓝/灰降采样概览图。成果默认加入内容树，可选立即另存版本化 `.famp-volume`、汇总 CSV、逐格 CSV 和 SVG；后台任务可取消且不会以半成品覆盖已有文件。
- 可使用“工具 → 点云高程剖面”在中央三维视图的所选可见点云上依次拾取起点和终点；程序按双精度真实坐标提取线段走廊内的点，计算沿线里程和带符号横向偏距，并按自定义间隔生成最低、最高、平均或默认中位数高程剖面。每个采样段保留点数及最低/最高包络，点数不足或没有点的段保持为空白，不会跨数据缺口伪造连线。
- 点云剖面默认作为类型化内存成果加入内容树并在程序内显示，可选立即另存版本化 `.famp-profile`、采样段统计 CSV、走廊原始点 CSV 和可缩放 SVG。剖面分析和长文件写入都可安全取消；原始点云不被修改，已有目标文件不被半成品覆盖。
- 将 PCD/LAS/LAZ/PLY/XYZ 文件拖入主窗口，或从“文件 → 最近打开”恢复最近 8 个有效文件。
- 使用 `Ctrl+Shift+Left/Right` 将选中的二维图元绕中心每次旋转 5°。
- 二维图元拖拽或旋转时使用完整视口重绘，投影线、指北针和制图信息均不会在旧位置留下重影。
- 使用 `Ctrl+Z` 和 `Ctrl+Shift+Z`/`Ctrl+Y` 撤销或重做最近 100 步编辑，包括二维图元新增/删除/变换/组合、三维测量的新增/清除、文字字体、制图比例尺、项目 CRS、控制点配准和点云重投影。
- 在中央三维点云视图直接拾取点，实时显示节点、连线和结果标签，可测量三维折线距离、任意朝向多边形面积/周长和三点夹角；结果关联图层 ID/CRS，保存到项目和考古报告，并可撤销、重做或集中清除。隐藏图层时测量随之隐藏但不删除，再次显示后可继续拾取并恢复结果。
- 在二维画布按当前制图比例尺测量多段线距离、多边形面积/周长和三点夹角，结果可随成果一起导出，并支持撤销、重做和集中清除。
- 通过“帮助”菜单查看离线快速入门、快捷键和 Qt/VTK/PCL 版本。
- 将完整二维画布按精确毫米定义的 A4/A3/自定义纸张、横向/纵向和 150/254/300/508/600 DPI 原子导出为 PDF、SVG、PNG 或 BMP，并可在保存前打开对应纸张预览。实尺成果必须选择默认的“保持当前制图比例尺（实尺，不缩放）”；“自动适合页面”会缩放成果。导出默认包含最小间隔 1 mm、5 mm/10 mm 加粗的米格纸；PDF/SVG 使用矢量毫米坐标，PNG/BMP 含米格纸时自动使用 254 或 508 DPI，使 1 mm 严格对应 10 或 20 像素。纸质打印还必须在打印机属性中选择“实际大小/100%”并关闭“适合页面/缩小超大页面”。
- 屏幕米格纸会读取窗口所在显示器的物理 DPI，并在 DPI、物理尺寸或所在屏幕变化时自动重算；显示器正确上报 EDID/物理尺寸时最小间隔为物理 1 mm。虚拟机、远程桌面或错误显示器元数据无法仅靠软件自动校正，严格现场验收应以直尺测量连续 100 格是否为 100 mm。

完整版本变更见 [`CHANGELOG.md`](CHANGELOG.md)。当前开发批次的已完成内容、未完成任务和验收标准见 [`DEVELOPMENT_STATUS.md`](DEVELOPMENT_STATUS.md)；源码分层规则见 [`docs/architecture/SOURCE_LAYOUT.md`](docs/architecture/SOURCE_LAYOUT.md)，v0.2.0 以上逐版本测试、风险和验收结论见 [`docs/qa/V0_2_0_PLUS_ACCEPTANCE.md`](docs/qa/V0_2_0_PLUS_ACCEPTANCE.md)。

## 考古制图工作流

1. 在左侧内容列表选择原始点云或任意派生点云。投影按钮会根据当前选择启用，不再依赖隐藏的“上一次切割结果”。
2. 按需执行平面切割、预处理、范围裁切、ICP 或 CRS 重投影。这一步可跳过；生成的点云会作为来源点云的同级兄弟节点紧跟在后。
3. 确认三向必做成果：俯视（沿 Z 轴垂直投影）用于平面轮廓，XOZ 和 YOZ 用于两个正交剖面；XOY 是可选辅助投影，不计入三向完成门禁。投影类型会显式传给自动绘图，不再通过浮点坐标特征猜测。
4. 选择任一方向生成当前投影预览。预览会立即以高对比颜色和更大点径显示在中央 VTK 视口，非模态决策窗正文只保留来源、类型和点数；“自动绘图”“加入内容列表”“关闭预览”的完整含义改由鼠标悬停提示，不再占用窗口正文。预览只服务于当前操作，不累计“三项预览”完成度。决策窗不会阻止旋转、缩放或平移 VTK；首次居中，之后在本次软件运行期间记住用户上次位置。窗口结束后临时投影自动消失并失效；只有选择“加入内容列表”才会生成独立内存点云，且原点云继续显示。
5. 必须先对俯视预览执行自动绘图，然后再按任意次序生成 XOZ 和 YOZ；俯视图尚未生成时，XOZ/YOZ 预览窗中的“自动绘图”不可用，弹窗和常驻流程栏会以橙色粗体突出“先完成俯视二维制图”。俯视、XOZ、YOZ 每次生成正式成果前都会分别弹出方向确认窗，可在 0°、顺时针 90°、180°、270°之间切换实时预览；四个方向共用按点云、坐标轴和文字完整旋转边界计算的中心与缩放比例，切换时只改变方向，不改变显示大小，也不会裁掉内容。俯视角度只控制俯视图及其两条剖切线，XOZ/YOZ 各自保存确认角度；调整剖面方向不会带动俯视锚点旋转。确认后 YOZ 仍固定位于俯视图正上方，XOZ 仍固定位于正右方。生成 XOZ/YOZ 时，程序使用当前选中的原始或切割点云（而不是被压平的预览副本）计算剖切位置，在俯视图上自动绘制对应剖面切割线，并将剖面图与该线的中心轴对齐。三图和两条切割线都同时出现在右侧画布与左侧“二维制图”文件夹；改变比例尺时保持相对关系，超界时只扩展画布。
6. 只有俯视、XOZ、YOZ 三项自动绘图同时实际存在于右侧画布和左侧“二维制图”目录时，第⑥步、二维编辑动作和 A4 导出才会解锁；删除或清空任一成果会立即重新锁定。解锁后可编辑轮廓、比例尺、文字、指北针和制图信息。A4/A3 实尺出图时选择纸张与方向、勾选 1 mm 米格纸、保留“实尺，不缩放”，点击“A4/A3 打印预览”；最终打印选择“实际大小/100%”，不得启用驱动缩放。

## 生成 DEM 与等高线

1. 在内容列表选择一个未锁定的点云。如果图层使用经纬度 CRS，先通过“工具 → 重投影所选点云…”转换到适合项目区域的投影坐标系；程序会拒绝直接用角度生成 DEM。没有声明 CRS 时，只有明确确认真实 X/Y 是米制平面坐标后才能继续。
2. 打开“工具 → DEM 与等高线…”。通常保留自动分辨率、中位数统计、自动等高距和一次平滑即可；稀疏点云可按需要启用最多 3 个连通网格的小空洞填补。
3. 默认不勾选“生成后立即另存文件”，DEM 和等高线会直接进入项目内容树。需要立即交换文件时再勾选该项，选择 `.famp-dem` 路径，并按需导出 ASC、DEM CSV、等高线 CSV、等高线 SVG 或加入二维制图画布；已有文件会在开始前确认并原子替换。
4. 生成完成后，控制台会报告网格尺寸、米制分辨率、有效/填补单元数、等高线条数、线点数、等高距和内存/文件成果。取消时不会集成不完整实体，也不会留下正在写入的半成品。

`.famp-dem` 保存完整网格、NoData、分辨率、原点、统计方式、单位、来源图层和 CRS，适合以后由 FAMP 继续读取。ASC/CSV/SVG 是交换成果；它们不能替代项目边车。加入二维画布的等高线最多包含 100 万个显示点，超过上限时文件成果仍会保留，但不会把超大图元塞入项目。

## 计算挖填方与体积

1. 在内容列表选择一个未锁定的当前地表点云，打开“工具 → 挖填方与体积…”。经纬度图层必须先重投影；没有声明 CRS 时，程序会要求确认真实 X/Y/Z 全部使用米。当前计算假定 X、Y 和高程 Z 使用同一长度单位。
2. 选择参考方式。“固定设计高程”直接输入米制高程，并可使用自动或手动网格分辨率；“已有 FAMP DEM”选择 `.famp-dem`，程序在后台读取后使用该 DEM 的分辨率生成当前网格。参考 DEM 必须与当前点云的 CRS 和单位一致，网格原点按整数单元对齐，且水平范围有重叠。
3. 选择最低值、最高值、平均值或默认中位数网格统计，按需填补最多 3 格的封闭 NoData 空洞，并设置米制平衡容差。只有当前和参考都具有有效高程的网格才参与参考 DEM 体积累计；参考 NoData 或超出参考范围会单独报告。
4. 默认不勾选“生成后立即另存文件”，挖填方成果会直接进入项目内容树。需要立即交换文件时再选择 `.famp-volume` 边车路径，并按需导出汇总 CSV、逐格 CSV 和概览 SVG；已有目标会统一确认，主边车失败时不集成结果，可选输出失败时保留已完成主成果并集中提示。
5. 完成后结果窗口显示挖方、填方、平衡区、缺少参考网格、高差范围和降采样分类图。高差定义为“当前地表 - 参考地表”：正值为挖方，负值为填方；挖方和填方体积分别以正数报告，净体积为“挖方 - 填方”，正值表示挖方多，负值表示填方多。

`.famp-volume` schema v1 保存当前 DEM、逐格高差、参考方式、CRS/单位/图层元数据以及完整面积和体积统计，并在读取时重新校验内部一致性。默认最多处理 1000 万个网格；程序内概览最多绘制 24 万个降采样块，SVG 最多约 25 万个色块。超过网格上限时应增大分辨率或先对点云降采样。

## 生成点云高程剖面

1. 在内容列表选择并显示一个点云，然后打开“工具 → 点云高程剖面…”。经纬度图层必须先重投影到适合项目区域的投影坐标系；没有声明 CRS 时，程序会要求确认真实 X/Y/Z 是本地米制坐标。
2. 在中央三维点云上左键拾取剖面起点，再拾取终点。橙色预览线只接受当前所选图层的点；Esc 或右键可取消。剖面按起点到终点方向计算沿线里程，左侧为正偏距、右侧为负偏距。
3. 设置完整走廊宽度、沿线采样间隔、每段最少点数和代表高程统计。默认中位数对少量离群点更稳定；最低/最高包络无论选择哪种代表值都会保留并显示。
4. 默认不勾选“生成后立即另存文件”，剖面会直接进入项目内容树。需要立即交换文件时再选择 `.famp-profile` 路径，并按需导出采样段 CSV、走廊原始点 CSV 和 SVG；已有文件会再次确认，所有写出均使用原子提交。
5. 完成后会显示剖面图及源点数、走廊点数、有效采样段和内存/文件成果。蓝线是所选代表高程，灰色范围是每段最低/最高高程；没有满足最少点数的段保持断开。取消时不会集成不完整实体，也不会留下未完成文件。

`.famp-profile` 保存剖面真实起终点、走廊/采样参数、来源图层/文件/CRS、全部走廊样本和每段统计，适合以后由 FAMP 精确读取。采样段 CSV 适合制图和统计，原始点 CSV 包含源点索引、沿线里程、带符号偏距及真实 XYZ；SVG 适合直接插入报告。为控制内存，单次最多生成 25 万个采样段并保留 200 万个走廊点，超过时应增大采样间隔、减小走廊宽度或先降采样。

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

当前 GoogleTest 用例覆盖点云质心与去中心化、图层/逐点属性模型、可取消任务状态机、原始坐标、OBB、统一 PCD/LAS/LAZ/PLY/XYZ 加载服务、真实压缩 LAZ 解码、LAS 逐点属性、属性着色、ICP 目标局部框架、非 ASCII 点云路径、项目文件 v1/v2/v3/v4 与迁移、统一内容树/拖放/锁定/原子批量操作、内存成果快照与恢复、显式投影类型、三向正交布局与四档比例尺、预览决策与入树门禁、稳定二维图元 ID、二维场景完整往返、三维测量记录的校验与项目往返、投影坐标单位、DEM 分辨率与单元统计、小空洞填补、等高线拓扑、挖填方固定高程/参考 DEM 比较、单位换算、网格对齐、结果一致性、参数界面、大网格概览降采样与端到端边车往返、点云剖面走廊/采样统计与真实坐标变换、地形、挖填方和剖面边车/交换格式原子读写及取消保护、坐标系、撤销/重做、A4/A3 实尺米格与打印预览、考古项目报告、字符串转换和物理比例尺计算。指定实测点云存在时还会执行 Unicode PCD、内存预处理/裁剪、平面/俯视投影、三向自动绘图四档比例尺、仿真刚体 ICP 和地形分析验收。首次配置可能需要从 GitHub 下载 GoogleTest。

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

生产版本不会自动把投影过程中的中间 PCD 文件写入工作目录。平面切割先生成预览，点击“确认”后才把结果作为内存点云加入内容列表。点云投影同样先预览，但只有在决策弹窗中选择“加入内容列表”才新建点云实体；选“自动绘图”只会生成“二维制图”下的图元。需要 PCD 文件时，再从内容列表显式另存所选点云。

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
如果只需要在无头环境检查程序能否进入 Qt 事件循环，可以使用：

```bash
QT_QPA_PLATFORM=offscreen timeout 6 ./build/bin/FAMP
```

退出码 `124` 代表进程保持运行；`QOpenGLWidget` 无法创建上下文的警告在无头模式下属于预期现象。此命令不验证 VTK 实际渲染，最终的可视化检查仍应在有 OpenGL 的图形桌面中完成。

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
