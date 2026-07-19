#include <gtest/gtest.h>

#include "HelpContent.h"

TEST(HelpContentTest, QuickStartCoversTheMainWorkflow)
{
    const QString content = famp::help::quickStartHtml();

    EXPECT_TRUE(content.contains(QStringLiteral("打开点云")));
    EXPECT_TRUE(content.contains(QStringLiteral(".famp")));
    EXPECT_TRUE(content.contains(QStringLiteral("60 秒")));
    EXPECT_TRUE(content.contains(QStringLiteral("原始坐标")));
    EXPECT_TRUE(content.contains(QStringLiteral("重新定位")));
    EXPECT_TRUE(content.contains(QStringLiteral("EPSG")));
    EXPECT_TRUE(content.contains(QStringLiteral("PROJ")));
    EXPECT_TRUE(content.contains(QStringLiteral("局部/真实坐标")));
    EXPECT_TRUE(content.contains(QStringLiteral("双精度变换")));
    EXPECT_TRUE(content.contains(QStringLiteral("重投影所选点云")));
    EXPECT_TRUE(content.contains(QStringLiteral("拖入主窗口")));
    EXPECT_TRUE(content.contains(QStringLiteral("最近打开")));
    EXPECT_TRUE(content.contains(QStringLiteral("切割与点云准备")));
    EXPECT_TRUE(content.contains(QStringLiteral("考古制图流程")));
    EXPECT_TRUE(content.contains(QStringLiteral("关闭预览")));
    EXPECT_TRUE(content.contains(QStringLiteral("临时投影自动消失")));
    EXPECT_TRUE(content.contains(QStringLiteral("二维制图”文件夹")));
    EXPECT_TRUE(content.contains(QStringLiteral("体素降采样")));
    EXPECT_TRUE(content.contains(QStringLiteral("统计离群点")));
    EXPECT_TRUE(content.contains(QStringLiteral("ICP")));
    EXPECT_TRUE(content.contains(QStringLiteral("空间参考")));
    EXPECT_TRUE(content.contains(QStringLiteral("控制点与空间配准")));
    EXPECT_TRUE(content.contains(QStringLiteral("RMSE")));
    EXPECT_TRUE(content.contains(QStringLiteral("不共线")));
    EXPECT_TRUE(content.contains(QStringLiteral("DEM 与等高线")));
    EXPECT_TRUE(content.contains(QStringLiteral(".famp-dem")));
    EXPECT_TRUE(content.contains(QStringLiteral("NoData")));
    EXPECT_TRUE(content.contains(QStringLiteral("投影坐标系")));
    EXPECT_TRUE(content.contains(QStringLiteral("挖填方与体积")));
    EXPECT_TRUE(content.contains(QStringLiteral(".famp-volume")));
    EXPECT_TRUE(content.contains(QStringLiteral("挖方 - 填方")));
    EXPECT_TRUE(content.contains(QStringLiteral("X/Y/Z")));
    EXPECT_TRUE(content.contains(QStringLiteral("点云高程剖面")));
    EXPECT_TRUE(content.contains(QStringLiteral(".famp-profile")));
    EXPECT_TRUE(content.contains(QStringLiteral("带符号偏距")));
    EXPECT_TRUE(content.contains(QStringLiteral("原始点 CSV")));
    EXPECT_TRUE(content.contains(QStringLiteral("导出成果")));
    EXPECT_TRUE(content.contains(QStringLiteral("测量面积")));
    EXPECT_TRUE(content.contains(QStringLiteral("中央三维点云")));
    EXPECT_TRUE(content.contains(QStringLiteral("实时预览")));
    EXPECT_TRUE(content.contains(QStringLiteral("三维结果")));
    EXPECT_TRUE(content.contains(QStringLiteral("关联图层 ID")));
    EXPECT_TRUE(content.contains(QStringLiteral("重新显示图层")));
    EXPECT_TRUE(content.contains(QStringLiteral("PDF")));
    EXPECT_TRUE(content.contains(QStringLiteral("SVG")));
    EXPECT_TRUE(content.contains(QStringLiteral("自定义纸张")));
    EXPECT_TRUE(content.contains(QStringLiteral("打印预览")));
    EXPECT_TRUE(content.contains(QStringLiteral("1 mm")));
    EXPECT_TRUE(content.contains(QStringLiteral("254")));
    EXPECT_TRUE(content.contains(QStringLiteral("实际大小/100%")));
    EXPECT_TRUE(content.contains(QStringLiteral("YOZ 始终在俯视图正上方")));
    EXPECT_TRUE(content.contains(QStringLiteral("XOZ 始终在俯视图正右方")));
    EXPECT_TRUE(content.contains(QStringLiteral("XOZ/YOZ 各自保存确认角度")));
    EXPECT_TRUE(content.contains(QStringLiteral("鼠标悬停")));
    EXPECT_TRUE(content.contains(QStringLiteral("对应剖面切割线")));
    EXPECT_TRUE(content.contains(QStringLiteral("实尺，不缩放")));
    EXPECT_TRUE(content.contains(QStringLiteral("缩小超大页面")));
    EXPECT_TRUE(content.contains(QStringLiteral("300")));
}

TEST(HelpContentTest, ShortcutsListCoreActions)
{
    const QString content = famp::help::shortcutsHtml();

    EXPECT_TRUE(content.contains(QStringLiteral("Ctrl+O")));
    EXPECT_TRUE(content.contains(QStringLiteral("Ctrl+Shift+O")));
    EXPECT_TRUE(content.contains(QStringLiteral("Ctrl+Shift+S")));
    EXPECT_TRUE(content.contains(QStringLiteral("Ctrl+S")));
    EXPECT_TRUE(content.contains(QStringLiteral("Ctrl+Z")));
    EXPECT_TRUE(content.contains(QStringLiteral("Ctrl+Shift+Z")));
    EXPECT_TRUE(content.contains(QStringLiteral("Ctrl+Shift+Left/Right")));
    EXPECT_TRUE(content.contains(QStringLiteral("Ctrl+Alt+D")));
    EXPECT_TRUE(content.contains(QStringLiteral("F1")));
}

TEST(HelpContentTest, AboutContentIncludesAllVersions)
{
    const QString content = famp::help::aboutHtml(
        QStringLiteral("1.2.3"),
        QStringLiteral("Qt-test"),
        QStringLiteral("VTK-test"),
        QStringLiteral("PCL-test"),
        QStringLiteral("PROJ-test"));

    EXPECT_TRUE(content.contains(QStringLiteral("FAMP 1.2.3")));
    EXPECT_TRUE(content.contains(QStringLiteral("Qt-test")));
    EXPECT_TRUE(content.contains(QStringLiteral("VTK-test")));
    EXPECT_TRUE(content.contains(QStringLiteral("PCL-test")));
    EXPECT_TRUE(content.contains(QStringLiteral("PROJ-test")));
}
