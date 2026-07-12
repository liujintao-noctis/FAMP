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
    EXPECT_TRUE(content.contains(QStringLiteral("拖入主窗口")));
    EXPECT_TRUE(content.contains(QStringLiteral("最近打开")));
    EXPECT_TRUE(content.contains(QStringLiteral("切割与投影")));
    EXPECT_TRUE(content.contains(QStringLiteral("体素降采样")));
    EXPECT_TRUE(content.contains(QStringLiteral("统计离群点")));
    EXPECT_TRUE(content.contains(QStringLiteral("ICP")));
    EXPECT_TRUE(content.contains(QStringLiteral("空间参考")));
    EXPECT_TRUE(content.contains(QStringLiteral("导出成果")));
    EXPECT_TRUE(content.contains(QStringLiteral("测量面积")));
    EXPECT_TRUE(content.contains(QStringLiteral("PDF")));
    EXPECT_TRUE(content.contains(QStringLiteral("SVG")));
    EXPECT_TRUE(content.contains(QStringLiteral("自定义纸张")));
    EXPECT_TRUE(content.contains(QStringLiteral("打印预览")));
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
