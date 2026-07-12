#include <gtest/gtest.h>

#include "HelpContent.h"

TEST(HelpContentTest, QuickStartCoversTheMainWorkflow)
{
    const QString content = famp::help::quickStartHtml();

    EXPECT_TRUE(content.contains(QStringLiteral("打开点云")));
    EXPECT_TRUE(content.contains(QStringLiteral("拖入主窗口")));
    EXPECT_TRUE(content.contains(QStringLiteral("最近打开")));
    EXPECT_TRUE(content.contains(QStringLiteral("切割与投影")));
    EXPECT_TRUE(content.contains(QStringLiteral("保存成果")));
}

TEST(HelpContentTest, ShortcutsListCoreActions)
{
    const QString content = famp::help::shortcutsHtml();

    EXPECT_TRUE(content.contains(QStringLiteral("Ctrl+O")));
    EXPECT_TRUE(content.contains(QStringLiteral("Ctrl+S")));
    EXPECT_TRUE(content.contains(QStringLiteral("Ctrl+Shift+Left/Right")));
    EXPECT_TRUE(content.contains(QStringLiteral("F1")));
}

TEST(HelpContentTest, AboutContentIncludesAllVersions)
{
    const QString content = famp::help::aboutHtml(
        QStringLiteral("1.2.3"),
        QStringLiteral("Qt-test"),
        QStringLiteral("VTK-test"),
        QStringLiteral("PCL-test"));

    EXPECT_TRUE(content.contains(QStringLiteral("FAMP 1.2.3")));
    EXPECT_TRUE(content.contains(QStringLiteral("Qt-test")));
    EXPECT_TRUE(content.contains(QStringLiteral("VTK-test")));
    EXPECT_TRUE(content.contains(QStringLiteral("PCL-test")));
}
