#pragma once

#include <QString>

namespace famp::help
{
inline QString quickStartHtml()
{
    return QStringLiteral(R"HTML(
<h2>FAMP 快速入门</h2>
<ol>
  <li><b>管理项目：</b>使用“文件 → 新建/打开/保存项目”管理 `.famp` 点云项目；项目会保存点云原始坐标与变换、控制点、考古字段、可见性、二维图元、二维/三维测量成果和窗口布局，未保存更改每 60 秒自动备份；源点云移动后，打开项目时可重新定位。</li>
  <li><b>坐标系与重投影：</b>使用“工具 → 项目坐标系”记录 EPSG 元数据；“坐标转换器”可用 PROJ 转换单个坐标点；选择点云后可按双精度变换和原点使用“点云局部/真实坐标”双向换算，或使用“重投影所选点云”逐点转换整云、另存 PCD。重投影会无损保留已有逐点属性，并移除该图层的旧三维测量和控制点；撤销时属性、坐标、测量与控制点会随图层状态一并恢复。</li>
  <li><b>打开点云：</b>使用“文件 → 打开点云”选择 PCD、LAS、LAZ、PLY 或 XYZ 文件，也可将文件直接拖入主窗口；LAS/LAZ 会保留常用逐点属性，XYZ 接受 3 列坐标或 6 列坐标/RGB。读取会在后台逐个完成，期间仍可查看和操作已加载内容，并可从状态栏安全取消当前文件及剩余队列。</li>
  <li><b>继续工作：</b>“文件 → 最近打开”会保留最近 8 个可用点云文件。</li>
  <li><b>检查点云：</b>在内容列表中切换可见性，并使用六个标准视图和包围盒检查数据。</li>
  <li><b>显示、处理与配准：</b>在内容列表选择点云后，可从“工具”调整点大小、透明度，并切换 RGB、统一颜色、局部高程 Z 或 LAS/LAZ 逐点属性渐变（自动/手动范围），属性模式会显示类型、值数和范围；也可在后台执行体素降采样、统计离群点去噪，或按局部 X/Y/Z 范围保留内部/外部点并另存为新 PCD。处理参数可保存/载入为 JSON 方案。加载至少两个点云后，可用“点云 ICP 配准”将源点云刚性对齐到目标点云并生成新 PCD，并可用配准体素降低大点云耗时；ICP 要求初始位置足够接近，无有效重叠时不会输出结果。FAMP 生成的 PCD 内嵌双精度空间参考；含属性的重投影成果还会内嵌原类型的逐点属性，重新打开仍可恢复。取消操作不会写入或集成结果。</li>
  <li><b>控制点空间配准：</b>选择点云后使用“工具 → 控制点与空间配准”，填写点云局部坐标和同名实测目标坐标；至少启用 3 个不共线点即可解算保距 3D 刚体变换。界面会列出逐点总残差以及 RMSE、平均和最大残差；可只保存记录，也可应用变换。应用时已有三维测量会同步旋转并保持测量尺度，操作可撤销/重做；控制点和质量统计会写入项目与考古报告。</li>
  <li><b>DEM 与等高线：</b>选择未锁定点云后使用“工具 → DEM 与等高线”。程序按双精度真实坐标生成网格，支持自动/手动分辨率、最低/最高/平均/中位数单元统计、最多 3 格的封闭 NoData 小空洞填补，以及自动/手动等高距、基准高程和 0–3 次平滑。经纬度图层必须先重投影到投影坐标系；未声明 CRS 时必须确认 X/Y 为米制平面坐标。成果原子保存为 `.famp-dem`，可选导出 ASC、DEM/等高线 CSV、SVG，并可把等高线作为可撤销且随比例尺缩放的图元加入二维画布。后台任务可取消且不会修改原点云。</li>
  <li><b>挖填方与体积：</b>选择未锁定的当前地表点云后使用“工具 → 挖填方与体积”，与固定设计高程或已有 `.famp-dem` 参考地表比较。参考 DEM 模式在后台读取并采用其分辨率，严格检查 CRS、单位、原点对齐、重叠和 NoData。高差为“当前 - 参考”：正值是挖方，负值是填方，净体积为“挖方 - 填方”。结果窗口显示红/蓝/灰降采样概览，成果原子保存为 `.famp-volume`，可选导出汇总 CSV、逐格 CSV 和 SVG。经纬度图层必须先重投影，未声明 CRS 时必须确认 X/Y/Z 全部以米为单位。</li>
  <li><b>点云高程剖面：</b>选择并显示点云后使用“工具 → 点云高程剖面”，在中央三维点云依次左键拾取起点和终点，Esc 或右键取消。程序按双精度真实坐标提取线段走廊，计算沿线里程和带符号偏距，并按自定义间隔输出最低、最高、平均或中位数代表高程以及段内最低/最高包络；空白段不会被跨越连线。成果原子保存为 `.famp-profile`，可选导出采样段 CSV、原始点 CSV 和 SVG，并在程序中预览。经纬度图层必须先重投影，后台任务可取消且不会修改原点云。</li>
  <li><b>切割与投影：</b>选择点云后创建水平、竖直或任意切割面，再投影到 XOY、XOZ 或 YOZ。</li>
  <li><b>二维制图：</b>生成投影连线，设置比例尺，添加文字、指北针和制图信息。</li>
  <li><b>测量成果：</b>使用“工具 → 测量距离/测量面积/测量角度”后，可在中央三维点云直接拾取点，也可在右侧二维制图画布选点。移动鼠标会实时预览节点、连线和数值；距离和面积用右键完成，二维画布也可双击完成；角度按边点—顶点—边点选择，Esc 可取消。二维与三维结果都会保存到项目和成果报告，并可撤销、重做或集中清除。隐藏点云图层不会删除已有三维测量，重新显示图层后测量标注和拾取功能一并恢复。</li>
  <li><b>考古图层记录与报告：</b>选择点云后使用“工具 → 考古图层属性”填写遗址、发掘区、探方/探沟、地层、遗迹编号、年代、日期、记录人、说明及自定义字段；修改可撤销/重做并随项目保存。使用“文件 → 导出考古项目报告”生成 PDF 或 HTML，按图层列出考古字段、控制点与配准残差，并汇总项目路径、坐标参考系、比例尺、点云文件/点数/原点/可见性，以及二维/三维距离、面积/周长和角度测量成果；三维成果同时列出关联图层 ID 和 CRS。</li>
  <li><b>导出成果：</b>使用“导出平面图”按 A4/A3/自定义纸张、横向/纵向和 150/300/600 DPI 导出 PDF、SVG、PNG 或 BMP；默认保持当前制图比例尺，也可选择自动适合页面，并能在保存前打开打印预览。</li>
</ol>
<p>操作前请先在内容列表中选中目标点云。控制台会显示加载、切割、投影和保存结果。</p>
)HTML");
}

inline QString shortcutsHtml()
{
    return QStringLiteral(R"HTML(
<h2>键盘快捷键</h2>
<table cellspacing="8">
  <tr><th align="left">快捷键</th><th align="left">功能</th></tr>
  <tr><td>Ctrl+O</td><td>打开点云</td></tr>
  <tr><td>Ctrl+N</td><td>新建项目</td></tr>
  <tr><td>Ctrl+Shift+O</td><td>打开项目</td></tr>
  <tr><td>Ctrl+Shift+S</td><td>保存项目</td></tr>
  <tr><td>Ctrl+S</td><td>保存二维制图成果</td></tr>
  <tr><td>Ctrl+Z</td><td>撤销最近的制图、测量或项目操作</td></tr>
  <tr><td>Ctrl+Shift+Z / Ctrl+Y</td><td>重做最近撤销的操作</td></tr>
  <tr><td>Ctrl+F12</td><td>全屏显示</td></tr>
  <tr><td>Delete</td><td>删除选中的二维图元</td></tr>
  <tr><td>方向键</td><td>移动选中的二维图元</td></tr>
  <tr><td>Ctrl+Shift+Left/Right</td><td>逆时针/顺时针旋转选中的二维图元</td></tr>
  <tr><td>Ctrl+Alt+D / Ctrl+Alt+A / Ctrl+Alt+G</td><td>测量三维点云或二维画布的距离/面积/角度</td></tr>
  <tr><td>F1</td><td>打开快速入门</td></tr>
</table>
<p>双击文字图元可以修改字体。拖动图元边界或控制点可以完成移动和旋转。</p>
)HTML");
}

inline QString aboutHtml(const QString& applicationVersion,
                         const QString& qtVersion,
                         const QString& vtkVersion,
                         const QString& pclVersion,
                         const QString& projVersion)
{
    return QStringLiteral(R"HTML(
<h2>FAMP %1</h2>
<p>Field Archaeology Mapping Program</p>
<p>用于点云查看、切割、投影和田野考古二维制图。</p>
<table cellspacing="8">
  <tr><td><b>Qt</b></td><td>%2</td></tr>
  <tr><td><b>VTK</b></td><td>%3</td></tr>
  <tr><td><b>PCL</b></td><td>%4</td></tr>
  <tr><td><b>PROJ</b></td><td>%5</td></tr>
</table>
<p><a href="https://github.com/liujintao-noctis/FAMP">GitHub 项目主页</a></p>
)HTML").arg(applicationVersion.toHtmlEscaped(),
             qtVersion.toHtmlEscaped(),
             vtkVersion.toHtmlEscaped(),
             pclVersion.toHtmlEscaped(),
             projVersion.toHtmlEscaped());
}
}
