#include "ControlPointDialog.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

TEST(ControlPointDialogTest, AddsACompleteRowAndCancelsAtomically)
{
    bool inspected = false;
    QTimer::singleShot(0, [&]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog)
        {
            QApplication::closeAllWindows();
            return;
        }
        QPushButton* addButton = nullptr;
        for (QPushButton* button : dialog->findChildren<QPushButton*>())
        {
            if (button->text() == QStringLiteral("添加控制点"))
            {
                addButton = button;
                break;
            }
        }
        QTableWidget* table = dialog->findChild<QTableWidget*>();
        if (addButton && table)
        {
            addButton->click();
            inspected = table->rowCount() == 1
                && table->item(0, 8) != nullptr
                && table->item(0, 8)->text() == QStringLiteral("—");
        }
        dialog->reject();
    });

    famp::control::EditResult result;
    result.applySolution = true;
    EXPECT_FALSE(famp::control::editControlPoints(
        nullptr, QStringLiteral("layer"), QStringLiteral("cloud.pcd"),
        {}, {}, result));
    EXPECT_TRUE(inspected);
    EXPECT_TRUE(result.applySolution);
    EXPECT_TRUE(result.points.isEmpty());
}

TEST(ControlPointDialogTest, SolvesValidRowsAndEnablesApplyOption)
{
    QVector<famp::control::Point> points;
    const QVector<famp::cloud::Point3d> coordinates{
        {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
    for (int index = 0; index < coordinates.size(); ++index)
    {
        famp::control::Point point;
        point.id = famp::control::createPointId();
        point.name = QStringLiteral("CP-%1").arg(index + 1);
        point.local = coordinates.at(index);
        point.target = coordinates.at(index);
        points.append(point);
    }

    bool inspected = false;
    QTimer::singleShot(0, [&]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog)
        {
            QApplication::closeAllWindows();
            return;
        }
        QPushButton* solveButton = nullptr;
        for (QPushButton* button : dialog->findChildren<QPushButton*>())
        {
            if (button->text() == QStringLiteral("解算并计算残差"))
            {
                solveButton = button;
                break;
            }
        }
        if (solveButton)
            solveButton->click();
        QCheckBox* apply = dialog->findChild<QCheckBox*>();
        QTableWidget* table = dialog->findChild<QTableWidget*>();
        inspected = solveButton && apply && table
            && apply->isEnabled() && apply->isChecked()
            && table->item(0, 8)->text() != QStringLiteral("—");
        dialog->reject();
    });

    famp::control::EditResult result;
    EXPECT_FALSE(famp::control::editControlPoints(
        nullptr, QStringLiteral("layer"), QStringLiteral("cloud.pcd"),
        {}, points, result));
    EXPECT_TRUE(inspected);
}
