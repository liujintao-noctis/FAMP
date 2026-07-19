#include "QDlgClip.h"

#include <gtest/gtest.h>

#include <QPushButton>

TEST(ClipDialogTest, UsesExplicitConfirmationInsteadOfSaving)
{
    QDlgClip dialog;
    QPushButton* previewButton = dialog.findChild<QPushButton*>(
        QStringLiteral("pBnClip"));
    QPushButton* confirmButton = dialog.findChild<QPushButton*>(
        QStringLiteral("pBnConfirm"));

    ASSERT_NE(previewButton, nullptr);
    ASSERT_NE(confirmButton, nullptr);
    EXPECT_EQ(previewButton->text(), QStringLiteral("开始切割"));
    EXPECT_EQ(confirmButton->text(), QStringLiteral("确认"));
    EXPECT_FALSE(confirmButton->isEnabled());
    EXPECT_EQ(dialog.findChild<QPushButton*>(QStringLiteral("pBnSave")),
              nullptr);

    dialog.setConfirmButtonEnabled(true);
    EXPECT_TRUE(confirmButton->isEnabled());
    dialog.setConfirmButtonEnabled(false);
    EXPECT_FALSE(confirmButton->isEnabled());
}
