#pragma once

#include <QString>

namespace famp::help
{
inline QString quickStartHtml()
{
    return QStringLiteral(R"HTML(
<h2>FAMP 快速入门</h2>
<ol>
  <li><b>管理项目：</b>使用“文件 → 新建/打开/保存项目”管理 `.famp` 点云项目；项目会保存点云原始坐标与变换、可见性、二维图元、测量成果和窗口布局，未保存更改每 60 秒自动备份；源点云移动后，打开项目时可重新定位。</li>
  <li><b>声明坐标系：</b>使用“工具 → 项目坐标系”记录 EPSG 元数据；“坐标转换器”可用 PROJ 转换单个坐标点，选择点云后还可用“点云局部/真实坐标”按原始坐标原点和双精度变换双向换算。</li>
  <li><b>打开点云：</b>使用“文件 → 打开点云”选择 PCD 或 LAS 文件，也可将文件直接拖入主窗口；读取会在后台逐个完成，期间仍可查看和操作已加载内容。</li>
  <li><b>继续工作：</b>“文件 → 最近打开”会保留最近 8 个可用点云文件。</li>
  <li><b>检查点云：</b>在内容列表中切换可见性，并使用六个标准视图和包围盒检查数据。</li>
  <li><b>显示与预处理：</b>在内容列表选择点云后，可从“工具”调整点大小、透明度和颜色，或在后台执行体素降采样、统计离群点去噪并另存为新 PCD。</li>
  <li><b>切割与投影：</b>选择点云后创建水平、竖直或任意切割面，再投影到 XOY、XOZ 或 YOZ。</li>
  <li><b>二维制图：</b>生成投影连线，设置比例尺，添加文字、指北针和制图信息。</li>
  <li><b>测量成果：</b>使用“工具 → 测量距离/测量面积”在二维画布点击测量；面积在选择至少 3 个点后右键或双击闭合，Esc 可取消。</li>
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
  <tr><td>Ctrl+Z</td><td>撤销二维图元操作</td></tr>
  <tr><td>Ctrl+Shift+Z / Ctrl+Y</td><td>重做二维图元操作</td></tr>
  <tr><td>Ctrl+F12</td><td>全屏显示</td></tr>
  <tr><td>Delete</td><td>删除选中的二维图元</td></tr>
  <tr><td>方向键</td><td>移动选中的二维图元</td></tr>
  <tr><td>Ctrl+Shift+Left/Right</td><td>逆时针/顺时针旋转选中的二维图元</td></tr>
  <tr><td>Ctrl+Alt+D / Ctrl+Alt+A</td><td>测量二维画布距离/面积</td></tr>
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
