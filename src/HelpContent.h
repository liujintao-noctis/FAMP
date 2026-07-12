#pragma once

#include <QString>

namespace famp::help
{
inline QString quickStartHtml()
{
    return QStringLiteral(R"HTML(
<h2>FAMP 快速入门</h2>
<ol>
  <li><b>打开点云：</b>使用“文件 → 打开点云”选择 PCD 或 LAS 文件。</li>
  <li><b>检查点云：</b>在内容列表中切换可见性，并使用六个标准视图和包围盒检查数据。</li>
  <li><b>切割与投影：</b>选择点云后创建水平、竖直或任意切割面，再投影到 XOY、XOZ 或 YOZ。</li>
  <li><b>二维制图：</b>生成投影连线，设置比例尺，添加文字、指北针和制图信息。</li>
  <li><b>保存成果：</b>使用“保存”导出当前二维制图成果。</li>
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
  <tr><td>Ctrl+S</td><td>保存二维制图成果</td></tr>
  <tr><td>Ctrl+F12</td><td>全屏显示</td></tr>
  <tr><td>Delete</td><td>删除选中的二维图元</td></tr>
  <tr><td>方向键</td><td>移动选中的二维图元</td></tr>
  <tr><td>Ctrl+Shift+Left/Right</td><td>逆时针/顺时针旋转选中的二维图元</td></tr>
  <tr><td>F1</td><td>打开快速入门</td></tr>
</table>
<p>双击文字图元可以修改字体。拖动图元边界或控制点可以完成移动和旋转。</p>
)HTML");
}

inline QString aboutHtml(const QString& applicationVersion,
                         const QString& qtVersion,
                         const QString& vtkVersion,
                         const QString& pclVersion)
{
    return QStringLiteral(R"HTML(
<h2>FAMP %1</h2>
<p>Field Archaeology Mapping Program</p>
<p>用于点云查看、切割、投影和田野考古二维制图。</p>
<table cellspacing="8">
  <tr><td><b>Qt</b></td><td>%2</td></tr>
  <tr><td><b>VTK</b></td><td>%3</td></tr>
  <tr><td><b>PCL</b></td><td>%4</td></tr>
</table>
<p><a href="https://github.com/liujintao-noctis/FAMP">GitHub 项目主页</a></p>
)HTML").arg(applicationVersion.toHtmlEscaped(),
             qtVersion.toHtmlEscaped(),
             vtkVersion.toHtmlEscaped(),
             pclVersion.toHtmlEscaped());
}
}
